#!/bin/bash
# bench/bench.sh, Benchmark SAR against tar using the Linux kernel source.
# Downloads and extracts the kernel automatically if not already present.
# Usage: bash bench/bench.sh

SAR_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz"
TARBALL="linux-7.0.tar.xz"
KERNEL_SRC="linux-7.0"
SAR="$(cd "$(dirname "$0")/.." && pwd)/sar"
NTHREADS=$(nproc)
BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
TMPDIR_BENCH="$BENCH_DIR/tmp"
TMP="$TMPDIR_BENCH/sar_bench"
BENCH_SETUP=""

die() {
  echo "ERROR: $1" >&2
  exit 1
}

[ -x "$SAR" ] || die "sar binary not found at $SAR, run 'make' first"
command -v sudo >/dev/null || die "sudo is required for cache dropping"

TIMEFORMAT="%R %U %S"

cd "$BENCH_DIR" || die "cannot cd to $BENCH_DIR"
mkdir -p "$TMPDIR_BENCH"

# Download and extract kernel if needed
if [ ! -d "$KERNEL_SRC" ]; then
  if [ ! -f "$TARBALL" ]; then
    echo "Downloading $TARBALL ..."
    wget -q --show-progress "$SAR_URL" || die "wget failed"
  fi
  echo "Extracting $TARBALL (this may take a minute) ..."
  tar xf "$TARBALL" || die "extraction failed"
fi

echo "Source : $BENCH_DIR/$KERNEL_SRC"
echo "Threads: $NTHREADS"
echo "(median run of 3; all metrics from the same execution; caches dropped before each run)"

# ---------------------------------------------------------------------------
# bench DESC CMD [ARGS...]
# Runs CMD 3 times, dropping caches before each.  Picks the run whose real
# time is the median (middle when sorted) and stores real/user/sys from that
# single execution in LAST_REAL / LAST_USER / LAST_SYS.
# ---------------------------------------------------------------------------
LAST_REAL=0 LAST_USER=0 LAST_SYS=0

bench() {
  local desc="$1"
  shift
  local runs=() tf r u s

  printf "  %-28s" "$desc"
  for _run in 1 2 3; do
    [ -n "$BENCH_SETUP" ] && eval "$BENCH_SETUP"
    sync && echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1
    tf=$(mktemp)
    { time "$@" >/dev/null 2>&1; } 2>"$tf"
    read -r r u s <"$tf"
    rm -f "$tf"
    runs+=("$r $u $s")
    printf "."
  done

  # Sort all three runs by real time, pick the middle one (median run)
  local median_run
  median_run=$(printf '%s\n' "${runs[@]}" | sort -n | sed -n '2p')

  LAST_REAL=$(echo "$median_run" | awk '{print $1}')
  LAST_USER=$(echo "$median_run" | awk '{print $2}')
  LAST_SYS=$(echo "$median_run" | awk '{print $3}')
  printf "  real=%7.3fs  user=%7.3fs  sys=%7.3fs\n" \
    "$LAST_REAL" "$LAST_USER" "$LAST_SYS"
}

# ---------------------------------------------------------------------------
# Pack
# ---------------------------------------------------------------------------
echo ""
echo "[ Pack ]"
bench "tar cf" tar cf "${TMP}.tar" "$KERNEL_SRC"
TAR_P_R=$LAST_REAL
TAR_P_U=$LAST_USER
TAR_P_S=$LAST_SYS

bench "sar p" "$SAR" p "${TMP}.sar" "$KERNEL_SRC"
SAR_P_R=$LAST_REAL
SAR_P_U=$LAST_USER
SAR_P_S=$LAST_SYS

bench "sar -j$NTHREADS p" "$SAR" -j"$NTHREADS" p "${TMP}_mt.sar" "$KERNEL_SRC"
SRJ_P_R=$LAST_REAL
SRJ_P_U=$LAST_USER
SRJ_P_S=$LAST_SYS

