#include "sar.h"

/* ----------------------------------------------------------------------------
 * filename_matches
 *
 * See if given filename matches a list of filepaths.
 * Returns 0 on success, -1 if any file failed.
 * ------------------------------------------------------------------------- */
static int filename_matches(const char *archived_name, const char **filepaths, int count){
  /* Local variables */
  int index = 0;

  /* Code */
  for (index = 0; index < count ; ++index){
    if (strstr(archived_name, filepaths[index]) != NULL) return -1;
  }
  return 0;
}

/* ----------------------------------------------------------------------------
 * grab
 *
 * Extract given files from the archive at 'archive_path'.
 * Returns 0 on success, -1 if any file failed.
 * ------------------------------------------------------------------------- */
int grab(FILE *archive, const char **filepaths, int count, int verbose){
  /* Local variables */
  int result = 0;
  int status = 0;
  int exitLoop = 0;
  int matched = 0;
  size_t n = 0;
  FileHeader header;
  DirCache cache;

  /* Code */
  /* Read first block */
  n = fread(&header, sizeof(header), 1, archive);

  if(n == 0) {
    if(feof(archive)){
      exitLoop = 1;
    } else {
      perror("fread header");
      result = -1;
    }
  } else {
    if(memcmp(header.magic, SAR_MAGIC, 3) != 0){
      fprintf(stderr, "error: bad magic - not a SAR archive\n");
      result = -1;
      exitLoop = 1;
    } else if(header.version != SAR_VERSION){
      fprintf(stderr, "error: unsupported archive version %d\n", header.version);
      result = -1;
      exitLoop = 1;
    }
  }

  /* Init mkdir cache */
  dircache_init(&cache);

  /* Exit loop when EOF reached */
  while(exitLoop == 0){
    matched = 0;

    /* Check if next block is any of the files to find */
    if(filename_matches(header.filename, filepaths, count)){
      if (verbose) fprintf(stdout, "grab: found file '%s'\n", header.filename);
      /* Jump to beginning of the block */
      fseek(archive, -sizeof(FileHeader), SEEK_CUR);

      /* Extract file */
      status = unpack_file(archive, &cache ,verbose);
      if(status == -1) {
        result = -1;
        fprintf(stderr, "error: could not unpack '%s'\n", header.filename);
      }

      matched = 1;
    }

    if (matched != 1) {
      /* Jump to next file if not found */
      fseek(archive, (long)header.file_size, SEEK_CUR);
    }

    /* Read next block */
    n = fread(&header, sizeof(header), 1, archive);

    if(n == 0) {
      /* Check if EOF */
      if(feof(archive)){
        exitLoop = 1;
      } else {
        /* Error found */
        perror("fread header");
        status = -1;
      }
    } else {
      if(memcmp(header.magic, SAR_MAGIC, 3) != 0){
        fprintf(stderr, "error: bad magic - not a SAR archive\n");
        result = -1;
        exitLoop = 1;
      } else if(header.version != SAR_VERSION){
        fprintf(stderr, "error: unsupported archive version %d\n", header.version);
        result = -1;
        exitLoop = 1;
      }
    }
  }

  /* Free mkdir cache */
  dircache_free(&cache);

  return result;
}
