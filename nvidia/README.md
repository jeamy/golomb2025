# NVIDIA CUDA Variant

This directory contains an experimental CUDA-assisted variant that accelerates early candidate filtering and guides the CPU search for Golomb rulers.

- Program: `golomb_nv` (built by this directory's `Makefile`)
- Helper: `build_cuda_nv.sh` (build + run wrapper; writes `GOL_n<n>_cuda.txt`)

## Quick start

```bash
# Build and run n=14 with LUT start length (-b) and hinting (-H)
./nvidia/build_cuda_nv.sh 15 -b -H

# Just run the built binary with a 60s watchdog
/usr/bin/time -f "WALL=%e" timeout 60s ./nvidia/golomb_nv 15 -b -H
```

On a GTX 1660 Ti (sm_75) this completes well under 30s (example: WALL≈0.32s). Your timing will vary by GPU/CPU.

Results are appended to `nvidia/GOL_n<n>_cuda.txt` and match the C variant format: `length`, `marks`, `positions`, `distances`, `missing`, `seconds`, `time`, `options`, and `optimal=yes` when applicable.

## Requirements

- NVIDIA GPU with Compute Capability ≥ 7.5 (e.g., GTX 1660 Ti).
- CUDA Toolkit 12.9 or 13.0.
- Host compilers: GCC/G++ 13 for Toolkit 12.9 builds. GCC/G++ 14 are OK with Toolkit 13.0.

Environment variables respected by the script:
- `CUDA_TOOLKIT` – toolkit root for `nvcc` (prepended to `PATH`).
- `CUDA_RUNTIME_HOME` – runtime root providing `libcudart.so` (prepended to `LD_LIBRARY_PATH`).
- `CC`, `HOSTCXX` – host C/C++ compilers; `HOSTCXX` is passed to nvcc via `-ccbin`.

The `nvidia/Makefile` embeds SASS and PTX:
```
-gencode arch=compute_75,code=[sm_75,compute_75]
```
This improves forward compatibility (PTX JIT fallback).

## Fix: CUDA 12.9 + GCC 13 glibc C23 prototypes

With GCC 13 + glibc 2.41, the C23 math prototypes for `sinpi/cospi/sinpif/cospif` carry `noexcept(true)`. CUDA 12.9's header declares these without `noexcept`, causing:

```
error: exception specification is incompatible with that of previous function
```

### Minimal, safe header patch (Option A)
Add `noexcept(true)` to those four declarations in:
```
/usr/local/cuda-12.9/targets/x86_64-linux/include/crt/math_functions.h
```
The functions are device built-ins and do not throw.

Automated backup + patch + verify (requires sudo):
```bash
H=/usr/local/cuda-12.9/targets/x86_64-linux/include/crt/math_functions.h
sudo cp -a "$H" "$H.bak.$(date +%s)"
# Append noexcept(true) to the four prototypes
sudo sed -i -E "s@(extern __DEVICE_FUNCTIONS_DECL__ __device_builtin__ double[[:space:]]+sinpi\(double x\));@\1 noexcept (true);@" "$H"
sudo sed -i -E "s@(extern __DEVICE_FUNCTIONS_DECL__ __device_builtin__ float[[:space:]]+sinpif\(float x\));@\1 noexcept (true);@" "$H"
sudo sed -i -E "s@(extern __DEVICE_FUNCTIONS_DECL__ __device_builtin__ double[[:space:]]+cospi\(double x\));@\1 noexcept (true);@" "$H"
sudo sed -i -E "s@(extern __DEVICE_FUNCTIONS_DECL__ __device_builtin__ float[[:space:]]+cospif\(float x\));@\1 noexcept (true);@" "$H"
# Verify (you should see noexcept(true) on all four)
grep -nE "sinpi\(double x\)|sinpif\(float x\)|cospi\(double x\)|cospif\(float x\)" "$H" -n
```

Revert:
```bash
# Replace <stamp> with the backup timestamp created above
sudo cp -a "$H.bak.<stamp>" "$H"
```

### Alternative (Option B)
Compile with CUDA Toolkit 13.0 headers (already fixed), but link/load CUDA Runtime 12.9 to match the driver:
```bash
CUDA_TOOLKIT=/usr/local/cuda \
CUDA_RUNTIME_HOME=/usr/local/cuda-12.9 \
CC=gcc-14 HOSTCXX=g++-14 \
./nvidia/build_cuda_nv.sh 15 -b
```
The `nvidia/Makefile` sets rpath to `$(CUDA_RUNTIME_HOME)/lib64`, so the chosen runtime is found at run time.

## Build & Run

Recommended via script (outputs `nvidia/GOL_n<n>_cuda.txt`):
```bash
./nvidia/build_cuda_nv.sh 15 -b          # build and run from LUT start length
./nvidia/build_cuda_nv.sh 16 -b -H       # enable LUT-based candidate ordering
```

 Fixed 12.9 wrapper (exact env)
 ```bash
 # Uses Toolkit 12.9 + Runtime 12.9 and GCC/G++ 13.4 exactly as validated
 ./nvidia/build_cuda_12.9nv.sh 14 -b -H
 ./nvidia/build_cuda_12.9nv.sh 15 -b
 ```
 This wrapper exports the following if not already set in your shell:
 - `CUDA_TOOLKIT=/usr/local/cuda-12.9`
 - `CUDA_RUNTIME_HOME=/usr/local/cuda-12.9`
 - `CC=gcc-13.4`, `HOSTCXX=g++-13.4`
Direct binary usage:
```bash
./nvidia/golomb_nv <n> [-b] [-H] [-v] [-f <cp.bin>] [-fi <sec>] [-vt <min>]
```

Key options
- `-b` – start at best-known optimal length from LUT (never copies positions).
- `-H` – enable LUT hinting (candidate ordering) and a one-shot fast-lane attempt.
- `-v` – verbose; diagnostics and heartbeats go to stderr (`[CUDA]`, `[VT]`).
- `-f` / `-fi` – checkpoint path and flush interval.

Note on timing: Avoid `-vt` for benchmark timing; the heartbeat join can skew the appended `seconds`.

## Troubleshooting

- Kernel launch error `device kernel image is invalid (200)`:
  - Ensure your GPU supports `sm_75` and that PTX fallback is present (see Makefile).
  - Ensure `CUDA_RUNTIME_HOME` matches the installed driver version.
  - Try compiling with the same-major Toolkit as your runtime/driver (e.g., Toolkit 12.9).
- GCC/headers conflicts on Toolkit 12.9: apply the header patch above or build with Toolkit 13.0 while keeping Runtime 12.9.

## License
MIT, see top-level `LICENSE`.
