#include "sar.h"

/* ----------------------------------------------------------------------------
 * compress_worker
 *
 * Function called by worker threads, compressed one chunk of data as a raw 
 * deflate block. 
 * ------------------------------------------------------------------------- */
static void *compress_worker(void *arg){
  /* Local variables */
  CompressChunk *c = (CompressChunk *)arg;
  z_stream strm;
  int ret, flush_mode;
  uint8_t *out_ptr = NULL;
  size_t out_rem, produced ;
  uInt avail;

  /* Code */
  c->result     = 0;
  c->output_len = 0;

  memset(&strm, 0, sizeof(strm));

  /* windowBits = -15: raw deflate, no zlib headers */
  ret = deflateInit2(&strm,
                    Z_DEFAULT_COMPRESSION,
                    Z_DEFLATED,
                    -15,
                    8,
                    Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    fprintf(stderr, "compress_worker: deflateInit2 failed (%d)\n", ret);
    c->result = -1;
    return NULL;
  }

  /* seed the dictionary with the tail of the previous chunk's raw data */
  if (c->dict != NULL && c->dict_len > 0) {
    ret = deflateSetDictionary(&strm, c->dict, (uInt)c->dict_len);
    if (ret != Z_OK) {
      fprintf(stderr, "compress_worker: deflateSetDictionary failed (%d)\n", ret);
      deflateEnd(&strm);
      c->result = -1;
      return NULL;
    }
  }

  strm.next_in  = c->input;
  strm.avail_in = (uInt)c->input_len;

  /* Z_SYNC_FLUSH for non-final chunks, Z_FINISH for the last chunk */
  flush_mode = c->is_last ? Z_FINISH : Z_SYNC_FLUSH;

  out_ptr = c->output;
  out_rem = c->output_cap;

  do {
    /* Check available space in chunk */
    avail = (uInt)(out_rem < (size_t)ZCHUNK ? out_rem : ZCHUNK);
    strm.next_out  = out_ptr;
    strm.avail_out = avail;

    ret = deflate(&strm, flush_mode);
    if (ret == Z_STREAM_ERROR) {
      fprintf(stderr, "compress_worker: deflate error\n");
      deflateEnd(&strm);
      c->result = -1;
      return NULL;
    }

    /* Update variables for next loop */
    produced = avail - strm.avail_out;
    out_ptr += produced;
    out_rem -= produced;
    c->output_len += produced;

  } while (strm.avail_in > 0 || strm.avail_out == 0);

  /* verify the stream completed cleanly in last chunk */
  if (c->is_last && ret != Z_STREAM_END) {
    fprintf(stderr, "compress_worker: stream did not finish cleanly\n");
    deflateEnd(&strm);
    c->result = -1;
    return NULL;
  }

  deflateEnd(&strm);
  return NULL;
}
 
/* ----------------------------------------------------------------------------
 * free_chunks
 * ------------------------------------------------------------------------- */
static void free_chunks(CompressChunk *chunks, int count) {
  /* Local variables */
  int i = 0;

  /* Code */
  for (i = 0; i < count; i++) {
    free(chunks[i].input);
    free(chunks[i].output);
  }
  free(chunks);
}
 
/* ----------------------------------------------------------------------------
 *  read_chunks
 *
 *  Read the entire source file into a CompressChunk array.
 *  Returns chunk count on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int read_chunks(FILE *src, CompressChunk **out){
  /* Local variables */
  CompressChunk *chunks = NULL;
  CompressChunk *tmp = NULL;
  CompressChunk *c = NULL;
  int count = 0;
  int capacity = 0;
  int new_cap = 0;
  int i = 0;
  size_t dict_start = 0;

  /* Code */
  while (!feof(src)) {
    /* reallocate memory if needed */
    if (count == capacity) {
      new_cap = capacity == 0 ? 16 : capacity * 2;
      tmp = realloc(chunks, new_cap * sizeof(CompressChunk));
      if (tmp == NULL) {
        perror("realloc chunks");
        goto fail;
      }
      chunks = tmp;
      capacity = new_cap;
    }

    c = &chunks[count];
    memset(c, 0, sizeof(*c));

    c->input = malloc(COMPRESS_CHUNK);
    if (c->input == NULL) {
      perror("malloc input");
      goto fail;
    }

    c->input_len = fread(c->input, 1, COMPRESS_CHUNK, src);
    if (c->input_len == 0) {
      free(c->input);
      c->input = NULL;
      break;   /* clean EOF */
    }
    if (ferror(src)) {
      fprintf(stderr, "error: read failed on source\n");
      free(c->input);
      goto fail;
    }

    /* safe output buffer size via deflateBound */
    /* use scope so tmp_strm only exists inside braces */
    {
      z_stream tmp_strm;
      memset(&tmp_strm, 0, sizeof(tmp_strm));
      deflateInit2(&tmp_strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        -15, 8, Z_DEFAULT_STRATEGY);
      c->output_cap = deflateBound(&tmp_strm, (uLong)c->input_len);
      deflateEnd(&tmp_strm);
    }
    c->output = malloc(c->output_cap);
    if (c->output == NULL) {
      perror("malloc output");
      free(c->input);
      goto fail;
    }

    c->dict = NULL;
    c->dict_len = 0;
    c->is_last = 0; /* updated after all chunks are read */
    count++;
  }

  /* second pass: assign dict pointers and mark last chunk */
  for(i = 1; i < count; i++){
    dict_start = chunks[i-1].input_len > DICT_SIZE
                ? chunks[i-1].input_len - DICT_SIZE : 0;
    chunks[i].dict = chunks[i-1].input + dict_start;
    chunks[i].dict_len = chunks[i-1].input_len - dict_start;
  }

  if (count > 0)
    chunks[count - 1].is_last = 1; /* mark the last chunk */

  *out = chunks;
  return count;

