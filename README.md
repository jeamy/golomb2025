### Benchmarks (2025-08-10, GTX 1660 Ti)

- CUDA, hints fast-lane: `./nvidia/golomb_nv 14 -b -H` → WALL ≈ 26.86 s
- CUDA, no hints (tuned): `./nvidia/golomb_nv 14 -b -wu 16384 -dh -dw 24 -ap` → WALL ≈ 193 s
- CUDA, no hints (tuned): `./nvidia/golomb_nv 14 -b -wu 32768 -dh -dw 32 -ap` → WALL ≈ 191 s
- CPU baseline, `-mp`: `./bin/golomb 14 -mp -b` → WALL ≈ 119.435 s

Notes
- The `-H` path skips GPU prefilter entirely (fast-lane first), improving end-to-end time on this machine.
- After warmup expansion, in-DFS hinting, and async prefilter, the no-hints path is ≈3:11–3:13 and still slower than CPU `-mp` (≈1:59). Further tuning planned.
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

Optional (only needed for the ASM paths)
* **FASM** – builds the `-af` assembler implementation.
* **NASM** – builds the `-an` assembler implementation.

The default flags are `-Wall -O3 -march=native -flto -fopenmp`.  No additional libraries are required.

## 3  Usage
```bash
./bin/golomb <marks> [options]
```
* **marks** – Target order *n* (number of marks).

**General Options**
| Flag | Description |
|------|-------------|
| `-v` | Verbose mode (prints intermediate search states). |
| `-vt <min>` | Periodic heartbeat every <min> minutes (prints elapsed time and current length). |
| `-o <file>` | Write result to a specific output file. |
| `-f <file>` | Enable checkpointing for `-mp` and save/resume progress to/from <file>. |
| `-fi <sec>` | Checkpoint flush interval in seconds (default 60). |
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
| `-e` | Enable SIMD (default if available). |
| `-af` | Use hand-written assembler hot-spot for distance checking (FASM build; x86-64 only). |
| `-an` | Use hand-written assembler hot-spot for distance checking (NASM build; x86-64 only). |
| `-t` | Run built-in benchmark suite for the given order and write `out/bench_n<marks>.txt`. |
Note on SIMD
- If compiled with AVX2/AVX-512, SIMD is enabled by default. At runtime the program prefers the AVX2 path; AVX-512 is used only when `GOLOMB_USE_AVX512=1` is set.
- The `-e` flag remains for compatibility and to make the intent explicit; it is not required on AVX2-capable builds.

### Recommended fastest run
For most systems the following yields the lowest runtime:
```bash
env OMP_NUM_THREADS=$(nproc) \
    OMP_PLACES=cores OMP_PROC_BIND=close \
    ./bin/golomb <n> -mp -b
```
- `-mp` provides the best scaling with low overhead.
- `-b` skips unnötige Längen-Iterationen, indem nur die Startlänge aus der LUT genutzt wird. Es werden niemals Positionslisten aus der LUT kopiert.
- SIMD ist standardmäßig aktiv (falls verfügbar); `-e` kann weggelassen werden.

* The solver writes results to `out/GOL_n<marks>.txt`.  See “Output file format” below.
* The runtime in seconds is printed after completion.

### Environment variables

- `GOLOMB_USE_AVX512=1` – erzwingt den AVX-512 Gather-Pfad (sonst wird AVX2 bevorzugt, falls verfügbar).
- `GOLOMB_NO_HINTS` – deaktiviert LUT-basierte Heuristiken, sobald die Variable GESETZT ist (unabhängig vom Wert). Das heißt:
  - Nicht gesetzt: Hints AN (falls eine LUT für `n` existiert).
  - `GOLOMB_NO_HINTS=1`: Hints AUS.
  - `GOLOMB_NO_HINTS=0`: Hints ebenfalls AUS (reine Präsenz genügt).
  Wichtig für Checkpoints: Beim Resume muss die Kandidatenordnung identisch sein – also entweder Hints an beiden Läufen an oder an beiden aus.
