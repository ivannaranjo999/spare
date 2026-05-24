#include "spare.h"

#define SPARE_PRINT_VERSION "v3.0" /* release version */

/* ----------------------------------------------------------------------------
 * Function helpers
 * ------------------------------------------------------------------------- */

/* Arguments for unpack function */
int do_unpack(FILE *fp, void *user_data){
  UnpackArgs *a = (UnpackArgs *)user_data;
  return unpack(fp, a->verbose);
}

/* Arguments for list function */
int do_list(FILE *fp, void *user_data){
  (void)user_data;
  return list(fp);
}

/* Arguments for grab function */
int do_grab(FILE *fp, void *user_data){
  GrabArgs *a = (GrabArgs *)user_data;
  return grab(fp, a->filepaths, a->nfiles, a->verbose);
}

/* Arguments for pack function */
int do_pack(FILE *fp, void *user_data){
  PackArgs *a = (PackArgs *)user_data;
  return pack(fp, a->filepaths, a->nfiles, a->sparse, a->verbose);
}

/* Arguments for insert function */
int do_insert(FILE *fp, void *user_data){
  InsertArgs *a = (InsertArgs *)user_data;
  return insert(fp, a->filepaths, a->nfiles, a->sparse, a->verbose);
}

/* ----------------------------------------------------------------------------
 * detect_archive_format
 * 
 * Returns format of the given SAR archive. Can be SZT or SAR.
 * ------------------------------------------------------------------------- */
ArchiveFormat detect_archive_format(const char *archive_path, int verbose){
  /* Local variables */
  unsigned char magic[4];
  FILE *archive;
  size_t n = 0;

  /* Code */
  archive = fopen(archive_path, "rb");
  if(archive == NULL){
    return ARCHIVE_DOESNOTEXIST;
  }

  n = fread(magic, 1, sizeof(magic), archive);
  fclose(archive);

  if (n < 2) return ARCHIVE_UNKNOWN;

  /* zstd magic: 0xFD2FB528 stored little-endian = 28 B5 2F FD */
  if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xB5 &&
      magic[2] == 0x2F && magic[3] == 0xFD) {
    if (verbose) printf("'%s' detected as compressed SAR archive\n",
                        archive_path);
    return ARCHIVE_SZT;
  }

  if (n >= 3 && memcmp(magic, SPARE_MAGIC, 3) == 0){
    if (verbose) printf("'%s' detected as SAR archive\n", archive_path);
    return ARCHIVE_SAR;
  }

  return ARCHIVE_UNKNOWN;

}

/* ----------------------------------------------------------------------------
 * check_archive_version
 *
 * Opens path, reads the first FileHeader, and verifies that the magic and
 * version match the compiled-in SPARE_VERSION.
 * Returns 0 on success, -1 on mismatch or read error.
 * ------------------------------------------------------------------------- */
int check_archive_version(const char *path){
  /* Local variables */
  FILE *f;
  FileHeader h;
  size_t n;

  /* Code */
  f = fopen(path, "rb");
  if(f == NULL){
    perror(path);
    return -1;
  }

  n = fread(&h, sizeof(h), 1, f);
  fclose(f);

  if(n == 0){
    fprintf(stderr, "error: could not read header from '%s'\n", path);
    return -1;
  }
  if(memcmp(h.magic, SPARE_MAGIC, 3) != 0){
    fprintf(stderr, "error: bad magic in '%s' - not a SAR archive\n", path);
    return -1;
  }
  if(h.version != SPARE_VERSION){
    fprintf(stderr, "error: archive '%s' uses format version %d, "
            "this build requires version %d\n", path, h.version, SPARE_VERSION);
    return -1;
  }

  return 0;
}

/* ----------------------------------------------------------------------------
 * decompress_in_ram_and_run
 * 
 * Generic action function call with previous decompression step
 * ------------------------------------------------------------------------- */
int decompress_in_ram_and_run(const char *src_path, ActionFn action_fn,
    void *user_data, int verbose) {
  /* Local variables */
  FILE *fp = NULL;
  pthread_t tid;
  DecompressRamArgs args;
  int ret;
 
  /* Code */
  if (decompress_arch_ram(&fp, src_path, &tid, &args, verbose) != 0) {
    fprintf(stderr, "error: decompress failed\n");
    return -1;
  }

  setvbuf(fp, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);
 
  ret = action_fn(fp, user_data);
 
  /* MUST fclose before join, unblocks the decompression thread */
  fclose(fp);
 
  if (decompress_arch_ram_join(tid, &args) != 0) {
    fprintf(stderr, "error: decompression thread failed\n");
    return -1;
  }
 
  return ret;
}

/* ----------------------------------------------------------------------------
 * decompress_in_disk_and_run
 * 
 * Generic action function call with previous decompression step
 * ------------------------------------------------------------------------- */
int decompress_in_disk_and_run(const char *dst_path, const char *src_path,
    const char *mode, ActionFn action_fn, void *user_data, int verbose) {
  /* Local variables */
  FILE *fp = NULL;
  int ret;
 
  /* Code */
  if (decompress_arch(dst_path, src_path, verbose) != 0) {
    fprintf(stderr, "error: decompress failed\n");
    return -1;
  }

  fp = fopen(dst_path, mode);
  if (fp == NULL){
    perror(dst_path);
    return -1;
  }

  setvbuf(fp, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);
 
  ret = action_fn(fp, user_data);
 
  fclose(fp);
 
  return ret;
}

/* ----------------------------------------------------------------------------
 * just_run
 *
 * Generic action function call with NO previous decompression step.
 * If archive_path is "-", uses stdin (mode "rb") or stdout (mode "wb"/"ab").
 * ------------------------------------------------------------------------- */
