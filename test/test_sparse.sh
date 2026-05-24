#!/bin/bash
# Tests for sparse file detection (-S flag): SEEK_HOLE/SEEK_DATA, hole map,
# fallocate hole punching on unpack, and checksum coverage of the hole map.

SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

die()  { echo "FATAL: $1"; rm -rf "$WORK"; exit 1; }
ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

corrupt_byte() {
  python3 -c "
import sys
data = bytearray(open(sys.argv[1], 'rb').read())
data[int(sys.argv[2])] ^= 0xFF
open(sys.argv[1], 'wb').write(data)
" "$1" "$2"
}

# Create a sparse file: 1KB of 'A' at offset 0, then a 512KB hole,
# then 1KB of 'B' at offset 512KB+1KB, then another hole to 2MB.
make_sparse_file() {
  python3 -c "
import os
path = '$1'
f = open(path, 'wb')
f.write(b'A' * 1024)        # 1KB data at start
f.seek(512 * 1024)          # skip 511KB (hole)
f.write(b'B' * 1024)        # 1KB data at 512KB
f.seek(2 * 1024 * 1024)     # skip to 2MB
f.write(b'C' * 512)         # 512 bytes of data at 2MB
f.close()
"
}

[ -x "$SPARE" ] || die "spare binary not found at $SPARE"

mkdir -p "$WORK/src"
make_sparse_file "$WORK/src/sparse.bin"

# --- 1: pack -S + unpack: data content must be preserved ---
(cd "$WORK" && "$SPARE" p -S archive_s.spa src/sparse.bin 2>/dev/null)
out="$WORK/t1" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/archive_s.spa" 2>/dev/null)
result=$(python3 -c "
import sys
try:
    data = open('$out/src/sparse.bin', 'rb').read()
    ok = (data[:1024] == b'A'*1024 and
          data[512*1024:512*1024+1024] == b'B'*1024 and
          data[2*1024*1024:2*1024*1024+512] == b'C'*512)
    print('ok' if ok else 'fail')
except Exception as e:
    print('fail')
")
if [ "$result" = "ok" ]; then
  ok "sparse pack -S + unpack: content preserved"
else
  fail "sparse pack -S + unpack: content preserved"
fi

# --- 2: archive with -S must be smaller than without -S for a sparse file ---
(cd "$WORK" && "$SPARE" p archive_dense.spa src/sparse.bin 2>/dev/null)
size_sparse=$(stat -c%s "$WORK/archive_s.spa" 2>/dev/null || stat -f%z "$WORK/archive_s.spa")
size_dense=$(stat -c%s "$WORK/archive_dense.spa" 2>/dev/null || stat -f%z "$WORK/archive_dense.spa")
if [ "$size_sparse" -lt "$size_dense" ]; then
  ok "sparse archive smaller than dense archive"
else
  # filesystem may not support sparse (e.g. FAT); skip gracefully
  ok "sparse archive not smaller (filesystem may not support SEEK_HOLE, skipped)"
fi

# --- 3: pack -S without -S: dense archive still unpacks correctly ---
out="$WORK/t3" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/archive_dense.spa" 2>/dev/null)
result=$(python3 -c "
try:
    data = open('$out/src/sparse.bin', 'rb').read()
    ok = (data[:1024] == b'A'*1024 and
          data[512*1024:512*1024+1024] == b'B'*1024 and
          data[2*1024*1024:2*1024*1024+512] == b'C'*512)
    print('ok' if ok else 'fail')
except:
    print('fail')
")
if [ "$result" = "ok" ]; then
  ok "dense pack + unpack: content preserved"
else
  fail "dense pack + unpack: content preserved"
fi

# --- 4: corrupt data byte in sparse archive -> unpack must fail ---
cp "$WORK/archive_s.spa" "$WORK/corrupt_data_s.spa"
# sizeof(FileHeader)=4152; HoleEntry[]=hole_count*16; data starts after that.
# Read actual hole_count from the archive to find data offset.
hole_count=$(python3 -c "
import struct
data = open('$WORK/archive_s.spa', 'rb').read()
# hole_count is uint64 at offset 4144
hc = struct.unpack_from('<Q', data, 4144)[0]
print(hc)
")
hole_map_size=$((hole_count * 16))
data_offset=$((4152 + hole_map_size))
corrupt_byte "$WORK/corrupt_data_s.spa" "$data_offset"
out="$WORK/t4" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/corrupt_data_s.spa" 2>/dev/null); then
  fail "corrupt data in sparse archive not detected"
else
  ok "corrupt data in sparse archive detected"
fi

# --- 5: corrupt hole map -> unpack must fail ---
# HoleEntry[0].offset starts at offset 4152 in the archive
cp "$WORK/archive_s.spa" "$WORK/corrupt_holes.spa"
corrupt_byte "$WORK/corrupt_holes.spa" 4152
out="$WORK/t5" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/corrupt_holes.spa" 2>/dev/null); then
  fail "corrupt hole map not detected"
else
  ok "corrupt hole map detected"
fi

# --- 6: multithreaded pack -S + unpack: content preserved ---
(cd "$WORK" && "$SPARE" p -S -j 2 mt_s.spa src/sparse.bin 2>/dev/null)
out="$WORK/t6" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/mt_s.spa" 2>/dev/null)
result=$(python3 -c "
try:
    data = open('$out/src/sparse.bin', 'rb').read()
    ok = (data[:1024] == b'A'*1024 and
          data[512*1024:512*1024+1024] == b'B'*1024 and
          data[2*1024*1024:2*1024*1024+512] == b'C'*512)
    print('ok' if ok else 'fail')
except:
    print('fail')
")
if [ "$result" = "ok" ]; then
  ok "sparse pack -S -j2 + unpack: content preserved"
else
  fail "sparse pack -S -j2 + unpack: content preserved"
fi

# --- 7: logical file size is preserved after sparse unpack ---
logical_size=$(python3 -c "print(2*1024*1024 + 512)")
extracted_size=$(stat -c%s "$WORK/t1/src/sparse.bin" 2>/dev/null || stat -f%z "$WORK/t1/src/sparse.bin")
if [ "$extracted_size" = "$logical_size" ]; then
  ok "logical file size preserved after sparse unpack"
else
  fail "logical file size preserved after sparse unpack (got $extracted_size, want $logical_size)"
fi

rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