- OpenMP: Für reproduzierbares Scheduling ggf. `OMP_NUM_THREADS`, `OMP_PLACES=cores`, `OMP_PROC_BIND=close` setzen.

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

### Checkpointing (-f)

The static multi-threaded solver (`-mp`) supports minimal checkpointing to survive long runs or interruptions.

- Enable with `-f <file>`: the solver will persist a bitset of processed top-level candidates (pairs `(second, third)`) to `<file>` periodically and am Ende eines kompletten Kandidaten-Passes für das aktuelle L.
- Resuming: rerun the exact same command (same `n`, same target length `L` implied by the loop, same solver `-mp`, and same hint ordering setting). The solver will skip already processed candidates and continue.
- Deterministic ordering: the checkpoint is only valid if the candidate ordering is identical. Therefore, resuming requires that either LUT-based ordering is enabled on both runs, or disabled on both runs. You can force disable hints via `GOLOMB_NO_HINTS=1`.
- File format: binary header (`"GRCP"`, version, `n`, `L`, total-candidate count, LUT-ref pair and a flag indicating whether hint ordering was used) followed by the bitset payload. The solver validates the header before resuming; mismatches are ignored and a fresh checkpoint is started.

  Header-Felder (Little-Endian)

  - __`GRCP`__ (4 Bytes, ASCII): Magic zur Identifikation des Formats.
  - __`version`__ (`uint32`): Formatversion, aktuell `1`. Andere Versionen werden abgewiesen (neuer Checkpoint wird begonnen).
  - __`n`__ (`uint32`): Ordnung (Anzahl der Marken).
  - __`L`__ (`uint32`): Ziel-Länge der aktuellen Runde.
  - __`total`__ (`uint64`): Anzahl der Top-Level-Kandidatenpaare `(second, third)` für dieses `n`/`L`. Bestimmt die Bitset-Breite. Anzahl Payload-Wörter: `words = ceil(total / 32)`; Payload-Größe in Bytes: `4 * words`.
  - __`hint_s`__, __`hint_t`__ (je `uint32`): Referenzpaar aus der LUT (`ref->pos[1]`, `ref->pos[2]`) zur Kandidaten-Priorisierung. `0` falls Hints deaktiviert oder keine LUT.
  - __`hint_used`__ (`uint32`): `0` = Hints AUS, `1` = Hints AN (inkl. Fast-Lane-Versuch). Muss zwischen Lauf und Resume identisch sein.

  Payload (Bitset)

  - Folge von `uint32`-Wörtern (Little-Endian). Bit `i` gesetzt ⇒ Kandidat `i` vollständig abgearbeitet. Nicht gesetzte Bits ⇒ noch offen.
- Interval: default 60s. Override at runtime with `-fi <sec>`.
- File lifetime: Die Datei wird NICHT automatisch gelöscht. Sie bleibt erhalten (auch bei erfolgreichem Abschluss). Ein erneuter Lauf mit demselben Pfad überschreibt sie.
- Signals/Abbruch: Es gibt keinen Signal-Handler. Wenn du den Prozess vor einem periodischen Flush beendest (z. B. Ctrl+C bevor `-fi` Sekunden verstrichen sind), wird evtl. KEIN Checkpoint geschrieben. Für schnelle erste Sicherungen `-fi` verkleinern (z. B. `-fi 10`).

Beispiele

