# Golomb Ruler Finder - Rust Implementation

Eine Rust-Implementation des Golomb-Ruler-Finders, die kompatibel mit den C-, Java-, Go- und Ruby-Versionen ist.

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
| `-m, --mp` | Multi-Processing verwenden |
| `-b, --best` | Verwende bekannte optimale Länge als Obergrenze für die Suche |
| `-o, --output <datei>` | Ausgabe in eine spezifische Datei schreiben |

## Algorithmus

Der Algorithmus verwendet Backtracking mit Pruning:

1. Markierungen werden immer in aufsteigender Reihenfolge hinzugefügt
2. Eine partielle Lösung wird sofort verworfen, wenn eine doppelte Distanz auftritt
3. Eine bitset-basierte Distanzprüfung für optimale Performance
4. Für Multi-Processing wird der Suchraum in Aufgaben aufgeteilt, die durch unterschiedliche Positionen für die ersten beiden Markierungen definiert sind

## Multi-Processing

Die Multi-Processing-Option (`-mp`) nutzt die Rayon-Bibliothek für parallele Verarbeitung:

1. Der Suchraum wird in Aufgaben aufgeteilt, basierend auf den Positionen der ersten beiden Markierungen
2. Die Anzahl der Threads wird automatisch an die verfügbaren CPU-Kerne angepasst
3. Ein gemeinsam genutztes Mutex ermöglicht frühe Terminierung, sobald eine Lösung gefunden wurde
4. Die Arbeitslast wird dynamisch angepasst, um eine gleichmäßige Verteilung zu gewährleisten

## Ausgabeformat

Die Ausgabe ist kompatibel mit den C-, Java-, Go- und Ruby-Versionen:

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
- Effiziente Speichernutzung mit kompakten Datenstrukturen
- Hervorragende Parallelisierung durch Rayons Work-Stealing-Algorithmus
- Native Binaries ohne externe Abhängigkeiten

## Architektur

Die Implementation ist in mehrere Module aufgeteilt:

- `main.rs`: CLI-Parser und Programmsteuerung
- `ruler.rs`: GolombRuler-Struktur und -Methoden
- `solver.rs`: Such-Algorithmus und Parallelisierung
- `lut.rs`: Look-Up Table für bekannte optimale Lineale
