### Benchmarks (2026-09-27, GTX 1660 Ti + Ryzen 7 3700X)

- CUDA, no hints: `./nvidia/golomb_nv 14 -b` → ≈ 0.5 s, `15 -b` → ≈ 5.7 s, `16 -b` → ≈ 1.2 s
- CUDA, no hints: `./nvidia/golomb_nv 17 -b` → 533 s (8:53, single run)
- CUDA, guided fast-lane: `./nvidia/golomb_nv 14|15|16|17 -b -H` → ≈ 0.24 / 0.25 / 0.5 / 0.6 s
- CPU baseline, `-mp`: `./bin/golomb 14 -mp -b` → ≈ 22–34 s, `15 -mp -b` → ≈ 193 s, `16 -mp -b` → ≈ 2396 s

Details and the algorithm description are in `nvidia/README.md`.

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
| `-T <num>` | Set number of OpenMP threads for parallel solvers (default: all available cores). Affects `-mp`, `-d`, `-c`, `-g`, `-p`. |
| `--help`| Display this help message and exit. |

**Solver Types (DFS-based, exact)**
| Flag | Description |
|------|-------------|
| `-s` | Force single-threaded execution (highest priority). |
| `-c` | Use creative solver. |
| `-d` | Use dynamic task-based solver. |
| `-mp`| Use multi-processing solver (static split, lowest priority). |
| `-to`| Traditional optimized solver (endpoint-aware DFS). Implies `-b`; combine with `-mp` for parallel prefix search. |

**Solver Types (heuristic, non-exact)**
| Flag | Description |
|------|-------------|
| `-g` | Evolutionary solver (iterated min-conflicts local search). Implies `-b`. |
| `-p` | Physics solver (discrete simulated annealing). Implies `-b`. |

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
│   ├── lut.c                  # built-in optimal rulers table & helpers
│   ├── solver.c               # branch-and-bound solver (bitset, OpenMP)
│   ├── solver_traditional_opt.c  # endpoint-aware DFS (-to)
│   ├── solver_evolution.c     # iterated min-conflicts local search (-g)
│   ├── solver_physics.c       # discrete simulated annealing (-p)
│   ├── solver_creative.c      # hybrid work-stealing solver (-c)
│   └── main.c                 # CLI / program entry
├── test/             # benchmark and test programs
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

1.  `-p` (Physics / Simulated Annealing)
2.  `-g` (Evolutionary / Min-Conflicts)
3.  `-to` (Traditional Optimized DFS)
4.  `-c` (Creative solver)
5.  `-d` (Dynamic task solver)
6.  `-mpa` (NASM assembler solver)
7.  `-mp` (Static multi-threaded solver)
8.  `-s` / default (Single-threaded)

For example, if both `-mp` and `-g` are used, the `-g` flag takes precedence.

#### Solver Algorithms Explained

| Flag | Algorithm | Parallelism | Key Idea |
|------|-----------|-------------|----------|
| `-s` | Single-threaded baseline | none | Classic depth-first search with pruning; most portable, easiest to debug.
| `-mp` | Static multi-threaded solver | OpenMP `parallel for` (fixed chunks) | Splits the first decision level evenly among threads once; minimal overhead, excellent cache locality.
| `-d` | Dynamic task solver | OpenMP tasks (recursive) | Each recursive call can spawn a task; uses `OMP_CANCELLATION` so threads that finish early can cancel siblings once a solution is found. Offers perfect load balancing but high task-management overhead.
| `-c` | Creative solver | Custom hybrid work-stealing pool | Starts with a static top-level split like `-mp`, then dynamically re-balances deeper nodes via a lock-free work queue. Adaptive granularity heuristics keep the task count low while preventing idle threads.
| `-to` | Traditional optimized | none | Endpoint-aware DFS that fixes the right endpoint L from the start and prunes distances to L immediately. 3–4× faster than `-s` for same search space.
| `-g` | Evolutionary (Min-Conflicts) | none | Iterated local search: randomly place marks, then repeatedly move the most conflicting mark to its best position. Restarts with optional crossover from best-seen solution. Runs until solution found.
| `-p` | Physics (Simulated Annealing) | none | Discrete SA over integer positions with conflict-oriented neighborhood: selects a conflicting mark, samples k random positions, accepts best via Metropolis criterion. Runs until solution found.

The `-c` variant was added to bridge the gap between the cheap but rigid `-mp` split and the flexible but heavyweight `-d` tasks. On medium-sized search spaces (e.g. n = 14–16) it often yields the best wall-time.

#### Heuristic Solvers (`-g`, `-p`)

These solvers use stochastic local search and are **not** exhaustive. They implicitly set `-b` (start at the known optimal length from LUT) and run until a valid Golomb ruler is found or the process is interrupted.

