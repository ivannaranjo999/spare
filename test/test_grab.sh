#!/bin/bash
# Tests for g (grab) action

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
(cd "$WORK" && "$SAR" p  archive.sar src)
(cd "$WORK" && "$SAR" pz archive.sgz src)

# --- 1: g grabs top-level file from SAR ---
out="$WORK/t1" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sar" src/a.txt)
check "g sar top-level" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 2: g grabs nested file from SAR ---
out="$WORK/t2" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sar" src/subdir/b.txt)
check "g sar nested" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"

# --- 3: g grabs multiple files from SAR ---
out="$WORK/t3" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sar" src/a.txt src/c.txt)
check "g multi: a.txt" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"
check "g multi: c.txt" "$(cat "$WORK/src/c.txt")" "$(cat "$out/src/c.txt")"

# --- 4: g grabs file from SGZ ---
out="$WORK/t4" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sgz" src/a.txt)
check "g sgz" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 5: g does not extract non-requested files ---
out="$WORK/t5" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sar" src/a.txt)
if [ -f "$out/src/c.txt" ]; then
  fail "g extracted unrequested file c.txt"
else
  ok "g only extracts requested file"
fi

# --- 6: g on non-existent file does not crash (exits 0) ---
out="$WORK/t6" && mkdir "$out"
(cd "$out" && "$SAR" g "$WORK/archive.sar" src/does_not_exist.txt) > /dev/null 2>&1
check "g missing file exit code" "$?" "0"

# --- 7: g via stdin pipe (SAR) ---
out="$WORK/t7" && mkdir "$out"
(cd "$out" && "$SAR" g - src/a.txt < "$WORK/archive.sar")
check "g stdin sar" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 8: g via stdin pipe (SGZ with -z) ---
out="$WORK/t8" && mkdir "$out"
(cd "$out" && "$SAR" g - -z src/subdir/b.txt < "$WORK/archive.sgz")
check "g stdin sgz" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
