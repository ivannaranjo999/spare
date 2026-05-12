#ifndef SAR_H
#define SAR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <utime.h>
#include <dirent.h>
#include <pthread.h>
#include <zlib.h>

#define SAR_MAGIC "SAR" /* Magic string at start of every header */
#define SAR_VERSION 1 /* format version */
#define SAR_MAX_PATH 4096 /* max length of stored path */
#define SAR_ARCHIVE_BUF_SIZE 1024*1024 /* 1MB read buffer */

#define SAR_PACK_THREADS 4 /* Pack worker threads */
#define SAR_COMPRESS_THREADS 4 /* Compression worker threads*/

#define COPY_BUFFER_SIZE 4096 
#define ZCHUNK 16384
#define COMPRESS_CHUNK (128 * 1024) /* 128 KB input per chunk */
#define DICT_SIZE (32  * 1024) /* 32  KB dictionary */

#define TMP_FILENAME "sar.tmp" /* Temporal file for in disk operations */

/* File header struct for SAR archives */
typedef struct {
  char     magic[3];
  uint8_t  version;
  char     filename[SAR_MAX_PATH];
  uint64_t file_size;
  uint32_t mode;
  int64_t  mtime;
  uint8_t  _pad[4]; /* Keeps sizeof(FileHeader) a multiple of 8 */

} FileHeader;

/* Struct for parallel compression */
typedef struct {
  uint8_t *input;    /* raw chunk data to compress */
  size_t input_len;  /* bytes in this chunk */
  uint8_t *dict;     /* last DICT_SIZE bytes of prev chunk raw */
  size_t dict_len;   /* 0 for first chunk */
  uint8_t *output;   /* compressed raw deflate output */
  size_t output_cap; /* allocated size of output buffer */
  size_t output_len; /* bytes written by thread */
  int is_last;       /* 1 if this is the final chunk */
  int result;        /* 0 = ok, -1 = error */
} CompressChunk;

/* Struct for decompression without using disk */
typedef struct{
  int src_fd;   /* File descriptor for compressed file */
  int write_fd; /* File descriptor for pipe write end */
  int verbose;  /* Verbose flag */
  int result;   /* Result value */
} DecompressRamArgs;

typedef enum {
  ARCHIVE_DOESNOTEXIST,
  ARCHIVE_UNKNOWN,
  ARCHIVE_SAR,
  ARCHIVE_SGZ
} ArchiveFormat;

typedef enum {
  DONOTFLUSH,
  DOFLUSH
} FlushNeeded;

/* Functions used somewhere else */
int pack(FILE *archive_path, const char **filepaths, int count, int verbose);
int pack_file(FILE *archive, const char *filepath, int verbose);
int pack_threads(const char *archive_path, const char **filepaths, int count, int verbose);
int unpack(FILE *archive_path, int verbose);
int unpack_file(FILE *archive, int verbose);
int compress_arch(const char *dst_path, const char *src_path, int verbose);
int compress_arch_threads(const char *dst_path, const char *src_path, int verbose);
int decompress_arch(const char *dst_path, const char *src_path, int verbose);
int decompress_arch_ram(FILE **dst_fp, const char *src_path, 
                        pthread_t *dst_thread, DecompressRamArgs *dst_args, 
                        int verbose);
int list(FILE *archive);
int grab(FILE *archive, const char **filepaths, int count, int verbose);
int insert(FILE *archive_path, const char **filepaths, int count, int verbose);
int decompress_arch_ram_join(pthread_t thread, DecompressRamArgs *arg);

/* Pointer to function of any action with the FILE* of the uncompressed file 
 * and unknown arguments */
typedef int (*ActionFn)(FILE *fp, void *user_data);
typedef struct { int verbose; } UnpackArgs;
typedef struct { const char **filepaths; int nfiles; int verbose; } GrabArgs;
typedef struct { const char **filepaths; int nfiles; int verbose; } PackArgs;
typedef struct { const char **filepaths; int nfiles; int verbose; } InsertArgs;

int do_unpack(FILE *fp, void *user_data);
int do_list(FILE *fp, void *user_data);
int do_grab(FILE *fp, void *user_data);
int do_pack(FILE *fp, void *user_data);
int do_pack_threads(FILE *fp, void *user_data);
int do_insert(FILE *fp, void *user_data);
ArchiveFormat detect_archive_format(const char *archive_path, int verbose);
int decompress_in_ram_and_run(const char *src_path, int verbose, 
  ActionFn action_fn, void *user_data) ;
int decompress_in_disk_and_run(const char *dst_path,
  const char *src_path, const char *mode, int verbose, ActionFn action_fn,
  void *user_data) ;
int compress_in_disk(const char *dst_path, const char *src_path, int verbose,
  int use_threads);
int just_run(const char *archive_path, ActionFn action_fn, void *user_data) ;
void usage(const char *name);

#endif
