#include "sar.h"

typedef struct {
  uint8_t *input;      /* raw chunk data to compress               */
  size_t   input_len;  /* bytes in this chunk                      */
  uint8_t *dict;       /* last DICT_SIZE bytes of prev chunk raw   */
  size_t   dict_len;   /* 0 for first chunk                        */
  uint8_t *output;     /* compressed raw deflate output            */
  size_t   output_cap; /* allocated size of output buffer          */
  size_t   output_len; /* bytes written by thread                  */
  int      is_last;    /* 1 if this is the final chunk             */
  int      result;     /* 0 = ok, -1 = error                       */
} CompressChunk;

/* ----------------------------------------------------------------------------
 * decompress_arch
 *
 * Decompress to 'dst' from 'src'
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
int decompress_arch(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  int ret;
  unsigned have;
  z_stream strm;
  unsigned char in[ZCHUNK];
  unsigned char out[ZCHUNK];
  FILE *dst;
  FILE *src;

  /* Code */
  if (verbose)
    printf("decompressing from '%s'\n", 
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

  /* Initialize the zlib stream for decompression */
  strm.zalloc   = Z_NULL;
  strm.zfree    = Z_NULL;
  strm.opaque   = Z_NULL;
  strm.avail_in = 0;
  strm.next_in  = Z_NULL;
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
      fclose(dst);
      fclose(src);
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
        fclose(dst);
        fclose(src);
        return -1;
      }
      have = ZCHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dst) != have || ferror(dst)) {
        (void)inflateEnd(&strm);
        fclose(dst);
        fclose(src);
        return -1;
      }
    } while (strm.avail_out == 0);
  } while (ret != Z_STREAM_END);

  if (ret != Z_STREAM_END) {
    fprintf(stderr, "error: incomplete or corrupt gzip stream\n");
    (void)inflateEnd(&strm);
    fclose(dst);
    fclose(src);
    return -1;
  }

  /* Clean up */
  (void)inflateEnd(&strm);
  fclose(dst);
  fclose(src);
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

  /* Code */
  if (verbose)
    printf("compressing to '%s'\n", 
      dst_path);

  dst = fopen(dst_path, "wb");
  if (dst == NULL) {
    fprintf(stderr, "error: could not open '%s'\n", dst_path);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  src = fopen(src_path, "rb");
  if (src == NULL) {
    fprintf(stderr, "error: could not open '%s'\n", src_path);
    fclose(dst);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  /* Initialize the zlib stream for compression */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit2(&strm,
                     Z_DEFAULT_COMPRESSION,
                     Z_DEFLATED,
                     15+16,
                     8,
                     Z_DEFAULT_STRATEGY);
  if (ret != Z_OK){
    return -1;
    fclose(dst);
    fclose(src);
  } 

  /* Compress until EOF */
  do {
    strm.avail_in = fread(in, 1, ZCHUNK, src);
    if (ferror(src)) {
      (void)deflateEnd(&strm);
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
        fclose(dst);
        fclose(src);
        return -1;
      }
      have = ZCHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dst) != have || ferror(dst)) {
        (void)deflateEnd(&strm);
        fclose(dst);
        fclose(src);
        return -1;
      }
    } while (strm.avail_out == 0);
    if (strm.avail_in != 0){
      fprintf(stderr, "error: failure during compression\n");
      fclose(dst);
      fclose(src);
      return -1;
    }
  } while (flush != Z_FINISH);
  if(ret != Z_STREAM_END){
      fprintf(stderr, "error: failure during compression\n");
      fclose(dst);
      fclose(src);
      return -1;
  }

  /* Clean up */
  (void)deflateEnd(&strm);

  fclose(dst);
  fclose(src);

  return 0;
}

/* ----------------------------------------------------------------------------
 * compress_worker
 *
 * Each thread compresses one chunk as a raw deflate block
 * (windowBits = -15, no per-chunk gzip envelope).
 *
 * The dictionary is loaded for compression context only. Because
 * we flush with Z_SYNC_FLUSH, all back-references are resolved
 * within the current chunk window before the block boundary
 * the decompressor never needs to know about the dictionary.
 *
 * The last chunk uses Z_FINISH to close the deflate stream.
 * ------------------------------------------------------------------------- */
