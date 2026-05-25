#include "spare.h"

/* SEEK_DATA / SEEK_HOLE are Linux extensions exposed via _GNU_SOURCE.
 * -std=gnu11 implies _GNU_SOURCE but some linters miss it; define fallbacks. */
#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE 4
#endif

/* ------------------------------------------------------------------ */
/*  Internal types                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
  char filepath[SPARE_MAX_PATH];
  char linkpath[SPARE_MAX_PATH];
  uint8_t *dest; /* pointer into mmap region for this file */
  uint64_t file_size;
  uint64_t stored_size; /* actual bytes stored (< file_size when sparse) */
  HoleEntry *holes;
  uint64_t hole_count;
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  int64_t mtime;
} WorkItem;

typedef struct {
  WorkItem *items; /* this thread's slice of the work array  */
  int       count; /* number of items in the slice           */
  int       verbose;
  int       result; /* thread writes 0 or -1 here             */
} ThreadArgs;

/* ------------------------------------------------------------------ */
/*  Data-region iterator                                               */
/* ------------------------------------------------------------------ */

/* Called for each chunk of file data during region iteration.
 * Return 0 to continue, -1 to abort. */
typedef int (*DataChunkCb)(const void *buf, size_t n, void *ctx);

/* Context for write_chunk: write data chunks to an archive FILE* */
typedef struct {
  FILE *dst;
  const char *path;
} WriteCtx;

/* Feed each chunk into an xxhash state */
static int hash_chunk(const void *buf, size_t n, void *ctx) {
  XXH64_update((XXH64_state_t *)ctx, buf, n);
  return 0;
}

/* Write each chunk to an archive FILE* */
static int write_chunk(const void *buf, size_t n, void *ctx) {
  WriteCtx *c = ctx;
  if (fwrite(buf, 1, n, c->dst) != n) {
    fprintf(stderr, "error: failed to write data for '%s'\n", c->path);
    return -1;
  }
  return 0;
}

/* Copy each chunk into a mmap region, advancing the destination pointer */
static int memcpy_chunk(const void *buf, size_t n, void *ctx) {
  uint8_t **dst = ctx;
  memcpy(*dst, buf, n);
  *dst += n;
  return 0;
}


/* ----------------------------------------------------------------------------
 * foreach_data_region_fd
 *
 * Reads every data byte of src (skipping sparse holes) and calls cb for each
 * chunk read. filepath is used only for error messages.
 * Returns 0 on success, -1 on I/O error or if cb returns -1.
 * ------------------------------------------------------------------------- */
static int foreach_data_region_fd(int fd, const char *filepath,
    const HoleEntry *holes, uint64_t hole_count, uint64_t file_size,
    void *buf, size_t buf_size, DataChunkCb cb, void *ctx) {
  uint64_t i, region_start, region_end, remaining;
  size_t chunk;
  ssize_t nr;

  if (hole_count == 0) {
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) { perror(filepath); return -1; }
    for (;;) {
      nr = read(fd, buf, buf_size);
      if (nr == 0) break;
      if (nr < 0) { perror(filepath); return -1; }
      if (cb(buf, (size_t)nr, ctx) != 0) return -1;
    }
  } else {
    for (i = 0; i <= hole_count; i++) {
      region_start = (i == 0) ? 0 : holes[i-1].offset + holes[i-1].length;
      region_end   = (i == hole_count) ? file_size : holes[i].offset;
      if (region_start >= region_end) continue;
      if (lseek(fd, (off_t)region_start, SEEK_SET) == (off_t)-1) {
        perror(filepath); return -1;
      }
      remaining = region_end - region_start;
      while (remaining > 0) {
        chunk = remaining < buf_size ? (size_t)remaining : buf_size;
        nr = read(fd, buf, chunk);
        if (nr <= 0) break;
        if (cb(buf, (size_t)nr, ctx) != 0) return -1;
        remaining -= (size_t)nr;
      }
    }
  }
  return 0;
}

/* ----------------------------------------------------------------------------
 * build_hole_map
 *
 * Walks fd with SEEK_DATA / SEEK_HOLE to find sparse regions. On success,
 * *holes_out / *hole_count_out / *stored_size_out are set. If the filesystem
 * doesn't support sparse detection (EINVAL) or the file has no holes, the
 * outs are left at their defaults (NULL / 0 / file_size) and 0 is returned.
 * Returns 0 on success, -1 on allocation failure.
 * ------------------------------------------------------------------------- */
