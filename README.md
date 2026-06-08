# SPARE - SParse Archive RElay

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

SPARE is a fast archiver written in C, designed for sysadmins and storage engineers. Its key feature is **sparse file support**: VM disk images, database files, and other sparse files are packed and restored with their holes intact, so you never pay to store or transfer zero-filled regions.

```
Usage:
Actions:
  spare p   <archive.spa> <file1..fileN>       Pack files or directories into a SPA archive.
  spare pz  <archive.szt> <file1..fileN>       Pack and compress (zstd) into a SPA archive.
  spare u   <archive.spa|.szt>                 Unpack a SPA archive.
  spare l   <archive.spa|.szt>                 List contents of a SPA archive.
  spare g   <archive.spa|.szt> <file1..fileN>  Extract specific files from a SPA archive.
  spare i   <archive.spa|.szt> <file1..fileN>  Insert files into an existing SPA archive.
Flags:
  -h         print this help.
  -V         print version.
  -v         verbose output.
  -j [N]     use N threads for packing and compression (default: all cores).
  -z         when archive path is '-', treat stdin as compressed (SZT).
  -S         detect and preserve sparse holes (VM images, database files).
  -C <dir>   extract files into <dir> instead of current directory.
```
## Benchmarks

OS page caches are dropped before each run:
```
sync && echo 3 | sudo tee /proc/sys/vm/drop_caches
```
Each command is run **five times**; the **median** wall-clock time is reported. Run `bash bench/bench.sh` to reproduce.

---

### Linux kernel 7.0, many small files

~75,000 files, ~1.5 GB on disk. Tests archiver throughput on a realistic source tree.

**Wall-clock time**

| Operation         | tar     | spare   | spare -j4 |
|---|---|---|---|
| Pack              | 22.380s | 22.868s | 13.232s |
| Pack and compress | 46.495s | 36.782s | 17.793s |
| Unpack            | 2.718s | 5.098s | -       |
| Unpack compressed | 5.663s | 6.924s | -       |

**Compressed archive size**

| | tar czf | spare pz | spare -j4 pz |
|---|---|---|---|
| Size  | 265615532 B | 239405378 B | 238107822 B |
| Ratio | 16.94% | 15.26% | 15.18% |


---

### Sparse VM image, 4 GB image, ~20% real data

A raw QEMU disk image: 4 GB logical size, ~820 MB allocated on disk, the rest zeroed holes. Represents the common case for VM disks, database files, and thin-provisioned volumes.

**Pack: archive size and wall-clock time**

| | tar cf | tar --sparse cf | spare p | spare -S p | spare -S -j4 p |
|---|---|---|---|---|---|
| Archive size | 4294973440 B (100.0%) | 859842560 B (20.0%) | 4294967421 B (100.0%) | 859832461 B (20.0%) | 859832461 B (20.0%) |
| Pack time    | 9.351s | 3.085s | 15.117s | 2.224s | 4.811s |

**Unpack: restoring holes**

| | tar --sparse xf | spare -S u |
|---|---|---|
| Unpack time | 2.668s | 4.573s |


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
SPA archives are a flat binary file built as a concatenation of blocks, one per file. Each block starts with a fixed-size header followed by the variable-length filename, an optional hole map, and the file data.

```
[ FileHeader(58B) | filename[name_len] | HoleEntry[hole_count] | stored_size bytes ]  ...
```

### FileHeader fields

| Field | Type | Offset | Description |
|---|---|---|---|
| magic | char[3] | 0 | Always `"SPA"` |
| version | uint8 | 3 | Format version (5) |
| mode | uint32 | 4 | File permissions and type |
| uid | uint32 | 8 | Owner user ID |
| gid | uint32 | 12 | Owner group ID |
| file_size | uint64 | 16 | Logical file size (`stat st_size`) |
| mtime | int64 | 24 | Last-modified time (Unix timestamp) |
| checksum | uint64 | 32 | xxh64 of (header with checksum=0) + filename + hole map + data |
| stored_size | uint64 | 40 | Bytes of data stored in archive (equals file_size when not sparse) |
| hole_count | uint64 | 48 | Number of `HoleEntry` pairs after the filename |
| name_len | uint16 | 56 | Length in bytes of the filename that immediately follows this header |

Total: 58 bytes.

After the header, `name_len` bytes of the stored path follow (no null terminator). Then come `hole_count` HoleEntry structs, then `stored_size` bytes of file data.

### HoleEntry (16 bytes)

| Field | Type | Description |
|---|---|---|
| offset | uint64 | Where the hole starts in the logical file |
| length | uint64 | Length of the hole in bytes |

### Per-file checksums

Every block carries an xxh64 checksum over: the FileHeader (checksum field zeroed), the filename bytes, the HoleEntry array, and the stored data bytes. This detects corruption in filenames, permissions, timestamps, sizes, hole maps, and file contents.

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
  MANDIR=$HOME/.local/share/man/man1 \
  BASH_COMPDIR=$HOME/.local/share/bash-completion/completions \
  ZSH_COMPDIR=$HOME/.local/share/zsh/site-functions \
  FISH_COMPDIR=$HOME/.config/fish/completions
```

> **Zsh note:** the user completion dir is not loaded automatically. Add this line to your `~/.zshrc` once:
> ```zsh
> fpath=(~/.local/share/zsh/site-functions $fpath)
> ```

> **Man note:** to make `man spare` work for a user install, ensure `~/.local/share/man` is in your `MANPATH`. Add this to your shell config once:
> ```sh
> export MANPATH="$HOME/.local/share/man:$MANPATH"
> ```

To uninstall, run the same command replacing `install` with `uninstall`:
```
sudo make uninstall
```
```
make uninstall PREFIX=$HOME/.local/bin \
  MANDIR=$HOME/.local/share/man/man1 \
  BASH_COMPDIR=$HOME/.local/share/bash-completion/completions \
  ZSH_COMPDIR=$HOME/.local/share/zsh/site-functions \
  FISH_COMPDIR=$HOME/.config/fish/completions
```
