# NVIDIA CUDA Variant

This directory contains an experimental CUDA-assisted variant.  In its default
no-hints mode it generates valid endpoint-aware depth-4 prefixes on the GPU
(depth-5 for n>=14), and the exact remaining bit-parallel DFS runs hybrid:
the GPU drains the prefix list from the front, CPU OpenMP threads from the back.

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

## Algorithm

### Problem and search shape

A Golomb ruler with `n` marks `0 = m_0 < m_1 < ... < m_{n-1} = L` has all
`n(n-1)/2` pairwise distances distinct.  With `-b` the program searches for
a ruler of exactly the LUT length `L`.  Both endpoints are fixed from the
start (**endpoint-aware search**): every inner mark `y` immediately
contributes its distance `L - y` to the right end, so conflicts with the
endpoint are detected at the moment `y` is placed instead of at the very
end of the search.

The search is a depth-first enumeration of the inner marks in ascending
order.  It is split into three stages:

1. **Roots** `(s, t)` = `(m_1, m_2)` are enumerated on the host
   (`s <= L/2`, `t <= L - (n-3)`).
2. **Frontier** (GPU, `frontier_kernel` / `expand_kernel`): every valid
   root is extended to all legal prefixes `(s, t, u)` and, for n >= 14,
   `(s, t, u, v)`.  Each prefix gets a score (its number of legal next
   marks); the list is sorted by score.  The score only changes the order,
   never the set of prefixes.
3. **Completion** (GPU + CPU): every prefix is completed by an exact DFS
   (`gb_dfs` / `gb_dfs_reg` in `golomb_bits.h`).  The union of all prefix
   subtrees is exactly the full search tree, so "no ruler found" means no
   ruler of length `L` exists (under the pruning rules below, which are
   all exact).

### Bit-parallel DFS

State after the marks `m_0 .. m_{d-1}` (last mark `x`) and the endpoint `L`:

| Mask | Meaning (bit set) |
|---|---|
| `D` | distance `k` is already used (all pairs, including each mark to `L`) |
| `LIST` | `x - k` is a placed mark (distances from `x` back to all marks) |
| `MARKS` | `p` is a placed mark (absolute positions, `L` excluded) |
| `F` | the candidate `y = x + g` is illegal |

A candidate `y` is illegal if (i) some `y - m_j` is already in `D`,
(ii) `L - y` is already in `D`, or (iii) some `y - m_j` equals `L - y`
(the two kinds of new distances collide with each other).  All three
conditions are stored in the single mask `F`, relative to `x`.  The next
legal mark is therefore the lowest clear bit of `F` above the current
cursor: one find-first-zero instead of scanning every position with `d`
bit tests.

Placing `y = x + g` (with `e = L - y`) updates the state with a few shifts
and ORs:

```
LIST' = (LIST << g) | bit(g)              new left distances y - m_j
D'    = D | LIST' | bit(e)
E     = MARKS shifted by (L - 2y)         bits m_j + L - 2y
F'    = (F >> g) | D' | E | bit(e/2) if e is even
```

- `F >> g` moves all old constraints to the new reference point `y`.
- `D'` forbids every used distance measured from `y` itself.
- `E` covers the new distance `e` measured from every older mark, and the
  new left distances seen from the endpoint side (both give the same bits).
- `bit(e/2)` forbids the midpoint where a future left distance to `y` would
  equal that future mark's distance to `L` (condition iii).

Every mask update is exactly reversible (`D`, `LIST`, `MARKS` are restored
by clearing the bits that were added; `F` is taken from the per-level
stack), so backtracking needs no recomputation.  Masks have
`K = ceil((L+1)/32)` 32-bit words (K=4 for n=14, K=5 for n=15, ...).

### Pruning (all exact)

When the mark with index `d` is placed at `y`, the segment `[y, L]` still
holds `n - d` marks and `n-1-d` gaps:

- **OGR bound:** that segment is itself a Golomb ruler, so
  `L - y >= OGR(n - d)`, the optimal length of an `(n-d)`-mark ruler
  (LUT values, all proven).
