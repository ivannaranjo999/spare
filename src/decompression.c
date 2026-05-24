#include "spare.h"

typedef enum { DONOTFLUSH, DOFLUSH } FlushNeeded;

/* ----------------------------------------------------------------------------
 * decompress_stream
 *
 * Zstd streaming decompress loop. Reads compressed data from src and writes
 * decompressed bytes to dst.
 * ------------------------------------------------------------------------- */
static int decompress_stream(FILE *dst, FILE *src, FlushNeeded flush_pipe){
  /* Local variables */
  ZSTD_DCtx *dctx = NULL;
  void *in_buf = NULL;
  void *out_buf = NULL;
  size_t in_size;
  size_t out_size;
  size_t nread;
  size_t ret;
  ZSTD_inBuffer input;
  ZSTD_outBuffer output;
  int result = 0;

  /* Code */

  /* Initialize zstd decompression context */
  dctx = ZSTD_createDCtx();
  if (dctx == NULL) {
    fprintf(stderr, "error: ZSTD_createDCtx failed\n");
    return -1;
  }

  in_size = ZSTD_DStreamInSize();
  out_size = ZSTD_DStreamOutSize();
  in_buf = malloc(in_size);
  out_buf = malloc(out_size);
  if (in_buf == NULL || out_buf == NULL) {
    perror("malloc");
    result = -1;
    goto cleanup;
  }

  /* Decompress until EOF */
  for (;;) {
    nread = fread(in_buf, 1, in_size, src);
    if (ferror(src)) {
      fprintf(stderr, "error: read failed during decompression\n");
      result = -1;
      goto cleanup;
    }
    if (nread == 0) break;

    input.src = in_buf;
    input.size = nread;
    input.pos = 0;

    /* Drive output until the current input chunk is fully consumed */
    while (input.pos < input.size) {
      output.dst = out_buf;
      output.size = out_size;
      output.pos = 0;

      ret = ZSTD_decompressStream(dctx, &output, &input);
      if (ZSTD_isError(ret)) {
        fprintf(stderr, "error: decompression failed: %s\n",
                ZSTD_getErrorName(ret));
        result = -1;
        goto cleanup;
      }

      if (fwrite(out_buf, 1, output.pos, dst) != output.pos || ferror(dst)) {
        fprintf(stderr, "error: write failed during decompression\n");
        result = -1;
        goto cleanup;
      }

      /* flush after every output chunk so the pipe reader unblocks */
      if (flush_pipe == DOFLUSH) fflush(dst);
    }
  }

  /* Clean up */
cleanup:
  ZSTD_freeDCtx(dctx);
  free(in_buf);
  free(out_buf);
  return result;
}

/* ----------------------------------------------------------------------------
 * decompress_ram_worker
 *
 * Worker entry point. Reads a compressed SAR file, decompresses it, and writes
 * raw SAR bytes into the pipe write end. Closing the write end signals EOF
 * to the pipe read end.
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
  setvbuf(src, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);

  dst = fdopen(args->write_fd, "wb");
  if (dst == NULL) {
    fprintf(stderr, "error: decompress_ram_worker: could not fdopen "
      "write end\n");
    close(args->write_fd);
    args->result = -1;
    return NULL;
  }
  /* No setvbuf needed, no write to disk */

  /* Decompress and pipe to read end */
  args->result = decompress_stream(dst, src, DOFLUSH);

  /* Clean up */
  fclose(dst); /* closing write end signals EOF to read end */
  fclose(src);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * decompress_arch_ram
 *
 * Decompress from 'src_path' using a worker thread into a pipe. The caller
 * receives a FILE* for the read end to consume the decompressed bytes.
 * This allows decompression and the consuming action to run concurrently.
 * ------------------------------------------------------------------------- */
int decompress_arch_ram(FILE **dst_fp, const char *src_path,
  pthread_t *dst_thread, DecompressRamArgs *dst_args, int verbose){
  /* Local variables */
  int pipe_fds[2];
  int src_fd;
  int err;

  /* Code */
  if (verbose)
    printf("decompressing '%s' into memory pipe ...\n", src_path);

  /* open as fd, worker thread will call fdopen later */
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
  dst_args->write_fd = pipe_fds[1]; /* write end for the worker thread */
  dst_args->verbose = verbose;
  dst_args->result = 0;

  /* The caller reads from the read end of the pipe */
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
 * Wait for the worker thread to finish decompression.
 * ------------------------------------------------------------------------- */
int decompress_arch_ram_join(pthread_t thread, DecompressRamArgs *arg){
  pthread_join(thread, NULL);
  return arg->result;
}

/* ----------------------------------------------------------------------------
 * decompress_arch
 *
 * Decompress from 'src_path' to 'dst_path' on disk.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int decompress_arch(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  FILE *dst;
  FILE *src;
  int ret;

  /* Code */
  if (verbose)
    printf("decompressing from '%s' ...\n", src_path);

  dst = fopen(dst_path, "wb");
  if(dst == NULL){
    fprintf(stderr, "error: could not open '%s'\n", dst_path);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);

  src = fopen(src_path, "rb");
  if(src == NULL){
    fprintf(stderr, "error: could not open '%s'\n", src_path);
    fclose(dst);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SPARE_ARCHIVE_BUF_SIZE);

  /* Decompress */
  ret = decompress_stream(dst, src, DONOTFLUSH);

  /* Clean up */
  fclose(dst);
  fclose(src);
  return ret;
}
