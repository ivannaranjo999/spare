#!/bin/bash
# Tests for pz (pack+compress) and decompression paths

SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

die() { echo "FATAL: $1"; rm -rf "$WORK"; exit 1; }
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
fail(){ echo "FAIL: $1"; FAIL=$((FAIL+1)); }
check() {
  if [ "$2" = "$3" ]; then ok "$1"; else fail "$1 (got '$2', want '$3')"; fi
}

[ -x "$SPARE" ] || die "spare binary not found at $SPARE"

# Setup
mkdir -p "$WORK/src/subdir"
echo "hello world" > "$WORK/src/a.txt"
echo "foo bar baz" > "$WORK/src/subdir/b.txt"
echo "third file"  > "$WORK/src/c.txt"

# --- 1: pz + u single-thread roundtrip ---
out="$WORK/t1" && mkdir "$out"
(cd "$WORK" && "$SPARE" pz t1.szt src)
(cd "$out"  && "$SPARE" u "$WORK/t1.szt")
check "pz+u: a.txt"        "$(cat "$WORK/src/a.txt")"        "$(cat "$out/src/a.txt")"
check "pz+u: subdir/b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"
check "pz+u: c.txt"        "$(cat "$WORK/src/c.txt")"        "$(cat "$out/src/c.txt")"

# --- 2: pz + u multi-thread roundtrip ---
out="$WORK/t2" && mkdir "$out"
(cd "$WORK" && "$SPARE" -j 4 pz t2.szt src)
(cd "$out"  && "$SPARE" u "$WORK/t2.szt")
check "pz+u multi: a.txt"        "$(cat "$WORK/src/a.txt")"        "$(cat "$out/src/a.txt")"
check "pz+u multi: subdir/b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"

# --- 3: SZT file starts with zstd magic (28 b5 2f fd) ---
(cd "$WORK" && "$SPARE" pz magic.szt src)
magic=$(xxd -p -l 4 "$WORK/magic.szt")
check "szt has zstd magic" "$magic" "28b52ffd"

# --- 4: SZT is smaller than SAR for compressible content ---
# Use a repetitive file to guarantee compression wins
python3 -c "print('AAAA' * 4096)" > "$WORK/src/big.txt"
(cd "$WORK" && "$SPARE" p  size.sar src)
(cd "$WORK" && "$SPARE" pz size.szt src)
sar_size=$(stat -c '%s' "$WORK/size.sar")
szt_size=$(stat -c '%s' "$WORK/size.szt")
if [ "$szt_size" -lt "$sar_size" ]; then
  ok "szt smaller than sar for compressible data"
else
  fail "szt ($szt_size) not smaller than sar ($sar_size)"
fi

# --- 5: pz then l lists files ---
(cd "$WORK" && "$SPARE" pz list.szt src)
listing=$(cd "$WORK" && "$SPARE" l list.szt)
check "pz+l: a.txt listed"        "$(echo "$listing" | grep -c 'a.txt')"        "1"
check "pz+l: subdir/b.txt listed" "$(echo "$listing" | grep -c 'subdir/b.txt')" "1"
check "pz+l: c.txt listed"        "$(echo "$listing" | grep -c 'c.txt')"        "1"

# --- 6: pz then g grabs file ---
out="$WORK/t6" && mkdir "$out"
(cd "$WORK" && "$SPARE" pz grab.szt src)
(cd "$out"  && "$SPARE" g "$WORK/grab.szt" src/a.txt)
check "pz+g content" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 7: single-thread and multi-thread pz produce same content after unpack ---
out_s="$WORK/t7s" && mkdir "$out_s"
out_m="$WORK/t7m" && mkdir "$out_m"
(cd "$WORK" && "$SPARE" p    t7.sar src)
(cd "$WORK" && "$SPARE" pz   t7s.szt src)
(cd "$WORK" && "$SPARE" -j 4 pz t7m.szt src)
(cd "$out_s" && "$SPARE" u "$WORK/t7s.szt")
(cd "$out_m" && "$SPARE" u "$WORK/t7m.szt")
check "st vs mt pz: a.txt"        "$(cat "$out_s/src/a.txt")"        "$(cat "$out_m/src/a.txt")"
check "st vs mt pz: subdir/b.txt" "$(cat "$out_s/src/subdir/b.txt")" "$(cat "$out_m/src/subdir/b.txt")"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
