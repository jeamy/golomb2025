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
| `-a` | Use hand-written assembler hot-spot for distance checking (x86-64 only). |
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
   - `-d`  – dynamic tasks: OpenMP tasks from 2nd mark downward for automatic work-stealing (erfordert `OMP_CANCELLATION=TRUE`).

● **With LUT entry** – If an optimal length for the requested order exists in the LUT, the solver starts at that length and verifies the result: *Optimal ✅* or *Not optimal ❌*.

● **Without LUT entry** – If the order is beyond the LUT, the solver incrementally tests longer lengths until a valid ruler is found. No comparison is possible, but runtime is still measured and printed.

### Sample Runtimes

Update (2025-08-07)
- `n = 12`, Flags: `-mp` on AVX2 system: **~0.18–0.21 s**
  ```bash
  ./bin/golomb 12 -mp
  # example: 0.186 s, Optimal ✅
  ```
Die folgenden Tabellen sind historisch und teilweise vor der -mp-Neuimplementierung entstanden:

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

Einordnung: Für kleine Ordnungen (z. B. n = 12) ist `-mp` nach dem Kandidaten-Ordering Update am schnellsten. Für n = 14 war im historischen Lauf der kreative Solver (`-c`) im Vorteil; je nach Hardware kann sich das verschieben.




All runs used `env OMP_CANCELLATION=TRUE`. For n = 14 the dynamic task solver (`-d`) is roughly **19×** slower than the static solver (`-mp`). The AVX2 path (`-e`) offered no speed-up at this order on the test system.

Take-aways (aktualisiert 2025-08-07)
* `-mp` ist nach der Kandidaten-Ordering-Änderung für kleine Ordnungen (z. B. n = 12) sehr schnell (≤ 0.25 s auf AVX2).
* `-b` beeinflusst ausschließlich die Startlänge. Positionen werden immer vom Solver berechnet.
* SIMD ist standardmäßig aktiv; AVX-512 wird nur via `GOLOMB_USE_AVX512=1` gewählt, sonst AVX2.
* `-d` bleibt für diese Größenordnung deutlich langsamer (Task-Overhead).

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

## 10  License
This repository is released under the MIT License. See the `LICENSE` file for the full text.

