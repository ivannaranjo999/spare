#!/bin/bash
# Tests for per-file xxh64 checksums (header + data, checksum field zeroed)

SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

# sizeof(FileHeader): 3+1+4096+4+4+4+8+8+8+8+8 = 4152 (no padding)
HEADER_SIZE=4152
# mtime field offset: 3+1+4096+4+4+4+8 = 4120
MTIME_OFFSET=4120
# checksum field offset: 3+1+4096+4+4+4+8+8 = 4128 (not HEADER_SIZE-8; stored_size and hole_count follow it)
CHECKSUM_OFFSET=4128

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

[ -x "$SPARE" ] || die "spare binary not found at $SPARE"

mkdir -p "$WORK/src"
echo "hello checksum" > "$WORK/src/hello.txt"
ln -s "hello.txt" "$WORK/src/link"

# --- 1: normal single-threaded pack + unpack ---
(cd "$WORK" && "$SPARE" p archive.sar src/hello.txt 2>/dev/null)
out="$WORK/t1" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/archive.sar" 2>/dev/null)
if [ "$(cat "$out/src/hello.txt" 2>/dev/null)" = "hello checksum" ]; then
  ok "normal unpack (single-threaded)"
else
  fail "normal unpack (single-threaded)"
fi

# --- 2: corrupt data byte -> unpack must fail ---
cp "$WORK/archive.sar" "$WORK/corrupt_data.sar"
corrupt_byte "$WORK/corrupt_data.sar" "$HEADER_SIZE"
out="$WORK/t2" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/corrupt_data.sar" 2>/dev/null); then
  fail "corrupt data not detected"
else
  ok "corrupt data detected"
fi

# --- 3: corrupt header field (mtime) -> unpack must fail ---
cp "$WORK/archive.sar" "$WORK/corrupt_hdr.sar"
corrupt_byte "$WORK/corrupt_hdr.sar" "$MTIME_OFFSET"
out="$WORK/t3" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/corrupt_hdr.sar" 2>/dev/null); then
  fail "corrupt header not detected"
else
  ok "corrupt header detected"
fi

# --- 4: corrupt stored checksum field itself -> unpack must fail ---
cp "$WORK/archive.sar" "$WORK/corrupt_cksum.sar"
# checksum field is at CHECKSUM_OFFSET (stored_size and hole_count follow it)
corrupt_byte "$WORK/corrupt_cksum.sar" $CHECKSUM_OFFSET
out="$WORK/t4" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/corrupt_cksum.sar" 2>/dev/null); then
  fail "corrupt checksum field not detected"
else
  ok "corrupt checksum field detected"
fi

# --- 5: normal multi-threaded pack + unpack ---
(cd "$WORK" && "$SPARE" p -j 2 mt.sar src/hello.txt 2>/dev/null)
out="$WORK/t5" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/mt.sar" 2>/dev/null)
if [ "$(cat "$out/src/hello.txt" 2>/dev/null)" = "hello checksum" ]; then
  ok "normal unpack (multi-threaded)"
else
  fail "normal unpack (multi-threaded)"
fi

# --- 6: corrupt data in multi-threaded archive -> unpack must fail ---
cp "$WORK/mt.sar" "$WORK/mt_corrupt.sar"
corrupt_byte "$WORK/mt_corrupt.sar" "$HEADER_SIZE"
out="$WORK/t6" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/mt_corrupt.sar" 2>/dev/null); then
  fail "corrupt data in mt archive not detected"
else
  ok "corrupt data in mt archive detected"
fi

# --- 7: symlink pack + unpack ---
(cd "$WORK" && "$SPARE" p link.sar src/link 2>/dev/null)
out="$WORK/t7" && mkdir "$out"
(cd "$out" && "$SPARE" u "$WORK/link.sar" 2>/dev/null)
if [ "$(readlink "$out/src/link" 2>/dev/null)" = "hello.txt" ]; then
  ok "symlink unpack"
else
  fail "symlink unpack"
fi

# --- 8: corrupt symlink data -> unpack must fail ---
cp "$WORK/link.sar" "$WORK/link_corrupt.sar"
corrupt_byte "$WORK/link_corrupt.sar" "$HEADER_SIZE"
out="$WORK/t8" && mkdir "$out"
if (cd "$out" && "$SPARE" u "$WORK/link_corrupt.sar" 2>/dev/null); then
  fail "corrupt symlink data not detected"
else
  ok "corrupt symlink data detected"
fi

# --- 9: symlink pack via pipeline (p -) + unpack via pipeline (u -) ---
(cd "$WORK" && "$SPARE" p - src/link 2>/dev/null > link_pipe.sar)
out="$WORK/t9" && mkdir "$out"
(cd "$out" && cat "$WORK/link_pipe.sar" | "$SPARE" u - 2>/dev/null)
if [ "$(readlink "$out/src/link" 2>/dev/null)" = "hello.txt" ]; then
  ok "symlink pack/unpack via pipeline"
else
  fail "symlink pack/unpack via pipeline"
fi

# --- 10: corrupt symlink data in pipeline archive -> unpack must fail ---
cp "$WORK/link_pipe.sar" "$WORK/link_pipe_corrupt.sar"
corrupt_byte "$WORK/link_pipe_corrupt.sar" "$HEADER_SIZE"
out="$WORK/t10" && mkdir "$out"
if (cd "$out" && cat "$WORK/link_pipe_corrupt.sar" | "$SPARE" u - 2>/dev/null); then
  fail "corrupt symlink data in pipeline not detected"
else
  ok "corrupt symlink data in pipeline detected"
fi

rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
