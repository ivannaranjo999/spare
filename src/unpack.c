#include "sar.h"

/* ----------------------------------------------------------------------------
 * mkdir_parents
 *
 * Creates all parent directories for a given filepath.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int mkdir_parents(const char *filepath){
  char  tmp[SAR_MAX_PATH];
  char *p;

  strncpy(tmp, filepath, SAR_MAX_PATH - 1);
  tmp[SAR_MAX_PATH - 1] = '\0';

  /* Walk every '/' in the path and mkdir up to that point */
  for (p = tmp + 1; *p; p++){
    if (*p == '/'){
      *p = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST){
        perror(tmp);
        return -1;
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
int unpack_file(FILE *archive, int verbose){
  /* Local variables */
  FileHeader     header;
  FILE          *dst;
  char           buf[COPY_BUFFER_SIZE];
  uint64_t       remaining;
  size_t         bytes_read, to_write, n, chunk;
  struct utimbuf times;

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
  if(mkdir_parents(header.filename) != 0){
    fseek(archive, (long)header.file_size, SEEK_CUR);
    fprintf(stderr, "error: could not create parent dirs for '%s'\n", header.filename);
    return -1;
  }

  /* Open destination file */
  dst = fopen(header.filename, "wb");
  if (dst == NULL){
    perror(header.filename);
    fseek(archive, (long)header.file_size, SEEK_CUR);
    return -1;
  }

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
  int   result = 0;
  int   status;

  /* Code */
  while((status = unpack_file(archive, verbose)) == 1){
    /* Keep going until EOF or error */
  }

  if(status == -1) result = -1;

  return result;
}