static int build_hole_map(int fd, uint64_t file_size,
    HoleEntry **holes_out, uint64_t *hole_count_out, uint64_t *stored_size_out) {
  /* Local variables */
  HoleEntry *holes = NULL;
  uint64_t hole_count = 0;
  uint64_t capacity = 0;
  uint64_t stored_size = 0;
  off_t pos = 0;
  off_t data_start, data_end;

  /* Code */
  if (file_size == 0)
    return 0;

  while (pos < (off_t)file_size) {
    data_start = lseek(fd, pos, SEEK_DATA);
    if (data_start == -1) {
      if (errno == ENXIO) {
        /* rest of file is a trailing hole */
        if (hole_count == capacity) {
          uint64_t new_cap = capacity == 0 ? 4 : capacity * 2;
          HoleEntry *tmp = realloc(holes, new_cap * sizeof(HoleEntry));
          if (!tmp) { free(holes); return -1; }
          holes = tmp; capacity = new_cap;
        }
        holes[hole_count].offset = (uint64_t)pos;
        holes[hole_count].length = file_size - (uint64_t)pos;
        hole_count++;
        break;
      }
      /* EINVAL: filesystem doesn't support SEEK_DATA, treat as non-sparse */
      free(holes);
      return 0;
    }
    if (data_start > pos) {
      /* hole between pos and data_start */
      if (hole_count == capacity) {
        uint64_t new_cap = capacity == 0 ? 4 : capacity * 2;
        HoleEntry *tmp = realloc(holes, new_cap * sizeof(HoleEntry));
        if (!tmp) { free(holes); return -1; }
        holes = tmp; capacity = new_cap;
      }
      holes[hole_count].offset = (uint64_t)pos;
      holes[hole_count].length = (uint64_t)(data_start - pos);
      hole_count++;
    }

    data_end = lseek(fd, data_start, SEEK_HOLE);
    if (data_end == -1 || data_end > (off_t)file_size)
      data_end = (off_t)file_size;
    if (data_end <= data_start)
      data_end = (off_t)file_size;

    stored_size += (uint64_t)(data_end - data_start);
    pos = data_end;
  }

  if (hole_count == 0) {
    free(holes);
    return 0; /* stored_size_out already set to file_size by caller */
  }

  *holes_out = holes;
  *hole_count_out = hole_count;
  *stored_size_out = stored_size;
  return 0;
}

/* ----------------------------------------------------------------------------
 * fill_workitem
 *
 * Zero and populate a WorkItem. Pass NULL for linkpath on regular files.
 * stored_size and holes are initialised to non-sparse defaults; callers that
 * detect holes must update them after calling this.
 * ------------------------------------------------------------------------- */
static void fill_workitem(WorkItem *w, const char *filepath, const char *linkpath,
  uint64_t file_size, uint32_t mode, uint32_t uid, uint32_t gid, int64_t mtime){
  /* Code */
  memset(w, 0, sizeof(*w));
  strncpy(w->filepath, filepath, SPARE_MAX_PATH - 1);
  w->filepath[SPARE_MAX_PATH - 1] = '\0';
  if (linkpath != NULL) {
    strncpy(w->linkpath, linkpath, SPARE_MAX_PATH - 1);
    w->linkpath[SPARE_MAX_PATH - 1] = '\0';
  }
  w->file_size = file_size;
  w->stored_size = file_size;
  w->holes = NULL;
  w->hole_count = 0;
  w->mode = mode;
  w->uid = uid;
  w->gid = gid;
  w->mtime = mtime;
  w->dest = NULL;
}