- **`-g` (Evolutionary/Min-Conflicts)**: Reliable for n ≤ 12 (typically finds optimal in seconds). For larger n the search time grows steeply but the solver will eventually find a solution.
- **`-p` (Physics/SA)**: Reliable for n ≤ 11. For n = 12+ the success probability per restart is lower; the solver keeps retrying indefinitely.
- **`-to` (Traditional Optimized)**: Exact DFS, always finds the solution. Faster than `-s` by 3–4× due to endpoint-aware pruning.

These solvers only attempt the LUT target length (no L-incrementing loop). If no LUT entry exists for the given n, they fall back to a heuristic lower bound.

**Parallel restarts**: Both `-g` and `-p` run multiple independent restarts in parallel using OpenMP. Each thread has its own RNG state and working memory — no synchronization overhead. The first thread to find a valid ruler wins; all others terminate immediately. Use `-T <num>` to control the number of threads (default: all cores). This provides near-linear speedup in the probability of finding a solution per unit time.

#### Detailed Algorithm Descriptions

##### Traditional Optimized (`-to`) — Endpoint-Aware Branch & Bound

The standard DFS solver places marks left-to-right and only discovers whether `pos[n-1] == L` at the deepest recursion level. The optimized variant fixes both endpoints from the start:

1. **Setup**: `pos[0] = 0`, `pos[n-1] = L`. The distance L is immediately marked as used.
2. **Recursive expansion**: For each candidate position `next`, the solver checks:
   - Distance to the previous mark: `next - pos[depth-1]`
   - **Distance to the fixed endpoint**: `L - next` (this is the key optimization)
   - All distances to previously placed marks
3. **Pruning**: Branches are rejected as soon as *any* distance collides — including the endpoint distance. This prunes the tree much earlier than the standard solver, which only validates the endpoint constraint at the very bottom.
4. **Symmetry breaking**: The first inner mark is limited to `≤ L/2`.

**Complexity**: Worst-case exponential (exact solver), but the early endpoint pruning reduces the effective search space by 3–4× compared to the standard DFS.

##### Evolutionary Solver (`-g`) — Iterated Min-Conflicts with Crossover

A stochastic local search that treats the Golomb ruler problem as a constraint satisfaction problem (CSP). The "conflicts" are duplicate distances.

**Outer loop** (restarts until solution found):
1. **Initialization**: Place n marks randomly on `[0, L]` with fixed endpoints `0` and `L`.
2. Every 3rd restart: use **distance-aware crossover** between the best-seen solution and a fresh random individual to seed the next local search (memetic diversity injection).

**Inner loop** (min-conflicts local search, budget = 800·n iterations per restart):
1. **Conflict detection**: Build `dist_count[d]` — how often each distance d occurs. `total_conflicts` = number of distances that occur more than once.
2. **Variable selection**: Randomly pick an inner mark that participates in at least one conflict.
3. **Value selection** (best-move scan): Remove the mark from `dist_count`, then scan *all* L−2 free positions. For each candidate position p, count conflicts it would create:
   - `dist_count[d] > 0` → collision with existing distances
   - `seen_this[d]` → self-collision (two new distances from p are equal)
   - Select the position with the fewest conflicts (ties broken randomly).
4. **Plateau escape**: If the best move returns the mark to its old position for 12 consecutive iterations, replace it with a random free position instead.
5. **Stagnation restart**: If `total_conflicts` hasn't improved for `4·n` iterations, randomize all inner marks and rebuild `dist_count`.

**Key data structures**:
- `dist_count[0..L]`: frequency array of all pairwise distances (O(1) lookup, O(n) update per move)
- `seen_this[0..L]`: temporary array to detect self-collisions during the best-move scan
- `occupied[0..L]`: boolean array tracking which positions are taken

**Incremental updates**: `mc_remove_mark` and `mc_add_mark` update `dist_count` in O(n) per move, avoiding a full O(n²) rebuild.

##### Physics Solver (`-p`) — Discrete Simulated Annealing

A thermodynamics-inspired optimization that treats marks as particles on a discrete integer lattice, with "energy" equal to the number of distance conflicts.

**Outer loop** (restarts until solution found):
1. **Random initialization**: Place n marks randomly on `[0, L]` with fixed endpoints.
2. Sort marks. Compute initial `dist_count` and `total` conflicts.

**Inner loop** (SA with conflict-oriented neighborhood, 600·n² iterations per restart):
1. **Conflict-driven variable selection**: Identify all inner marks involved in at least one duplicate distance. Randomly select one.
2. **Neighborhood sampling**: Remove the selected mark from `dist_count`. Sample `k = 8 + 2·T` random free positions (T = current temperature). For each sample, estimate the conflicts it would produce (fast O(n) probe using `dist_count`). Keep the best candidate.
3. **Exact evaluation**: Insert the best candidate into `dist_count` via `sa_add` (which counts actual conflicts including self-duplicates). Compute `delta = new_total - original_total`.
4. **Metropolis acceptance**:
   - If `delta ≤ 0` (improvement): accept unconditionally.
   - If `delta > 0` (worsening): accept with probability `exp(-delta / T)`.
   - Otherwise: undo the move (restore old position in `dist_count`).
