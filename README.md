# SPARE - SParse Archive RElay 🍁
SParse Archive RElay, SPARE, is a C tool that is able to **archive** a list of files and directories in a single file and **compress** it if desired. SPARE also allows actions like:

- 🌲 Listing the contents of a `.sar|.sgz` file.
- 🌳 Grabbing specific files or directories from a `.sar|.sgz` file.
- 🌴 Inserting specific files or directories to a `.sar|.sgz` file.
- 🌻 Flags for multithreading in packing and compression.

The tool is used as follows:

```
Usage:
Actions:
  spare p   <archive.sar> <file1..fileN>       Pack given files or folders to a SPARE archive.
  spare pz  <archive.szt> <file1..fileN>       Pack given files or folders to a SPARE archive and compress it.
  spare u   <archive.sar|.szt>                 Unpack SPARE archive.
  spare l   <archive.sar|.szt>                 List files contained in a SPARE archive.
  spare g   <archive.sar|.szt> <file1..fileN>  Grab specific files contained in a SPARE archive.
  spare i   <archive.sar|.szt> <file1..fileN>  Insert specific files to a SPARE archive.
Flags:
  -v         verbose output.
  -j [N]     use N threads for packing and compression (default: all cores).
  -z         when archive path is '-', treat stdin as compressed (SZT).
  -S         detect and preserve sparse holes (VM images, database files).
  -C <dir>   extract files into <dir> instead of current directory.
```
## Benchmarks
### Conditions
The following command is run before every command to ensure OS page caching are dropped. 
```
sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
```

Each command is ran **thrice** and the **median** is taken.

### Results
**Real (wall-clock) time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | spare      | spare -j4 |
|-------------------|----------|----------|-----|
| Pack              | 19.658s  | 21.499s  | 13.889s  |
| Pack and compress | 47.003s  | 30.079s  | 17.650s  |
| Unpack            |  2.691s  |  2.843s  | -        |
| Unpack compressed |  5.332s  |  4.522s  | -        |

**User time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | spare      | spare -j4 |
|-------------------|----------|----------|-----|
| Pack              |  1.070s  |  1.564s  |  1.764s  |
| Pack and compress | 46.568s  |  9.110s  | 24.185s  |
| Unpack            |  0.524s  |  0.660s  | -        |
| Unpack compressed |  5.401s  |  3.056s  | -        |

**Sys time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | spare      | spare -j4 |
|-------------------|----------|----------|-----|
| Pack              |  5.563s  |  6.054s  |  7.873s  |
| Pack and compress |  4.629s  |  7.626s  |  7.805s  |
| Unpack            |  2.118s  |  2.070s  | -        |
| Unpack compressed |  2.708s  |  2.927s  | -        |

**Compression ratios** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
|  | tar czf | spare pz | spare -j4 pz |
|---|---|---|---|
| Absolute values | 265615532/1568397485 | 241568771/1568397485 | 241278130/1568397485 |
| Ratio           | 16.94% | 15.40% | 15.38% |

## Stdin / Stdout piping

Use `-` as the archive path to read from stdin or write to stdout, enabling SPARE to participate in shell pipelines without creating an archive file as the final destination.

```sh
spare p  - file1 file2 | ssh user@host "spare u -"        # copy files over SSH
spare p  - file1 file2 | sha256sum                       # checksum without a file
spare pz - file1 file2 | aws s3 cp - s3://bucket/b.szt   # stream to object storage
gpg -d secrets.szt   | spare u - -z                      # decrypt and unpack
```

Use `-z` when reading a compressed archive from stdin so SPARE knows the format without being able to inspect the file header.

### Disk usage per operation

| Command | Writes to disk |
|---|---|
| `spare p  -` *(single-threaded)* | zero |
| `spare u  -` *(uncompressed)*    | zero |
| `spare l  -` *(uncompressed)*    | zero |
| `spare g  -` *(uncompressed)*    | zero |
| `spare p  - -j N`                | `spare.tmp`: `pack_threads` requires mmap, which needs a seekable file |
| `spare pz -`                     | `spare.tmp`: compression runs on the whole archive after packing, so the packed archive must exist first |
| `spare u  - -z`                  | `spare.tmp`: the decompressor requires a seekable file path, not a pipe, stdin is buffered first |
| `spare l  - -z` / `spare g - -z`  | `spare.tmp` + `spare_stdin.tmp`: decompressed archive written to disk (list/grab use fseek), plus stdin buffered |

