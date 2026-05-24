#include "spare.h"

#define TMP_FILENAME       "spare.tmp"       /* Temp file for on-disk operations */
#define TMP_STDIN_FILENAME "spare_stdin.tmp" /* Temp file to buffer stdin        */

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
  const char *extract_dir = NULL;
  const char **filepaths = NULL;
  char *endptr;
  char abs_archive[SPARE_MAX_PATH];
  int i = 0;
  int verbose = 0;
  int nfiles = 0;
  int use_zstream = 0;
  int is_stream = 0;
  int sparse = 0;
  int ret = 0;
  long val = 0;
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
    } else if(strcmp(argv[i], "-z") == 0){
      use_zstream = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-j") == 0){
      argv[i] = NULL;
      /* By default use num of cores available */
      g_nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);
      if (g_nthreads < 1) g_nthreads = 1;
      if (i + 1 < argc && argv[i+1] != NULL) {
        val = strtol(argv[i+1], &endptr, 10);
        /* endptr stops at the first char that could not parse. Conditions:
         * - At least one character is consumed && 
         * - The whole string is consumed */
        if (endptr != argv[i+1] && *endptr == '\0') {
          g_nthreads = (int)val;
          if (g_nthreads < 1) g_nthreads = 1;
          argv[i+1] = NULL;
          i++;
        }
      }
    } else if(strncmp(argv[i], "-j", 2) == 0 && argv[i][2] != '\0'){
      g_nthreads = atoi(argv[i] + 2);
      if (g_nthreads < 1) g_nthreads = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-V") == 0){
      print_version(argv[0]);
      return 0;
    } else if(strcmp(argv[i], "-S") == 0){
      sparse = 1;
      argv[i] = NULL;
    } else if(strcmp(argv[i], "-C") == 0){
      argv[i] = NULL;
      if (i + 1 < argc && argv[i+1] != NULL) {
        extract_dir = argv[i+1];
        argv[i+1] = NULL;
        i++;
      } else {
        fprintf(stderr, "error: '-C' requires a directory argument\n");
        return 1;
      }
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

  is_stream = (strcmp(archive_path, "-") == 0);
  /* Verbose would corrupt information */
  if (is_stream) verbose = 0;

  /* Detect archive format (skipped for stream: format declared by -z flag) */
  if (is_stream) {
    archive_format = use_zstream ? ARCHIVE_SZT : ARCHIVE_SAR;
  } else {
    archive_format = detect_archive_format(archive_path, verbose);
    if ((archive_format == ARCHIVE_SAR) && 
        (check_archive_version(archive_path) != 0))
      return 1;
  }

  /* If -C is set, resolve archive path to absolute before chdir so relative
   * paths like 'archive.spa' still work after the directory change */
  if (extract_dir != NULL && !is_stream) {
    if (realpath(archive_path, abs_archive) == NULL) {
      perror(archive_path);
      return 1;
    }
    archive_path = abs_archive;
  }
  if (extract_dir != NULL) {
    if (chdir(extract_dir) != 0) {
      perror(extract_dir);
      return 1;
    }
  }

  /* Action - p */
  if (strcmp(action, "p") == 0){
    PackArgs a = { filepaths, nfiles, sparse, verbose };
    if (nfiles == 0) {
      fprintf(stderr, "error: 'p' requires at least one file\n");
      usage(argv[0]);
      return 1;
    }

    /* Pack to stdout */
    if (is_stream) {
      if (g_nthreads == 1) {
        /* Write to stdout */
        return just_run(archive_path, "wb", do_pack, &a) == 0 ? 0 : 1;
      }
      /* Multithreaded: mmap needs a real file, stream result to stdout */
      if (pack_threads(TMP_FILENAME, filepaths, nfiles, sparse, verbose) != 0) 
        return 1;
      /* Write to stdout */
      if (stream_file_to_stdout(TMP_FILENAME) != 0) {
        remove(TMP_FILENAME);
        return 1;
      }
      return remove(TMP_FILENAME) == 0 ? 0 : 1;
    }

    /* Pack to file in disk */
    if (g_nthreads == 1){
      return just_run(archive_path, "wb", do_pack, &a) == 0 ? 0 : 1;
    } else {
      return pack_threads(archive_path, filepaths, nfiles, sparse, verbose) == 0 ? 0 : 1;
    }

  /* Action - pz */
  } else if (strcmp(action, "pz") == 0){
    PackArgs a = { filepaths, nfiles, sparse, verbose };
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
      if (pack_threads(TMP_FILENAME, filepaths, nfiles, sparse, verbose) != 0){
        fprintf(stderr, "error: pack failed\n");
        return 1;
      }
    }

    /* Accepts file in disk or stdout as compression destiny */
    compress_arch(archive_path, TMP_FILENAME, verbose);
    return remove(TMP_FILENAME) == 0 ? 0 : 1;

  /* Action - u */
  } else if (strcmp(action, "u") == 0){
    UnpackArgs a = { verbose };

    /* Unpack from stdin */
    if (is_stream) {
      if (archive_format == ARCHIVE_SZT) {
        /* decompress_in_ram_and_run requires a file in disk */
        if (buffer_stdin_to_file(TMP_FILENAME) != 0) return 1;
        ret = decompress_in_ram_and_run(TMP_FILENAME, do_unpack, &a, verbose);
        remove(TMP_FILENAME);
        return ret == 0 ? 0 : 1;
      }
      /* Read from stdin */
      return just_run(archive_path, "rb", do_unpack, &a) == 0 ? 0 : 1;
    }

    /* Unpack from disk, .spa or .szt */
    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_unpack, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SZT) {
      return decompress_in_ram_and_run(archive_path, do_unpack, &a, verbose)
        == 0 ? 0 : 1;
    } else {
      fprintf(stderr, "error: non existing file or corrupt format for '%s'\n",
        archive_path);
      return 1;
    }

  /* Action - l */
  } else if (strcmp(action, "l") == 0){

    if (is_stream) {
      if (archive_format == ARCHIVE_SZT) {
        /* list uses fseek, needs a seekable file, not a pipe */
        if (buffer_stdin_to_file(TMP_STDIN_FILENAME) != 0) return 1;
        ret = decompress_in_disk_and_run(TMP_FILENAME, TMP_STDIN_FILENAME,
          "rb", do_list, NULL, verbose);
        remove(TMP_STDIN_FILENAME);
        remove(TMP_FILENAME);
        return ret == 0 ? 0 : 1;
      }
      /* Read from stdin */
      return just_run(archive_path, "rb", do_list, NULL) == 0 ? 0 : 1;
    }

    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_list, NULL) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SZT) {
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

    if (is_stream) {
      if (archive_format == ARCHIVE_SZT) {
        /* grab uses fseek, needs a seekable file, not a pipe */
        if (buffer_stdin_to_file(TMP_STDIN_FILENAME) != 0) return 1;
        ret = decompress_in_disk_and_run(TMP_FILENAME, TMP_STDIN_FILENAME,
          "rb", do_grab, &a, verbose);
        remove(TMP_STDIN_FILENAME);
        remove(TMP_FILENAME);
        return ret == 0 ? 0 : 1;
      }
      /* Read from stdin */
      return just_run(archive_path, "rb", do_grab, &a) == 0 ? 0 : 1;
    }

    if (archive_format == ARCHIVE_SAR) {
      return just_run(archive_path, "rb", do_grab, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SZT) {
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
    if (is_stream) {
      fprintf(stderr, "error: 'i' does not support stdin/stdout\n");
      return 1;
    }
    if (archive_format == ARCHIVE_SAR) {
      PackArgs a = { filepaths, nfiles, sparse, verbose };
      return just_run(archive_path, "ab", do_pack, &a) == 0 ? 0 : 1;
    } else if (archive_format == ARCHIVE_SZT) {
      InsertArgs a = { filepaths, nfiles, sparse, verbose };
      decompress_in_disk_and_run(TMP_FILENAME, archive_path, "ab",
        do_insert, &a, verbose);
      compress_arch(archive_path, TMP_FILENAME, verbose);
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
