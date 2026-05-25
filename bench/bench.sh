#!/bin/bash
# bench/bench.sh, Benchmark spare against tar.
#
# Two test suites:
#   1. Linux kernel 7.0 source tree (many small files, ~1.5 GB)
#   2. Sparse QEMU VM image (4 GB logical, ~20% real data)
#
# Usage:
#   bash bench/bench.sh                 # run both suites
#   bash bench/bench.sh --kernel-only
#   bash bench/bench.sh --sparse-only

KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz"
TARBALL="linux-7.0.tar.xz"
KERNEL_SRC="linux-7.0"
SPARE="$(cd "$(dirname "$0")/.." && pwd)/spare"
J=4
BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$BENCH_DIR/tmp"

die() {
  echo "ERROR: $1" >&2
  exit 1
}

[ -x "$SPARE" ] || die "spare binary not found at $SPARE, run 'make' first"
command -v sudo >/dev/null || die "sudo is required for cache dropping"

mkdir -p "$TMP"

TIMEFORMAT="%R"

# ---------------------------------------------------------------------------
# bench DESC CMD...
# Runs CMD 3 times, drops caches before each, picks the median wall-clock.
# Result stored in LAST_REAL.
# ---------------------------------------------------------------------------
LAST_REAL=0

bench() {
  local desc="$1"
  shift
  local runs=() tf r

  printf "  %-34s" "$desc"
  for _ in 1 2 3; do
    sync && echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1
    tf=$(mktemp)
    { time "$@" >/dev/null 2>&1; } 2>"$tf"
    read -r r <"$tf"
    rm -f "$tf"
    runs+=("$r")
    printf "."
  done

  LAST_REAL=$(printf '%s\n' "${runs[@]}" | sort -n | sed -n '2p')
  printf "  %7.3fs\n" "$LAST_REAL"
}