```bash
# Langer Lauf mit Checkpoint (mit Hints = Standard)
./bin/golomb 14 -mp -f out/cp_n14.bin -fi 30

# Resume (gleiche Flags und identische Umgebung für die Kandidatenordnung)
./bin/golomb 14 -mp -f out/cp_n14.bin -fi 30

# Hints explizit abschalten (ordnet Kandidaten rein lexikographisch)
env GOLOMB_NO_HINTS=1 ./bin/golomb 14 -mp -f out/cp_n14_nohints.bin -fi 30
env GOLOMB_NO_HINTS=1 ./bin/golomb 14 -mp -f out/cp_n14_nohints.bin -fi 30

### Checkpoint-Analyse-Skripte (`script/`)

* __`script/cpod`__
  - Zeigt die Header-Bytes (erste 64 Bytes) des Checkpoints in Hex via `od` und die Dateigröße.
  - Nutzung:
    ```bash
    script/cpod out/cp15_resume.bin
    ```

* __`script/cppy`__
  - Python-Parser für den Checkpoint: liest den Header (`GRCP`, version, `n`, `L`, `total`, `hint_s`, `hint_t`, `hint_used`), zählt gesetzte Bits in der Bitset-Payload und gibt den Fortschritt in Prozent aus.
  - Annahmen: Little-Endian, 40-Byte-Header. Erfordert Python ≥ 3.8.
  - Nutzung:
    ```bash
    script/cppy out/cp15_resume.bin
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
   - `-mp` – Parallelisierung über eine geordnete Kandidatenliste der Paare (second, third) mit OpenMP `parallel for` und `schedule(dynamic, 16)`. Falls eine LUT für `n` existiert, werden die Paare nach Nähe zum LUT-Paar `(ref->pos[1], ref->pos[2])` sortiert, sodass vielversprechende Kandidaten zuerst geprüft werden. Frühabbruch über gemeinsames Flag.
   - `-mpa` – Option A: OpenMP-Harness in C (Kandidatenliste + LUT-Ordering + Taskloop), aber die eigentliche DFS/Backtracking-Logik läuft in NASM (`dfs_asm`).
   - `-d`  – dynamic tasks: OpenMP tasks from 2nd mark downward for automatic work-stealing (erfordert `OMP_CANCELLATION=TRUE`).

● **With LUT entry** – If an optimal length for the requested order exists in the LUT, the solver starts at that length and verifies the result: *Optimal ✅* or *Not optimal ❌*.

● **Without LUT entry** – If the order is beyond the LUT, the solver incrementally tests longer lengths until a valid ruler is found. No comparison is possible, but runtime is still measured and printed.

### Sample Runtimes

CPU Bench (2025-12-30)

Sources
- Summary TSV: `out/bench_n13.txt`
- Full per-run outputs: `out/GOL_n13_*.txt`

| Flags | `seconds` | Output file |
|-------|-----------|-------------|
| `-mp` | 2.325 | `out/GOL_n13_mp.txt` |
| `-mpa` | 3.517 | `out/GOL_n13_mpa.txt` |
| `-mp -b` | 2.459 | `out/GOL_n13_mp_b.txt` |
| `-mp -e` | 2.442 | `out/GOL_n13_mp_e.txt` |
| `-mp -af` | 2.439 | `out/GOL_n13_mp_af.txt` |
| `-mp -an` | 2.465 | `out/GOL_n13_mp_an.txt` |
| `-mp -e -af` | 2.427 | `out/GOL_n13_mp_e_af.txt` |
| `-mp -e -an` | 2.382 | `out/GOL_n13_mp_e_an.txt` |
| `-mp -b -af` | 2.308 | `out/GOL_n13_mp_b_af.txt` |
| `-mp -b -an` | 2.365 | `out/GOL_n13_mp_b_an.txt` |

Quick observations
- `-b` and `-e` have only a small impact for `n=13`.
- `-af/-an` are correct (same `positions=` / `distances=` as `-mp`), but they are not faster here.

#### Order n = 14 (bench run 2025-07-06)

CPU Bench (2025-12-30)

Sources
- Summary TSV: `out/bench_n14.txt`
- Full per-run outputs: `out/GOL_n14_*.txt`

| Flags | `seconds` | Output file |
|-------|-----------|-------------|
| `-mp` | 21.342 | `out/GOL_n14_mp.txt` |
| `-mp -b` | 21.664 | `out/GOL_n14_mp_b.txt` |
| `-mp -e` | 22.027 | `out/GOL_n14_mp_e.txt` |
| `-mp -af` | 21.061 | `out/GOL_n14_mp_af.txt` |
| `-mp -an` | 20.645 | `out/GOL_n14_mp_an.txt` |
| `-mp -e -af` | 21.017 | `out/GOL_n14_mp_e_af.txt` |
| `-mp -e -an` | 21.133 | `out/GOL_n14_mp_e_an.txt` |
| `-mp -b -af` | 20.950 | `out/GOL_n14_mp_b_af.txt` |
| `-mp -b -an` | 21.386 | `out/GOL_n14_mp_b_an.txt` |

