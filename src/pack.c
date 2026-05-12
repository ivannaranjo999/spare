#include "sar.h"

/* ------------------------------------------------------------------ */
/*  Internal types                                                     */
/* ------------------------------------------------------------------ */
 
typedef struct {
  char filepath[SAR_MAX_PATH];
  char linkpath[SAR_MAX_PATH];
  uint8_t *dest; /* pointer into mmap region for this file */
  uint64_t file_size;
  uint32_t mode;
  int64_t mtime;
} WorkItem;
 
typedef struct {
    WorkItem *items;        /* this thread's slice of the work array  */
    int       count;        /* number of items in the slice           */
    int       verbose;
    int       result;       /* thread writes 0 or -1 here             */
} ThreadArgs;
 
/* ----------------------------------------------------------------------------
 * collect_files
 *
 * Recursively walks filepath and appends one WorkItem per regular
 * file into *items, growing the array as needed.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int collect_files(const char *filepath, WorkItem **items, int *count, int *capacity) {
  /* Local variables */
  struct stat st;
  DIR *dir;
  struct dirent *entry;
  char fullpath[SAR_MAX_PATH];
  char linkbuf[SAR_MAX_PATH];
  ssize_t linklen;
  int result = 0;

  /* Code */
  /* Read metadata with lstat to not follow symlinks */
  if (lstat(filepath, &st) != 0) {
    perror(filepath);
    return -1;
  }

  /* If symlink, store the link target as file data */
  if (S_ISLNK(st.st_mode)) {
    linklen = readlink(filepath, linkbuf, sizeof(linkbuf) - 1);
    if (linklen < 0) {
      perror(filepath);
      return -1;
    }
    linkbuf[linklen] = '\0';

    /* grow the array if needed */
    if (*count == *capacity) {
      int       new_cap = *capacity == 0 ? 64 : *capacity * 2;
      WorkItem *tmp     = realloc(*items, new_cap * sizeof(WorkItem));
      if (tmp == NULL) {
        perror("realloc");
        return -1;
      }
      *items    = tmp;
      *capacity = new_cap;
    }

    WorkItem *w = &(*items)[*count];
    memset(w, 0, sizeof(*w));
    strncpy(w->filepath, filepath, SAR_MAX_PATH - 1);
    w->filepath[SAR_MAX_PATH - 1] = '\0';
    strncpy(w->linkpath, linkbuf, SAR_MAX_PATH - 1);
    w->linkpath[SAR_MAX_PATH - 1] = '\0';
    w->file_size = (uint64_t)linklen;
    w->mode      = (uint32_t)st.st_mode;
    w->mtime     = (int64_t)st.st_mtime;
    w->dest      = NULL;

    (*count)++;
    return 0;
  }

  /* recurse into directories */
  if (S_ISDIR(st.st_mode)) {
    dir = opendir(filepath);
    if (dir == NULL) {
        perror(filepath);
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0)  continue;
      if (strcmp(entry->d_name, "..") == 0) continue;
      if (snprintf(fullpath, sizeof(fullpath), "%s/%s",
          filepath, entry->d_name) >= (int)sizeof(fullpath)) {
        fprintf(stderr, "error: path too long: '%s/%s'\n",
                filepath, entry->d_name);
        result = -1;
        continue;
      }
      if (collect_files(fullpath, items, count, capacity) != 0)
          result = -1;
    }
    closedir(dir);
    return result;
  }

  /* skip non-regular files silently */
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "skipping '%s': not a regular file\n", filepath);
    return 0;
  }

  /* grow the array if needed */
  if (*count == *capacity) {
    int       new_cap = *capacity == 0 ? 64 : *capacity * 2;
    WorkItem *tmp     = realloc(*items, new_cap * sizeof(WorkItem));
    if (tmp == NULL) {
      perror("realloc");
      return -1;
    }
    *items    = tmp;
    *capacity = new_cap;
  }

  /* fill in the work item, dest is assigned later by assign_offsets */
  WorkItem *w = &(*items)[*count];
  memset(w, 0, sizeof(*w));
  strncpy(w->filepath, filepath, SAR_MAX_PATH - 1);
  w->filepath[SAR_MAX_PATH - 1] = '\0';
  w->file_size = (uint64_t)st.st_size;
  w->mode      = (uint32_t)st.st_mode;
  w->mtime     = (int64_t)st.st_mtime;
  w->dest      = NULL;

  (*count)++;
  return 0;
}
 