- **Free-distance bound:** the `n-1-d` gaps are distinct distances that are
  not used yet, so `L - y` is at least the sum of the `n-1-d` smallest
  distances missing from `D`.  This is the strongest cut in practice (it
  halves the GPU time for n=15).
- **Mirror symmetry:** a ruler and its mirror image are equivalent; only
  rulers whose first gap is smaller than their last gap are searched
  (`m_{n-2} < L - m_1`, also used as `L - y >= OGR(n-d-1) + m_1 + 1`).
  This cut is disabled in the `-H` lane because that lane only contains the
  subtree of one `(s, t)` pair, not both mirror images.

The upper bound on `y` is the tightest of the three; the search window
for the next mark is `[cursor+1, L - bound - x]`.

### Mapping to the GPU

- One thread completes one prefix.  Threads are **persistent**: each thread
  takes the next prefix from a device-side atomic counter until the chunk
  is empty, so one heavy subtree only occupies its own thread.
- The per-level `F` masks and gaps live in a **register shift-stack**
  (`gb_dfs_reg<K, LV>`, `LV` levels, statically indexed).  A dynamically
  indexed stack in local memory did not fit L1 at full occupancy.
- Single-bit helpers use arithmetic per-word masks, because nvcc turns the
  obvious `if ((b >> 5) == i) a[i] |= m` loop into a dynamic array index,
  which pushes every mask into local memory.
- The kernel stops through a flag in device memory.  A GPU hit also sets
  the host-mapped flag; a CPU hit is copied to the device flag.

### Hybrid scheduling

The sorted prefix list is a queue with two ends.  The GPU worker thread
takes large chunks from the front (the best-scored, largest subtrees);
OpenMP threads take single prefixes from the back and run the same DFS
(`gb_dfs`) on the CPU.  Every prefix is handed out exactly once, and both
sides stop within microseconds of a hit.  Every reported ruler is
re-validated on the host before it is printed.

### Correctness checks

- The host DFS matches a naive brute-force count exactly for n=4..11 at
  `L_opt .. L_opt+4`, both from depth 1 and summed over all `(s, t)`
  prefixes.
- The register-stack variant gives identical results to the reference DFS
  for 57 674 prefixes (n=6..12).
- `GOLOMB_COUNT=1` counts all rulers through the production GPU pipeline;
  the totals equal the CPU-only totals exactly (up to ~2.5e10 rulers,
  across the K=3..6 word boundaries, with and without the mirror cut).

## Design and performance notes (2026-09-27)

- __Device frontier (default without `-H` and without a checkpoint)__: The
  device fixes `L` as a real endpoint, enumerates every valid `(s,t,u)`
  prefix and ranks them by their number of legal continuations (search
  order only).  For n>=14 a second device pass expands each prefix to
  depth 5 `(s,t,u,v)` (`GOLOMB_D5_MIN` overrides the threshold); coarser
  items leave a long tail at the end of the run.
- __Bit-parallel DFS (`golomb_bits.h`)__: one "forbidden next mark" mask per
  level combines the three endpoint-aware conditions (left distance used,
  distance to `L` used, left distance equal to the new distance to `L`).
  Placing a mark updates it with a shift and a few ORs, so the next legal
  candidate is a single find-first-zero instead of a scan with `d` bit
  tests per position.  The same template runs on the GPU and on the CPU.
- __Exact pruning__: the segment `[y, L]` holding `m` marks is at least
  `OGR(m)` long (LUT lengths for `m < n`) and at least the sum of the `m-1`
  smallest distances not used yet; the mirror cut keeps only rulers whose
  first gap is smaller than their last gap (disabled in the `-H` lane,
  which does not contain both mirror images; `GOLOMB_NO_MIRROR=1` disables
  it everywhere).
- __GPU kernel__: persistent threads pull prefixes from a device-side
  counter, so a slow subtree only blocks its own thread.  The per-level
  masks live in a statically indexed register shift-stack; the bit helpers
  are written so nvcc cannot turn them into dynamically indexed arrays
  (that demoted every mask to local memory).  A CPU hit reaches running
  kernels through a device-memory stop flag (polling host-mapped memory
  from ~24k threads would go over PCIe).
