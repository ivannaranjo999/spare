#ifndef CONFIG_H
#define CONFIG_H

#define SPARE_ARCHIVE_BUF_SIZE  (1024 * 1024) /* 1 MB , archive-level read/write I/O buffer */
#define SPARE_FILE_BUF_SIZE     (64   * 1024) /* 64 KB, per-file I/O buffer                 */
#define COPY_BUFFER_SIZE        (64   * 1024) /* 64 KB, streaming copy buffer                */
#define COPY_BUFFER_SIZE_SMALL  (4   * 1024) /* 4 KB , copy buffer for recursive calls      */

#endif