/* ----------------------------------------------------------------------------
 * assign_offsets
 *
 * Walk the flat WorkItem array, compute each file's byte offset and 
 * set dest = mmap_base + offset.
 * Returns the total archive size in bytes.
 * ------------------------------------------------------------------------- */
static uint64_t assign_offsets(WorkItem *items, int count, uint8_t *mmap_base) {
  uint64_t offset = 0;
  for (int i = 0; i < count; i++) {
    items[i].dest = mmap_base + offset;
    offset += sizeof(FileHeader) + items[i].file_size;
  }
  return offset;
}
 
/* ----------------------------------------------------------------------------
 *  write_item
 *
 *  Write one WorkItem (header + file data) directly into its assigned
 *  mmap region. Called from worker threads.
 *  Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
 
static int write_item(const WorkItem *w, int verbose){
  /* Local variables */
  /* write header directly into mmap destination */
  FileHeader *header = (FileHeader *)w->dest;
  memset(header, 0, sizeof(FileHeader));
  memcpy(header->magic, SAR_MAGIC, 3);
  header->version = SAR_VERSION;
  header->file_size = w->file_size;
  header->mode = w->mode;
  header->mtime = w->mtime;
  strncpy(header->filename, w->filepath, SAR_MAX_PATH - 1);
  header->filename[SAR_MAX_PATH - 1] = '\0';
  uint8_t *data_dest;

  /* Code */
  data_dest = w->dest + sizeof(FileHeader);

  /* If symlink, do not fopen anything */
  if (S_ISLNK(w->mode)) {
    memcpy(data_dest, w->linkpath, w->file_size);
    if (verbose)
      printf("packed: '%s' -> '%s'\n", w->filepath, w->linkpath);
    return 0;
  }

  /* read file data directly into mmap region after the header */
  FILE *src = fopen(w->filepath, "rb");
  if (src == NULL) {
    perror(w->filepath);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_FILE_BUF_SIZE);

  uint64_t remaining = w->file_size;
  uint8_t buf[COPY_BUFFER_SIZE];

  while (remaining > 0) {
    size_t chunk      = remaining < sizeof(buf) ? remaining : sizeof(buf);
    size_t bytes_read = fread(buf, 1, chunk, src);
    if (bytes_read == 0) {
      fprintf(stderr, "error: unexpected EOF reading '%s'\n", w->filepath);
      fclose(src);
      return -1;
    }
    memcpy(data_dest, buf, bytes_read);
    data_dest += bytes_read;
    remaining -= bytes_read;
  }

  if (ferror(src)) {
    fprintf(stderr, "error: failed to read '%s'\n", w->filepath);
    fclose(src);
    return -1;
  }

  fclose(src);

  if (verbose)
    printf("packed: '%s' (%llu + %llu bytes)\n",
            w->filepath,
            (unsigned long long)sizeof(FileHeader),
            (unsigned long long)w->file_size);

  return 0;
}

/* ----------------------------------------------------------------------------
 * worker_thread
 *
 * pthread entry point. Processes its assigned slice of WorkItems.
 *
 * ------------------------------------------------------------------------- */
 
static void *worker_thread(void *arg) {
  ThreadArgs *args = (ThreadArgs *)arg;
  args->result = 0;

  for (int i = 0; i < args->count; i++) {
    if (write_item(&args->items[i], args->verbose) != 0)
      args->result = -1;
  }

  return NULL;
}
 
