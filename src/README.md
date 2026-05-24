# File & function documentation

## Index
- [spare.h](#spareh)
- [config.h](#configh)
- [main.c](#mainc)
- [helpers.c](#helpersc)
- [pack.c](#packc)
- [unpack.c](#unpackc)
- [list.c](#listc)
- [grab.c](#grabc)
- [insert.c](#insertc)
- [compression.c](#compressionc)
- [decompression.c](#decompressionc)

---

## spare.h
This file is the main header of SPARE. It is included in all C files and contains declarations that are used at least in two of them.

## config.h
Contains defines that can be modified by the user before compiling if desired.

## main.c
The entry point of SPARE. It is in charge of parsing the input arguments, which can be actions, flags or paths, and calling the function that performs what the user desires.

### Function call order by action

**p**: pack

*Single-threaded (file or stream):*
- `just_run` > `do_pack` > `pack` > `pack_file`

*Multi-threaded `-j N` (to file):*
- `pack_threads`
  - `collect_files`
  - `assign_offsets`
  - [spawn N threads] > `worker_thread` > `write_item`
  - [join N threads]

*Multi-threaded `-j N` (stream):*
- `pack_threads`
  - `collect_files`
  - `assign_offsets`
  - [spawn N threads] > `worker_thread` > `write_item`
  - [join N threads]
- `stream_file_to_stdout`

---

**pz**: pack and compress

*Single-threaded:*
- `just_run` > `do_pack` > `pack` > `pack_file`
- `compress_arch`

*Multi-threaded `-j N`:*
- `pack_threads`
  - `collect_files`
  - `assign_offsets`
  - [spawn N threads] > `worker_thread` > `write_item`
  - [join N threads]
- `compress_arch`

---

**u**: unpack

*.sar (file or stream):*
- `just_run` > `do_unpack` > `unpack` > `unpack_file`

*.szt (file):*
- `decompress_in_ram_and_run`
  - `decompress_arch_ram` > [spawn 1 thread] > `decompress_ram_worker` > `decompress_stream`
  - `do_unpack` > `unpack` > `unpack_file`
  - `decompress_arch_ram_join` [join]

*.szt (stream):*
- `buffer_stdin_to_file`
- `decompress_in_ram_and_run`
  - `decompress_arch_ram` > [spawn 1 thread] > `decompress_ram_worker` > `decompress_stream`
  - `do_unpack` > `unpack` > `unpack_file`
  - `decompress_arch_ram_join` [join]

---

**l**: list

*.sar (file or stream):*
- `just_run` > `do_list` > `list` > `get_filename`

*.szt (file):*
- `decompress_in_disk_and_run` > `decompress_arch` > `do_list` > `list` > `get_filename`

*.szt (stream):*
- `buffer_stdin_to_file`
- `decompress_in_disk_and_run` > `decompress_arch` > `do_list` > `list` > `get_filename`

---

**g**: grab

*.sar (file or stream):*
- `just_run` > `do_grab` > `grab` > `unpack_file`

*.szt (file):*
- `decompress_in_disk_and_run` > `decompress_arch` > `do_grab` > `grab` > `unpack_file`

*.szt (stream):*
- `buffer_stdin_to_file`
- `decompress_in_disk_and_run` > `decompress_arch` > `do_grab` > `grab` > `unpack_file`

---

**i**: insert

*.sar:*
- `just_run` > `do_pack` > `pack` > `pack_file`

*.szt:*
- `decompress_in_disk_and_run` > `decompress_arch` > `do_insert` > `insert` > `pack_file`
- `compress_arch`

## helpers.c
Here can be found helper functions with the objective of reducing duplicated code. 

### do_unpack, do_list, do_grab, do_pack and do_insert
These functions are **action functions**, which are required to make the following functions generic:
- decompress_in_ram_and_run
- decompress_in_disk_and_run
- just_run

### detect_archive_format
Returns the format of the given file, it can be a .szt (compressed .sar) or .sar. If none of both, return unknown or not existing.

### check_archive_version
Parses the version of the given file. This is required to ensure spare binary do not accept a .sar file that has a different FileHeader struct.

### decompress_in_ram_and_run
Generic function to decompress an archive to RAM and perform the desired action function.

It uses *decompress_arch_ram* and *decompress_arch_ram_join* under the hood.

### decompress_in_disk_and_run
Generic function to decompress an archive to disk and perform the desired action function.

It uses *decompress_arch*.

### just_run
Generic function to run the desired action on an archive.

### stream_file_to_stdout
Grabs file and streams it to stdout. Used when a tmp file is needed instead of directly writing in stdout. E.g, mmap in multithreading packaging.

### buffer_stdin_to_file
Copies all of stdin into a file in disk. Used when reading a compressed archive from stdin. E.g, when unpacking a compressed file from stdin.

### checksum_compute
Computes a xxh64 over FileHeader with checksum field zeroed, hole map and file data.

### usage
Prints help.

### print_version
Prints SPARE version.

---

## pack.c
Handles all packing logic. Contains two entry points: a single-threaded streaming path (*pack_file*) and a multithreaded mmap path (*pack_threads*).

**Threading:** supported via *pack_threads*. The file list is pre-scanned, each file is assigned a non-overlapping region in the output, and worker threads write concurrently with no mutex needed. *pack_file* and *pack* are always single-threaded.

### Internal types

**WorkItem** holds everything needed to pack one file: its path, metadata (mode, uid, gid, mtime), file size, stored size, hole map, and a pointer into the mmap output region assigned to it.

**ThreadArgs** is passed to each worker thread. It contains a slice of the WorkItem array that the thread is responsible for, plus a result field the thread writes back when done.

### Data-region iterator

**DataChunkCb** is a function pointer type. Any function matching `int fn(const void *buf, size_t n, void *ctx)` can be used as a callback with *foreach_data_region*.

**WriteCtx** is a small context struct that bundles a destination `FILE *` and a filepath string together so *write_chunk* can receive both through the single `void *ctx` argument.

**hash_chunk**, **write_chunk** and **memcpy_chunk** are the three callbacks used with *foreach_data_region*: feeding bytes into an xxhash state, writing bytes to an archive FILE*, and copying bytes into an mmap region respectively.

**foreach_data_region** is the shared iterator. Given an open file and a hole map, it reads every data byte (skipping holes) and calls the provided callback for each chunk. Dense files are read sequentially; sparse files seek to each data region in turn. This avoids duplicating the region-iteration loop across the three places that need it.

### build_hole_map
Walks a file descriptor using SEEK_DATA and SEEK_HOLE to discover which byte ranges are holes. Builds a HoleEntry array and computes the stored size (logical size minus total hole bytes). If the filesystem does not support sparse detection, it returns with no holes set and the file is treated as dense.

### fill_workitem
Populates a WorkItem with file metadata. stored_size and holes default to non-sparse values; the caller updates them after *build_hole_map* if needed.

### collect_files
Recursively walks a filepath and appends one WorkItem per file into a flat array, growing it as needed. Handles regular files, directories and symlinks. When the sparse flag is set, it folds hole detection into this pre-scan step by opening each file, calling *build_hole_map*, and closing it, no data bytes are read here.

### assign_offsets
Walks the WorkItem array and sets each item's `dest` pointer to its position in the mmap region, accounting for header size, hole map size and stored data size. This is used for parallel writing using threads without mutex or collisions.

### fill_header
Populates a FileHeader struct. Does not set the checksum field, which must be computed and stored by the caller after the data is available.

### write_item
Writes one WorkItem (header + hole map + file data) into its assigned mmap region. Called from worker threads. Uses *foreach_data_region* with *memcpy_chunk* to copy file data.

### worker_thread
pthread entry point. Iterates its assigned WorkItem slice and calls *write_item* on each.

### pack_threads
The multithreaded pack path. Runs in phases:
1. Collect all files into a flat WorkItem array (with hole detection if -S)
2. Calculate total archive size
3. Open and pre-allocate the output file
4. mmap the output file
5. Assign dest pointers to each WorkItem
6. Divide work across threads and spawn them
7. Join threads, msync, and release the mapping

mmap requires knowing all sizes upfront, which is why the pre-scan in *collect_files* must happen before any data is written.

### pack_file
The single-threaded streaming path for one file. For regular files, it makes two passes: first to compute the checksum (using *foreach_data_region* with *hash_chunk*), then to write header + hole map + data (using *foreach_data_region* with *write_chunk*). Handles directories by recursing and symlinks by storing the link target as file data.

### pack
Calls *pack_file* for each input path. Entry point for single-threaded packing.

---

## unpack.c
Handles extraction of archives. Contains DirCache, a helper structure to avoid redundant mkdir calls during extraction.

**Threading:** not supported. Files are extracted one at a time sequentially. Parallelising unpacking would require either multiple DirCaches or locking, and the bottleneck is usually disk I/O rather than CPU anyway.

### DirCache
A sorted array of directory paths that have already been created. Uses binary search to check and insert entries efficiently. Avoids calling mkdir on the same directory more than once when unpacking many files with shared parent directories.

### dircache_init and dircache_free
Initialize and free a DirCache struct.

### dircache_contains
Performs binary search on DirCache struct.

### dircache_insert
Inserts path to DirCache in a sorted position.

### mkdir_parents
Creates all parent directories for a given filepath, skipping any already in the DirCache. E.g., for `a/b/c.txt` it ensures `a/` and `a/b/` exist.

### unpack_file
Reads one block (header + hole map + data) from the archive and restores the file to disk. Verifies the checksum after writing. For sparse files, it pre-allocates the logical file size with ftruncate, writes each data region at the correct offset, then punches holes with fallocate. Restores ownership, permissions and modification time.

### unpack
Loops calling *unpack_file* until EOF. Entry point for extraction.

---

## list.c
**Threading:** not supported. Sequential scan over headers.

### get_filename
Reads one header from the archive, prints the stored filename, then skips over the hole map and data to land at the next header.

### list
Loops calling *get_filename* until EOF. Entry point for listing archive contents.

---

## grab.c
**Threading:** not supported. Sequential scan; files are extracted one at a time as they are found.

### filename_matches
Checks if a stored filename contains any of the requested filepaths as a substring. Returns -1 on match, 0 otherwise.

### grab
Scans the archive header by header. When a filename matches, it seeks back and delegates to *unpack_file* to extract it. Non-matching entries are skipped by seeking past their hole map and data. Uses a DirCache to avoid redundant mkdir calls.

---

## insert.c
**Threading:** not supported. Delegates to *pack_file*, which is the single-threaded streaming path.

### insert
Appends files to an already-open archive by calling *pack_file* on each. Thin wrapper, the archive FILE* must already be open in append mode, which the caller handles.

---

## compression.c
**Threading:** supported via zstd's internal thread pool. When `-j N` is set, *compress_arch* passes `N` to `ZSTD_c_nbWorkers` and zstd handles the parallelism internally.

### compress_arch
Zstd streaming compress from src_path to dst_path. Supports `-` as dst_path to stream to stdout. When g_nthreads > 1, enables zstd's internal thread pool so compression itself is parallelised without any extra threading logic here.

---

## decompression.c
In charge of decompression functions. Refer to *decompress_in_ram_and_run* and *decompress_in_disk_and_run* for entry points.

**Threading:** not parallel. *decompress_arch_ram* spawns one thread so decompression and the consuming action overlap (producer/consumer pipeline), but the decompression itself is always single-threaded.

### decompress_stream
Zstd streaming decompress loop. Reads compressed data from a source FILE and writes decompressed bytes to a destination FILE. The FlushNeeded parameter controls whether to fflush after each output chunk, needed when writing to a pipe so the reading end does not block.

### decompress_ram_worker
pthread worker function. Opens the compressed source file, decompresses it, and writes raw SPARE bytes into the write end of a pipe. Closing the write end on exit signals EOF to the reader.

### decompress_arch_ram
Creates a pipe, spawns *decompress_ram_worker* in a thread, and returns the read end of the pipe as a FILE*. The caller can then read decompressed SPARE bytes from it while decompression runs concurrently.

### decompress_arch_ram_join
Waits for the decompression thread started by *decompress_arch_ram* to finish and returns its result.

### decompress_arch
Decompress from src_path to dst_path on disk. Simpler than the RAM path, no pipe or thread needed.