# ---------------------------------------------------------------------------
# Suite 1: Linux kernel source tree
# ---------------------------------------------------------------------------
run_kernel_bench() {
  cd "$BENCH_DIR" || die "cannot cd to $BENCH_DIR"

  if [ ! -d "$KERNEL_SRC" ]; then
    if [ ! -f "$TARBALL" ]; then
      echo "Downloading $TARBALL ..."
      wget -q --show-progress "$KERNEL_URL" || die "wget failed"
    fi
    echo "Extracting $TARBALL (this may take a minute) ..."
    tar xf "$TARBALL" || die "extraction failed"
  fi

  local SRC_SIZE
  SRC_SIZE=$(du -sb "$KERNEL_SRC" | cut -f1)

  echo ""
  echo "=== Suite 1: Linux kernel 7.0 source tree ==="
  printf "    Source: %s bytes  |  Threads: %d  |  Median of 3 runs\n" "$SRC_SIZE" "$J"

  # -- Pack ----------------------------------------------------------------
  echo ""
  echo "[ Pack ]"
  bench "tar cf" tar cf "$TMP/k.tar" "$KERNEL_SRC"
  TAR_P=$LAST_REAL
  bench "spare p" "$SPARE" p "$TMP/k.spa" "$KERNEL_SRC"
  SPA_P=$LAST_REAL
  bench "spare -j$J p" "$SPARE" -j$J p "$TMP/k_mt.spa" "$KERNEL_SRC"
  SPJ_P=$LAST_REAL
  rm -f "$TMP/k.tar" "$TMP/k.spa" "$TMP/k_mt.spa"

  # -- Pack + compress ------------------------------------------------------
  echo ""
  echo "[ Pack and compress ]"
  bench "tar czf" tar czf "$TMP/k.tar.gz" "$KERNEL_SRC"
  TAR_Z=$LAST_REAL
  bench "spare pz" "$SPARE" pz "$TMP/k.szt" "$KERNEL_SRC"
  SPA_Z=$LAST_REAL
  bench "spare -j$J pz" "$SPARE" -j$J pz "$TMP/k_mt.szt" "$KERNEL_SRC"
  SPJ_Z=$LAST_REAL

  local TAR_Z_SIZE SPA_Z_SIZE SPJ_Z_SIZE TAR_Z_RATIO SPA_Z_RATIO SPJ_Z_RATIO
  TAR_Z_SIZE=$(stat -c%s "$TMP/k.tar.gz")
  SPA_Z_SIZE=$(stat -c%s "$TMP/k.szt")
  SPJ_Z_SIZE=$(stat -c%s "$TMP/k_mt.szt")
  TAR_Z_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $TAR_Z_SIZE/$SRC_SIZE*100}")
  SPA_Z_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $SPA_Z_SIZE/$SRC_SIZE*100}")
  SPJ_Z_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $SPJ_Z_SIZE/$SRC_SIZE*100}")
  rm -f "$TMP/k.tar.gz" "$TMP/k_mt.szt"

  # -- Unpack ---------------------------------------------------------------
  echo ""
  echo "[ Unpack ]"
  tar cf "$TMP/k_u.tar" "$KERNEL_SRC" 2>/dev/null
  "$SPARE" p "$TMP/k_u.spa" "$KERNEL_SRC" 2>/dev/null
  local UDIR
  UDIR=$(mktemp -d)
  local SETUP="rm -rf '${UDIR:?}/'* && mkdir -p '$UDIR'"
  bench "tar xf" bash -c "$SETUP && tar xf '$TMP/k_u.tar' -C '$UDIR'"
  TAR_U=$LAST_REAL
  bench "spare u" bash -c "$SETUP && '$SPARE' u -C '$UDIR' '$TMP/k_u.spa'"
  SPA_U=$LAST_REAL
  rm -rf "$UDIR" "$TMP/k_u.tar" "$TMP/k_u.spa"

  # -- Unpack compressed ----------------------------------------------------
  echo ""
  echo "[ Unpack compressed ]"
  tar czf "$TMP/k_u.tar.gz" "$KERNEL_SRC" 2>/dev/null
  # k.szt is still present from the pack+compress section above
  UDIR=$(mktemp -d)
  SETUP="rm -rf '${UDIR:?}/'* && mkdir -p '$UDIR'"
  bench "tar xzf" bash -c "$SETUP && tar xzf '$TMP/k_u.tar.gz' -C '$UDIR'"
  TAR_UZ=$LAST_REAL
  bench "spare u" bash -c "$SETUP && '$SPARE' u -C '$UDIR' '$TMP/k.szt'"
  SPA_UZ=$LAST_REAL
  rm -rf "$UDIR" "$TMP/k_u.tar.gz" "$TMP/k.szt"

  # -- Markdown output ------------------------------------------------------
  echo ""
  echo "--- Markdown output ---"
  echo ""
  echo "**Wall-clock time**, Linux kernel 7.0 source tree"
  echo ""
  printf "| Operation         | tar     | spare   | spare -j%d |\n" "$J"
  echo "|---|---|---|---|"
  printf "| Pack              | %.3fs | %.3fs | %.3fs |\n" "$TAR_P" "$SPA_P" "$SPJ_P"
  printf "| Pack and compress | %.3fs | %.3fs | %.3fs |\n" "$TAR_Z" "$SPA_Z" "$SPJ_Z"
  printf "| Unpack            | %.3fs | %.3fs | -       |\n" "$TAR_U" "$SPA_U"
  printf "| Unpack compressed | %.3fs | %.3fs | -       |\n" "$TAR_UZ" "$SPA_UZ"
  echo ""
  echo "**Compressed archive size**, Linux kernel 7.0 source tree"
  echo ""
  printf "| | tar czf | spare pz | spare -j%d pz |\n" "$J"
  echo "|---|---|---|---|"
  printf "| Size  | %d B | %d B | %d B |\n" "$TAR_Z_SIZE" "$SPA_Z_SIZE" "$SPJ_Z_SIZE"
  printf "| Ratio | %s | %s | %s |\n" "$TAR_Z_RATIO" "$SPA_Z_RATIO" "$SPJ_Z_RATIO"
}

