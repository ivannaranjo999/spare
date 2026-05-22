#!/bin/sh
###############################################################################
# isSparse.sh
#
# Checks how sparse a file is.
###############################################################################
if [ $# -ne 1 ]; then
  echo "Usage: $0 <file>" >&2
  exit 1
fi

FILE="$1"

if [ ! -f "$FILE" ]; then
  echo "Error: '$FILE' is not a regular file." >&2
  exit 1
fi

MIN_SPARSE_SIZE=512
LOGICAL_SIZE=$(stat -c '%s' "$FILE")
BLOCKS=$(stat -c '%b' "$FILE")
BLOCK_SIZE=$(stat -c '%B' "$FILE")
ALLOCATED_BYTES=$((BLOCKS * BLOCK_SIZE))

if [ "$LOGICAL_SIZE" -eq 0 ]; then
  echo "file=$FILE sparse=false logical_size=0 allocated=0 holes=0 sparseness=0.000%"
  exit 0
fi

HOLE_BYTES=$((LOGICAL_SIZE - ALLOCATED_BYTES))
SPARSENESS=$(awk "BEGIN { printf \"%.3f\", ($HOLE_BYTES / $LOGICAL_SIZE) * 100 }")

if [ "$ALLOCATED_BYTES" -lt "$LOGICAL_SIZE" ] && [ "$LOGICAL_SIZE" -gt "$MIN_SPARSE_SIZE" ]; then
  SPARSE=true
else
  SPARSE=false
  HOLE_BYTES=0
  SPARSENESS=0.000
fi

echo "file=$FILE sparse=$SPARSE logical_size=$LOGICAL_SIZE allocated=$ALLOCATED_BYTES holes=$HOLE_BYTES sparseness=$SPARSENESS%"

exit 0