fail:
  free_chunks(chunks, count);
  return -1;
}
 
/* ----------------------------------------------------------------------------
 * write_gzip_header
 *
 * Minimal 10-byte gzip header (RFC 1952).
 * ------------------------------------------------------------------------- */
static int write_gzip_header(FILE *dst) {
  /* Local variables */
  uint8_t hdr[10] = {
    0x1f, 0x8b,             /* gzip magic */
    0x08,                   /* compression method: deflate */
    0x00,                   /* flags: none */
    0x00, 0x00, 0x00, 0x00, /* mtime: 0 */
    0x00,                   /* extra flags: none */
    0xff                    /* OS: TODO: Put one depending on OS */
  };

  /* Code */
  if (fwrite(hdr, 1, sizeof(hdr), dst) != sizeof(hdr)) {
    fprintf(stderr, "error: failed to write gzip header\n");
    return -1;
  }
  return 0;
}
 
/* ----------------------------------------------------------------------------
 * write_gzip_footer
 *
 * 8-byte gzip footer: CRC32 then uncompressed size, little-endian.
 * ------------------------------------------------------------------------- */
static int write_gzip_footer(FILE *dst, uint32_t crc, uint32_t total_size) {
  /* Local variables */
  uint8_t footer[8];
  int i;

  /* Code */
  for (i = 0; i < 4; ++i){
    /* Write CRC in first 4 indices */
    footer[i] = (crc >> (i*8)) & 0xff;

    /* Write uncompressed size in last 4 indices */
    footer[i+4] = (total_size >> (i*8)) & 0xff;
  }

  if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer)) {
    fprintf(stderr, "error: failed to write gzip footer\n");
    return -1;
  }
  return 0;
}

/* ----------------------------------------------------------------------------
 * compress_arch
 *
 * Compress to 'dst' from 'src'
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int compress_arch(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  int ret, flush;
  unsigned have;
  z_stream strm;
  unsigned char in[ZCHUNK];
  unsigned char out[ZCHUNK];
  FILE *dst = NULL;
  FILE *src = NULL;
  int dst_is_stdout = 0;

  /* Code */
  dst_is_stdout = (strcmp(dst_path, "-") == 0);

  if (verbose)
    printf("compressing to '%s' ...\n", dst_is_stdout ? "stdout" : dst_path);

  if (dst_is_stdout) {
    dst = stdout;
  } else {
    dst = fopen(dst_path, "wb");
    if (dst == NULL) {
      fprintf(stderr, "error: could not open '%s'\n", dst_path);
      return -1;
    }
    setvbuf(dst, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);
  }

  src = fopen(src_path, "rb");
  if (src == NULL) {
    fprintf(stderr, "error: could not open '%s'\n", src_path);
    if (!dst_is_stdout) fclose(dst);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  /* Initialize the zlib stream for compression */
  memset(&strm, 0, sizeof(strm));
  ret = deflateInit2(&strm,
                     Z_DEFAULT_COMPRESSION,
                     Z_DEFLATED,
                     15+16,
                     8,
                     Z_DEFAULT_STRATEGY);
  if (ret != Z_OK){
    if (!dst_is_stdout) fclose(dst);
    fclose(src);
    return -1;
  }

  /* Compress until EOF */
  do {
    strm.avail_in = fread(in, 1, ZCHUNK, src);
    if (ferror(src)) {
      (void)deflateEnd(&strm);
      if (!dst_is_stdout) fclose(dst);
      fclose(src);
      return -1;
    }
    flush = feof(src) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = in;

    /* Run deflate() on input until output buffer not full */
    do {
      strm.avail_out = ZCHUNK;
      strm.next_out = out;
      ret = deflate(&strm, flush);
      if(ret == Z_STREAM_ERROR){
        fprintf(stderr, "error: failure during compression\n");
        if (!dst_is_stdout) fclose(dst);
        fclose(src);
        return -1;
      }
      have = ZCHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dst) != have || ferror(dst)) {
        (void)deflateEnd(&strm);
        if (!dst_is_stdout) fclose(dst);
        fclose(src);
        return -1;
      }
    } while (strm.avail_out == 0);
    if (strm.avail_in != 0){
      fprintf(stderr, "error: failure during compression\n");
      if (!dst_is_stdout) fclose(dst);
      fclose(src);
      return -1;
    }
  } while (flush != Z_FINISH);
  if(ret != Z_STREAM_END){
      fprintf(stderr, "error: failure during compression\n");
      if (!dst_is_stdout) fclose(dst);
      fclose(src);
      return -1;
  }

  /* Clean up */
  (void)deflateEnd(&strm);

  if (dst_is_stdout) fflush(dst); else fclose(dst);
  fclose(src);

  return 0;
}
 
