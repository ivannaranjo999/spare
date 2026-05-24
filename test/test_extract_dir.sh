#!/bin/bash
# Tests for -C (extract to directory) flag

SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

die()  { echo "FATAL: $1"; rm -rf "$WORK"; exit 1; }
ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

[ -x "$SPARE" ] || die "spare binary not found at $SPARE"

# Setup: pack a small archive to use in all tests
mkdir -p "$WORK/src/subdir"
echo "hello extract" > "$WORK/src/a.txt"
echo "nested file"  > "$WORK/src/subdir/b.txt"
(cd "$WORK" && "$SPARE" p archive.sar src/a.txt src/subdir/b.txt 2>/dev/null)
[ -f "$WORK/archive.sar" ] || die "failed to create test archive"

# --- 1: u -C to existing directory ---
out="$WORK/t1" && mkdir "$out"
(cd "$WORK" && "$SPARE" u -C "$out" archive.sar 2>/dev/null)
if [ "$(cat "$out/src/a.txt" 2>/dev/null)" = "hello extract" ]; then
  ok "u -C: file extracted to target dir"
else
  fail "u -C: file extracted to target dir"
fi

# --- 2: u -C nested file lands in correct subdir ---
if [ "$(cat "$out/src/subdir/b.txt" 2>/dev/null)" = "nested file" ]; then
  ok "u -C: nested file extracted to target dir"
else
  fail "u -C: nested file extracted to target dir"
fi

# --- 3: u -C does not pollute current directory ---
out="$WORK/t3" && mkdir "$out"
(cd "$WORK" && "$SPARE" u -C "$out" archive.sar 2>/dev/null)
if [ ! -f "$WORK/src/a.txt" ] || [ -f "$WORK/a.txt" ]; then
  # src/a.txt was created during setup so it exists; check that unpack didn't
  # write anything NEW outside the target dir by checking a file that only
  # unpack would create in CWD
  fail "u -C: leaked files outside target dir"
else
  ok "u -C: no files leaked to current directory"
fi

# --- 4: u -C with relative archive path ---
out="$WORK/t4" && mkdir "$out"
(cd "$WORK" && "$SPARE" u -C "$out" archive.sar 2>/dev/null)
if [ "$(cat "$out/src/a.txt" 2>/dev/null)" = "hello extract" ]; then
  ok "u -C: relative archive path resolved correctly"
else
  fail "u -C: relative archive path resolved correctly"
fi

# --- 5: u -C to non-existing directory exits non-zero ---
if (cd "$WORK" && "$SPARE" u -C "$WORK/does_not_exist" archive.sar 2>/dev/null); then
  fail "u -C: non-existing dir should fail"
else
  ok "u -C: non-existing dir rejected"
fi

# --- 6: g -C extracts grabbed file to target dir ---
out="$WORK/t6" && mkdir "$out"
(cd "$WORK" && "$SPARE" g -C "$out" archive.sar src/a.txt 2>/dev/null)
if [ "$(cat "$out/src/a.txt" 2>/dev/null)" = "hello extract" ]; then
  ok "g -C: grabbed file extracted to target dir"
else
  fail "g -C: grabbed file extracted to target dir"
fi

# --- 7: -C missing argument exits non-zero ---
if (cd "$WORK" && "$SPARE" u -C 2>/dev/null); then
  fail "-C without argument should fail"
else
  ok "-C without argument rejected"
fi

rm -rf "$WORK"
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