# Pack archives only needed for timing, free space before next section
rm -f "${TMP}".tar "${TMP}".sar "${TMP}_mt".sar

# ---------------------------------------------------------------------------
# Pack + compress
# ---------------------------------------------------------------------------
echo ""
echo "[ Pack and compress ]"
bench "tar czf" tar czf "${TMP}.tar.gz" "$KERNEL_SRC"
TAR_Z_R=$LAST_REAL
TAR_Z_U=$LAST_USER
TAR_Z_S=$LAST_SYS

bench "sar pz" "$SAR" pz "${TMP}.szt" "$KERNEL_SRC"
SAR_Z_R=$LAST_REAL
SAR_Z_U=$LAST_USER
SAR_Z_S=$LAST_SYS

bench "sar -j$NTHREADS pz" "$SAR" -j"$NTHREADS" pz "${TMP}_mt.szt" "$KERNEL_SRC"
SRJ_Z_R=$LAST_REAL
SRJ_Z_U=$LAST_USER
SRJ_Z_S=$LAST_SYS

# ---------------------------------------------------------------------------
# Unpack, build reference archives once, outside of timing
# ---------------------------------------------------------------------------
echo ""
echo "[ Preparing archives for unpack benchmarks ... ]"
tar cf "${TMP}_u.tar" "$KERNEL_SRC" 2>/dev/null
"$SAR" p "${TMP}_u.sar" "$KERNEL_SRC" 2>/dev/null

UDIR=$(mktemp -d)
BENCH_SETUP="rm -rf '${UDIR:?}/'* && mkdir -p '$UDIR'"

echo ""
echo "[ Unpack ]"
bench "tar xf" bash -c "tar xf '${TMP}_u.tar' -C '$UDIR'"
TAR_U_R=$LAST_REAL
TAR_U_U=$LAST_USER
TAR_U_S=$LAST_SYS

bench "sar u" bash -c "cd '$UDIR' && '$SAR' u '${TMP}_u.sar'"
SAR_U_R=$LAST_REAL
SAR_U_U=$LAST_USER
SAR_U_S=$LAST_SYS

BENCH_SETUP=""
rm -rf "$UDIR"

# ---------------------------------------------------------------------------
# Unpack compressed, build reference archives once, outside of timing
# ---------------------------------------------------------------------------
echo ""
echo "[ Preparing compressed archives for unpack benchmarks ... ]"
tar czf "${TMP}_u.tar.gz" "$KERNEL_SRC" 2>/dev/null
"$SAR" pz "${TMP}_u.szt" "$KERNEL_SRC" 2>/dev/null

UDIR=$(mktemp -d)
BENCH_SETUP="rm -rf '${UDIR:?}/'* && mkdir -p '$UDIR'"

echo ""
echo "[ Unpack compressed ]"
bench "tar xzf" bash -c "tar xzf '${TMP}_u.tar.gz' -C '$UDIR'"
TAR_UZ_R=$LAST_REAL
TAR_UZ_U=$LAST_USER
TAR_UZ_S=$LAST_SYS

bench "sar u szt" bash -c "cd '$UDIR' && '$SAR' u '${TMP}_u.szt'"
SAR_UZ_R=$LAST_REAL
SAR_UZ_U=$LAST_USER
SAR_UZ_S=$LAST_SYS

BENCH_SETUP=""
rm -rf "$UDIR"