/* ----------------------------------------------------------------------------
 *  pack_threads
 *
 *  Create an archive at archive_path containing all files in
 *  filepaths[0..count-1] using mmap + multithreading.
 *  Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
 
int pack_threads(const char *archive_path, const char **filepaths, int count, int verbose) {
  WorkItem *items = NULL;
  int n_items  = 0;
  int capacity = 0;
  int result = 0;

  /* --- phase 1: collect all files into a flat array ------------- */

  if (verbose)
    printf("pre-allocating files in memory ...\n");

  for (int i = 0; i < count; i++) {
    if (collect_files(filepaths[i], &items, &n_items, &capacity) != 0)
        result = -1;
  }

  if (n_items == 0) {
    fprintf(stderr, "error: no files to pack\n");
    free(items);
    return -1;
  }

  /* --- phase 2: calculate total archive size -------------------- */

  uint64_t total_size = 0;
  for (int i = 0; i < n_items; i++)
    total_size += sizeof(FileHeader) + items[i].file_size;

  if (verbose)
    printf("pack: %d files, total archive size: %llu bytes\n",
            n_items, (unsigned long long)total_size);

  /* --- phase 3: open and pre-allocate output file --------------- */

  int fd = open(archive_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror(archive_path);
    free(items);
    return -1;
  }

  if (ftruncate(fd, (off_t)total_size) != 0) {
    perror("ftruncate");
    close(fd);
    free(items);
    return -1;
  }

  /* --- phase 4: mmap the output file ---------------------------- */

  uint8_t *mmap_base = mmap(NULL, total_size,
                            PROT_WRITE, MAP_SHARED, fd, 0);
  if (mmap_base == MAP_FAILED) {
    perror("mmap");
    close(fd);
    free(items);
    return -1;
  }

  close(fd);  /* fd no longer needed once mapped */

  /* --- phase 5: assign dest pointers to each work item ---------- */

  assign_offsets(items, n_items, mmap_base);

  /* --- phase 6: divide work and spawn threads ------------------- */

  int n_threads = SAR_PACK_THREADS;
  if (n_threads > n_items) n_threads = n_items;  /* cap at file count */

  pthread_t  threads[SAR_PACK_THREADS];
  ThreadArgs args[SAR_PACK_THREADS];

  /* distribute items evenly, first (n_items % n_threads) threads  */
  /* get one extra item so no file is left unassigned               */
  int base   = n_items / n_threads;
  int extra  = n_items % n_threads;
  int offset = 0;

  for (int t = 0; t < n_threads; t++) {
    args[t].items   = &items[offset];
    args[t].count   = base + (t < extra ? 1 : 0);
    args[t].verbose = verbose;
    args[t].result  = 0;
    offset += args[t].count;

    if (pthread_create(&threads[t], NULL, worker_thread, &args[t]) != 0) {
      perror("pthread_create");
      worker_thread(&args[t]);  /* fall back to main thread */
    }
  }

  /* --- phase 7: join all threads -------------------------------- */

  for (int t = 0; t < n_threads; t++) {
    pthread_join(threads[t], NULL);
    if (args[t].result != 0) result = -1;
  }

  /* --- phase 8: flush and release the mapping ------------------- */

  if (msync(mmap_base, total_size, MS_SYNC) != 0) {
    perror("msync");
    result = -1;
  }

  munmap(mmap_base, total_size);
  free(items);

  return result;
}


