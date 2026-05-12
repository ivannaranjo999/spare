#include "sar.h"

/* ----------------------------------------------------------------------------
 * dircache_init
 *
 * Inits dircache struct.
 * ------------------------------------------------------------------------- */
void dircache_init(DirCache *c){
  c->dirs     = NULL;
  c->count    = 0;
  c->capacity = 0;
}

/* ----------------------------------------------------------------------------
 * dircache_free
 *
 * Frees dircache struct.
 * ------------------------------------------------------------------------- */
void dircache_free(DirCache *c){
  /* Local variables */
  int i;
 
  /* Code */
  for (i = 0; i < c->count; i++)
    free(c->dirs[i]);
  free(c->dirs);
  c->dirs     = NULL;
  c->count    = 0;
  c->capacity = 0;
}

/* ----------------------------------------------------------------------------
 * dircache_search
 *
 * Performs binary search in DirCache struct.
 * Returns found or insertion value
 * ------------------------------------------------------------------------- */
static int dircache_search(DirCache *c, const char *path){
  /* Local variables */
  int lo = 0;
  int hi = c->count - 1;
  int mid, cmp;
 
  /* Code */
  while (lo <= hi) {
    mid = (lo + hi) / 2;
    cmp = strcmp(c->dirs[mid], path);
    if (cmp == 0) return mid; /* found */
    if (cmp < 0) lo = mid + 1;
    else hi = mid - 1;
  }
  return lo; /* insertion point */
}

/* ----------------------------------------------------------------------------
 * dircache_contains
 *
 * Performs binary search in DirCache struct.
 * Returns 1 if path is in cache, 0 if not
 * ------------------------------------------------------------------------- */
static int dircache_contains(DirCache *c, const char *path){
  /* Local variables */
  int idx;
 
  /* Code */
  if (c->count == 0) return 0;
  idx = dircache_search(c, path);
  return (idx < c->count && strcmp(c->dirs[idx], path) == 0);
}
 
/* ----------------------------------------------------------------------------
 * dircache_insert
 *
 * Insert path into cache at sorted position.
 * Returns 0 on success, -1 on error
 * ------------------------------------------------------------------------- */
static int dircache_insert(DirCache *c, const char *path){
  /* Local variables */
  int    idx;
  int    new_cap;
  char **tmp;
  char  *copy;
 
  /* Code */
  /* grow if needed */
  if (c->count == c->capacity) {
    new_cap = c->capacity == 0 ? 64 : c->capacity * 2;
    tmp = realloc(c->dirs, new_cap * sizeof(char *));
    if (tmp == NULL) {
      perror("realloc dircache");
      return -1;
    }
    c->dirs = tmp;
    c->capacity = new_cap;
  }
 
  copy = strdup(path);
  if (copy == NULL) {
    perror("strdup dircache");
    return -1;
  }
 
  /* find insertion point and shift entries right */
  idx = dircache_search(c, path);
  memmove(&c->dirs[idx + 1], &c->dirs[idx],
          (c->count - idx) * sizeof(char *));
  c->dirs[idx] = copy;
  c->count++;
  return 0;
}

/* ----------------------------------------------------------------------------
 * mkdir_parents
 *
 * Creates all parent directories for a given filepath.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int mkdir_parents(const char *filepath, DirCache *cache, int verbose){
  char  tmp[SAR_MAX_PATH];
  char *p;

  strncpy(tmp, filepath, SAR_MAX_PATH - 1);
  tmp[SAR_MAX_PATH - 1] = '\0';

  /* Walk every '/' in the path and mkdir up to that point */
  for (p = tmp + 1; *p; p++){
    if (*p == '/'){
      *p = '\0';

      /* Skip if already created */
      if (!dircache_contains(cache, tmp)) {
        if (verbose) printf("creating '%s' path ...\n", tmp);
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST){
          perror(tmp);
          *p = '/';
          return -1;
        }
        /* record it regardless of whether mkdir or EEXIST */
        dircache_insert(cache,tmp);
      }
      *p = '/';
    }
  }
  return 0;
}

/* ----------------------------------------------------------------------------
 * unpack_file
 *
 * Read one (header + data) block from the archive and write the file to disk.
 * The archive must be positioned at the start of a FileHeader when this is
 * called.
 * Returns 1 if a file was extracted successfully,
 *         0 if we have reached EOF
 *        -1 on error
 * ------------------------------------------------------------------------- */