/* ----------------------------------------------------------------------------
 * collect_files
 *
 * Recursively walks filepath and appends one WorkItem per regular
 * file into *items, growing the array as needed.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int collect_files(const char *filepath, WorkItem **items, int *count,
  int *capacity, int sparse) {
  /* Local variables */
  struct stat st;
  DIR *dir;
  struct dirent *entry;
  char fullpath[SPARE_MAX_PATH];
  char linkbuf[SPARE_MAX_PATH];
  ssize_t linklen;
  int result = 0;
  int fd;

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
      int new_cap = *capacity == 0 ? 64 : *capacity * 2;
      WorkItem *tmp = realloc(*items, new_cap * sizeof(WorkItem));
      if (tmp == NULL) {
        perror("realloc");
        return -1;
      }
      *items = tmp;
      *capacity = new_cap;
    }

    WorkItem *w = &(*items)[*count];
    fill_workitem(w, filepath, linkbuf, (uint64_t)linklen,
      (uint32_t)st.st_mode, (uint32_t)st.st_uid, (uint32_t)st.st_gid,
      (int64_t)st.st_mtime);
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
      if (collect_files(fullpath, items, count, capacity, sparse) != 0)
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
    int new_cap = *capacity == 0 ? 64 : *capacity * 2;
    WorkItem *tmp = realloc(*items, new_cap * sizeof(WorkItem));
    if (tmp == NULL) {
      perror("realloc");
      return -1;
    }
    *items = tmp;
    *capacity = new_cap;
  }

  /* fill in the work item, dest is assigned later by assign_offsets */
  WorkItem *w = &(*items)[*count];
  fill_workitem(w, filepath, NULL, (uint64_t)st.st_size,
    (uint32_t)st.st_mode, (uint32_t)st.st_uid, (uint32_t)st.st_gid,
    (int64_t)st.st_mtime);

  /* fold hole detection into the pre-scan: open+lseek+close, no data read */
  if (sparse && st.st_size > 0) {
    fd = open(filepath, O_RDONLY);
    if (fd >= 0) {
      build_hole_map(fd, (uint64_t)st.st_size, &w->holes, &w->hole_count, 
        &w->stored_size);
      close(fd);
    }
  }

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
  /* Local variables */
  uint64_t offset = 0;
  int i;

  /* Code */
  for (i = 0; i < count; i++) {
    items[i].dest = mmap_base + offset;
    offset += sizeof(FileHeader)
      + items[i].hole_count * sizeof(HoleEntry)
      + items[i].stored_size;
  }
  return offset;
}

/* ----------------------------------------------------------------------------
 * fill_header
 *
 * Zero and populate a FileHeader. Centralises the field assignments that
 * would otherwise be duplicated in write_item and pack_file.
 * checksum is NOT set here because it covers both the header and the file
 * data, which is only available after fill_header returns. Callers must
 * call checksum_compute() and store the result in h->checksum themselves.
 * ------------------------------------------------------------------------- */
static void fill_header(FileHeader *h, const char *filepath,
    uint64_t file_size, uint64_t stored_size, uint64_t hole_count,
    uint32_t mode, uint32_t uid, uint32_t gid, int64_t mtime){
  /* Code */
  memset(h, 0, sizeof(*h));
  memcpy(h->magic, SPARE_MAGIC, 3);
  h->version = SPARE_VERSION;
  h->file_size = file_size;
  h->stored_size = stored_size;
  h->hole_count = hole_count;
  h->mode = mode;
  h->uid = uid;
  h->gid = gid;
  h->mtime = mtime;
  strncpy(h->filename, filepath, SPARE_MAX_PATH - 1);
  h->filename[SPARE_MAX_PATH - 1] = '\0';
}

/* ----------------------------------------------------------------------------
 *  write_item
 *
 *  Write one WorkItem (header + hole map + file data) directly into its
 *  assigned mmap region. Called from worker threads.
 *  Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */

