# Golomb Ruler Finder - Rust Implementation

Eine Rust-Implementation des Golomb-Ruler-Finders, die kompatibel mit den C-, Java- und Go-Versionen ist.

## Überblick

Diese Implementation verwendet moderne Rust-Features und bietet eine effiziente, threadsichere Lösung für das Golomb-Ruler-Problem. Die Implementation beinhaltet:

- Optimierte Algorithmen mit effizienter Speichernutzung
- Multi-Threading mit Rayon für parallele Verarbeitung
- Integrierte LUT (Look-Up Table) für bekannte optimale Lineale
- Kompatibles Ausgabeformat zu anderen Implementationen

## Voraussetzungen

- **Rust 1.70+**
- **Cargo** (Rust's Paketmanager)

## Build & Ausführung

### Installation

```bash
# Mache Build-Skript ausführbar
chmod +x build.sh

# Führe das Build-Skript aus
./build.sh
```

### Ausführung

```bash
# Grundlegende Ausführung (sucht nach einem 5-Mark-Lineal)
./target/release/golomb 5

# Mit Multi-Processing und Verbose-Modus
./target/release/golomb 5 -mp -v

# Suche nach dem optimalen Lineal
./target/release/golomb 5 -b

# Ausgabe in bestimmte Datei schreiben
./target/release/golomb 5 -o ausgabe.txt
```

## Kommandozeilenoptionen

| Flag | Beschreibung |
|------|-------------|
| `-v, --verbose` | Verbose-Modus (gibt Zwischenschritte aus) |
| `--mp` | Multi-Processing verwenden (kein Kurzflag; `-mp` wird von clap als `-m -p` interpretiert und schlägt fehl) |
| `-b, --best` | Verwende bekannte optimale Länge als Obergrenze für die Suche |
| `-o, --output <datei>` | Ausgabe in eine spezifische Datei schreiben |

## Algorithmus

Die Such-Engine ist ein direkter Port des endpoint-aware DFS aus der
C-Implementierung (`src/solver_traditional_opt.c`, der `-to`-Solver) und
derselben Technik, die auch die CUDA-Variante nutzt (`nvidia/golomb_bits.h`):
beide Linealenden (`0` und `L`) werden fixiert, bevor eine innere Markierung
gesetzt wird, sodass die Distanz zum fixen rechten Endpunkt sofort geprüft
wird — statt erst, wenn alle Markierungen platziert sind. Belegte Distanzen
werden in einem Bitset (ein Bit pro Distanzwert) verfolgt, das beim
Absteigen inkrementell gesetzt und beim Backtracking zurückgerollt wird,
statt bei jedem Knoten neu berechnet zu werden (die vorherige Version
klonte dafür bei jedem Kandidaten den kompletten Distanz-Set).

1. Markierungen werden immer in aufsteigender Reihenfolge hinzugefügt.
2. **Endpoint-aware Pruning**: für jede Kandidatenposition `next` wird die
   Distanz zum fixen Endpunkt `L - next` geprüft, bevor irgendetwas anderes
   passiert — das schneidet Teilbäume viel früher ab als das klassische
   linksbündige Backtracking.
3. **Symmetriebrechung**: die erste innere Markierung ist auf `<= L/2`
   begrenzt (Spiegelbilder werden nicht doppelt durchsucht).
4. **Bitset-Distanzverfolgung**: `Vec<u64>`, ein Bit pro Distanz,
   inkrementell gesetzt/gelöscht statt bei jedem Knoten neu berechnet.

## Multi-Processing

`--mp` spiegelt `solve_golomb_traditional_opt_mt` aus dem C-Code: alle
gültigen, verschiedenen `(pos[1], pos[2])`-Präfixe werden vorab vollständig
aufgezählt (gleiche Grenzen und Symmetriebrechung wie bei der
Einzelthread-Suche) und über Rayons `par_iter().find_map_any(...)` verteilt.

1. Jeder Präfix läuft als eigenständige Aufgabe mit eigenem Bitset und
   Scratch-Buffer — kein gemeinsamer veränderlicher Zustand, keine Locks im
   heißen Pfad.
2. Ein `AtomicBool` signalisiert allen Workern, sobald ein Treffer
   gefunden wurde; `find_map_any` bricht die Iteration ab, sobald ein
   `Some` zurückkommt.
3. Der Fortschrittszähler (`States searched`) wird pro Kandidat einmal
   atomar addiert statt pro DFS-Knoten — das vermeidet Cache-Line-Konkurrenz
   zwischen Threads und war bei n=14 für einen 2x-Speedup verantwortlich
   (63,7 s → 31,7 s).

## Ausgabeformat

Die Ausgabe ist kompatibel mit den C-, Java- und Go-Versionen:

```
length=<letzte-Markierung>
marks=<n>
positions=<durch Leerzeichen getrennte Markierungspositionen>
distances=<alle messbaren Abstände>
missing=<Abstände 1..Länge, die NICHT messbar sind>
seconds=<Rohzeit in Sekunden>
time=<formatierte Zeit, s.mmm>
options=<Kommandozeilenflags oder "std">
optimal=<yes|no>   # nur wenn ein Referenzlineal existiert
```

## Performance-Merkmale

Die Rust-Implementation bietet:

- Sehr schnelle Ausführung durch Zero-Cost-Abstraktionen von Rust
- Effiziente Speichernutzung mit kompakten Datenstrukturen (Bitset statt Klonen des Distanz-Sets)
- Hervorragende Parallelisierung durch Rayons Work-Stealing-Algorithmus
- Native Binaries ohne externe Abhängigkeiten

### Benchmarks (2026-09-27, gleiche Maschine wie die Root-README-Benchmarks)

`./target/release/golomb <n> -b`, Wall-Clock:

| n | Sekunden |
|---|----------|
| 9 | 0,0003 |
| 10 | 0,002 |
| 11 | 0,012 |
| 12 | 0,76 |
| 13 | 12,2 |
| 14 (`--mp`) | 31,7 |

Zum Vergleich: die vorherige (linksbündige, pro Knoten klonende)
Implementierung brauchte bereits für n=12 spürbar länger und wurde für
`--mp` bei größerem `n` durch eine unvollständige Aufgaben-Enumeration
(`generate_tasks`) sogar zum Korrektheitsrisiko — dieselbe Klasse Bug wie
der jetzt behobene Go-Solver, nur dass hier zusätzlich nicht jedes
`(mark1, mark2)`-Präfix abgedeckt war.

## Architektur

Die Implementation ist in mehrere Module aufgeteilt:

- `main.rs`: CLI-Parser und Programmsteuerung
- `ruler.rs`: GolombRuler-Struktur und -Methoden
- `solver.rs`: Such-Algorithmus und Parallelisierung
- `lut.rs`: Look-Up Table für bekannte optimale Lineale