int unpack_file(FILE *archive, DirCache *cache , int verbose){
  /* Local variables */
  FileHeader header;
  FILE *dst;
  char buf[COPY_BUFFER_SIZE];
  uint64_t remaining, len;
  size_t bytes_read, to_write, n, chunk;
  struct utimbuf times;
  char linkbuf[SAR_MAX_PATH];

  /* Code */
  /* Read header */
  n = fread(&header, sizeof(header), 1, archive);

  if(n == 0) {
    if(feof(archive)) return 0;
    perror("fread header");
    return -1;
  }

  /* Validate magic bytes */
  if(memcmp(header.magic, SAR_MAGIC, 3) != 0){
    fprintf(stderr, "error: bad magic - make sure this is a sar archive\n");
    return -1;
  }

  /* Validate version */
  if(header.version != SAR_VERSION){
    fprintf(stderr, "error: unsupported archive version %d\n", header.version);
    return -1;
  }

  /* Ensure filename is null terminated */
  header.filename[SAR_MAX_PATH - 1] = '\0';

  /* Create parent dirs if needed */
  if(mkdir_parents(header.filename, cache, verbose) != 0){
    fseek(archive, (long)header.file_size, SEEK_CUR);
    fprintf(stderr, "error: could not create parent dirs for '%s'\n", header.filename);
    return -1;
  }

  /* Restore symlink */
  if (S_ISLNK(header.mode)) {
    len = header.file_size < SAR_MAX_PATH - 1 ? 
      header.file_size : SAR_MAX_PATH - 1;

    /* read target path string from archive */
    if (fread(linkbuf, 1, len, archive) != len) {
      fprintf(stderr, "error: failed to read symlink target for '%s'\n",
        header.filename);
      return -1;
    }
    linkbuf[len] = '\0';

    /* remove existing file/link at destination if any */
    unlink(header.filename);

    if (symlink(linkbuf, header.filename) != 0) {
      perror(header.filename);
      return -1;
    }

    if (verbose)
      printf("unpacked: '%s' -> '%s'\n", header.filename, linkbuf);

    return 1;
  }

  /* Open destination file */
  dst = fopen(header.filename, "wb");
  if (dst == NULL){
    perror(header.filename);
    fseek(archive, (long)header.file_size, SEEK_CUR);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SAR_FILE_BUF_SIZE);

  /* Copy data from archive to destination file */
  remaining = header.file_size;

  while(remaining > 0){
    chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);

    bytes_read = fread(buf, 1, chunk, archive);
    if(bytes_read == 0){
      fprintf(stderr,
        "error: unexpected end of archive while reading '%s'\n",
        header.filename);
      fclose(dst);
      return -1;
    }

    to_write = bytes_read;
    if(fwrite(buf, 1, to_write, dst) != to_write){
      fprintf(stderr,
        "error: failed to write to '%s'\n",
        header.filename);
      fclose(dst);
      return -1;
    }

    remaining -= bytes_read;
  }

  fclose(dst);

  /* Restore permissions */
  if(chmod(header.filename, (mode_t)header.mode) != 0){
    perror("chmod");
  }

  /* Restore modification time */
  times.actime  = (time_t)header.mtime; /* access time = mtime */
  times.modtime = (time_t)header.mtime; /* last modified time */

  if (utime(header.filename, &times) != 0) {
    perror("utime");
  }

  if(verbose)
    printf("unpacked: '%s' (%llu bytes)\n",
      header.filename, (unsigned long long)header.file_size);

  return 1;
}

/* ----------------------------------------------------------------------------
 * unpack
 *
 * Extract all files from the archive at 'archive_path'.
 * Returns 0 on success, -1 if any file failed.
 * ------------------------------------------------------------------------- */
int unpack(FILE *archive, int verbose){
  /* Local variables */
  int result = 0;
  int status;
  DirCache cache;

  /* Code */
  /* Init mkdir cache */
  dircache_init(&cache);

  while((status = unpack_file(archive, &cache ,verbose)) == 1){
    /* Keep going until EOF or error */
  }

  /* Free mkdir cache */
  dircache_free(&cache);

  if(status == -1) result = -1;

  return result;
}