The zero-disk guarantee only holds end-to-end when both sides of the pipe use uncompressed single-threaded operations. For example, `spare pz - | ssh host "spare u - -z"` writes a temp file on **both** machines.

## The format
SPARE archives are just a flat binary file which is built as a concatenation of blocks, one per file. Each block contains a header and the file contents. The header is a fixed-size C struct storing everything needed to reconstruct the file.

```
[ FileHeader | HoleEntry[hole_count] | stored_size bytes ]  ...
```

### FileHeader fields

| Field | Type | Offset | Description |
|---|---|---|---|
| magic | char[3] | 0 | Always `"SAR"` |
| version | uint8 | 3 | Format version (3) |
| filename | char[4096] | 4 | Stored path |
| mode | uint32 | 4100 | File permissions and type |
| uid | uint32 | 4104 | Owner user ID |
| gid | uint32 | 4108 | Owner group ID |
| file_size | uint64 | 4112 | Logical file size (`stat st_size`) |
| mtime | int64 | 4120 | Last-modified time (Unix timestamp) |
| checksum | uint64 | 4128 | xxh64 of (header with checksum=0) + hole map + data |
| stored_size | uint64 | 4136 | Bytes of data stored in archive (equals file_size when not sparse) |
| hole_count | uint64 | 4144 | Number of `HoleEntry` pairs after this header |

Total: 4152 bytes, no padding.

### HoleEntry (16 bytes)

| Field | Type | Description |
|---|---|---|
| offset | uint64 | Where the hole starts in the logical file |
| length | uint64 | Length of the hole in bytes |

### Per-file checksums

Every block carries an xxh64 checksum over: the FileHeader (checksum field zeroed), the HoleEntry array, and the stored data bytes. This detects corruption in filenames, permissions, timestamps, sizes, hole maps, and file contents.

### Sparse file support

With the `-S` flag, SPARE uses `SEEK_HOLE`/`SEEK_DATA` to skip zero regions during packing. Only the actual data is stored, holes are recorded in the HoleEntry array and recreated with `fallocate(PUNCH_HOLE)` on unpack. This is a Linux-specific optimisation; on filesystems that don't support it, SPARE falls back to storing the full file.

Sparse detection is folded into the existing pre-scan phase of multithreaded packing (`-j N`): each file is opened, lseeked for holes, then closed, no extra data read. The data is then read only once by the worker threads, skipping hole regions.

## Compression
When invoked with `pz`/`u`, SPARE compresses and decompresses the entire archive using **zstd**. The output is a standard `.szt` file (a valid zstd stream readable by `zstd -d`).

Compression is applied to the **whole archive** after packing, not per file. Multi-threading (`-j N`) is passed directly to zstd's built-in worker pool, replacing the manual pigz-style approach used with gzip.

Only `p` action has its `pz` alternative since SPARE is able to detect in the rest of actions if the provided archive is compressed or not.

## Building & Installing
List of dependencies:
- zstd
- xxhash (header only, no linking required)

To build binary, run:
```
make
```

**System install** (requires sudo, installs shell completions automatically):
```
sudo make install
```

**User install** (no sudo required, override PREFIX and completion dirs):
```
make install PREFIX=$HOME/.local/bin \
  BASH_COMPDIR=$HOME/.local/share/bash-completion/completions \
  ZSH_COMPDIR=$HOME/.local/share/zsh/site-functions \
  FISH_COMPDIR=$HOME/.config/fish/completions
```

> **Zsh note:** the user completion dir is not loaded automatically. Add this line to your `~/.zshrc` once:
> ```zsh
> fpath=(~/.local/share/zsh/site-functions $fpath)
> ```

To uninstall, run the same command replacing `install` with `uninstall`:
```
sudo make uninstall
```
```
make uninstall PREFIX=$HOME/.local/bin \
  BASH_COMPDIR=$HOME/.local/share/bash-completion/completions \
  ZSH_COMPDIR=$HOME/.local/share/zsh/site-functions \
  FISH_COMPDIR=$HOME/.config/fish/completions
```