/* ----------------------------------------------------------------------------
 * compress_arch_threads
 *
 * Multithreaded pigz-style compression.
 *
 * Produces a single valid gzip stream:
 *   [gzip header]
 *   [raw deflate block 0 - Z_SYNC_FLUSH, no dict]
 *   [raw deflate block 1 - Z_SYNC_FLUSH, dict=tail of chunk 0]
 *   ...
 *   [raw deflate block N - Z_FINISH,     dict=tail of chunk N-1]
 *   [gzip footer: CRC32 + total uncompressed size]
 *
 * decompressArch handles this format with zero changes because it
 * uses inflateInit2 with windowBits=15+16 which accepts raw deflate
 * wrapped in a single gzip envelope.
 *
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int compress_arch_threads(const char *dst_path, const char *src_path,
                          int verbose){
  /* Local variables */
  FILE *src = NULL;
  FILE *dst = NULL;
  CompressChunk *chunks = NULL;
  CompressChunk *c = NULL;
  int n_chunks = 0;
  int result = 0;
  int base = 0;
  int batch = 0;
  int i = 0;
  int t = 0;
  int err = 0;
  int dst_is_stdout = 0;
  uint32_t crc = 0, total_size = 0;
  pthread_t *threads = NULL;

  /* Code */
  dst_is_stdout = (strcmp(dst_path, "-") == 0);

  if (verbose)
    printf("compressing to '%s' (%d threads, %d KB chunks) ...\n",
          dst_is_stdout ? "stdout" : dst_path, g_nthreads, COMPRESS_CHUNK / 1024);

  /* open files */
  src = fopen(src_path, "rb");
  if (src == NULL) {
    perror(src_path);
    return -1;
  }
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

  /* read all chunks */
  n_chunks = read_chunks(src, &chunks);
  fclose(src);
  src = NULL;

  if (n_chunks < 0) {
    fprintf(stderr, "error: failed to read source into chunks\n");
    fclose(dst);
    return -1;
  }
  if (n_chunks == 0) {
    fprintf(stderr, "error: source file is empty\n");
    fclose(dst);
    return -1;
  }

  if (verbose)
    printf("compress: %d chunk(s) to process\n", n_chunks);

  threads = malloc(g_nthreads * sizeof(pthread_t));
  if (!threads) {
    result = -1;
    goto cleanup;
  }

  /*  write gzip header */

  if (write_gzip_header(dst) != 0) {
    result = -1;
    goto cleanup;
  }

  /* compress in batches of g_nthreads */
  for (base = 0; base < n_chunks; base += g_nthreads) {
    batch = n_chunks - base;
    if (batch > g_nthreads) batch = g_nthreads;

    /* spawn threads */
    for (t = 0; t < batch; t++) {
      err = pthread_create(&threads[t], NULL,
                              compress_worker, &chunks[base + t]);
      if (err != 0) {
        perror("pthread_create");
        compress_worker(&chunks[base + t]);  /* fallback */
      }
    }

    /* join threads */
    for (t = 0; t < batch; t++) {
      pthread_join(threads[t], NULL);
      if (chunks[base + t].result != 0) {
        fprintf(stderr, "error: compression failed on chunk %d\n",
                base + t);
        result = -1;
      }
    }

    /* write blocks in order, ordering is required for gzip      */
    for (t = 0; t < batch; t++) {
      c = &chunks[base + t];
      if (c->output_len > 0) {
        if (fwrite(c->output, 1, c->output_len, dst) != c->output_len
          || ferror(dst)) {
          fprintf(stderr, "error: write failed on chunk %d\n",
                  base + t);
          result = -1;
        }
      }
    }

    if (verbose)
      printf("compress: batch %d-%d done\r", base, base + batch - 1);
  }
    if (verbose)
      printf("\n");

  /* Write gzip footer */
  crc = crc32(0L, Z_NULL, 0);
  total_size = 0;

  for (i = 0; i < n_chunks; i++) {
    crc = crc32(crc, chunks[i].input, (uInt)chunks[i].input_len);
    total_size += (uint32_t)chunks[i].input_len;
  }

  if (write_gzip_footer(dst, crc, total_size) != 0)
    result = -1;

  if (verbose && result == 0)
    printf("compress: done, %u bytes uncompressed\n", total_size);

cleanup:
  free(threads);
  free_chunks(chunks, n_chunks);
  if (dst_is_stdout) fflush(dst); else fclose(dst);
  return result;
}