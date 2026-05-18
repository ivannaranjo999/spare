#include "sar.h"

/* ----------------------------------------------------------------------------
 * compress_arch
 *
 * Compress src_path to dst_path using zstd streaming.
 * When g_nthreads > 1, sets ZSTD_c_nbWorkers so zstd manages its own thread
 * pool internally, no manual chunking needed unlike the old pigz approach.
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int compress_arch(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  FILE *src = NULL;
  FILE *dst = NULL;
  ZSTD_CCtx *cctx = NULL;
  void *in_buf = NULL;
  void *out_buf = NULL;
  size_t in_size;
  size_t out_size;
  size_t nread;
  size_t remaining;
  ZSTD_EndDirective mode;
  ZSTD_inBuffer input;
  ZSTD_outBuffer output;
  int result = 0;
  int dst_is_stdout;
  int done;

  /* Code */
  dst_is_stdout = (strcmp(dst_path, "-") == 0);

  if (verbose)
    printf("compressing to '%s' (%d thread(s)) ...\n",
           dst_is_stdout ? "stdout" : dst_path, g_nthreads);

  src = fopen(src_path, "rb");
  if (src == NULL) { perror(src_path); return -1; }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  if (dst_is_stdout) {
    dst = stdout;
  } else {
    dst = fopen(dst_path, "wb");
    if (dst == NULL) {
      perror(dst_path);
      fclose(src);
      return -1;
    }
    setvbuf(dst, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);
  }

  /* Initialize zstd compression context */
  cctx = ZSTD_createCCtx();
  if (cctx == NULL) {
    fprintf(stderr, "error: ZSTD_createCCtx failed\n");
    result = -1;
    goto cleanup;
  }

  ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, ZSTD_CLEVEL_DEFAULT);
  /* nbWorkers > 0 enables zstd's internal thread pool; ignored if library
   * was not compiled with ZSTD_MULTITHREAD, falling back to single-thread */
  if (g_nthreads > 1)
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, g_nthreads);

  in_size = ZSTD_CStreamInSize();
  out_size = ZSTD_CStreamOutSize();
  in_buf = malloc(in_size);
  out_buf = malloc(out_size);
  if (in_buf == NULL || out_buf == NULL) {
    perror("malloc");
    result = -1;
    goto cleanup;
  }

  /* Compress until EOF. ZSTD_e_end on the last chunk flushes and closes
   * the stream in one call; ZSTD_e_continue on all other chunks. */
  for (;;) {
    nread = fread(in_buf, 1, in_size, src);
    if (ferror(src)) {
      fprintf(stderr, "error: read failed on '%s'\n", src_path);
      result = -1;
      goto cleanup;
    }

    mode = feof(src) ? ZSTD_e_end : ZSTD_e_continue;
    input.src = in_buf;
    input.size = nread;
    input.pos = 0;

    /* Drive the compressor until it has consumed all of input. With MT
     * compression the internal buffers may fill faster, requiring multiple
     * calls even when avail_in hasn't changed. */
    done = 0;
    while (!done) {
      output.dst = out_buf;
      output.size = out_size;
      output.pos = 0;

      remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
      if (ZSTD_isError(remaining)) {
        fprintf(stderr, "error: compression failed: %s\n",
                ZSTD_getErrorName(remaining));
        result = -1;
        goto cleanup;
      }

      if (fwrite(out_buf, 1, output.pos, dst) != output.pos || ferror(dst)) {
        fprintf(stderr, "error: write failed\n");
        result = -1;
        goto cleanup;
      }

      /* For ZSTD_e_end: done when remaining==0 (stream fully flushed).
       * For ZSTD_e_continue: done when all input has been consumed. */
      done = (mode == ZSTD_e_end) ? (remaining == 0) : (input.pos == input.size);
    }

    if (mode == ZSTD_e_end) break;
  }

  /* Clean up */
cleanup:
  ZSTD_freeCCtx(cctx);
  free(in_buf);
  free(out_buf);
  fclose(src);
  if (dst_is_stdout) fflush(dst); else if (dst) fclose(dst);
  return result;
}
