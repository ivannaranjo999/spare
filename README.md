# SAR - Simple ARchiver 🍁
Simple ARchiver, SAR, is a C tool that is able to **archive** a list of files and directories in a single file and **compress** it if desired. SAR also allows actions like:

- 🌲 Listing the contents of a `.sar|.sgz` file.
- 🌳 Grabbing specific files or directories from a `.sar|.sgz` file.
- 🌴 Inserting specific files or directories to a `.sar|.sgz` file.
- 🌻 Flags for multithreading in packing and compression.

The tool is used as follows:

```
Usage:
Actions:
  sar p   <archive.sar> <file1..fileN>       Pack given files or folders to a SAR archive.
  sar pz  <archive.szt> <file1..fileN>       Pack given files or folders to a SAR archive and compress it.
  sar u   <archive.sar|.szt>                 Unpack SAR archive.
  sar l   <archive.sar|.szt>                 List files contained in a SAR archive.
  sar g   <archive.sar|.szt> <file1..fileN>  Grab specific files contained in a SAR archive.
  sar i   <archive.sar|.szt> <file1..fileN>  Insert specific files to a SAR archive.
Flags:
  -v verbose output
  -p enable threading for packing.
  -c enable threading for compression.
  -T p and c flags.
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
| Operation         | tar      | sar      | sar -j4 |
|-------------------|----------|----------|-----|
| Pack              | 19.658s  | 21.499s  | 13.889s  |
| Pack and compress | 47.003s  | 30.079s  | 17.650s  |
| Unpack            |  2.691s  |  2.843s  | -        |
| Unpack compressed |  5.332s  |  4.522s  | -        |

**User time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | sar      | sar -j4 |
|-------------------|----------|----------|-----|
| Pack              |  1.070s  |  1.564s  |  1.764s  |
| Pack and compress | 46.568s  |  9.110s  | 24.185s  |
| Unpack            |  0.524s  |  0.660s  | -        |
| Unpack compressed |  5.401s  |  3.056s  | -        |

**Sys time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | sar      | sar -j4 |
|-------------------|----------|----------|-----|
| Pack              |  5.563s  |  6.054s  |  7.873s  |
| Pack and compress |  4.629s  |  7.626s  |  7.805s  |
| Unpack            |  2.118s  |  2.070s  | -        |
| Unpack compressed |  2.708s  |  2.927s  | -        |

**Compression ratios** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
|  | tar czf | sar pz | sar -j4 pz |
|---|---|---|---|
| Absolute values | 265615532/1568397485 | 241568771/1568397485 | 241278130/1568397485 |
| Ratio           | 16.94% | 15.40% | 15.38% |

## Stdin / Stdout piping

Use `-` as the archive path to read from stdin or write to stdout, enabling SAR to participate in shell pipelines without creating an archive file as the final destination.

```sh
sar p  - file1 file2 | ssh user@host "sar u -"        # copy files over SSH
sar p  - file1 file2 | sha256sum                       # checksum without a file
sar pz - file1 file2 | aws s3 cp - s3://bucket/b.szt   # stream to object storage
gpg -d secrets.szt   | sar u - -z                      # decrypt and unpack
```

Use `-z` when reading a compressed archive from stdin so SAR knows the format without being able to inspect the file header.

### Disk usage per operation

| Command | Writes to disk |
|---|---|
| `sar p  -` *(single-threaded)* | zero |
| `sar u  -` *(uncompressed)*    | zero |
| `sar l  -` *(uncompressed)*    | zero |
| `sar g  -` *(uncompressed)*    | zero |
| `sar p  - -j N`                | `sar.tmp`: `pack_threads` requires mmap, which needs a seekable file |
| `sar pz -`                     | `sar.tmp`: compression runs on the whole archive after packing, so the packed archive must exist first |
| `sar u  - -z`                  | `sar.tmp`: the decompressor requires a seekable file path, not a pipe — stdin is buffered first |
| `sar l  - -z` / `sar g - -z`  | `sar.tmp` + `sar_stdin.tmp`: decompressed archive written to disk (list/grab use fseek), plus stdin buffered |

The zero-disk guarantee only holds end-to-end when both sides of the pipe use uncompressed single-threaded operations. For example, `sar pz - | ssh host "sar u - -z"` writes a temp file on **both** machines.

## The format
SAR archives are just a flat binary file which is built as a concatenation of blocks, one per file. Each block contains a header and the file contents. The header is a fixed-size C struct storing everything needed to reconstruct the file.

```
[ FileHeader | raw bytes ][ FileHeader | raw bytes ] ...
```

## Compression
When invoked with `pz`/`u`, SAR compresses and decompresses the entire archive using **zstd**. The output is a standard `.szt` file (a valid zstd stream readable by `zstd -d`).

Compression is applied to the **whole archive** after packing, not per file. Multi-threading (`-j N`) is passed directly to zstd's built-in worker pool, replacing the manual pigz-style approach used with gzip.

Only `p` action has its `pz` alternative since SAR is able to detect in the rest of actions if the provided archive is compressed or not.

## Building & Installing
List of dependencies:
- zstd

To build binary, run:
```
make
```

To install binary, run:
```
sudo make install
```

To install binary in a custom path, run:
```
make install PREFIX=<your/path>
```

To uninstall binary, run:
```
sudo make uninstall
```

To uninstall binary from a custom path, run:
```
make uninstall PREFIX=<your/path>
```
