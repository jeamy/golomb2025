# Golomb-2025 – Optimal Golomb Ruler Finder

A small C command-line utility that searches for **optimal Golomb rulers** of a given length and verifies them against a built-in look-up table (LUT).

## 1  Background
A *Golomb ruler* is a set of integer marks where every pair of marks defines a unique distance. The length of the ruler is the position of the last mark. A ruler is *optimal* if, for its number of marks `n`, no shorter ruler exists. See `doc/` or Wikipedia for further details.

## 2  Build
```bash
make            # builds `bin/golomb`
make clean       # removes objects and binary
```
Requires `gcc` and GNU make. The default flags are `-Wall -O2`.

## 3  Usage
```bash
./bin/golomb <length> [-v]
```
* **length** – Target length *L* to search.
* `-v` – Verbose mode (prints intermediate search states).
* The result is also written to `GOL_<length>.txt` (length, marks, positions, runtime, and—if available—optimal status).
* The runtime in seconds is printed after completion.

Example:
```bash
./bin/golomb 11 -v
```

## 4  Files & Structure
```

golomb-2025/
├── include/          # public headers
│   └── golomb.h
├── src/              # implementation
│   ├── lut.c         # built-in optimal rulers table & helpers
│   ├── solver.c      # backtracking search
│   └── main.c        # CLI / program entry
├── data/             # human-readable reference list (optional)
├── Makefile
└── README.md
```

## 5  Algorithm
The solver uses recursive backtracking with pruning:
1. Always add marks in ascending order.
2. Reject a partial solution immediately when a duplicate distance appears.
3. Use a lower-bound heuristic: if even by spacing the remaining marks 1 apart the target length cannot be met, prune.

● **With LUT entry** – If the length exists in the table, the solver uses its mark count *n* and afterwards checks the result against the LUT: *Optimal ✅* or *Not optimal ❌*.

● **Without LUT entry** – If no entry is found, the solver searches for the first valid ruler by incrementing *n* from 2 up to `MAX_MARKS`. No comparison is performed, but the runtime is still measured and printed.

### Performance
The solver now uses a bitset for occupied distances (branch-and-bound). Optimal rulers up to order ≈ 18 are found within seconds, orders 24–25 usually within < 1 minute. For orders > 25 the runtime grows rapidly. Further speed-ups are possible via multithreading (OpenMP) and stronger symmetry-breaking rules.