static int write_item(const WorkItem *w, int verbose){
  /* Local variables */
  FileHeader *header = (FileHeader *)w->dest;
  HoleEntry *hole_dest;
  uint8_t *data_dest;
  uint8_t buf[COPY_BUFFER_SIZE];
  int src_fd;

  /* Code */
  fill_header(header, w->filepath, w->file_size, w->stored_size, w->hole_count,
    w->mode, w->uid, w->gid, w->mtime);

  hole_dest = (HoleEntry *)(w->dest + sizeof(FileHeader));
  if (w->hole_count > 0)
    memcpy(hole_dest, w->holes, w->hole_count * sizeof(HoleEntry));

  data_dest = w->dest + sizeof(FileHeader) + w->hole_count * sizeof(HoleEntry);

  if (S_ISLNK(w->mode)) {
    memcpy(data_dest, w->linkpath, w->file_size);
    header->checksum = checksum_compute(header, NULL, 0, data_dest, w->file_size);
    if (verbose)
      printf("packed: '%s' -> '%s'\n", w->filepath, w->linkpath);
    return 0;
  }

  src_fd = open(w->filepath, O_RDONLY);
  if (src_fd < 0) {
    perror(w->filepath);
    return -1;
  }

  if (foreach_data_region_fd(src_fd, w->filepath, w->holes, w->hole_count, w->file_size,
        buf, sizeof(buf), memcpy_chunk, &data_dest) != 0) {
    close(src_fd);
    return -1;
  }

  close(src_fd);

  header->checksum = checksum_compute(header, w->holes, w->hole_count,
    w->dest + sizeof(FileHeader) + w->hole_count * sizeof(HoleEntry),
    w->stored_size);

  if (verbose)
    printf("packed: '%s' (%llu + %llu + %llu bytes)\n",
      w->filepath,
      (unsigned long long)sizeof(FileHeader),
      (unsigned long long)(w->hole_count * sizeof(HoleEntry)),
      (unsigned long long)w->stored_size);

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

int pack_threads(const char *archive_path, const char **filepaths, int count,
  int sparse, int verbose) {
  /* Local variables */
  WorkItem *items = NULL;
  int n_items = 0;
  int capacity = 0;
  int result = 0;
  uint64_t total_size = 0;
  int fd = -1;
  uint8_t *mmap_base = NULL;
  int n_threads = 0;
  pthread_t *threads = NULL;
  ThreadArgs *args = NULL;
  int base = 0;
  int extra = 0;
  int offset = 0;
  int i = 0;
  int t = 0;

  /* Code */
  /* --- phase 1: collect all files into a flat array ------------- */
  /* For sparse mode, hole detection is folded into collect_files:  */
  /* each file is opened, lseeked for holes, closed, no data read. */

  if (verbose)
    printf("pre-allocating files in memory ...\n");

  for (i = 0; i < count; i++) {
    if (collect_files(filepaths[i], &items, &n_items, &capacity, sparse) != 0)
        result = -1;
  }

  if (n_items == 0) {
    fprintf(stderr, "error: no files to pack\n");
    free(items);
    return -1;
  }

  /* --- phase 2: calculate total archive size -------------------- */

  total_size = 0;
  for (i = 0; i < n_items; i++)
    total_size += sizeof(FileHeader)
      + items[i].hole_count * sizeof(HoleEntry)
      + items[i].stored_size;

  if (verbose)
    printf("pack: %d files, total archive size: %llu bytes\n",
            n_items, (unsigned long long)total_size);

  /* --- phase 3: open and pre-allocate output file --------------- */

  fd = open(archive_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
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

  mmap_base = mmap(NULL, total_size, PROT_WRITE, MAP_SHARED, fd, 0);
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

  n_threads = g_nthreads;
  if (n_threads > n_items) n_threads = n_items;  /* cap at file count */

  threads = malloc(n_threads * sizeof(pthread_t));
  args = malloc(n_threads * sizeof(ThreadArgs));
  if (!threads || !args) {
    free(threads); free(args); free(items);
    munmap(mmap_base, total_size);
    return -1;
  }

  /* distribute items evenly, first (n_items % n_threads) threads  */
  /* get one extra item so no file is left unassigned               */
  base = n_items / n_threads;
  extra = n_items % n_threads;
  offset = 0;

  for (t = 0; t < n_threads; t++) {
    args[t].items = &items[offset];
    args[t].count = base + (t < extra ? 1 : 0);
    args[t].verbose = verbose;
    args[t].result = 0;
    offset += args[t].count;

    if (pthread_create(&threads[t], NULL, worker_thread, &args[t]) != 0) {
      perror("pthread_create");
      worker_thread(&args[t]);  /* fall back to main thread */
    }
  }

  /* --- phase 7: join all threads -------------------------------- */

  for (t = 0; t < n_threads; t++) {
    pthread_join(threads[t], NULL);
    if (args[t].result != 0) result = -1;
  }

  /* --- phase 8: flush and release the mapping ------------------- */

  if (msync(mmap_base, total_size, MS_SYNC) != 0) {
    perror("msync");
    result = -1;
  }

  munmap(mmap_base, total_size);
  free(threads);
  free(args);

  for (i = 0; i < n_items; i++)
    free(items[i].holes);
  free(items);

  return result;
}


/* ----------------------------------------------------------------------------
 * pack_file
 *
 * Write one file into the open archive.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int pack_file(FILE *archive, const char *filepath, int sparse, int verbose){
  /* Local variables */
  struct stat st;
  int src_fd;
  DIR *dir;
  struct dirent *entry;
  FileHeader header;
  char buf[COPY_BUFFER_SIZE];
  char fullpath[SPARE_MAX_PATH];
  char linkbuf[SPARE_MAX_PATH];
  ssize_t linklen;
  XXH64_state_t state;
  WriteCtx wctx;
  HoleEntry *holes;
  uint64_t hole_count;
  uint64_t stored_size;
  int result = 0;

  /* Code */
  holes = NULL;
  hole_count = 0;
  stored_size = 0;

  if (lstat(filepath, &st) != 0) {
    perror(filepath);
    return -1;
  }

  /* Symlink: store the link target as file data */
  if (S_ISLNK(st.st_mode)) {
    linklen = readlink(filepath, linkbuf, sizeof(linkbuf) - 1);
    if (linklen < 0) {
      perror(filepath);
      return -1;
    }
    linkbuf[linklen] = '\0';

    fill_header(&header, filepath, (uint64_t)linklen, (uint64_t)linklen, 0,
      (uint32_t)st.st_mode, (uint32_t)st.st_uid, (uint32_t)st.st_gid,
      (int64_t)st.st_mtime);
    header.checksum = checksum_compute(&header, NULL, 0,
      linkbuf, (uint64_t)linklen);

    if (fwrite(&header, sizeof(header), 1, archive) != 1) {
      fprintf(stderr, "error: failed to write header for '%s'\n", filepath);
      return -1;
    }
    if (fwrite(linkbuf, 1, linklen, archive) != (size_t)linklen) {
      fprintf(stderr, "error: failed to write symlink target for '%s'\n",
        filepath);
      return -1;
    }

    if (verbose)
      printf("packed: '%s' -> '%s'\n", filepath, linkbuf);
    return 0;
  }

  /* Directory: recurse */
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
      if (pack_file(archive, fullpath, sparse, verbose) != 0) result = -1;
    }
    closedir(dir);
    return result;
  }

  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "skipping '%s': not a regular file\n", filepath);
    return -1;
  }

  /* Open source */
  src_fd = open(filepath, O_RDONLY);
  if (src_fd < 0){
    perror(filepath);
    return -1;
  }

  stored_size = (uint64_t)st.st_size;

  if (sparse && st.st_size > 0)
    build_hole_map(src_fd, (uint64_t)st.st_size, &holes, &hole_count, &stored_size);

  fill_header(&header, filepath, (uint64_t)st.st_size, stored_size, hole_count,
    (uint32_t)st.st_mode, (uint32_t)st.st_uid,
    (uint32_t)st.st_gid, (int64_t)st.st_mtime);

  /* Hash pass: compute checksum. Source stays in page cache so the re-read
   * for the write pass costs negligible extra I/O. */
  XXH64_reset(&state, 0);
  XXH64_update(&state, &header, sizeof(header));
  if (holes && hole_count > 0)
    XXH64_update(&state, holes, hole_count * sizeof(HoleEntry));
  if (foreach_data_region_fd(src_fd, filepath, holes, hole_count,
        (uint64_t)st.st_size, buf, sizeof(buf), hash_chunk, &state) != 0) {
    close(src_fd); free(holes); return -1;
  }
  header.checksum = (uint64_t)XXH64_digest(&state);

  if (fwrite(&header, sizeof(header), 1, archive) != 1) {
    fprintf(stderr, "error: failed to write header for '%s'\n", filepath);
    close(src_fd); free(holes); return -1;
  }
  if (holes && hole_count > 0) {
    if (fwrite(holes, sizeof(HoleEntry), hole_count, archive) != hole_count) {
      fprintf(stderr, "error: failed to write hole map for '%s'\n", filepath);
      close(src_fd); free(holes); return -1;
    }
  }

  /* Write pass: archive is written linearly; no seeking, buffer stays full. */
  wctx.dst = archive; wctx.path = filepath;
  if (foreach_data_region_fd(src_fd, filepath, holes, hole_count,
        (uint64_t)st.st_size, buf, sizeof(buf), write_chunk, &wctx) != 0) {
    close(src_fd); free(holes); return -1;
  }

  close(src_fd);
  free(holes);

  if (verbose)
    printf("packed: '%s' (%llu + %llu + %llu bytes)\n",
      filepath,
      (unsigned long long)sizeof(FileHeader),
      (unsigned long long)(hole_count * sizeof(HoleEntry)),
      (unsigned long long)stored_size);

  return 0;
}

/* ----------------------------------------------------------------------------
 * pack
 *
 * Create an archive at 'archive_path' containing all files in
 * 'filepaths[0..count-1]'.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int pack(FILE *archive, const char **filepaths, int count, int sparse, int verbose){
  /* Local variables */
  int   result = 0;
  int   it = 0;

  /* Code */

  for (it = 0; it < count; ++it){
    if(pack_file(archive, filepaths[it], sparse, verbose) != 0){
      result = -1; /* record failure but keep packing */
    }
  }

  return result;
}