/* ----------------------------------------------------------------------------
 * pack_file
 *
 * Write one file into the open archive.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int pack_file(FILE *archive, const char *filepath, int verbose){
  /* Local variables */
  struct stat st;
  FILE *src;
  DIR *dir;
  struct dirent *entry;
  FileHeader header;
  char buf[COPY_BUFFER_SIZE_SMALL];
  char fullpath[SAR_MAX_PATH];
  char linkbuf[SAR_MAX_PATH];
  size_t bytes_read;
  ssize_t linklen;
  int result = 0;

  /* Code */
  /* Read metadata with lstat to not follow symlinks */
  if (lstat(filepath, &st) != 0) {
    perror(filepath);
    return -1;
  }

  /* If symlink, store the link target as file data */
  if (S_ISLNK(st.st_mode)) {
    linklen = readlink(filepath, linkbuf, sizeof(linkbuf) - 1);
    if (linklen < 0) {
      perror(filepath);
      return -1;
    }
    linkbuf[linklen] = '\0';
 
    /* Fill header */
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SAR_MAGIC, 3);
    header.version = SAR_VERSION;
    header.file_size = (uint64_t)linklen;
    header.mode = (uint32_t)st.st_mode;
    header.mtime = (int64_t)st.st_mtime;
    strncpy(header.filename, filepath, SAR_MAX_PATH - 1);
    header.filename[SAR_MAX_PATH - 1] = '\0';
 
    /* Write header */
    if (fwrite(&header, sizeof(header), 1, archive) != 1) {
      fprintf(stderr, "error: failed to write header for '%s'\n", filepath);
      return -1;
    }
 
    /* Write target path as file data */
    if (fwrite(linkbuf, 1, linklen, archive) != (size_t)linklen) {
      fprintf(stderr, "error: failed to write symlink target for '%s'\n",
        filepath);
      return -1;
    }
 
    if (verbose)
      printf("packed: '%s' -> '%s'\n", filepath, linkbuf);
 
    /* Do not continue */
    return 0;
  }

  /* If dir, recurse */
  if(S_ISDIR(st.st_mode)){
    dir = opendir(filepath);
    if (dir == NULL){
      perror(filepath);
      return -1;
    }
    while((entry = readdir(dir)) != NULL){
      if(strcmp(entry->d_name, ".") == 0) continue;
      if(strcmp(entry->d_name, "..") == 0) continue;
      if (snprintf(fullpath, sizeof(fullpath), "%s/%s",
          filepath, entry->d_name) >= (int)sizeof(fullpath)){
        fprintf(stderr, "error: path too long: '%s/%s'\n", 
          filepath, entry->d_name);
        result = -1;
        continue;
      }
      if (pack_file(archive, fullpath, verbose) != 0) result = -1;
    }
    closedir(dir);

    /* Do not continue */
    return result;
  }

  /* Operate only regular files */
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "skipping '%s': not a regular file\n", filepath);
    return -1;
  }

  /* Open file */
  src = fopen(filepath, "rb");
  if (src == NULL){
    perror(filepath); 
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_FILE_BUF_SIZE);

  /* Fill header info */
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, SAR_MAGIC, 3);
  header.version = SAR_VERSION;
  header.file_size = (uint64_t)st.st_size;
  header.mode = (uint32_t)st.st_mode;
  header.mtime = (int64_t)st.st_mtime;
  strncpy(header.filename, filepath, SAR_MAX_PATH - 1);
  header.filename[SAR_MAX_PATH - 1] = '\0';

  /* Write header to archive */
  if (fwrite(&header, sizeof(header), 1, archive) != 1){
    fprintf(stderr, "error: failed to write header for '%s'\n", filepath);
    fclose(src);
    return -1;
  }

  /* Copy file data into archive */
  while ((bytes_read = fread(buf, 1, sizeof(buf), src)) > 0){
    if(fwrite(buf, 1, bytes_read, archive) != bytes_read){
      fprintf(stderr, "error: failed to write data for '%s'\n", filepath);
      fclose(src);
      return -1;
    }
  }

  if (ferror(src)){
    fprintf(stderr, "error: failed to read from '%s'\n", filepath);
    fclose(src);
    return -1;
  }

  fclose(src);
  if (verbose)
    printf("packed: '%s' (%llu + %llu bytes)\n", 
      filepath, (unsigned long long)sizeof(FileHeader),
      (unsigned long long)st.st_size);

  return 0;
}

/* ----------------------------------------------------------------------------
 * pack
 *
 * Create an archive at 'archive_path' containing all files in 
 * 'filepaths[0..count-1]'.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int pack(FILE *archive, const char **filepaths, int count, int verbose){
  /* Local variables */
  int   result = 0;
  int   it = 0;

  /* Code */

  for (it = 0; it < count; ++it){
    if(pack_file(archive, filepaths[it], verbose) != 0){
      result = -1; /* record failure but keep packing */
    }
  }

  return result;
}
