#!/bin/bash
# Tests for archive version and magic checks

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
# Assert command exits non-zero and stderr contains a pattern
expect_err() {
  local desc="$1" pattern="$2"
  shift 2
  err=$("$@" 2>&1)
  code=$?
  if [ "$code" -eq 0 ]; then
    fail "$desc: expected non-zero exit, got 0"
  elif echo "$err" | grep -qi "$pattern"; then
    ok "$desc"
  else
    fail "$desc: exit $code but stderr did not match '$pattern' (got: $err)"
  fi
}

[ -x "$SAR" ] || die "sar binary not found at $SAR"

# Setup: valid v2 archive
mkdir -p "$WORK/src"
echo "hello" > "$WORK/src/a.txt"
(cd "$WORK" && "$SAR" p valid.sar src)

# Build a wrong-version archive: pack a real one, then patch byte 3 (version) to 99
cp "$WORK/valid.sar" "$WORK/badver.sar"
printf '\x63' | dd of="$WORK/badver.sar" bs=1 seek=3 count=1 conv=notrunc 2>/dev/null

# Build a bad-magic archive: same size as a real header but first 3 bytes are wrong
cp "$WORK/valid.sar" "$WORK/badmagic.sar"
printf 'XXX' | dd of="$WORK/badmagic.sar" bs=1 seek=0 count=3 conv=notrunc 2>/dev/null

# --- 1-4: valid archive, all read actions succeed ---
out="$WORK/t1" && mkdir "$out"
(cd "$out" && "$SAR" u "$WORK/valid.sar") > /dev/null 2>&1
check "valid: u exits 0" "$?" "0"

listing=$(cd "$WORK" && "$SAR" l valid.sar 2>/dev/null)
check "valid: l exits 0" "$?" "0"

out="$WORK/t3" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/valid.sar" src/a.txt) > /dev/null 2>&1
check "valid: g exits 0" "$?" "0"

(cd "$WORK" && "$SAR" i valid.sar src/a.txt) > /dev/null 2>&1
check "valid: i exits 0" "$?" "0"

# --- 5-8: wrong version, all read actions reject with version error ---
out="$WORK/t5" && mkdir "$out"
expect_err "badver: u rejects"    "version" "$SAR" u "$WORK/badver.sar"
expect_err "badver: l rejects"    "version" "$SAR" l "$WORK/badver.sar"
out="$WORK/t7" && mkdir "$out"
expect_err "badver: g rejects"    "version" "$SAR" g "$WORK/badver.sar" src/a.txt
expect_err "badver: i rejects"    "version" "$SAR" i "$WORK/badver.sar" src/a.txt

# --- 9: bad magic, rejected with magic error ---
expect_err "badmagic: u rejects"  "magic"   "$SAR" u "$WORK/badmagic.sar"
expect_err "badmagic: l rejects"  "magic"   "$SAR" l "$WORK/badmagic.sar"

# --- 10: empty file, rejected (cannot read header) ---
touch "$WORK/empty.sar"
expect_err "empty: u rejects"     "."       "$SAR" u "$WORK/empty.sar"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
