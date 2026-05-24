#include "spare.h"

/* ----------------------------------------------------------------------------
 * get_filename
 *
 * Read one (header + data) block from the archive and write the file to disk.
 * The archive must be positioned at the start of a FileHeader when this is
 * called.
 * Returns 1 if a file was extracted successfully,
 *         0 if we have reached EOF
 *        -1 on error
 * ------------------------------------------------------------------------- */
static int get_filename(FILE *archive){
  /* Local variables */
  FileHeader header;
  size_t n;

  /* Code */
  /* Read header */
  n = fread(&header, sizeof(header), 1, archive);

  if(n == 0) {
    if(feof(archive)) return 0;
    perror("fread header");
    return -1;
  }

  if(memcmp(header.magic, SPARE_MAGIC, 3) != 0){
    fprintf(stderr, "error: bad magic - not a SAR archive\n");
    return -1;
  }
  if(header.version != SPARE_VERSION){
    fprintf(stderr, "error: unsupported archive version %d\n", header.version);
    return -1;
  }

  /* Ensure filename is null terminated */
  header.filename[SPARE_MAX_PATH - 1] = '\0';

  /* Print filename */
  fprintf(stdout, "%s\n", header.filename);

  /* Jump to next block */
  fseek(archive, (long)(header.hole_count * sizeof(HoleEntry)
     + header.stored_size), SEEK_CUR);

  return 1;
}

/* ----------------------------------------------------------------------------
 * list
 *
 * Shows all filenames inside a .sar file
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int list(FILE *archive){
  /* Local variables */
  int   result = 0;
  int   status;

  /* Code */
  while((status = get_filename(archive)) == 1){
    /* Keep going until EOF or error */
  }

  if(status == -1) result = -1;

  return result;
}