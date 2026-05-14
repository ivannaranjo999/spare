#include "sar.h"

int g_nthreads = 1;

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
  int i = 0;
  int verbose = 0;
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
    } else if(strcmp(argv[i], "-j") == 0){
      argv[i] = NULL;
      if (i + 1 < argc && argv[i+1] != NULL && isdigit((unsigned char)argv[i+1][0])){
        g_nthreads = atoi(argv[i+1]);
        if (g_nthreads < 1) g_nthreads = 1;
        argv[i+1] = NULL;
        i++;
      } else {
        g_nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (g_nthreads < 1) g_nthreads = 1;
      }
    } else if(strncmp(argv[i], "-j", 2) == 0 && argv[i][2] != '\0'){
      g_nthreads = atoi(argv[i] + 2);
      if (g_nthreads < 1) g_nthreads = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-V") == 0){
      print_version(argv[0]);
      return 0;
    }
  }
  if (verbose && g_nthreads > 1) printf("number of threads: %d\n", g_nthreads);

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
  archive_format = detect_archive_format(archive_path, verbose);

  /* Action - p */
  if (strcmp(action, "p") == 0){
    if (nfiles == 0) {
      fprintf(stderr, "error: 'p' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }

    PackArgs a = { filepaths, nfiles, verbose };
    if (g_nthreads == 1){
      return just_run(archive_path, "wb", do_pack, &a) == 0 ? 0 : 1;
    } else {
      return pack_threads(archive_path, filepaths, nfiles, verbose)
        == 0 ? 0 : 1;
    }

  /* Action - pz */
  } else if (strcmp(action, "pz") == 0){
    PackArgs a = { filepaths, nfiles, verbose };
    if (nfiles == 0) {
      fprintf(stderr, "error: 'pz' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }

    if (g_nthreads == 1){
      if (just_run(TMP_FILENAME, "wb", do_pack, &a) != 0){
        fprintf(stderr, "error: pack failed\n");
        return 1;
      }
    } else {
      if (pack_threads(TMP_FILENAME, filepaths, nfiles, verbose) != 0){
        fprintf(stderr, "error: pack failed\n");
        return 1;
      }
    }

    compress_in_disk(archive_path, TMP_FILENAME, verbose);
    return remove(TMP_FILENAME) == 0 ? 0 : 1;

  /* Action - u */
  } else if (strcmp(action, "u") == 0){
    UnpackArgs a = { verbose };
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_unpack, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      return decompress_in_ram_and_run(archive_path, do_unpack, &a, verbose)
        == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - l */
  } else if (strcmp(action, "l") == 0){
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_list, NULL) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      /* fd in RAM cannot  */
      decompress_in_disk_and_run(TMP_FILENAME, archive_path, "rb", do_list,
        NULL, verbose);
      return remove(TMP_FILENAME) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - g */
  } else if (strcmp(action, "g") == 0){
    GrabArgs a = { filepaths, nfiles, verbose };
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_grab, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      decompress_in_disk_and_run(TMP_FILENAME, archive_path, "rb", 
        do_grab, &a, verbose);
      return remove(TMP_FILENAME) == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - i */
  } else if (strcmp(action, "i") == 0){
    if (nfiles == 0) {
      fprintf(stderr, "error: 'i' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }
    if (archive_format == ARCHIVE_SAR) {
      PackArgs a = { filepaths, nfiles, verbose };
      return just_run(archive_path, "ab", do_pack, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SGZ) {
      InsertArgs a = { filepaths, nfiles, verbose };
      decompress_in_disk_and_run(TMP_FILENAME, archive_path, "ab",
        do_insert, &a, verbose);
      compress_in_disk(archive_path, TMP_FILENAME, verbose);
      return remove(TMP_FILENAME) == 0 ? 0 : 1;
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
