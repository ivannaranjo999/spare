#!/bin/bash
# Run all test suites and report a combined result

DIR="$(cd "$(dirname "$0")" && pwd)"
PASS=0
FAIL=0

for script in "$DIR"/test_*.sh; do
  name=$(basename "$script")
  echo "=== $name ==="
  if bash "$script"; then
    PASS=$((PASS+1))
  else
    FAIL=$((FAIL+1))
  fi
  echo ""
done

echo "Suites: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