Quick observations
- For `n=14` the baseline `-mp` runs are ~21s, much slower than `n=13` (expected: search space explosion).
- `-b` and `-e` are close to noise level here.
- `-af/-an` are correct (same `positions=` / `distances=` as `-mp`), but they are not faster here.

Difference n=13 vs n=14
- The baseline runtime increases by ~9× (≈2.3s → ≈21.3s) which is consistent with the rapid growth of the search space.
- The ASM variants track the baseline closely (same order of magnitude) which indicates there is no useful speedup from `-af/-an` for these runs.

*(wall-clock seconds)*

### Benchmark suite variants

The built-in benchmark suite (`-t`) runs a fixed set of flag variants (see `src/bench.c`) and writes a TSV file to `out/bench_n<marks>.txt`.

The benchmark output files are written to the `out/` directory, e.g.
- `out/bench_n13.txt`
- `out/bench_n14.txt`

Additionally, each solver run writes a full result file `out/GOL_n<marks>_<suffix>.txt` containing `positions=`, `distances=`, `missing=`, `seconds=`, `options=`, and (if a LUT reference exists) `optimal=`.

Current variants list

```c
const char *variants[] = {
    "-mp",
    "-mp -b",
    "-mp -e",
    "-mp -af",
    "-mp -an",
    "-mp -e -af",
    "-mp -e -an",
    "-mp -b -af",
    "-mp -b -an",
    "-c",
    "-c -e",
    "-c -af",
    "-c -an",
    NULL
};
```

### ASM note (`-af/-an`)

The `-af/-an` assembler paths are now consistent with the LUT-verified `-mp` outputs for `n=13` and `n=14` (identical `positions=` / `distances=` in the `out/GOL_*.txt` files).

In these benchmark runs, the ASM paths do not provide a meaningful speedup; in some cases they are slightly slower than the plain `-mp` path.

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
* `-af` / `-an` – use hand-optimised assembly hot-spots for distance checking.
* `-b` – start search from best-known optimal length instead of naive lower bound.

#### Recommended Combinations

