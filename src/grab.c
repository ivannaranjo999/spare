#include "spare.h"

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
 * read_header_and_name: read FileHeader + variable-length filename from 
 * archive.
 * Returns 1 on success, 0 on clean EOF, -1 on error.
 * ------------------------------------------------------------------------- */
static int read_header_and_name(FILE *archive, FileHeader *h, char *filename){
  size_t n = fread(h, sizeof(*h), 1, archive);
  if(n == 0){
    if(feof(archive)) return 0;
    perror("fread header");
    return -1;
  }
  if(memcmp(h->magic, SPARE_MAGIC, 3) != 0){
    fprintf(stderr, "error: bad magic - not a SPA archive\n");
    return -1;
  }
  if(h->version != SPARE_VERSION){
    fprintf(stderr, "error: unsupported archive version %d\n", h->version);
    return -1;
  }
  if(fread(filename, 1, h->name_len, archive) != h->name_len){
    fprintf(stderr, "error: failed to read filename\n");
    return -1;
  }
  filename[h->name_len] = '\0';
  return 1;
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
  int is_root;
  FileHeader header;
  char filename[SPARE_MAX_PATH];
  DirCache cache;

  /* Code */
  /* Read first block */
  status = read_header_and_name(archive, &header, filename);
  if(status == 0){
    exitLoop = 1;
  } else if(status == -1){
    result = -1;
    exitLoop = 1;
  }

  is_root = (getuid() == 0);
  dircache_init(&cache);

  /* Exit loop when EOF reached */
  while(exitLoop == 0){
    matched = 0;

    /* Check if next block is any of the files to find */
    if(filename_matches(filename, filepaths, count)){
      if (verbose) fprintf(stdout, "grab: found file '%s'\n", filename);
      /* Seek back to start of block (header + name already consumed) */
      fseek(archive, -(long)(sizeof(FileHeader) + header.name_len), SEEK_CUR);

      /* Extract file */
      status = unpack_file(archive, &cache, is_root, verbose);
      if(status == -1) {
        result = -1;
        fprintf(stderr, "error: could not unpack '%s'\n", filename);
      }

      matched = 1;
    }

    if (matched != 1) {
      /* Jump to next file if not found */
      fseek(archive, (long)(header.hole_count * sizeof(HoleEntry)
        + header.stored_size), SEEK_CUR);
    }

    /* Read next block */
    status = read_header_and_name(archive, &header, filename);
    if(status == 0){
      exitLoop = 1;
    } else if(status == -1){
      result = -1;
      exitLoop = 1;
    }
  }

  /* Free mkdir cache */
  dircache_free(&cache);

  return result;
}
