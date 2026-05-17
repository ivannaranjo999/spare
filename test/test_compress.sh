#!/bin/bash
# Tests for pz (pack+compress) and decompression paths

SAR="$(cd "$(dirname "$0")/.." && pwd)/sar"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

die() { echo "FATAL: $1"; rm -rf "$WORK"; exit 1; }
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
fail(){ echo "FAIL: $1"; FAIL=$((FAIL+1)); }
check() {
  if [ "$2" = "$3" ]; then ok "$1"; else fail "$1 (got '$2', want '$3')"; fi
}

[ -x "$SAR" ] || die "sar binary not found at $SAR"

# Setup
mkdir -p "$WORK/src/subdir"
echo "hello world" > "$WORK/src/a.txt"
echo "foo bar baz" > "$WORK/src/subdir/b.txt"
echo "third file"  > "$WORK/src/c.txt"

# --- 1: pz + u single-thread roundtrip ---
out="$WORK/t1" && mkdir "$out"
(cd "$WORK" && "$SAR" pz t1.sgz src)
(cd "$out"  && "$SAR" u "$WORK/t1.sgz")
check "pz+u: a.txt"        "$(cat "$WORK/src/a.txt")"        "$(cat "$out/src/a.txt")"
check "pz+u: subdir/b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"
check "pz+u: c.txt"        "$(cat "$WORK/src/c.txt")"        "$(cat "$out/src/c.txt")"

# --- 2: pz + u multi-thread roundtrip ---
out="$WORK/t2" && mkdir "$out"
(cd "$WORK" && "$SAR" -j 4 pz t2.sgz src)
(cd "$out"  && "$SAR" u "$WORK/t2.sgz")
check "pz+u multi: a.txt"        "$(cat "$WORK/src/a.txt")"        "$(cat "$out/src/a.txt")"
check "pz+u multi: subdir/b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"

# --- 3: SGZ file starts with gzip magic (1f 8b) ---
(cd "$WORK" && "$SAR" pz magic.sgz src)
magic=$(xxd -p -l 2 "$WORK/magic.sgz")
check "sgz has gzip magic" "$magic" "1f8b"

# --- 4: SGZ is smaller than SAR for compressible content ---
# Use a repetitive file to guarantee compression wins
python3 -c "print('AAAA' * 4096)" > "$WORK/src/big.txt"
(cd "$WORK" && "$SAR" p  size.sar src)
(cd "$WORK" && "$SAR" pz size.sgz src)
sar_size=$(stat -c '%s' "$WORK/size.sar")
sgz_size=$(stat -c '%s' "$WORK/size.sgz")
if [ "$sgz_size" -lt "$sar_size" ]; then
  ok "sgz smaller than sar for compressible data"
else
  fail "sgz ($sgz_size) not smaller than sar ($sar_size)"
fi

# --- 5: pz then l lists files ---
(cd "$WORK" && "$SAR" pz list.sgz src)
listing=$(cd "$WORK" && "$SAR" l list.sgz)
check "pz+l: a.txt listed"        "$(echo "$listing" | grep -c 'a.txt')"        "1"
check "pz+l: subdir/b.txt listed" "$(echo "$listing" | grep -c 'subdir/b.txt')" "1"
check "pz+l: c.txt listed"        "$(echo "$listing" | grep -c 'c.txt')"        "1"

# --- 6: pz then g grabs file ---
out="$WORK/t6" && mkdir "$out"
(cd "$WORK" && "$SAR" pz grab.sgz src)
(cd "$out"  && "$SAR" g "$WORK/grab.sgz" src/a.txt)
check "pz+g content" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 7: single-thread and multi-thread pz produce same content after unpack ---
out_s="$WORK/t7s" && mkdir "$out_s"
out_m="$WORK/t7m" && mkdir "$out_m"
(cd "$WORK" && "$SAR" p    t7.sar src)
(cd "$WORK" && "$SAR" pz   t7s.sgz src)
(cd "$WORK" && "$SAR" -j 4 pz t7m.sgz src)
(cd "$out_s" && "$SAR" u "$WORK/t7s.sgz")
(cd "$out_m" && "$SAR" u "$WORK/t7m.sgz")
check "st vs mt pz: a.txt"        "$(cat "$out_s/src/a.txt")"        "$(cat "$out_m/src/a.txt")"
check "st vs mt pz: subdir/b.txt" "$(cat "$out_s/src/subdir/b.txt")" "$(cat "$out_m/src/subdir/b.txt")"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