-   **For fastest performance (based on the 2025-12-30 runs above):** The static solver (`-mp`) is the default recommendation. In these runs, neither `-e` nor `-af/-an` provided a consistent speedup; they were mostly within noise and sometimes slower.
    ```bash
    # Recommended default
    ./bin/golomb <n> -mp

    # Optional: experiment if you want to compare on your machine
    ./bin/golomb <n> -mp -an
    ./bin/golomb <n> -mp -af
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

### Environment variables

- `GOLOMB_USE_AVX512=1` – Erzwingt die AVX-512-Variante für den Distanz-Duplikat-Test (standardmäßig wird AVX2 bevorzugt).
- `GOLOMB_NO_HINTS=1` – Deaktiviert die LUT-gestützte Priorisierung der Kandidatenpaare `(second, third)` und den einmaligen Fast-Lane-Versuch mit dem LUT-Paar. Korrektheit bleibt unverändert.
- `OMP_NUM_THREADS`, `OMP_PLACES`, `OMP_PROC_BIND` – Kontrolle der Thread-Anzahl und Bindung.
- `OMP_CANCELLATION=TRUE` – empfohlen für den `-d` Solver (nicht erforderlich für `-mp`).

### Semantik von `-b`
- `-b` nutzt nur die bekannte optimale Länge aus der LUT als Startlänge (Upper Bound). Es findet keinerlei Kopieren von LUT-Positionen statt. Die vollständige Lineal-Lösung wird stets durch die Suche konstruiert und validiert.

## 6  Development Notes
This project was developed using **Windsurf**, an advanced AI-powered development environment.  
The entire codebase was programmed through pair-programming with **OpenAI o3**, making it a showcase of modern AI-assisted development.

Visit Windsurf: <https://codeium.com/windsurf>

## 7  Java Implementation

A modern Java implementation of the Golomb ruler search algorithm is available in the `java/` directory. This implementation is a port of the original C version and includes the following features:

- **Java 24 Support**: Uses modern Java features including records, pattern matching, and enhanced APIs
- **Multi-threaded Search**: Parallel processing with Fork-Join framework (`-mp` flag)
- **Built-in LUT**: Look-up table with known optimal rulers for verification
- **Comprehensive Testing**: Full test suite with JUnit 5

To run the Java implementation:

```bash
cd java
mvn clean compile
mvn exec:java -Dexec.args="5 -v -mp"
```

For more details, see the Java implementation's README in the `java/` directory.

## 8  Go Implementation

A Go implementation of the Golomb ruler search algorithm is available in the `go/` directory. This implementation is a port of the original C version with idiomatic Go features:

- **Go 1.23 Support**: Uses modern Go features and idioms
- **Goroutine-based Parallelism**: Multi-processing search using goroutines and contexts (`-mp` flag)
- **Built-in LUT**: Look-up table with known optimal rulers up to 28 marks
- **Compatible Output Format**: Produces output files compatible with the C and Java versions

To build and run the Go implementation:

```bash
cd go
./build.sh      # Builds the binary and creates a symlink in ../bin/
./golomb 5 -v -b -mp
```

The Go implementation supports the same core command-line options as the C and Java versions:

- `-v`: Verbose output
- `-mp`: Use multi-processing solver with goroutines
- `-b`: Use best-known ruler length as starting point
- `-o <file>`: Write result to specific output file

For more details, see the Go implementation's README in the `go/` directory.

## 9  Rust Implementation

A Rust implementation of the Golomb ruler search algorithm is available in the `rust/` directory. This implementation is a port of the previous versions with idiomatic Rust features:

- **Rust 1.70+ Support**: Uses modern Rust features and zero-cost abstractions
- **Rayon-based Parallelism**: Multi-processing search using Rayon's work-stealing thread pool (`--mp` flag)
- **Built-in LUT**: Look-up table with known optimal ruler lengths for all marks 1-28
- **Compatible Output Format**: Produces output files compatible with the C, Java, Go, and Ruby versions

To build and run the Rust implementation:

```bash
cd rust
./build.sh      # Builds the binary with cargo and creates a symlink in ../bin/
./target/release/golomb 5 -v -b --mp
```

The Rust implementation supports the same core command-line options as the other versions:

- `-v, --verbose`: Verbose output
- `--mp`: Use multi-processing solver with Rayon
- `-b, --best`: Use best-known ruler length as search upper bound
- `-o, --output <file>`: Write result to specific output file

For more details, see the Rust implementation's README in the `rust/` directory.

## NVIDIA CUDA Variant (experimental)

This repository contains an optional NVIDIA CUDA implementation in `nvidia/` to accelerate early candidate filtering and guide the CPU search.

### Files
- `nvidia/golomb_nv.cu` – CUDA/host code.
- `nvidia/Makefile` – nvcc build rules (targets sm_75 with PTX fallback).
- `nvidia/build_cuda_nv.sh` – convenience build/run script that also appends metadata to the output file.
- `nvidia/build_cuda_12.9nv.sh` – fixed-env wrapper for CUDA 12.9 + GCC/G++ 13.4 (preferred/tested).

### Requirements
- NVIDIA GPU with Compute Capability ≥ 7.5 (e.g. GTX 1660 Ti).
- CUDA Toolkit 12.9 (tested) or 13.0 (fixes C23 math prototype noexcept mismatch).
- Compilers: GCC/G++ 13.4 with Toolkit 12.9; GCC/G++ 14 are OK with Toolkit 13.0.

Environment variables used by the scripts:
- `CUDA_TOOLKIT` – nvcc toolkit root (`/usr/local/cuda-12.9` in the 12.9 wrapper).
- `CUDA_RUNTIME_HOME` – runtime used for loading (`/usr/local/cuda-12.9`).
- `CC=gcc-13.4`, `HOSTCXX=g++-13.4` – host compilers (12.9 wrapper).

The CUDA `Makefile` embeds SASS and PTX to improve forward compatibility:
```
-gencode arch=compute_75,code=[sm_75,compute_75]
```

Toolchain/Runtime split (important)
- The helper script intentionally compiles with Toolkit 13.0 but links/loads with Runtime 12.9 to match the installed driver.
- It exports these variables and adjusts search paths so they take effect:
  - `CUDA_TOOLKIT` → prepended to `PATH` for `nvcc`
  - `CUDA_RUNTIME_HOME` → prepended to `LD_LIBRARY_PATH` for `libcudart.so`
  - `CC`, `HOSTCXX` → used by `make`/`nvcc -Xcompiler ... -ccbin ...`
  - The `nvidia/Makefile` also sets an rpath to `$(CUDA_RUNTIME_HOME)/lib64`, so the chosen runtime is used at run time without additional `LD_LIBRARY_PATH`.

  Further details, troubleshooting, and the CUDA 12.9 header patch instructions are documented in `nvidia/README.md`.

### Recent CUDA changes (2025-08-10)

- Warmup DFS(3) window expanded to a tunable default of 8192 (`-wu`/`GOLOMB_WARMUP`).
- Main CPU search switched to pure DFS(3) over `(s,t)` (aligned with C `-mp`).
- OpenMP scheduling set to `schedule(dynamic, 16)` for warmup and main loops.
- Optional in-DFS hinting at depth 3 (`-dh`) with a tunable window around `u_hint` (`-dw`, default 16).
- Optional asynchronous GPU prefilter (`-ap`) overlaps GPU scoring with CPU warmup.
- Fast-lane hints (`-H`) now skip GPU prefilter entirely to preserve fast start.

### Build & Run
Recommended: use the helper script (writes output to `nvidia/GOL_n<n>_cuda.txt`).
```bash
./nvidia/build_cuda_nv.sh 15 -b          # build and run n=15 from LUT length
./nvidia/build_cuda_nv.sh 16 -b -H       # enable LUT hints/fast-lane
```

Override examples
```bash
# Use a different Toolkit (e.g. compile with 12.9 instead of 13.0)
CUDA_TOOLKIT=/usr/local/cuda-12.9 ./nvidia/build_cuda_nv.sh 15 -b