int just_run(const char *archive_path, const char *mode, ActionFn action_fn, void *user_data) {
  /* Local variables */
  FILE *fp = NULL;
  int is_stdio = 0;
  int ret;

  /* Code */
  is_stdio = (strcmp(archive_path, "-") == 0);

  if (is_stdio) {
    fp = (mode[0] == 'r') ? stdin : stdout;
  } else {
    fp = fopen(archive_path, mode);
    if (fp == NULL) {
      perror(archive_path);
      return -1;
    }
    setvbuf(fp, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);
  }

  ret = action_fn(fp, user_data);

  if (is_stdio) {
    if (mode[0] != 'r') fflush(fp);
  } else {
    fclose(fp);
  }
  return ret;
}

/* ----------------------------------------------------------------------------
 * stream_file_to_stdout
 *
 * Copy contents of path to stdout. Used after pack_threads writes to a temp
 * file so the caller can stream the result without losing multithreading.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int stream_file_to_stdout(const char *path) {
  /* Local variables */
  FILE *src = NULL;
  char buf[COPY_BUFFER_SIZE];
  size_t n;

  /* Code */
  src = fopen(path, "rb");
  if (src == NULL) {
    perror(path);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);

  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (fwrite(buf, 1, n, stdout) != n) {
      fprintf(stderr, "error: failed to stream '%s' to stdout\n", path);
      fclose(src);
      return -1;
    }
  }

  if (ferror(src)) {
    fprintf(stderr, "error: failed to read '%s'\n", path);
    fclose(src);
    return -1;
  }

  fclose(src);
  fflush(stdout);
  return 0;
}

/* ----------------------------------------------------------------------------
 * buffer_stdin_to_file
 *
 * Copy all of stdin into dst_path. Used when reading a compressed archive
 * from stdin: we buffer it to a seekable temp file so the decompressor can
 * read it normally.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int buffer_stdin_to_file(const char *dst_path) {
  /* Local variables */
  FILE *dst = NULL;
  char buf[COPY_BUFFER_SIZE];
  size_t n;

  /* Code */
  dst = fopen(dst_path, "wb");
  if (dst == NULL) {
    perror(dst_path);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);

  while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      fprintf(stderr, "error: failed to buffer stdin to '%s'\n", dst_path);
      fclose(dst);
      return -1;
    }
  }

  if (ferror(stdin)) {
    fprintf(stderr, "error: failed to read stdin\n");
    fclose(dst);
    return -1;
  }

  fclose(dst);
  return 0;
}

/* ----------------------------------------------------------------------------
 * checksum_compute
 *
 * Computes xxh64 over FileHeader (checksum field zeroed) + hole map + data.
 * Pass holes=NULL & hole_count=0 for non-sparse files (symlinks, dense files).
 * ------------------------------------------------------------------------- */
uint64_t checksum_compute(const FileHeader *h, const HoleEntry *holes,
  uint64_t hole_count, const void *data, uint64_t size) {
  /* Local variables */
  FileHeader tmp;
  XXH64_state_t state;

  /* Code */
  memcpy(&tmp, h, sizeof(tmp));
  tmp.checksum = 0;
  XXH64_reset(&state, 0);
  XXH64_update(&state, &tmp, sizeof(tmp));
  if (holes && hole_count > 0)
    XXH64_update(&state, holes, hole_count * sizeof(HoleEntry));
  XXH64_update(&state, data, size);
  return (uint64_t)XXH64_digest(&state);
}

/* ----------------------------------------------------------------------------
 * usage
 * 
 * Help information
 * ------------------------------------------------------------------------- */
void usage(const char *name){
  print_version(name);
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "Actions:\n");
  fprintf(stderr, "  %s p   <archive.sar> <file1..fileN>       Pack given files or folders to a SAR archive.\n", name);
  fprintf(stderr, "  %s pz  <archive.szt> <file1..fileN>       Pack given files or folders to a SAR archive and compress it.\n", name);
  fprintf(stderr, "  %s u   <archive.sar|.szt>                 Unpack SAR archive.\n", name);
  fprintf(stderr, "  %s l   <archive.sar|.szt>                 List files contained in a SAR archive.\n", name);
  fprintf(stderr, "  %s g   <archive.sar|.szt> <file1..fileN>  Grab specific files contained in a SAR archive.\n", name);
  fprintf(stderr, "  %s i   <archive.sar|.szt> <file1..fileN>  Insert specific files to a SAR archive.\n", name);
  fprintf(stderr, "Flags:\n");
  fprintf(stderr, "  -h         prints this information.\n");
  fprintf(stderr, "  -v         verbose output.\n");
  fprintf(stderr, "  -j [N]     use N threads for packing and compression (default: all cores).\n");
  fprintf(stderr, "  -z         when archive path is '-', treat stdin as compressed (SZT).\n");
  fprintf(stderr, "  -S         detect and preserve sparse holes (VM images, database files).\n");
  fprintf(stderr, "\nPipeline:\n");
  fprintf(stderr, "  Use '-' as archive path to read/write stdin/stdout.\n");
  fprintf(stderr, "  %s p  - <file1..fileN>  | %s u  -       Pack and extract via pipe.\n", name, name);
  fprintf(stderr, "  %s pz - <file1..fileN>  | %s u  - -z    Pack compressed (zstd) and extract via pipe.\n", name, name);
}

/* ----------------------------------------------------------------------------
 * print_version
 * 
 * Prints SAR version
 * ------------------------------------------------------------------------- */
void print_version (const char *name){
  printf("%s %s\n", name, SPARE_PRINT_VERSION);
  printf("\n");
  printf("Written by Ivan Naranjo Ortega.\n");
}
