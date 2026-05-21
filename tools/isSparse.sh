#!/bin/sh
###############################################################################
# isSparse.sh
#
# Checks how sparse a file is. Output is pipe-friendly (key value per line).
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

LOGICAL_SIZE=$(stat -c '%s' "$FILE")
BLOCKS=$(stat -c '%b' "$FILE")
BLOCK_SIZE=$(stat -c '%B' "$FILE")
ALLOCATED_BYTES=$(( BLOCKS * BLOCK_SIZE ))

if [ "$LOGICAL_SIZE" -eq 0 ]; then
  echo "file $FILE"
  echo "sparse false"
  echo "logical_size 0"
  echo "allocated 0"
  echo "holes 0"
  echo "sparseness 0.000%"
  exit 0
fi

HOLE_BYTES=$(( LOGICAL_SIZE - ALLOCATED_BYTES ))
SPARSENESS=$(awk "BEGIN { printf \"%.3f\", ($HOLE_BYTES / $LOGICAL_SIZE) * 100 }")

if [ "$ALLOCATED_BYTES" -lt "$LOGICAL_SIZE" ]; then
  SPARSE=true
else
  SPARSE=false
  HOLE_BYTES=0
  SPARSENESS=0.000
fi

echo "file $FILE"
echo "sparse $SPARSE"
echo "logical_size $LOGICAL_SIZE"
echo "allocated $ALLOCATED_BYTES"
echo "holes $HOLE_BYTES"
echo "sparseness $SPARSENESS%"

exit 0
