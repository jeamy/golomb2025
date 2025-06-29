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
* Ergebnis wird zusätzlich in `GOL_<length>.txt` gespeichert (Länge, Markierungen, Positionen, Laufzeit, ggf. Optimal-Status).
* Laufzeit in Sekunden wird nach Abschluss auf der Konsole ausgegeben.

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

● **Mit LUT-Eintrag** – Ist die Länge in der Tabelle vorhanden, übernimmt der Solver deren Markierungszahl *n* und prüft das Ergebnis anschließend: *Optimal ✅* oder *Nicht optimal ❌*.

● **Ohne LUT-Eintrag** – Fehlt ein Eintrag, sucht der Solver selbstständig nach dem ersten gültigen Lineal, indem er *n* von 2 bis `MAX_MARKS` erhöht. Hier erfolgt kein Vergleich, aber die Laufzeit wird gemessen und ausgegeben.

Future work could add multithreading (OpenMP) and a more advanced branch-and-bound heuristic for larger rulers.
# golomb2025
