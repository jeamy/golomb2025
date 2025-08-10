#!/usr/bin/env bash
# Wrapper to build & run the CUDA variant using CUDA Toolkit 12.9 and Runtime 12.9
# with GCC/G++ 13.4, matching the configuration validated on this system.
#
# Usage:
#   ./nvidia/build_cuda_12.9nv.sh <n> [args...]
# Examples:
#   ./nvidia/build_cuda_12.9nv.sh 14 -b -H
#   ./nvidia/build_cuda_12.9nv.sh 15 -b

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Exact environment used in successful runs
export CUDA_TOOLKIT="${CUDA_TOOLKIT:-/usr/local/cuda-12.9}"
export CUDA_RUNTIME_HOME="${CUDA_RUNTIME_HOME:-/usr/local/cuda-12.9}"
export CC="${CC:-gcc-13.4}"
export HOSTCXX="${HOSTCXX:-g++-13.4}"

printf '[build_cuda_12.9nv] CUDA_TOOLKIT=%s\n' "$CUDA_TOOLKIT"
printf '[build_cuda_12.9nv] CUDA_RUNTIME_HOME=%s\n' "$CUDA_RUNTIME_HOME"
printf '[build_cuda_12.9nv] CC=%s HOSTCXX=%s\n' "$CC" "$HOSTCXX"

exec "$SCRIPT_DIR/build_cuda_nv.sh" "$@"