- __Hybrid scheduling__: the GPU takes large chunks from the front of the
  score-sorted list, OpenMP threads take single prefixes from the back.
  The slower CPU therefore never holds a heavy subtree the GPU would finish
  sooner, and both sides poll the found flag inside the DFS.  Every
  emitted ruler is re-validated on the host.
- __Hints fast-lane (`-H`)__: The LUT's positions 1 and 2 seed one guided
  subtree, which is expanded into depth-4..6 subprefixes and searched by the
  same hybrid engine.  The LUT ruler is never copied; the result is still
  constructed and validated by exact DFS.
- __Fallbacks__: with a checkpoint (`-f`), without a GPU, or when frontier
  generation fails, the older root-candidate path is used (`-ap`, `-wu`,
  `-dh`, `-dw` affect only that path).  L > 255 uses the older per-thread
  kernels.  Allocation, launch and transfer failures are completed on the CPU.

### Benchmarks (2026-09-27, GTX 1660 Ti + Ryzen 7 3700X, CUDA 13.0 + GCC 15)

Time-to-first at the LUT length (`-b`), wall clock, median of 3 runs:

| Case | Command | before (09-26) | now |
|---|---|---|---|
| n=14 CUDA hybrid | `./nvidia/golomb_nv 14 -b` | 4.4 s | 0.5 s |
| n=15 CUDA hybrid | `./nvidia/golomb_nv 15 -b` | 24.9 s | 5.7 s |
| n=16 CUDA hybrid | `./nvidia/golomb_nv 16 -b` | 235 s | 1.2 s |
| n=14 CUDA guided | `./nvidia/golomb_nv 14 -b -H` | 0.33 s | 0.24 s |
| n=15 CUDA guided | `./nvidia/golomb_nv 15 -b -H` | 0.95 s | 0.35 s |
| n=16 CUDA guided | `./nvidia/golomb_nv 16 -b -H` | 14–21 s | 0.5 s |

CPU reference (unchanged code, `out/` logs): `./bin/golomb 14 -mp -b`
22–34 s, `./bin/golomb 15 -mp -b` 193 s, `./bin/golomb 16 -mp -b` 2396 s.

Time-to-first depends on where the (essentially unique) ruler sits in the
search order.  A luck-free throughput measure is the exhaustive search one
below the optimum (no ruler exists, every subtree is visited):

| Exhaustive | before | now |
|---|---|---|
| n=13, `GOLOMB_TARGET_L=105` | 20.3 s | 0.4 s |
| n=14, `GOLOMB_TARGET_L=126` | not measured | 0.6 s |
| n=15, `GOLOMB_TARGET_L=150` | not measured | 6.3 s |

Notes
- Logs are written to `out/GOL_n<n>_nv.txt` (and `_nv_H` for `-H`).
- `-b` reads the LUT length of n, and the LUT lengths of all smaller rulers
  as `OGR(m)` pruning bounds (proven values).  `-H` additionally reads LUT
  positions 1 and 2; the full LUT ruler is never emitted as a result.
- `optimal=yes` means "L equals the LUT length": like the CPU `-b` mode,
  this program searches only that length and does not itself prove that
  L-1 is infeasible.  `GOLOMB_TARGET_L=<L-1>` runs that proof.

Environment variables
- `GOLOMB_DEBUG=1`: per-chunk launch/done logs.
- `GOLOMB_GPU_CHUNK=<N>` (>=1024): prefixes per GPU chunk (default 4 per
  resident thread).
- `GOLOMB_GRID_PER_SM=<N>`: cap resident blocks per SM (tuning).
- `GOLOMB_D5_MIN=<n>`: smallest n that uses the depth-5 frontier (default 14).
- `GOLOMB_TARGET_L=<L>`: search another length (testing / proofs).
- `GOLOMB_NO_MIRROR=1`: disable the mirror symmetry cut.
- `GOLOMB_NO_GPU_DFS=1`: CPU-only hybrid (GPU still builds the frontier).
- `GOLOMB_COUNT=1`: count every ruler instead of stopping at the first
  (GPU and CPU shares are printed).  Used to validate the device path: GPU
  totals equal the CPU-only totals exactly, e.g. n=9 L=159/160 (~2.5e10
  rulers), n=10 L=127/128, n=11 L=95/96, n=12–14 at L_opt..L_opt+2, with
  and without the mirror cut.  The host DFS itself matches a naive
  brute-force count for n=4..11, L_opt..L_opt+4.