# Use a different Runtime (must match your driver)
CUDA_RUNTIME_HOME=/usr/local/cuda-12.3 ./nvidia/build_cuda_nv.sh 15 -b

# Use different host compilers
CC=gcc-13 HOSTCXX=g++-13 ./nvidia/build_cuda_nv.sh 15 -b
```

GCC compatibility for CUDA 12.9
- With CUDA 12.9 on recent distros, GCC 14 can conflict with glibc C23 math prototypes. Use GCC 13 for a stable build.
- Install compatibility packages:
  - Fedora/RHEL/Rocky/OL: `sudo dnf install gcc13 gcc13-c++`
  - Ubuntu/Debian: `sudo apt update && sudo apt install gcc-13 g++-13`
  - SUSE/SLES: `sudo zypper install gcc13 gcc13-c++`
- Build with CUDA 12.9 Toolkit and GCC 13:
```bash
CUDA_TOOLKIT=/usr/local/cuda-12.9 CC=gcc-13 HOSTCXX=g++-13 \
  ./nvidia/build_cuda_nv.sh 16 -b
```
Host compiler policy: https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#host-compiler-support-policy

 Header patch (Option A)
 - On systems with GCC 13 + glibc 2.41, CUDA 12.9 may fail to compile due to differing exception specifications on `sinpi/cospi/sinpif/cospif`.
 - The minimal, safe fix is to add `noexcept(true)` to those four declarations in `crt/math_functions.h`. See `nvidia/README.md` for the one-liner `sed` patch and revert instructions.

You can also run the binary directly:
```bash
nvidia/golomb_nv <n> [-b] [-v] [-H] [-f <cp.bin>] [-fi <sec>] [-vt <min>]
```

Key options
- `-b` – use best-known optimal length from LUT as starting length (never copies positions).
- `-H` – enable LUT-based hinting (candidate ordering) and a one-shot fast-lane attempt with the LUT pair. Hints are DISABLED by default.
- `-v` – verbose.
- `-f <file>` / `-fi <sec>` – checkpoint path and flush interval.
- `-vt <min>` – heartbeat interval (prints to stderr).

Advanced CUDA CLI flags and tuning (2025-08-10)

- `-wu <N>` or `GOLOMB_WARMUP=<N>`
  - Warmup window for DFS(3) over `(s,t)`. Default: 8192. Typical: 16384 for n=14.
- `-dh` or `GOLOMB_DFS3_HINT=1`
  - Enable in-DFS hinting at depth==3: try `u_hint` first and then a small neighborhood before falling back to plain `dfs(3, ...)`.
- `-dw <W>` or `GOLOMB_UWIN=<W>`
  - Half-width of the `u_hint` neighborhood. Default: 16. Try 8–32.
- `-ap` or `GOLOMB_ASYNC_PREF=1`
  - Asynchronous GPU prefilter in a background thread, overlapped with CPU warmup. Skipped automatically when `-H` is set.

Examples (CUDA 12.9 wrapper):
```bash
# Hints fast-lane (fast start; skips GPU prefilter):
./nvidia/build_cuda_12.9nv.sh 14 -b -H

