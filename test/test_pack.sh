#!/bin/bash
# Tests for p (pack) and u (unpack) actions

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
ln -s "a.txt"        "$WORK/src/link_to_a"
chmod 750            "$WORK/src/a.txt"

# --- 1: single file roundtrip ---
out="$WORK/t1" && mkdir "$out"
(cd "$WORK" && "$SAR" p t1.sar src/a.txt)
(cd "$out"  && "$SAR" u "$WORK/t1.sar")
check "single file content" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"

# --- 2: directory tree roundtrip ---
out="$WORK/t2" && mkdir "$out"
(cd "$WORK" && "$SAR" p t2.sar src)
(cd "$out"  && "$SAR" u "$WORK/t2.sar")
check "dir: a.txt"        "$(cat "$WORK/src/a.txt")"        "$(cat "$out/src/a.txt")"
check "dir: subdir/b.txt" "$(cat "$WORK/src/subdir/b.txt")" "$(cat "$out/src/subdir/b.txt")"
check "dir: c.txt"        "$(cat "$WORK/src/c.txt")"        "$(cat "$out/src/c.txt")"

# --- 3: multiple separate file arguments ---
out="$WORK/t3" && mkdir "$out"
(cd "$WORK" && "$SAR" p t3.sar src/a.txt src/c.txt)
(cd "$out"  && "$SAR" u "$WORK/t3.sar")
check "multi-arg: a.txt" "$(cat "$WORK/src/a.txt")" "$(cat "$out/src/a.txt")"
check "multi-arg: c.txt" "$(cat "$WORK/src/c.txt")" "$(cat "$out/src/c.txt")"

# --- 4: symlink preserved ---
out="$WORK/t4" && mkdir "$out"
(cd "$WORK" && "$SAR" p t4.sar src)
(cd "$out"  && "$SAR" u "$WORK/t4.sar")
if [ -L "$out/src/link_to_a" ]; then
  check "symlink target" "$(readlink "$out/src/link_to_a")" "a.txt"
else
  fail "symlink not restored as symlink"
fi

# --- 5: permissions preserved ---
out="$WORK/t5" && mkdir "$out"
(cd "$WORK" && "$SAR" p t5.sar src/a.txt)
(cd "$out"  && "$SAR" u "$WORK/t5.sar")
check "mode preserved" "$(stat -c '%a' "$out/src/a.txt")" "750"

# --- 6: mtime preserved ---
orig_mtime=$(stat -c '%Y' "$WORK/src/a.txt")
out="$WORK/t6" && mkdir "$out"
(cd "$WORK" && "$SAR" p t6.sar src/a.txt)
(cd "$out"  && "$SAR" u "$WORK/t6.sar")
check "mtime preserved" "$(stat -c '%Y' "$out/src/a.txt")" "$orig_mtime"

# --- 7: multi-thread pack produces same content ---
out_s="$WORK/t7s" && mkdir "$out_s"
out_m="$WORK/t7m" && mkdir "$out_m"
(cd "$WORK" && "$SAR" p t7s.sar src)
(cd "$WORK" && "$SAR" -j 4 p t7m.sar src)
(cd "$out_s" && "$SAR" u "$WORK/t7s.sar")
(cd "$out_m" && "$SAR" u "$WORK/t7m.sar")
check "multi-thread: a.txt"        "$(cat "$out_s/src/a.txt")"        "$(cat "$out_m/src/a.txt")"
check "multi-thread: subdir/b.txt" "$(cat "$out_s/src/subdir/b.txt")" "$(cat "$out_m/src/subdir/b.txt")"

# --- 8: p overwrites existing archive ---
out="$WORK/t8" && mkdir "$out"
(cd "$WORK" && "$SAR" p t8.sar src/a.txt)
echo "new content" > "$WORK/src/a.txt"
(cd "$WORK" && "$SAR" p t8.sar src/a.txt)
(cd "$out"  && "$SAR" u "$WORK/t8.sar")
check "overwrite: new content" "$(cat "$out/src/a.txt")" "new content"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