# ---------------------------------------------------------------------------
# Compression ratios
# ---------------------------------------------------------------------------
echo ""
echo "[ Compression ratios ]"
SRC_SIZE=$(du -sb "$KERNEL_SRC" | cut -f1)
TGZ_SIZE=$(stat -c %s "${TMP}.tar.gz")
SGZ_SIZE=$(stat -c %s "${TMP}.szt")
SGJZ_SIZE=$(stat -c %s "${TMP}_mt.szt")
TAR_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $TGZ_SIZE/$SRC_SIZE*100}")
SGZ_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $SGZ_SIZE/$SRC_SIZE*100}")
SGJZ_RATIO=$(awk "BEGIN{printf \"%.2f%%\", $SGJZ_SIZE/$SRC_SIZE*100}")
echo "  Source:            $SRC_SIZE bytes"
echo "  tar czf:           $TGZ_SIZE bytes  ($TAR_RATIO)"
echo "  sar pz:            $SGZ_SIZE bytes  ($SGZ_RATIO)"
echo "  sar -j$NTHREADS pz: $SGJZ_SIZE bytes  ($SGJZ_RATIO)"

# Cleanup temp archives
rm -f "${TMP}".tar "${TMP}".sar "${TMP}_mt".sar \
  "${TMP}".tar.gz "${TMP}".szt "${TMP}_mt".szt \
  "${TMP}_u".tar "${TMP}_u".sar \
  "${TMP}_u".tar.gz "${TMP}_u".szt

# ---------------------------------------------------------------------------
# Markdown tables
# ---------------------------------------------------------------------------
J="sar -j$NTHREADS"

echo ""
echo "**Real (wall-clock) time** for [Linux kernel 7.0]($SAR_URL)"
printf "| Operation         | tar      | sar      | %s |\n" "$J"
echo "|-------------------|----------|----------|-----|"
printf "| Pack              | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_P_R" "$SAR_P_R" "$SRJ_P_R"
printf "| Pack and compress | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_Z_R" "$SAR_Z_R" "$SRJ_Z_R"
printf "| Unpack            | %6.3fs  | %6.3fs  | -        |\n" "$TAR_U_R" "$SAR_U_R"
printf "| Unpack compressed | %6.3fs  | %6.3fs  | -        |\n" "$TAR_UZ_R" "$SAR_UZ_R"

echo ""
echo "**User time** for [Linux kernel 7.0]($SAR_URL)"
printf "| Operation         | tar      | sar      | %s |\n" "$J"
echo "|-------------------|----------|----------|-----|"
printf "| Pack              | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_P_U" "$SAR_P_U" "$SRJ_P_U"
printf "| Pack and compress | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_Z_U" "$SAR_Z_U" "$SRJ_Z_U"
printf "| Unpack            | %6.3fs  | %6.3fs  | -        |\n" "$TAR_U_U" "$SAR_U_U"
printf "| Unpack compressed | %6.3fs  | %6.3fs  | -        |\n" "$TAR_UZ_U" "$SAR_UZ_U"

echo ""
echo "**Sys time** for [Linux kernel 7.0]($SAR_URL)"
printf "| Operation         | tar      | sar      | %s |\n" "$J"
echo "|-------------------|----------|----------|-----|"
printf "| Pack              | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_P_S" "$SAR_P_S" "$SRJ_P_S"
printf "| Pack and compress | %6.3fs  | %6.3fs  | %6.3fs  |\n" "$TAR_Z_S" "$SAR_Z_S" "$SRJ_Z_S"
printf "| Unpack            | %6.3fs  | %6.3fs  | -        |\n" "$TAR_U_S" "$SAR_U_S"
printf "| Unpack compressed | %6.3fs  | %6.3fs  | -        |\n" "$TAR_UZ_S" "$SAR_UZ_S"

echo ""
echo "**Compression ratios** for [Linux kernel 7.0]($SAR_URL)"
printf "| %s | tar czf | sar pz | %s pz |\n" "" "$J"
echo "|---|---|---|---|"
printf "| Absolute values | %d/%d | %d/%d | %d/%d |\n" \
  "$TGZ_SIZE" "$SRC_SIZE" "$SGZ_SIZE" "$SRC_SIZE" "$SGJZ_SIZE" "$SRC_SIZE"
printf "| Ratio           | %s | %s | %s |\n" \
  "$TAR_RATIO" "$SGZ_RATIO" "$SGJZ_RATIO"
