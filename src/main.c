#include "sar.h"

/* ----------------------------------------------------------------------------
 * Function helpers
 * ------------------------------------------------------------------------- */
/* Pointer to function of any action with the FILE* of the uncompressed file 
 * and unknown arguments */
typedef int (*ActionFn)(FILE *fp, void *user_data);

/* Arguments for unpack function */
typedef struct { int verbose; } UnpackArgs;
static int do_unpack(FILE *fp, void *user_data){
  UnpackArgs *a = (UnpackArgs *)user_data;
  return unpack(fp, a->verbose);
}

/* Arguments for list function */
static int do_list(FILE *fp, void *user_data){
  (void)user_data;
  return list(fp);
}

/* Arguments for grab function */
typedef struct { const char **filepaths; int nfiles; int verbose; } GrabArgs;
static int do_grab(FILE *fp, void *user_data){
  GrabArgs *a = (GrabArgs *)user_data;
  return grab(fp, a->filepaths, a->nfiles, a->verbose);
}

/* ----------------------------------------------------------------------------
 * detect_archive_format
 * 
 * Returns format of the given SAR archive. Can be SGZ or SAR.
 * ------------------------------------------------------------------------- */
ArchiveFormat detect_archive_format(const char *archive_path){
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
  if (magic[0] == 0x1F && magic[1] == 0x8B) return ARCHIVE_SGZ;
    
  if (n >=3 && memcmp(magic, SAR_MAGIC, 3) == 0) return ARCHIVE_SAR;

  return ARCHIVE_UNKNOWN;

}

/* ----------------------------------------------------------------------------
 * decompress_and_run
 * 
 * Generic action function call with previous decompression step
 * ------------------------------------------------------------------------- */
static int decompress_and_run(const char *archive_path, int verbose,
                        ActionFn action_fn, void *user_data) {
  /* Local variables */
  FILE *fp = NULL;
  pthread_t tid;
  DecompressRamArgs args;
  int ret;
 
  /* Code */
  if (decompress_arch_ram(&fp, archive_path, &tid, &args, verbose) != 0) {
    fprintf(stderr, "error: decompress failed\n");
    return -1;
  }
 
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
 * just_run
 * 
 * Generic action function call with NO previous decompression step
 * ------------------------------------------------------------------------- */
static int just_run(const char *archive_path,
                           ActionFn action_fn, void *user_data) {
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
static void usage(const char *name){
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

/* ----------------------------------------------------------------------------
 * main
 * 
 * SAR tool entry point.
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[]){
  /* Local variables */
  const char *action = NULL;
  const char *archive_path = NULL;
  const char **filepaths = NULL;
  const char *tmpFile = "sar.tmp";
  int i = 0;
  int verbose = 0;
  int threads_pack = 0;
  int threads_compress = 0;
  int nfiles = 0;
  ArchiveFormat archive_format = ARCHIVE_DOESNOTEXIST;

  /* Code */
  /* Consume flags */
  for (i = 1; i < argc; ++i){
    if(strcmp(argv[i], "-v") == 0){
      verbose = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-h") == 0){
      usage(argv[0]);
      return 0;
    } else if(strcmp(argv[i], "-p") == 0){
      threads_pack = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-c") == 0){
      threads_compress = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-T") == 0){
      threads_pack = 1;
      threads_compress = 1;
      argv[i] = NULL;
    }
  }

  /* Check if min amount of argc is present */
  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }

  /* Collect positional args */
  for(i = 1; i < argc; ++i){
    if(argv[i] == NULL) continue; /* Consumed flag */
    if(action == NULL) { action = argv[i]; continue; }
    if(archive_path == NULL) { archive_path = argv[i]; continue; }
    /* From here just files */
    if(filepaths  == NULL) { filepaths = (const char **)&argv[i]; }
    nfiles++;
  }

  if (action == NULL || archive_path == NULL){
    usage(argv[0]);
    return 1;
  }

  /* Detect if given SAR is compressed or not */
  archive_format = detect_archive_format(archive_path);
  if (archive_format == ARCHIVE_SAR && verbose)
    fprintf(stdout, "'%s' detected as SAR archive\n", archive_path);
  if (archive_format == ARCHIVE_SGZ && verbose)
    fprintf(stdout, "'%s' detected as compressed SAR archive\n", archive_path);

  /* Action - p */
  if (strcmp(action, "p") == 0){
    if (nfiles == 0) {
      fprintf(stderr, "error: 'p' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }

    if (threads_pack == 0){
      return pack(archive_path, filepaths, nfiles, verbose) == 0 ? 0 : 1;
    } else {
      return pack_threads(archive_path, filepaths, nfiles, verbose) == 0 ? 0 : 1;
    }

  /* Action - pz */
  } else if (strcmp(action, "pz") == 0){
    if (nfiles == 0) {
      fprintf(stderr, "error: 'pz' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }

    if (threads_pack == 0){
      if (pack(tmpFile, filepaths, nfiles, verbose) != 0){
        fprintf(stderr, "error: pack failed\n");
        return 1;
      }
    } else {
      if (pack_threads(tmpFile, filepaths, nfiles, verbose) != 0){
        fprintf(stderr, "error: pack failed\n");
        return 1;
      }
    }

    if (threads_compress == 0){
      if (compress_arch(archive_path, tmpFile, verbose) != 0) {
        fprintf(stderr, "error: compress failed\n");
        return 1;
      }
    } else {
      if (compress_arch_threads(archive_path, tmpFile, verbose) != 0) {
        fprintf(stderr, "error: compress failed\n");
        return 1;
      }
    }

    return remove(tmpFile) == 0 ? 0 : 1;

  /* Action - u */
  } else if (strcmp(action, "u") == 0){
    UnpackArgs a = { verbose };
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, do_unpack, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      return decompress_and_run(archive_path, verbose, do_unpack, &a) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - l */
  } else if (strcmp(action, "l") == 0){
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, do_list, NULL) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      return decompress_and_run(archive_path, verbose, do_list, NULL) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - g */
  } else if (strcmp(action, "g") == 0){
    GrabArgs a = { filepaths, nfiles, verbose };
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, do_grab, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      return decompress_and_run(archive_path, verbose, do_grab, &a) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - i */
  } else if (strcmp(action, "i") == 0){
    if (archive_format == ARCHIVE_SAR) {
      if (nfiles == 0) {
        fprintf(stderr, "error: 'i' requires at least one file\n");
        usage(argv[0]);
        return 1;
      }

      return pack(archive_path, filepaths, nfiles, verbose) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      /* RAM cannot be used here as we do compress the file afterwards */
      if(decompress_arch(tmpFile, archive_path, verbose) != 0){
        fprintf(stderr, "error: decompress failed\n");
        return 1;
      }

      if(insert(tmpFile, filepaths, nfiles, verbose)){
        fprintf(stderr, "error: insert failed\n");
        return 1;
      }

      if (threads_compress == 0){
        if (compress_arch(archive_path, tmpFile, verbose) != 0) {
          fprintf(stderr, "error: compress failed\n");
          return 1;
        }
      } else {
        if (compress_arch_threads(archive_path, tmpFile, verbose) != 0) {
          fprintf(stderr, "error: compress failed\n");
          return 1;
        }
      }

      return remove(tmpFile) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Unknown action */
  } else {
    fprintf(stderr, "error: unknown action '%s'\n", action);
    usage(argv[0]);
    return 1;
  }
}