5. **Cooling schedule**: `T *= 0.99995`, clamped at `T_min = 0.005`.
6. **Reheating**: If no accepted move for `4·n` iterations, reset `T = 2.0`.

**Key insight**: The sampling size `k` is temperature-dependent. At high T, few samples are drawn (more random exploration). As T decreases, more samples are drawn (more greedy, hill-climbing behavior). This smoothly transitions from random search to local optimization.

**Correctness safeguard**: When `total ≤ 0` (potential incremental drift), a full O(n²) recount is triggered. Solutions are sorted before output.

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

This repository contains an optional NVIDIA CUDA implementation in `nvidia/`.
The full description (algorithm, pruning, GPU mapping, validation, tuning
variables) is in [`nvidia/README.md`](nvidia/README.md).

### How it works (short)
- Both endpoints `0` and `L` are fixed (endpoint-aware search; `-b` takes
  `L` from the LUT).  The GPU enumerates all valid prefixes `(s,t,u)` and,
  for n >= 14, `(s,t,u,v)`, and sorts them by their number of legal
  continuations (search order only).
- Every prefix is completed by an exact **bit-parallel DFS**
  (`nvidia/golomb_bits.h`): one mask per level marks all illegal next
  positions (used left distance, used distance to `L`, collision between
  both), so the next legal mark is a single find-first-zero.  Placing a
  mark updates the mask with a shift and a few ORs.
- Exact pruning: the rest segment `[y, L]` with `m` marks is at least
  `OGR(m)` long (LUT values) and at least the sum of the `m-1` smallest
  unused distances; mirror images are cut (first gap < last gap).
- GPU and CPU share one queue: persistent GPU threads take large chunks
  from the front, OpenMP threads take single prefixes from the back with
  the same DFS.  A hit on either side stops both; every result is
  re-validated on the host.
- `-H` searches only the subtree under the LUT pair `(m_1, m_2)` with the
  same engine; the LUT ruler itself is never copied into the result.
- Like the CPU `-b` mode, the program searches only `L` = LUT length;
  `optimal=yes` comes from the LUT.  `GOLOMB_TARGET_L=<L-1>` runs the
  exhaustive search one below.

### Files
- `nvidia/golomb_nv.cu` – host code, frontier kernels, persistent DFS kernel, hybrid scheduler.
- `nvidia/golomb_bits.h` – bit-parallel DFS shared by GPU and CPU.
- `nvidia/Makefile` – nvcc build rules (sm_75 SASS + PTX fallback).
- `nvidia/build_cuda_nv.sh` – build + run helper (CUDA 13.0, gcc-15 by default).
- `nvidia/build_cuda_12.9nv.sh` – older, consistent CUDA 12.9 + GCC 13.4 toolchain.

### Requirements
- NVIDIA GPU with Compute Capability ≥ 7.5 (e.g. GTX 1660 Ti).
- Driver with CUDA UMD ≥ 13.0 (tested: 615.71.09, UMD 13.4).
- CUDA Toolkit 13.0 for compiling **and** linking (toolkit and runtime must
  be the same release; 13.0 headers with the 12.9 runtime change struct
  layouts such as `cudaDeviceProp`).
- GCC/G++ 15 as host compiler (nvcc 13.0 rejects GCC 16).

### Build & Run
```bash
make -C nvidia                               # CUDA 13.0 + gcc-15/g++-15 (defaults)
./nvidia/golomb_nv 15 -b                     # search n=15 at the LUT length
./nvidia/golomb_nv 15 -b -H                  # guided fast-lane
./nvidia/build_cuda_nv.sh 16 -b              # build + run, writes nvidia/GOL_n16_cuda.txt
```

Overrides: `CUDA_TOOLKIT`, `CUDA_RUNTIME_HOME` (defaults to the toolkit),
`CC`, `HOSTCXX`.

Key options
- `-b` – use the LUT length of n (and the LUT lengths of smaller rulers as pruning bounds).
- `-H` – guided fast-lane under the LUT pair `(m_1, m_2)`.
- `-v` – verbose; `-vt <min>` – heartbeat on stderr.
- `-f <file>` / `-fi <sec>` – checkpoint path and interval (uses the older
  root-prefilter path; `-wu`, `-dh`, `-dw`, `-ap` tune only that path).

Useful environment variables (full list in `nvidia/README.md`)
- `GOLOMB_TARGET_L=<L>` – search another length (e.g. L-1 as exhaustive check).
- `GOLOMB_COUNT=1` – count all rulers instead of stopping at the first.
- `GOLOMB_NO_MIRROR=1`, `GOLOMB_NO_GPU_DFS=1`, `GOLOMB_DEBUG=1`.

Where results are written
- Binary: `out/GOL_n<n>_nv.txt` (`_nv_H` with `-H`), same format as the C
  variant; diagnostics go to stderr with `[CUDA]`/`[VT]` prefixes.
- Script: additionally `nvidia/GOL_n<n>_cuda.txt`.

## 10  License
This repository is released under the MIT License. See the `LICENSE` file for the full text.
