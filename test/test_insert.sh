#!/bin/bash
# Tests for i (insert) action

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
echo "inserted"    > "$WORK/new.txt"
mkdir -p "$WORK/extra"
echo "extra file"  > "$WORK/extra/e.txt"

# --- 1: i adds file to SAR, l confirms it is listed ---
(cd "$WORK" && "$SAR" p archive.sar src)
(cd "$WORK" && "$SAR" i archive.sar new.txt)
listing=$(cd "$WORK" && "$SAR" l archive.sar)
check "i sar: new file listed" "$(echo "$listing" | grep -c 'new.txt')" "1"

# --- 2: i adds file to SAR, u can extract it ---
out="$WORK/t2" && mkdir "$out"
(cd "$WORK" && "$SAR" p t2.sar src)
(cd "$WORK" && "$SAR" i t2.sar new.txt)
(cd "$out"  && "$SAR" u "$WORK/t2.sar")
check "i sar: inserted content" "$(cat "$WORK/new.txt")" "$(cat "$out/new.txt")"

# --- 3: original files still intact after insert ---
check "i sar: original a.txt intact" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 4: i adds directory to SAR ---
out="$WORK/t4" && mkdir "$out"
(cd "$WORK" && "$SAR" p t4.sar src)
(cd "$WORK" && "$SAR" i t4.sar extra)
(cd "$out"  && "$SAR" u "$WORK/t4.sar")
check "i dir: extra/e.txt" "$(cat "$WORK/extra/e.txt")" "$(cat "$out/extra/e.txt")"

# --- 5: i adds file to SGZ, l confirms it is listed ---
(cd "$WORK" && "$SAR" pz archive.szt src)
(cd "$WORK" && "$SAR" i archive.szt new.txt)
listing=$(cd "$WORK" && "$SAR" l archive.szt)
check "i szt: new file listed" "$(echo "$listing" | grep -c 'new.txt')" "1"

# --- 6: i on SGZ, u can extract inserted file ---
out="$WORK/t6" && mkdir "$out"
(cd "$WORK" && "$SAR" pz t6.szt src)
(cd "$WORK" && "$SAR" i t6.szt new.txt)
(cd "$out"  && "$SAR" u "$WORK/t6.szt")
check "i szt: inserted content" "$(cat "$WORK/new.txt")" "$(cat "$out/new.txt")"

# --- 7: i rejects pipe mode ---
err=$(cd "$WORK" && "$SAR" i - new.txt 2>&1)
exit_code=$?
check "i pipe: exit code non-zero" "$exit_code" "1"
if echo "$err" | grep -q "does not support"; then
  ok "i pipe: error message mentions stdin/stdout"
else
  fail "i pipe: expected error message, got: $err"
fi

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
