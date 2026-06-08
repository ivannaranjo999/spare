#!/bin/bash
# Record a spare demo session.
# Run: asciinema rec assets/demo.cast -f asciicast-v2 --command "bash tools/record_demo.sh"

SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
WORK="$(mktemp -d)"
trap "rm -rf '$WORK'" EXIT

[ -x "$SPARE" ] || {
  echo "spare binary not found — run 'make' first"
  exit 1
}

truncate -s 4G "$WORK/vm.qcow2"
dd if=/dev/urandom of="$WORK/vm.qcow2" bs=1M count=100 conv=notrunc 2>/dev/null

cd "$WORK"

ls -lh vm.qcow2
"$SPARE" -S -v p vm.spa vm.qcow2
ls -lh vm.spa
"$SPARE" -S -v u vm.spa
ls -lh vm.qcow2
