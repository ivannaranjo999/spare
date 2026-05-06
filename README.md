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

### Speed matrix

Command legend:
| Operation         | tar     | sar    | sar -p    | sar -c    | sar -T    |
|-------------------|---------|--------|-----------|-----------|-----------|
| Pack              | tar cf  | sar p  | sar -p p  | -         | -         |
| Pack and compress | tar czf | sar pz | sar -p pz | sar -c pz | sar -T pz |
| Unpack            | tar xf  | sar u  | -         | -         | -         |
| Unpack compressed | tar xzf | sar u  | -         | -         | -         |

**User time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar     | sar    | sar -p    | sar -c    | sar -T    |
|-------------------|---------|--------|-----------|-----------|-----------|
| Pack              | 1.26s   | 1.92s  | 1.74s     | -         | -         |
| Pack and compress | 45.80s  | 49.41s | 50.06s    | 72.63s    | 71.90s    |
| Unpack            | 0.66s   | 1.37s  | -         | -         | -         |
| Unpack compressed | 6.24s   | 8.68s  | -         | -         | -         |

**Sys time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar     | sar    | sar -p    | sar -c    | sar -T    |
|-------------------|---------|--------|-----------|-----------|-----------|
| Pack              | 6.18s   | 6.62s  | 6.89s     | -         | -         |
| Pack and compress | 4.43s   | 7.77s  | 7.23s     | 11.94s    | 10.58s    |
| Unpack            | 3.73s   | 5.30s  | -         | -         | -         |
| Unpack compressed | 4.40s   | 5.99s  | -         | -         | -         |

**Total time** for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
| Operation         | tar     | sar    | sar -p    | sar -c    | sar -T    |
|-------------------|---------|--------|-----------|-----------|-----------|
| Pack              | 21.955s | 25.135s| 12.615s   | -         | -         |
| Pack and compress | 46.237s | 71.21s | 58.670s   | 47.446s   | 34.487s   |
| Unpack            | 8.010s  | 10.778s| -         | -         | -         |
| Unpack compressed | 7.154s  | 17.375s| -         | -         | -         |

### Compression matrix

Ratios for [Linux kernel 7.0](https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz)
|                 | tar czf        | sar pz         | sar -T pz        |
|-----------------|----------------|----------------|------------------|
| Absolute values | 268216/1846928 | 274304/1846928 | 274224/1846928   |
| Ratio           | 14.52%         | 14.85%         | 14.84%           |

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
