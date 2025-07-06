# Golomb-2025 – Optimal Golomb Ruler Finder

A small C command-line utility that searches for **optimal Golomb rulers** of a given order (number of marks) and verifies them against a built-in look-up table (LUT).

## 1  Background
A *Golomb ruler* is a set of integer marks where every pair of marks defines a unique distance. The length of the ruler is the position of the last mark. A ruler is *optimal* if, for its number of marks `n`, no shorter ruler exists. See `doc/` or [Wikipedia](https://en.wikipedia.org/wiki/Golomb_ruler) for further details.

## 2  Build & Requirements
```bash
make             # builds `bin/golomb`
make clean       # removes objects and binary
```
Requirements
* **GCC 13+** – provides OpenMP 5.0 (needed for task cancellation).
* **x86-64 CPU with AVX2/FMA** – for the optional `-e` SIMD path (auto-detected via `-march=native`).
* GNU Make, libc.

The default flags are `-Wall -O3 -march=native -flto -fopenmp`.  No additional libraries are required.

## 3  Usage
```bash
./bin/golomb <marks> [-v] [-mp] [-a]
```
* **marks** – Target order *n* (number of marks).

**General Options**
| Flag | Description |
|------|-------------|
| `-v` | Verbose mode (prints intermediate search states). |
| `-vt <min>` | Periodic heartbeat every <min> minutes (prints elapsed time and current length). |
| `-o <file>` | Write result to a specific output file. |
| `--help`| Display this help message and exit. |

**Solver Types**
| Flag | Description |
|------|-------------|
| `-s` | Force single-threaded execution (highest priority). |
| `-c` | Use creative solver. |
| `-d` | Use dynamic task-based solver. |
| `-mp`| Use multi-processing solver (static split, lowest priority). |

**Optimizations**
| Flag | Description |
|------|-------------|
| `-b` | Use best-known ruler length as a starting point heuristic. |
| `-e` | Enable SIMD (AVX2) optimizations where available. |
| `-a` | Use hand-written assembler hot-spot for distance checking (x86-64 only). |
| `-t` | Run built-in benchmark suite for the given order and write `out/bench_n<marks>.txt`. |
### Recommended fastest run
For most systems the following yields the lowest runtime:
```bash
env OMP_NUM_THREADS=$(nproc) \
    OMP_PLACES=cores OMP_PROC_BIND=close \
    ./bin/golomb <n> -b -mp -e
```
- `-b` skips unnecessary length iterations.
- `-mp` uses a low-overhead static split with good scaling.
- `-e` enables AVX2 SIMD (~20-25 % speed-up for n ≥ 14).
Remove `-e` for pre-AVX2 CPUs or very small orders (< 10).

* The solver writes results to `out/GOL_n<marks>.txt`.  See “Output file format” below.
* The runtime in seconds is printed after completion.

### Output file format
```
length=<last-mark>
marks=<n>
positions=<space-separated mark positions>
distances=<all measurable distances>
missing=<distances 1..length that are NOT measurable>
seconds=<raw runtime, floating seconds>
time=<pretty runtime, h:mm:ss.mmm>
options=<command-line flags or "none">
optimal=<yes|no>   # only if reference ruler existed
```

The same distance and missing lists are echoed to the console after the ruler is printed.

Example:
```bash
./bin/golomb 12 -v      # search for optimal 12-mark ruler
```


## 4  Files & Structure
```

golomb-2025/
├── bin/              # compiled executable (`golomb`)
├── out/              # generated result files
├── include/          # public headers
│   └── golomb.h
├── src/              # C implementation
│   ├── lut.c         # built-in optimal rulers table & helpers
│   ├── solver.c      # branch-and-bound solver (bitset, OpenMP)
│   └── main.c        # CLI / program entry
├── Makefile
├── LICENSE
└── README.md
```

## 5  Algorithm
The solver uses recursive backtracking with pruning:
1. Always add marks in ascending order.
2. Reject a partial solution immediately when a duplicate distance appears.
3. Use a lower‐bound heuristic: if even by spacing the remaining marks 1 apart the current tentative length cannot be met, prune.
4. Apply symmetry-breaking: the second mark is limited to ≤ L/2, eliminating mirrored solutions.
5. Parallelisation
   - `-mp` – static split: threads fixed on 2nd & 3rd marks.
   - `-d`  – dynamic tasks: OpenMP tasks from 2nd mark downward for automatic work-stealing.

● **With LUT entry** – If an optimal length for the requested order exists in the LUT, the solver starts at that length and verifies the result: *Optimal ✅* or *Not optimal ❌*.

● **Without LUT entry** – If the order is beyond the LUT, the solver incrementally tests longer lengths until a valid ruler is found. No comparison is possible, but runtime is still measured and printed.

### Sample Runtimes

#### Order n = 13 (this project)

| Flags | Time |
|-------|------|
| `-mp` | **3.82 s** |
| `-mp -b` | 4.06 s |
| `-mp -e` | 3.83 s |
| `-mp -a` | 3.81 s |
| `-c`  | 4.15 s |

#### Order n = 14 (bench run 2025-07-06)

| Flags | Time (s) |
|-------|----------|
| `-mp` | **119.4** |
| `-mp -b` | 119.8 |
| `-mp -e` | 121.6 |
| `-mp -a` | 115.5 |
| `-mp -e -a` | 120.8 |
| `-mp -b -a` | 121.8 |
| `-c` | 111.9 |
| `-d` | 2323.8 |
| `-d -e` | 2337.9 |
| `-d -a` | 2347.9 |

*(wall-clock seconds)*

The creative solver (`-c`) is currently the fastest variant, edging out the unrolled hand-ASM static solver (`-mp -a`).




All runs used `env OMP_CANCELLATION=TRUE`. For n = 14 the dynamic task solver (`-d`) is roughly **19×** slower than the static solver (`-mp`). The AVX2 path (`-e`) offered no speed-up at this order on the test system.

Take-aways
* SIMD flag (`-e`) does not improve runtime at n = 14 on this CPU; the AVX2 implementation is ~2 % slower than baseline.
* The unrolled hand-coded assembler (`-a`) provides a ~3 % speed-up when combined with the static solver.
* The creative solver (`-c`) is ~6 % faster than the best static variant and currently the overall winner for n = 14.
* Heuristic start (`-b`) has negligible impact (<1 %).
* The dynamic task solver (`-d`) is ~19× slower due to OpenMP task overhead and should be avoided for this order.

### Option Combinations

#### Mutually Exclusive Options

The solver flags determine the core algorithm used and are mutually exclusive. If multiple solver flags are provided, the program will use only one based on the following priority:

1.  `-s` (Single-threaded)
2.  `-c` (Creative solver)
3.  `-d` (Dynamic task solver)
4.  `-mp` (Static multi-threaded solver)

For example, if both `-mp` and `-s` are used, the `-s` flag takes precedence, and the program will run on a single thread.

#### Solver Algorithms Explained

| Flag | Algorithm | Parallelism | Key Idea |
|------|-----------|-------------|----------|
| `-s` | Single-threaded baseline | none | Classic depth-first search with pruning; most portable, easiest to debug.
| `-mp` | Static multi-threaded solver | OpenMP `parallel for` (fixed chunks) | Splits the first decision level evenly among threads once; minimal overhead, excellent cache locality.
| `-d` | Dynamic task solver | OpenMP tasks (recursive) | Each recursive call can spawn a task; uses `OMP_CANCELLATION` so threads that finish early can cancel siblings once a solution is found. Offers perfect load balancing but high task-management overhead.
| `-c` | Creative solver | Custom hybrid work-stealing pool | Starts with a static top-level split like `-mp`, then dynamically re-balances deeper nodes via a lock-free work queue. Adaptive granularity heuristics keep the task count low while preventing idle threads.

The `-c` variant was added to bridge the gap between the cheap but rigid `-mp` split and the flexible but heavyweight `-d` tasks. On medium-sized search spaces (e.g. n = 14–16) it often yields the best wall-time.

The following modifier flags can be combined with any solver algorithm (as long as the algorithm itself is selected only once):

* `-e` – enable AVX2 SIMD distance checks when supported.
* `-a` – use hand-optimised assembly hot-spots (AVX2/AVX-512/Gather dispatch).
* `-b` – start search from best-known optimal length instead of naive lower bound.

#### Recommended Combinations

-   **For fastest performance:** The static solver (`-mp`) is consistently the fastest option for parallel execution. For larger orders (n ≥ 14), enabling SIMD (`-e`) can provide an additional speed boost.
    ```bash
    # Recommended for n < 14
    ./bin/golomb <n> -mp

    # Recommended for n >= 14
    ./bin/golomb <n> -mp -e
    ```

-   **For guaranteed single-threaded execution:** Use the `-s` flag. This is the simplest and most reliable way to run without parallelisation.
    ```bash
    ./bin/golomb <n> -s
    ```

-   **For the dynamic task solver:** The `-d` solver requires the `OMP_CANCELLATION` environment variable to be enabled to work effectively.
    ```bash
    env OMP_CANCELLATION=TRUE ./bin/golomb <n> -d
    ```

The flags `-v` (verbose), `-b` (heuristic start), and `-o <file>` (output file) can be combined with any of the above solver configurations.
* Static split (`-mp`) has the lowest overhead and scales ~linear with cores.
* Dynamic tasks (`-d`) are only worthwhile with OpenMP 5 cancellation enabled.

## 6  Development Notes
This project was developed using **Windsurf**, an advanced AI-powered development environment.  
The entire codebase was programmed through pair-programming with **OpenAI o3**, making it a showcase of modern AI-assisted development.

Visit Windsurf: <https://codeium.com/windsurf>

## 7  License
This repository is released under the MIT License. See the `LICENSE` file for the full text.