Build: `make CC=gcc-15 HOSTCXX=g++-15` with CUDA 13.0 (nvcc rejects GCC 16).
The SM count is read with `cudaDeviceGetAttribute`, because `cudaDeviceProp`
differs between the 13.0 headers and the 12.9 runtime and reported 1 SM.

## Requirements

- NVIDIA GPU with Compute Capability ≥ 7.5 (e.g., GTX 1660 Ti).
- CUDA Toolkit 12.9 (tested) or 13.0 (headers fix CUDA 12.9 math prototypes).
- Host compilers: GCC/G++ 13.4 for Toolkit 12.9 builds. GCC/G++ 14 are OK with Toolkit 13.0.

Environment variables respected by the script:
- `CUDA_TOOLKIT` – toolkit root for `nvcc` (prepended to `PATH`).
- `CUDA_RUNTIME_HOME` – runtime root providing `libcudart.so` (prepended to `LD_LIBRARY_PATH`).
- `CC`, `HOSTCXX` – host C/C++ compilers; `HOSTCXX` is passed to nvcc via `-ccbin`.

The `nvidia/Makefile` embeds SASS and PTX:
```
-gencode arch=compute_75,code=[sm_75,compute_75]
```
This improves forward compatibility (PTX JIT fallback).

Suppressing NVCC diagnostics (optional):
- You can pass `-diag-suppress=177 -diag-suppress=550` via `NVFLAGS` to hide "declared but never referenced" / "set but never used" warnings while iterating. The codebase prefers fixing underlying causes; these flags are for temporary use.

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

### CLI reference and tuning (2025-08-10)

Advanced flags for A/B tuning (with env var equivalents):

- `-wu <N>` or `GOLOMB_WARMUP=<N>`
  - Warmup window for DFS(3) over `(s,t)`. Default: 8192. Typical: 16384 for n=14.
- `-dh` or `GOLOMB_DFS3_HINT=1`
  - Enable in-DFS hinting at depth==3: try `u_hint` and a small neighborhood before falling back to plain `dfs(3, ...)`.
- `-dw <W>` or `GOLOMB_UWIN=<W>`
  - Half-width of the hint neighborhood around `u_hint`. Default: 16. Try 8–32.
- `-ap` or `GOLOMB_ASYNC_PREF=1`
  - Run GPU prefilter asynchronously in a worker thread and overlap with warmup. Skipped when `-H` is set.

Examples (CUDA 12.9 wrapper recommended):

```bash
# Hints fast-lane (restored behavior):
./nvidia/build_cuda_12.9nv.sh 14 -b -H

# No hints, tuned for earlier hit probability:
./nvidia/build_cuda_12.9nv.sh 14 -b -wu 16384 -dh -dw 24 -ap

# Using environment variables instead of flags:
GOLOMB_WARMUP=16384 GOLOMB_DFS3_HINT=1 GOLOMB_UWIN=24 GOLOMB_ASYNC_PREF=1 \
  ./nvidia/build_cuda_12.9nv.sh 14 -b
```

Note on timing: Avoid `-vt` for benchmark timing; the heartbeat join can skew the appended `seconds`.

## Troubleshooting

- Kernel launch error `device kernel image is invalid (200)`:
  - Ensure your GPU supports `sm_75` and that PTX fallback is present (see Makefile).
  - Ensure `CUDA_RUNTIME_HOME` matches the installed driver version.
  - Try compiling with the same-major Toolkit as your runtime/driver (e.g., Toolkit 12.9).
- GCC/headers conflicts on Toolkit 12.9: apply the header patch above or build with Toolkit 13.0 while keeping Runtime 12.9.

## License
MIT, see top-level `LICENSE`.