static void *compress_worker(void *arg){
  /* Local variables */
  CompressChunk *c = (CompressChunk *)arg;
  z_stream      strm;
  int           ret, flush_mode;
  uint8_t      *out_ptr = NULL;
  size_t        out_rem, produced ;
  uInt          avail;

  /* Code */
  c->result     = 0;
  c->output_len = 0;

  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;

  /* windowBits = -15: raw deflate, no zlib/gzip header per chunk   */
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

  /* seed the dictionary with the tail of the previous chunk's raw
   * data. This gives the compressor back-reference context across
   * the chunk boundary without encoding any cross-block references
   * in the bitstream, Z_SYNC_FLUSH ensures they stay local. */
  if (c->dict != NULL && c->dict_len > 0) {
    ret = deflateSetDictionary(&strm, c->dict, (uInt)c->dict_len);
    if (ret != Z_OK) {
      fprintf(stderr, "compress_worker: deflateSetDictionary failed (%d)\n", ret);
      deflateEnd(&strm);
      c->result = -1;
      return NULL;
    }
  }

  /* feed all input in one shot, then flush */
  strm.next_in  = c->input;
  strm.avail_in = (uInt)c->input_len;

  /* choose flush mode:
   * Z_SYNC_FLUSH byte-aligns output, resolves all pending
   *                back-references within this chunk's window.
   * Z_FINISH     on the last chunk, closes the deflate stream. */
  flush_mode = c->is_last ? Z_FINISH : Z_SYNC_FLUSH;

  out_ptr = c->output;
  out_rem = c->output_cap;

  do {
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

    produced = avail - strm.avail_out;
    out_ptr += produced;
    out_rem -= produced;
    c->output_len += produced;

  } while (strm.avail_in > 0 || strm.avail_out == 0);

  /* for the last chunk, verify the stream completed cleanly         */
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
 *  read_chunks
 *
 *  Read the entire source file into a CompressChunk array.
 *  dict pointers point into the previous chunk's input buffer
 *  do not free chunks until all threads have finished.
 *  Returns chunk count on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int read_chunks(FILE *src, CompressChunk **out){
  /* Local variables */
  CompressChunk *chunks     = NULL;
  CompressChunk *tmp        = NULL;
  CompressChunk *c          = NULL;
  int            count      = 0;
  int            capacity   = 0;
  int            new_cap    = 0;
  int            i          = 0;
  size_t         dict_start = 0;
  z_stream       tmp_strm;

  /* Code */
  while (!feof(src)) {
    /* grow array if needed */
    if (count == capacity) {
      new_cap = capacity == 0 ? 16 : capacity * 2;
      tmp = realloc(chunks,
        new_cap * sizeof(CompressChunk));
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
    {
      tmp_strm.zalloc = Z_NULL;
      tmp_strm.zfree  = Z_NULL;
      tmp_strm.opaque = Z_NULL;
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
  for (int i = 0; i < count; i++) {
    free(chunks[i].input);
    free(chunks[i].output);
  }
  free(chunks);
  return -1;
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
 * write_gzip_header
 *
 * Minimal 10-byte gzip header (RFC 1952).
 * ------------------------------------------------------------------------- */
static int write_gzip_header(FILE *dst) {
  /* Local variables */
  uint8_t hdr[10] = {
    0x1f, 0x8b,             /* gzip magic                         */
    0x08,                   /* compression method: deflate        */
    0x00,                   /* flags: none                        */
    0x00, 0x00, 0x00, 0x00, /* mtime: 0                           */
    0x00,                   /* extra flags: none                  */
    0xff                    /* OS: unknown                        */
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

  /* Code */
  footer[0] = (crc       ) & 0xff;
  footer[1] = (crc >>  8 ) & 0xff;
  footer[2] = (crc >> 16 ) & 0xff;
  footer[3] = (crc >> 24 ) & 0xff;

  footer[4] = (total_size      ) & 0xff;
  footer[5] = (total_size >>  8) & 0xff;
  footer[6] = (total_size >> 16) & 0xff;
  footer[7] = (total_size >> 24) & 0xff;

  if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer)) {
    fprintf(stderr, "error: failed to write gzip footer\n");
    return -1;
  }
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
int compress_arch_threads(const char *dst_path, const char *src_path, int verbose){
  /* Local variables */
  FILE          *src      = NULL;
  FILE          *dst      = NULL;
  CompressChunk *chunks   = NULL;
  CompressChunk *c        = NULL;
  int            n_chunks = 0;
  int            result   = 0;
  int            base     = 0;
  int            batch    = 0;
  int            i        = 0;
  int            t        = 0;
  int            err      = 0;
  pthread_t      threads[SAR_COMPRESS_THREADS];
  uint32_t       crc, total_size;
 
  /* Code */
  if (verbose)
    printf("compressing to '%s' (%d threads, %d KB chunks)\n",
          dst_path, SAR_COMPRESS_THREADS, COMPRESS_CHUNK / 1024);

  /* open files */
  src = fopen(src_path, "rb");
  if (src == NULL) {
    perror(src_path);
    return -1;
  }
  setvbuf(src, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  dst = fopen(dst_path, "wb");
  if (dst == NULL) {
    perror(dst_path);
    fclose(src);
    return -1;
  }
  setvbuf(dst, NULL, _IOFBF, SAR_ARCHIVE_BUF_SIZE);

  /* phase 1: read all chunks */
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

  /* phase 2: write single gzip header */

  if (write_gzip_header(dst) != 0) {
    result = -1;
    goto cleanup;
  }

  /* phase 3: compress in batches of SAR_COMPRESS_THREADS
   *
   * Within a batch threads run fully in parallel.
   * We must NOT start batch N+1 until batch N is complete because
   * each chunk's dict pointer references the previous chunk's
   * input buffer, which must stay alive and unmodified.
   * Since we never free buffers until all batches are done, this
   * is already safe. Batching just bounds peak thread count. */

  for (base = 0; base < n_chunks; base += SAR_COMPRESS_THREADS) {
    batch = n_chunks - base;
    if (batch > SAR_COMPRESS_THREADS) batch = SAR_COMPRESS_THREADS;

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

  /* phase 4: CRC32 + total size over all raw input
   * Computed on main thread after all batches, iterating input
   * buffers in order. CRC must cover original bytes sequentially. */

  crc        = crc32(0L, Z_NULL, 0);
  total_size = 0;

  for (i = 0; i < n_chunks; i++) {
      crc = crc32(crc, chunks[i].input, (uInt)chunks[i].input_len);
      total_size += (uint32_t)chunks[i].input_len;
  }

  /* --- phase 5: write single gzip footer ------------------------ */

  if (write_gzip_footer(dst, crc, total_size) != 0)
    result = -1;

  if (verbose && result == 0)
    printf("compress: done, %u bytes uncompressed\n", total_size);

cleanup:
  free_chunks(chunks, n_chunks);
  fclose(dst);
  return result;
}
