#!/bin/bash
# Tests for uid/gid preservation across pack/unpack

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
mkdir -p "$WORK/src"
echo "test content" > "$WORK/src/a.txt"
ln -s "a.txt" "$WORK/src/link"

# --- 1: uid preserved ---
src_uid=$(stat -c '%u' "$WORK/src/a.txt")
out="$WORK/t1" && mkdir "$out"
(cd "$WORK" && "$SPARE" p t1.spa src/a.txt)
(cd "$out"  && "$SPARE" u "$WORK/t1.spa")
check "uid preserved" "$(stat -c '%u' "$out/src/a.txt")" "$src_uid"

# --- 2: gid preserved (primary group) ---
src_gid=$(stat -c '%g' "$WORK/src/a.txt")
check "gid preserved" "$(stat -c '%g' "$out/src/a.txt")" "$src_gid"

# --- 3: gid preserved with a secondary group (skipped if none available) ---
primary_gid=$(id -g)
secondary_gid=$(id -G | tr ' ' '\n' | grep -v "^${primary_gid}$" | head -1)

if [ -n "$secondary_gid" ]; then
  echo "secondary group content" > "$WORK/src/b.txt"
  chown ":$secondary_gid" "$WORK/src/b.txt"
  out3="$WORK/t3" && mkdir "$out3"
  (cd "$WORK" && "$SPARE" p t3.spa src/b.txt)
  (cd "$out3"  && "$SPARE" u "$WORK/t3.spa")
  check "gid secondary group preserved" \
    "$(stat -c '%g' "$out3/src/b.txt")" "$secondary_gid"
else
  echo "SKIP: gid secondary group (user has no secondary groups)"
fi

# --- 4: uid/gid preserved for symlinks ---
sym_uid=$(stat -c '%u' "$WORK/src/link")
sym_gid=$(stat -c '%g' "$WORK/src/link")
out4="$WORK/t4" && mkdir "$out4"
(cd "$WORK" && "$SPARE" p t4.spa src/link)
(cd "$out4"  && "$SPARE" u "$WORK/t4.spa")
check "symlink uid preserved" "$(stat -c '%u' "$out4/src/link")" "$sym_uid"
check "symlink gid preserved" "$(stat -c '%g' "$out4/src/link")" "$sym_gid"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
