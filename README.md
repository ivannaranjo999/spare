# SAR - Simple ARchiver 🍁
Simple ARchiver, SAR, is a C tool that is able to **archive** a list of files and directories in a single file and **compress** it if desired. SAR also allows actions like:

- 🌲 Listing the contents of a `.sar|.sgz` file.
- 🌳 Grabbing specific files or directories from a `.sar|.sgz` file.
- 🌴 Inserting specific files or directories to a `.sar|.sgz` file.
- 🐷 Flags for multithreading in packing and compression (pigz style!).

The tool is used as follows:

```
Usage:
Actions:
  sar p   <archive.sar> <file1..fileN>       Pack given files or folders to a SAR archive.
  sar pz  <archive.sgz> <file1..fileN>       Pack given files or folders to a SAR archive and compress it.
  sar u   <archive.sar|.sgz>                 Unpack SAR archive.
  sar l   <archive.sar|.sgz>                 List files contained in a SAR archive.
  sar g   <archive.sar|.sgz> <file1..fileN>  Grab specific files contained in a SAR archive.
  sar i   <archive.sar|.sgz> <file1..fileN>  Insert specific files to a SAR archive.
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
| Pack              | 18.885s  | 21.054s  | 14.452s  |
| Pack and compress | 46.625s  | 75.355s  | 34.143s  |
| Unpack            |  2.699s  |  2.827s  | -        |
| Unpack compressed |  5.623s  |  7.945s  | -        |

**User time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | sar      | sar -j4 |
|-------------------|----------|----------|-----|
| Pack              |  1.077s  |  1.550s  |  1.874s  |
| Pack and compress | 46.391s  | 53.048s  | 68.559s  |
| Unpack            |  0.547s  |  0.627s  | -        |
| Unpack compressed |  5.721s  |  8.173s  | -        |

**Sys time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar      | sar      | sar -j4 |
|-------------------|----------|----------|-----|
| Pack              |  5.184s  |  5.913s  |  8.302s  |
| Pack and compress |  4.606s  |  8.354s  | 10.608s  |
| Unpack            |  2.105s  |  2.099s  | -        |
| Unpack compressed |  2.841s  |  3.269s  | -        |

**Compression ratios** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
|  | tar czf | sar pz | sar -j4 pz |
|---|---|---|---|
| Absolute values | 265615532/1568397485 | 271562997/1568397485 | 271496378/1568397485 |
| Ratio           | 16.94% | 17.31% | 17.31% |

## Stdin / Stdout piping

Use `-` as the archive path to read from stdin or write to stdout, enabling SAR to participate in shell pipelines without creating an archive file as the final destination.

```sh
sar p  - file1 file2 | ssh user@host "sar u -"        # copy files over SSH
sar p  - file1 file2 | sha256sum                       # checksum without a file
sar pz - file1 file2 | aws s3 cp - s3://bucket/b.sgz   # stream to object storage
gpg -d secrets.sgz   | sar u - -z                      # decrypt and unpack
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
| `sar u  - -z`                  | `sar.tmp`: decompressor needs to seek inside the gzip stream, a pipe is not seekable |
| `sar l  - -z` / `sar g - -z`  | `sar.tmp` + `sar_stdin.tmp`: same reason as above, plus an extra file to buffer stdin |

The zero-disk guarantee only holds end-to-end when both sides of the pipe use uncompressed single-threaded operations. For example, `sar pz - | ssh host "sar u - -z"` writes a temp file on **both** machines.

## The format
SAR archives are just a flat binary file which is built as a concatenation of blocks, one per file. Each block contains a header and the file contents. The header is a fixed-size C struct storing everything needed to reconstruct the file.

```
[ FileHeader | raw bytes ][ FileHeader | raw bytes ] ...
```

## Compression
When invoked with `pz`/`u`, SAR compresses and decompresses the entire archive using **zlib's deflate/inflate** algorithm. The result is a **gzip envelope** with the same format produced by standard `gzip` tool.

Compression is applied to the **whole archive** after packing, not per file. This ensures better compression that compressing each file individually.

Only `p` action has its `pz` alternative since SAR is able to detect in the rest of actions if the provided archive is compressed or not.

## Building & Installing
List of dependencies:
- zlib

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
