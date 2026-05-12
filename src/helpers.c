#include "sar.h"

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
  return pack(fp, a->filepaths, a->nfiles, a->verbose);
}

/* Arguments for insert function */
int do_insert(FILE *fp, void *user_data){
  InsertArgs *a = (InsertArgs *)user_data;
  return insert(fp, a->filepaths, a->nfiles, a->verbose);
}

/* ----------------------------------------------------------------------------
 * detect_archive_format
 * 
 * Returns format of the given SAR archive. Can be SGZ or SAR.
 * ------------------------------------------------------------------------- */
ArchiveFormat detect_archive_format(const char *archive_path, int verbose){
  /* Local variables */
  unsigned char magic[3];
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

  /* gzip magic is 0x1F 0x8B */
  if (magic[0] == 0x1F && magic[1] == 0x8B){
    if (verbose) printf("'%s' detected as compressed SAR archive\n", 
                        archive_path);
    return ARCHIVE_SGZ;
  }
    
  if (n >=3 && memcmp(magic, SAR_MAGIC, 3) == 0){
    if (verbose) printf("'%s' detected as SAR archive\n", archive_path);
    return ARCHIVE_SAR;
  }

  return ARCHIVE_UNKNOWN;

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

  setvbuf(fp, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);
 
  ret = action_fn(fp, user_data);
 
  /* MUST fclose before join — unblocks the decompression thread */
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

  setvbuf(fp, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);
 
  ret = action_fn(fp, user_data);
 
  fclose(fp);
 
  return ret;
}

/* ----------------------------------------------------------------------------
 * compress_in_disk
 *
 * Calls compress_arch or compress_arch_threads
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int compress_in_disk(const char *dst_path, const char *src_path, 
            int use_threads, int verbose){
  if (use_threads == 0){
    if (compress_arch(dst_path, src_path, verbose) != 0) {
      fprintf(stderr, "error: compress_arch failed\n");
      return 1;
    }
  } else {
    if (compress_arch_threads(dst_path, src_path, verbose) != 0) {
      fprintf(stderr, "error: compress_arch_threads failed\n");
      return 1;
    }
  }

  return 0;
}

/* ----------------------------------------------------------------------------
 * just_run
 * 
 * Generic action function call with NO previous decompression step
 * ------------------------------------------------------------------------- */
int just_run(const char *archive_path, ActionFn action_fn, void *user_data) {
  /* Local variables */
  FILE *fp  = NULL;
  int ret;
 
  /* Code */
  fp = fopen(archive_path, "rb");
  if (fp == NULL) {
    perror(archive_path);
    return -1;
  }
  setvbuf(fp, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);
 
  ret = action_fn(fp, user_data);
 
  fclose(fp);
  return ret;
}

/* ----------------------------------------------------------------------------
 * usage
 * 
 * Help information
 * ------------------------------------------------------------------------- */
void usage(const char *name){
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "Actions:\n");
  fprintf(stderr, "  %s p   <archive.sar> <file1..fileN>       Pack given files or folders to a SAR archive.\n", name);
  fprintf(stderr, "  %s pz  <archive.sgz> <file1..fileN>       Pack given files or folders to a SAR archive and compress it.\n", name);
  fprintf(stderr, "  %s u   <archive.sar|.sgz>                 Unpack SAR archive.\n", name);
  fprintf(stderr, "  %s l   <archive.sar|.sgz>                 List files contained in a SAR archive.\n", name);
  fprintf(stderr, "  %s g   <archive.sar|.sgz> <file1..fileN>  Grab specific files contained in a SAR archive.\n", name);
  fprintf(stderr, "  %s i   <archive.sar|.sgz> <file1..fileN>  Insert specific files to a SAR archive.\n", name);
  fprintf(stderr, "Flags:\n");
  fprintf(stderr, "  -h prints this information.\n");
  fprintf(stderr, "  -v verbose output.\n");
  fprintf(stderr, "  -p enable threading for packing.\n");
  fprintf(stderr, "  -c enable threading for compression.\n");
  fprintf(stderr, "  -T p and c flags.\n");
}