# ---------------------------------------------------------------------------
# Suite 2: Sparse QEMU VM image
# ---------------------------------------------------------------------------
run_sparse_bench() {
  local VM_IMG="$TMP/vm.img"
  local LOGICAL_SIZE=4294967296 # 4 GB

  if [ ! -f "$VM_IMG" ]; then
    echo ""
    echo "Creating 4 GB sparse VM image (~20% real data) ..."
    qemu-img create -f raw "$VM_IMG" 4G >/dev/null
    # Write 820 MB of random data at the start; rest stays as holes (~20% fill)
    dd if=/dev/urandom of="$VM_IMG" bs=1M count=820 conv=notrunc 2>/dev/null
    printf "    Logical: 4 GB  |  On-disk: %s\n" "$(du -sh "$VM_IMG" | cut -f1)"
  fi

  echo ""
  echo "=== Suite 2: Sparse QEMU VM image (4 GB, ~20% real data) ==="
  printf "    Logical: %d bytes  |  Threads: %d  |  Median of 3 runs\n" "$LOGICAL_SIZE" "$J"

  # -- Pack -----------------------------------------------------------------
  echo ""
  echo "[ Pack ]"
  bench "tar cf  (no sparse)" tar cf "$TMP/vm.tar" "$VM_IMG"
  TAR_P=$LAST_REAL
  TAR_P_SZ=$(stat -c%s "$TMP/vm.tar")
  bench "tar --sparse -cf (sparse)" tar --sparse -cf "$TMP/vm_s.tar" "$VM_IMG"
  TARS_P=$LAST_REAL
  TARS_P_SZ=$(stat -c%s "$TMP/vm_s.tar" 2>/dev/null || echo 0)
  bench "spare p  (no -S)" "$SPARE" p "$TMP/vm.spa" "$VM_IMG"
  SPA_P=$LAST_REAL
  SPA_P_SZ=$(stat -c%s "$TMP/vm.spa")
  bench "spare -S p" "$SPARE" -S p "$TMP/vm_s.spa" "$VM_IMG"
  SPAS_P=$LAST_REAL
  SPAS_P_SZ=$(stat -c%s "$TMP/vm_s.spa")
  bench "spare -S -j$J p" "$SPARE" -S -j$J p "$TMP/vm_sm.spa" "$VM_IMG"
  SPASM_P=$LAST_REAL
  SPASM_P_SZ=$(stat -c%s "$TMP/vm_sm.spa")
  rm -f "$TMP/vm.tar" "$TMP/vm.spa"

  # -- Unpack (sparse-aware) ------------------------------------------------
  echo ""
  echo "[ Unpack, restoring holes ]"
  local UDIR
  UDIR=$(mktemp -d)
  local SETUP="rm -rf '${UDIR:?}/'* && mkdir -p '$UDIR'"
  bench "tar --sparse -xf" bash -c "$SETUP && tar --sparse -xf '$TMP/vm_s.tar' -C '$UDIR'"
  TARS_U=$LAST_REAL
  bench "spare -S u" bash -c "$SETUP && '$SPARE' u -C '$UDIR' '$TMP/vm_s.spa'"
  SPAS_U=$LAST_REAL
  rm -rf "$UDIR" "$TMP/vm_s.tar" "$TMP/vm_s.spa" "$TMP/vm_sm.spa"

  # -- Markdown output ------------------------------------------------------
  local pct_tar pct_tars pct_spa pct_spas pct_spasm
  pct_tar=$(awk "BEGIN{printf \"%.1f%%\", $TAR_P_SZ  /$LOGICAL_SIZE*100}")
  pct_tars=$(awk "BEGIN{printf \"%.1f%%\", $TARS_P_SZ /$LOGICAL_SIZE*100}")
  pct_spa=$(awk "BEGIN{printf \"%.1f%%\", $SPA_P_SZ  /$LOGICAL_SIZE*100}")
  pct_spas=$(awk "BEGIN{printf \"%.1f%%\", $SPAS_P_SZ /$LOGICAL_SIZE*100}")
  pct_spasm=$(awk "BEGIN{printf \"%.1f%%\", $SPASM_P_SZ/$LOGICAL_SIZE*100}")

  echo ""
  echo "--- Markdown output ---"
  echo ""
  echo "**Pack: archive size and wall-clock time**, 4 GB sparse VM image"
  echo ""
  printf "| | tar cf | tar --sparse cf | spare p | spare -S p | spare -S -j%d p |\n" "$J"
  echo "|---|---|---|---|---|---|"
  printf "| Archive size | %d B (%s) | %d B (%s) | %d B (%s) | %d B (%s) | %d B (%s) |\n" \
    "$TAR_P_SZ" "$pct_tar" \
    "$TARS_P_SZ" "$pct_tars" \
    "$SPA_P_SZ" "$pct_spa" \
    "$SPAS_P_SZ" "$pct_spas" \
    "$SPASM_P_SZ" "$pct_spasm"
  printf "| Pack time    | %.3fs | %.3fs | %.3fs | %.3fs | %.3fs |\n" \
    "$TAR_P" "$TARS_P" "$SPA_P" "$SPAS_P" "$SPASM_P"
  echo ""
  echo "**Unpack: restoring holes**, 4 GB sparse VM image"
  echo ""
  echo "| | tar --sparse xf | spare -S u |"
  echo "|---|---|---|"
  printf "| Unpack time | %.3fs | %.3fs |\n" "$TARS_U" "$SPAS_U"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
case "${1:-all}" in
--kernel-only) run_kernel_bench ;;
--sparse-only) run_sparse_bench ;;
*)
  run_kernel_bench
  run_sparse_bench
  ;;
esac
