#include "sar.h"

/* ----------------------------------------------------------------------------
 * inflate_stream
 *
 * Inflate loop. Reads compressed data from src and writes decompressed bytes
 * to dst.
 * ------------------------------------------------------------------------- */
static int inflate_stream(FILE *dst, FILE *src, FlushNeeded flush_pipe){
  /* Local variables */
  int ret;
  unsigned have;
  z_stream strm;
  unsigned char in[ZCHUNK];
  unsigned char out[ZCHUNK];
 
  /* Code */
  /* Initialize the zlib stream for decompression */
  memset(&strm, 0, sizeof(strm));
  ret = inflateInit2(&strm, 15 + 16);
  if (ret != Z_OK) {
    fclose(dst);
    fclose(src);
    return -1;
  }
 
  /* Decompress until EOF */
  do{
    strm.avail_in = fread(in, 1, ZCHUNK, src);
    if(ferror(src)){
      (void)inflateEnd(&strm);
      return -1;
    }
    if(strm.avail_in == 0) break;
    strm.next_in = in;
 
    do{
      strm.avail_out = ZCHUNK;
      strm.next_out  = out;
      ret = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_STREAM_ERROR  ||
          ret == Z_NEED_DICT     ||
          ret == Z_DATA_ERROR    ||
          ret == Z_MEM_ERROR) {
        fprintf(stderr, "error: failure during decompression (%d)\n", ret);
        (void)inflateEnd(&strm);
        return -1;
      }
      have = ZCHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dst) != have || ferror(dst)) {
        (void)inflateEnd(&strm);
        return -1;
      }

      /* flush after every inflate output so the pipe reader unblocks */
      if (flush_pipe == DOFLUSH) fflush(dst);
    } while (strm.avail_out == 0);
  } while (ret != Z_STREAM_END);
 
  if (ret != Z_STREAM_END) {
    fprintf(stderr, "error: incomplete or corrupt gzip stream\n");
    (void)inflateEnd(&strm);
    return -1;
  }
 
  /* Clean up */
  (void)inflateEnd(&strm);
  return 0;
}

/* ----------------------------------------------------------------------------
 * decompress_ram_worker
 *
 * Worker entry point. Reads a compressed SAR file, inflates it, and write
 * raw SAR bytes into the pipe write end. When done, closing the write end
 * signals EOF to the pipe read end.
 * ------------------------------------------------------------------------- */
static void *decompress_ram_worker(void *arg){
  /* Local variables */
  DecompressRamArgs *args = (DecompressRamArgs*)arg;
  FILE *src;
  FILE *dst;

  /* Code */
  src = fdopen(args->src_fd, "rb");
  if (src == NULL) {
    fprintf(stderr, "error: decompress_ram_worker: could not fdopen src\n");
    close(args->write_fd);
    args->result = -1;
    return NULL;
  }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  dst = fdopen(args->write_fd, "wb");
  if (src == NULL) {
    fprintf(stderr, "error: decompress_ram_worker: could not fdopen "
      "write end\n");
    close(args->write_fd);
    args->result = -1;
    return NULL;
  }
  /* No setvbuf needed, no write in disk */

  /* Inflate */
  args->result = inflate_stream(dst, src, DOFLUSH);

  /* Clean up */
  fclose(dst);
  /* Sign EOF to read end */
  fclose(src);

  return NULL;
}

/* ----------------------------------------------------------------------------
 * decompress_arch_ram
 * 
 * Decompress from 'src_path' using a worker thread to a file descriptor using 
 * a pipe to 'dst_fd'
 * ------------------------------------------------------------------------- */
int decompress_arch_ram(FILE **dst_fp, const char *src_path, 
                        pthread_t *dst_thread, DecompressRamArgs *dst_args, 
                        int verbose){
  /* Local variables */
  int pipe_fds[2];
  int src_fd;
  int err;

  /* Code */
  if (verbose)
    printf("decompressing '%s' into memory pipe ...\n", src_path);

  /* open as worker thread will perform fdopen later */
  src_fd = open(src_path, O_RDONLY);
  if (src_fd < 0) {
    perror(src_path);
    return -1;
  }

  if(pipe(pipe_fds) != 0) {
    perror("pipe");
    close(src_fd);
    return -1;
  }

  dst_args->src_fd = src_fd;
  dst_args->write_fd = pipe_fds[1]; /* write end of pipe for thread */
  dst_args->verbose = verbose;
  dst_args->result = 0;

  /* The caller receives a FILE* */
  *dst_fp = fdopen(pipe_fds[0], "rb");
  if(*dst_fp == NULL) {
    perror("fdopen");
    close(src_fd);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return -1;
  }

  /* Spawn decompression thread */
  err = pthread_create(dst_thread, NULL, decompress_ram_worker, dst_args);
  if (err != 0) {
    fprintf(stderr, "error: could not spawn decompression thread\n");
    fclose(*dst_fp); /* also closes pipe_fds[0] */
    close(pipe_fds[1]);
    close(src_fd);
    *dst_fp = NULL;
    return -1;
  }
 
  return 0;
}

/* ----------------------------------------------------------------------------
 * decompress_arch_ram_join
 * 
 * Wait for worker thread to finish decompression
 * ------------------------------------------------------------------------- */
int decompress_arch_ram_join(pthread_t thread, DecompressRamArgs *arg){
  pthread_join(thread, NULL);
  return arg->result;
}

/* ----------------------------------------------------------------------------
 * decompress_arch
 *
 * Decompress to 'dst' from 'src'
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int decompress_arch(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  int ret;
  FILE *dst;
  FILE *src;

  /* Code */
  if (verbose)
    printf("decompressing from '%s' ...\n", 
      src_path);

  dst = fopen(dst_path, "wb");
  if(dst == NULL){
    fprintf(stderr, "error: could not open '%s'\n", dst_path);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  src = fopen(src_path, "rb");
  if(src == NULL){
    fprintf(stderr, "error: could not open '%s'\n", dst_path);
    fclose(dst);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  /* Inflate */
  ret = inflate_stream(dst, src, DONOTFLUSH);

  /* Clean up */
  fclose(dst);
  fclose(src);
  return ret;
}