# No hints, tuned for earlier hit probability:
./nvidia/build_cuda_12.9nv.sh 14 -b -wu 16384 -dh -dw 24 -ap

# Same via environment variables:
GOLOMB_WARMUP=16384 GOLOMB_DFS3_HINT=1 GOLOMB_UWIN=24 GOLOMB_ASYNC_PREF=1 \
  ./nvidia/build_cuda_12.9nv.sh 14 -b
```

Environment variables
- `GOLOMB_NO_HINTS` – when set, disables LUT hinting even if `-H` is passed. Useful for deterministic resume of checkpoints.
- `GOLOMB_WARMUP` – warmup window size for DFS(3) over `(s,t)`.
- `GOLOMB_DFS3_HINT` – enable depth-3 in-DFS hinting using `u_hint`.
- `GOLOMB_UWIN` – half-width of the `u_hint` neighborhood to try first.
- `GOLOMB_ASYNC_PREF` – run GPU prefilter asynchronously (overlap with warmup).

Output & logging
- Output format in `GOL_n<n>_cuda.txt` matches the C variant: `length`, `marks`, `positions`, `distances`, `missing`, `seconds`, `time`, `options`, and `optimal=yes` when applicable.
- CUDA diagnostics and heartbeats are printed to stderr with `[CUDA]`/`[VT]` prefixes to keep the output file clean.

Timing note (`-vt`)
- The heartbeat thread sleeps for `-vt` minutes; on program exit the main thread joins it. This can add up to the sleep duration to the measured `seconds` that the script appends. For realistic timing during benchmarks, omit `-vt`.

GPU prefilter (current status)
- The CUDA kernel performs a lightweight feasibility check over candidate pairs and prioritises those that pass deeper checks.
- On some driver/runtime setups (e.g. compiling with Toolkit 13.0 and loading with Runtime 12.9 on Turing, sm_75), a launch error can occur: `device kernel image is invalid (200)`. In this case the program falls back to CPU-only search; correctness is unaffected.
- The `nvidia/Makefile` now includes PTX fallback (`code=[sm_75,compute_75]`) to mitigate compatibility issues. If you still see the error, ensure the runtime matches the installed driver and that your GPU supports sm_75.
  - Troubleshooting: You may also compile with the same-major Toolkit as your Runtime/driver, e.g.
    ```bash
    CUDA_TOOLKIT=/usr/local/cuda-12.9 ./nvidia/build_cuda_nv.sh 16 -b
    ```

Where results are written
- Script: `nvidia/GOL_n<n>_cuda.txt` (appends `seconds`, `time`, `options`, `optimal`).
- Binary (stdout): same core fields as the C variant; diagnostics to stderr.

## 10  License
This repository is released under the MIT License. See the `LICENSE` file for the full text.

