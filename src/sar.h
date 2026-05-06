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

typedef struct {
  char     magic[3];
  uint8_t  version;
  char     filename[SAR_MAX_PATH];
  uint64_t file_size;
  uint32_t mode;
  int64_t  mtime;
  uint8_t  _pad[4]; /* Keeps sizeof(FileHeader) a multiple of 8 */

} FileHeader;

typedef enum {
  ARCHIVE_DOESNOTEXIST,
  ARCHIVE_UNKNOWN,
  ARCHIVE_SAR,
  ARCHIVE_SGZ
} ArchiveFormat;

int pack(const char *archive_path, const char **filepaths, int count, int verbose);
int pack_file(FILE *archive, const char *filepath, int verbose);
int pack_threads(const char *archive_path, const char **filepaths, int count, int verbose);
int unpack(const char *archive_path, int verbose);
int unpack_file(FILE *archive, int verbose);
int compress_arch(const char *dst_path, const char *src_path, int verbose);
int compress_arch_threads(const char *dst_path, const char *src_path, int verbose);
int decompress_arch(const char *dst_path, const char *src_path, int verbose);
int list(const char *archive_path);
int grab(const char *archive_path, const char **filepaths, int count, int verbose);
int insert(const char *archive_path, const char **filepaths, int count, int verbose);

#endif
