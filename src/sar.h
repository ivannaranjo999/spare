#ifndef SAR_H
#define SAR_H

#include <stdint.h>
#include <ctype.h>
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
#include <zstd.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "config.h"

#define SAR_MAGIC "SAR" /* Magic string at start of every header */
#define SAR_VERSION 3 /* format version */
#define SAR_MAX_PATH 4096 /* max length of stored path */

extern int g_nthreads; /* Worker thread count, set by -j flag */

/* Sparse hole region: [offset, offset+length) is all zeros in the logical file */
typedef struct {
  uint64_t offset;
  uint64_t length;
} HoleEntry;

/* File header struct for SAR archives, 4152 bytes */
typedef struct {
  char     magic[3];               /* @ 0 */
  uint8_t  version;                /* @ 3 */
  char     filename[SAR_MAX_PATH]; /* @ 4 */
  uint32_t mode;                   /* @ 4100 */
  uint32_t uid;                    /* @ 4104*/
  uint32_t gid;                    /* @ 4108 */
  uint64_t file_size;              /* @ 4112, logical file size */
  int64_t  mtime;                  /* @ 4120 */
  uint64_t checksum;               /* @ 4128 */
  uint64_t stored_size;            /* @ 4136, bytes stored in archive */
  uint64_t hole_count;             /* @ 4144 num of HoleEntry after header */
} FileHeader;

/* Struct for decompression without using disk */
typedef struct{
  int src_fd;   /* File descriptor for compressed file */
  int write_fd; /* File descriptor for pipe write end */
  int verbose;  /* Verbose flag */
  int result;   /* Result value */
} DecompressRamArgs;

/* Struct for already created dirs */
typedef struct{
  char **dirs; /* sorted array of created dirs */
  int count;
  int capacity;
} DirCache;

typedef enum {
  ARCHIVE_DOESNOTEXIST,
  ARCHIVE_UNKNOWN,
  ARCHIVE_SAR,
  ARCHIVE_SZT
} ArchiveFormat;


/* Functions used somewhere else */
int pack(FILE *archive_path, const char **filepaths, int count, int sparse, int verbose);
int pack_file(FILE *archive, const char *filepath, int sparse, int verbose);
int pack_threads(const char *archive_path, const char **filepaths, int count, int sparse, int verbose);
int unpack(FILE *archive_path, int verbose);
int unpack_file(FILE *archive, DirCache *cache, int verbose);
int compress_arch(const char *dst_path, const char *src_path, int verbose);
int decompress_arch(const char *dst_path, const char *src_path, int verbose);
int decompress_arch_ram(FILE **dst_fp, const char *src_path,
                        pthread_t *dst_thread, DecompressRamArgs *dst_args,
                        int verbose);
int list(FILE *archive);
int grab(FILE *archive, const char **filepaths, int count, int verbose);
int insert(FILE *archive_path, const char **filepaths, int count, int sparse, int verbose);
int decompress_arch_ram_join(pthread_t thread, DecompressRamArgs *arg);
void dircache_init(DirCache *c);
void dircache_free(DirCache *c);
uint64_t checksum_compute(const FileHeader *h, const HoleEntry *holes,
                          uint64_t hole_count, const void *data, uint64_t size);

/* Pointer to function of any action with the FILE* of the uncompressed file
 * and unknown arguments */
typedef int (*ActionFn)(FILE *fp, void *user_data);
typedef struct { int verbose; } UnpackArgs;
typedef struct { const char **filepaths; int nfiles; int verbose; } GrabArgs;
typedef struct { const char **filepaths; int nfiles; int sparse; int verbose; } PackArgs;
typedef struct { const char **filepaths; int nfiles; int sparse; int verbose; } InsertArgs;

int do_unpack(FILE *fp, void *user_data);
int do_list(FILE *fp, void *user_data);
int do_grab(FILE *fp, void *user_data);
int do_pack(FILE *fp, void *user_data);
int do_pack_threads(FILE *fp, void *user_data);
int do_insert(FILE *fp, void *user_data);
ArchiveFormat detect_archive_format(const char *archive_path, int verbose);
int check_archive_version(const char *path);
int decompress_in_ram_and_run(const char *src_path, ActionFn action_fn,
    void *user_data, int verbose) ;
int decompress_in_disk_and_run(const char *dst_path,
  const char *src_path, const char *mode, ActionFn action_fn, void *user_data,
  int verbose) ;
int just_run(const char *archive_path, const char *mode, ActionFn action_fn, void *user_data);
int stream_file_to_stdout(const char *path);
int buffer_stdin_to_file(const char *dst_path);
void usage(const char *name);
void print_version (const char *name);

#endif
