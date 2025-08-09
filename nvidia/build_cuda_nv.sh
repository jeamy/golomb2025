#!/usr/bin/env bash
set -euo pipefail

# Build NVIDIA CUDA variant with a compiler toolkit and a (possibly different)
# runtime that matches the installed NVIDIA driver. Defaults:
#  - CUDA_TOOLKIT: /usr/local/cuda        (CUDA 13.0 on this system)
#  - CUDA_RUNTIME_HOME: /usr/local/cuda-12.9 (matches driver CUDA 12.9)
#  - CC: gcc-14
#  - HOSTCXX: g++-14 (nvcc host C++ compiler via -ccbin)
# You can override any via environment variables before calling this script.

CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_RUNTIME_HOME=${CUDA_RUNTIME_HOME:-/usr/local/cuda-12.9}
CC_BIN=${CC:-gcc-14}
HOSTCXX_BIN=${HOSTCXX:-g++-14}

echo "[build_cuda_nv] Using CUDA_TOOLKIT=$CUDA_TOOLKIT"
echo "[build_cuda_nv] Using CUDA_RUNTIME_HOME=$CUDA_RUNTIME_HOME"
echo "[build_cuda_nv] Using CC=$CC_BIN HOSTCXX=$HOSTCXX_BIN"

# Sanity checks
if ! command -v "$CC_BIN" >/dev/null 2>&1; then
  echo "[build_cuda_nv] Error: compiler $CC_BIN not found in PATH" >&2
  exit 1
fi
if ! command -v "$HOSTCXX_BIN" >/dev/null 2>&1; then
  echo "[build_cuda_nv] Error: host C++ compiler $HOSTCXX_BIN not found in PATH" >&2
  exit 1
fi
if [ ! -x "$CUDA_TOOLKIT/bin/nvcc" ]; then
  echo "[build_cuda_nv] Error: nvcc not found/executable at $CUDA_TOOLKIT/bin/nvcc" >&2
  exit 1
fi
if [ ! -d "$CUDA_RUNTIME_HOME/lib64" ]; then
  echo "[build_cuda_nv] Error: CUDA runtime lib64 directory missing: $CUDA_RUNTIME_HOME/lib64" >&2
  exit 1
fi

# Display versions
"$CUDA_TOOLKIT/bin/nvcc" --version || true
$CC_BIN --version | head -n1 || true
$HOSTCXX_BIN --version | head -n1 || true

# Build
echo "[build_cuda_nv] Cleaning and building CUDA target..."
CUDA_TOOLKIT="$CUDA_TOOLKIT" CUDA_RUNTIME_HOME="$CUDA_RUNTIME_HOME" CC="$CC_BIN" HOSTCXX="$HOSTCXX_BIN" \
  make -C "$(dirname "$0")/../nvidia" clean
CUDA_TOOLKIT="$CUDA_TOOLKIT" CUDA_RUNTIME_HOME="$CUDA_RUNTIME_HOME" CC="$CC_BIN" HOSTCXX="$HOSTCXX_BIN" \
  make -C "$(dirname "$0")/../nvidia" -j

echo "[build_cuda_nv] Done. Binary at nvidia/golomb_nv"

# If arguments are provided, run the CUDA binary and capture output to a file in the nvidia directory.
# Default run: 14 -b -vt 0.2
if [ "$#" -gt 0 ]; then
  RUN_ARGS=("$@")
else
  RUN_ARGS=(14 -b -vt 0.2)
fi

# Determine n (marks) as first positional argument
N_MARKS="${RUN_ARGS[0]}"
if ! [[ "$N_MARKS" =~ ^[0-9]+$ ]]; then
  echo "[build_cuda_nv] Warning: first arg is not a number. Defaulting n=14 for output filename."
  N_MARKS=14
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/golomb_nv"
OUT_FILE="$SCRIPT_DIR/GOL_n${N_MARKS}_cuda.txt"

echo "[build_cuda_nv] Running: $BIN ${RUN_ARGS[*]}"
SECONDS=0
set +e
"$BIN" "${RUN_ARGS[@]}" >"$OUT_FILE"
RC=$?
set -e
ELAPSED=$SECONDS

# Append simple metadata similar to C variant
pretty_time() {
  local s=$1
  local h=$((s/3600))
  local m=$(((s%3600)/60))
  local sec=$((s%60))
  printf "%d:%02d:%02d" "$h" "$m" "$sec"
}

{
  echo "seconds=$ELAPSED"
  echo "time=$(pretty_time "$ELAPSED")"
  echo -n "options="
  if [ "$#" -gt 0 ]; then echo "${RUN_ARGS[*]}"; else echo "14 -b -vt 0.2"; fi
} >>"$OUT_FILE"

if [ $RC -ne 0 ]; then
  echo "[build_cuda_nv] Run exited with code $RC. See $OUT_FILE for details." >&2
  exit $RC
fi

# Append optimal status at the end (CUDA variant searches for LUT length with -b)
echo "optimal=yes" >>"$OUT_FILE"

echo "[build_cuda_nv] Output written to $OUT_FILE"
