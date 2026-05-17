#!/bin/bash
# Tests for stdin/stdout pipeline support (-z flag and - archive path)

SAR="$(cd "$(dirname "$0")/.." && pwd)/sar"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

die() {
  echo "FATAL: $1"
  rm -rf "$WORK"
  exit 1
}
ok() {
  echo "PASS: $1"
  PASS=$((PASS + 1))
}
fail() {
  echo "FAIL: $1"
  FAIL=$((FAIL + 1))
}

check() {
  local desc="$1"
  if [ "$2" = "$3" ]; then ok "$desc"; else fail "$desc (got '$2', want '$3')"; fi
}

[ -x "$SAR" ] || die "sar binary not found at $SAR"

# --- setup: create a small file tree ---
mkdir -p "$WORK/src/subdir"
echo "hello world" >"$WORK/src/a.txt"
echo "foo bar baz" >"$WORK/src/subdir/b.txt"
echo "third file" >"$WORK/src/c.txt"

# Helper: pack from WORK, unpack into a fresh dir, compare one file.
# Usage: roundtrip <desc> <pack flags> <unpack flags> <relpath>
roundtrip() {
  local desc="$1" packf="$2" unpkf="$3" rel="$4"
  local out="$WORK/out_$$_$PASS"
  mkdir -p "$out"
  # shellcheck disable=SC2086
  (cd "$WORK" && $SAR $packf src) | (cd "$out" && $SAR $unpkf)
  check "$desc: $rel" "$(cat "$WORK/src/$rel")" "$(cat "$out/src/$rel")"
}

# --- test 1: p - (single-thread) | u - ---
roundtrip "p-/u- single" "p -" "u -" "a.txt"
roundtrip "p-/u- single" "p -" "u -" "subdir/b.txt"

# --- test 2: p - (multi-thread) | u - ---
roundtrip "p-/u- multi" "-j 4 p -" "u -" "a.txt"
roundtrip "p-/u- multi" "-j 4 p -" "u -" "subdir/b.txt"

# --- test 3: pz - | u - -z ---
roundtrip "pz-/u--z single" "pz -" "u - -z" "a.txt"
roundtrip "pz-/u--z single" "pz -" "u - -z" "subdir/b.txt"

# --- test 4: pz - (multi-thread) | u - -z ---
roundtrip "pz-/u--z multi" "-j 4 pz -" "u - -z" "a.txt"
roundtrip "pz-/u--z multi" "-j 4 pz -" "u - -z" "subdir/b.txt"

# --- test 5: l - (list uncompressed from stdin) ---
(cd "$WORK" && "$SAR" p archive.sar src)
listing=$(cd "$WORK" && "$SAR" l - <archive.sar)
check "l - contains a.txt" "$(echo "$listing" | grep -c 'a.txt')" "1"
check "l - contains subdir/b.txt" "$(echo "$listing" | grep -c 'subdir/b.txt')" "1"
check "l - contains c.txt" "$(echo "$listing" | grep -c 'c.txt')" "1"

# --- test 6: l - -z (list compressed from stdin) ---
(cd "$WORK" && "$SAR" pz archive.sgz src)
listing=$(cd "$WORK" && "$SAR" l - -z <archive.sgz)
check "l - -z contains a.txt" "$(echo "$listing" | grep -c 'a.txt')" "1"
check "l - -z contains subdir/b.txt" "$(echo "$listing" | grep -c 'subdir/b.txt')" "1"

# --- test 7: p - redirected to file, then normal unpack ---
out7="$WORK/out7"
mkdir -p "$out7"
(cd "$WORK" && "$SAR" p - src) >"$WORK/redir.sar"
(cd "$out7" && "$SAR" u "$WORK/redir.sar")
check "p - redirect, u file" "$(cat "$WORK/src/a.txt")" "$(cat "$out7/src/a.txt")"

# --- test 8: pz - redirected to file, u - -z to verify ---
out8="$WORK/out8"
mkdir -p "$out8"
(cd "$WORK" && "$SAR" pz - src) >"$WORK/redir.sgz"
(cd "$out8" && "$SAR" u - -z <"$WORK/redir.sgz")
check "pz - redirect, u - -z" "$(cat "$WORK/src/a.txt")" "$(cat "$out8/src/a.txt")"
check "pz - redirect, u - -z" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out8/src/subdir/b.txt")"

# --- test 9: g - (grab from uncompressed stdin) ---
out9="$WORK/out9"
mkdir -p "$out9"
(cd "$WORK" && "$SAR" p archive.sar src)
(cd "$out9" && "$SAR" g - src/a.txt <"$WORK/archive.sar")
check "g - grab a.txt" "$(cat "$WORK/src/a.txt")" "$(cat "$out9/src/a.txt")"

# --- test 10: g - -z (grab from compressed stdin) ---
out10="$WORK/out10"
mkdir -p "$out10"
(cd "$WORK" && "$SAR" pz archive.sgz src)
(cd "$out10" && "$SAR" g - -z src/subdir/b.txt <"$WORK/archive.sgz")
check "g - -z grab b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out10/src/subdir/b.txt")"

# --- cleanup ---
rm -rf "$WORK"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
