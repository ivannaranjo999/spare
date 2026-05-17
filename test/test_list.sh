#!/bin/bash
# Tests for l (list) action

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

# --- 1: l on SAR lists expected files ---
listing=$(cd "$WORK" && "$SAR" l archive.sar)
check "l sar: a.txt present"        "$(echo "$listing" | grep -c 'src/a.txt')"        "1"
check "l sar: subdir/b.txt present" "$(echo "$listing" | grep -c 'src/subdir/b.txt')" "1"
check "l sar: c.txt present"        "$(echo "$listing" | grep -c 'src/c.txt')"         "1"

# --- 2: l on SAR lists correct total count (3 files, no directory entries) ---
total=$(echo "$listing" | grep -c '.')
check "l sar: total entries" "$total" "3"

# --- 3: l on SGZ lists expected files ---
listing=$(cd "$WORK" && "$SAR" l archive.sgz)
check "l sgz: a.txt present"        "$(echo "$listing" | grep -c 'src/a.txt')"        "1"
check "l sgz: subdir/b.txt present" "$(echo "$listing" | grep -c 'src/subdir/b.txt')" "1"
check "l sgz: c.txt present"        "$(echo "$listing" | grep -c 'src/c.txt')"         "1"

# --- 4: l output is one entry per line (no blank lines between entries) ---
lines_with_src=$(echo "$listing" | grep -c 'src')
check "l sgz: each entry on its own line" "$lines_with_src" "3"

# --- 5: l on SAR via stdin pipe ---
listing=$(cd "$WORK" && "$SAR" l - < archive.sar)
check "l stdin: a.txt present" "$(echo "$listing" | grep -c 'src/a.txt')" "1"
check "l stdin: c.txt present" "$(echo "$listing" | grep -c 'src/c.txt')"  "1"

# --- 6: l on SGZ via stdin pipe (-z) ---
listing=$(cd "$WORK" && "$SAR" l - -z < archive.sgz)
check "l stdin sgz: a.txt present" "$(echo "$listing" | grep -c 'src/a.txt')" "1"

# --- 7: l exits 0 on success ---
(cd "$WORK" && "$SAR" l archive.sar) > /dev/null 2>&1
check "l exit code on success" "$?" "0"

# --- cleanup ---
rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
