# Golomb Ruler Finder - Ruby Implementation

Eine Ruby-Implementation des Golomb-Ruler-Finders mit Multi-Processing-Unterstützung und Kompatibilität mit den C- und Go-Versionen.

## Überblick

Diese Ruby-Implementation bietet die gleiche Funktionalität wie die C- und Go-Versionen mit folgenden Features:

- **Multi-Processing-Unterstützung**: Parallele Suche mit dem Parallel-Gem (`-mp` Flag)
- **Integrierte LUT**: Nachschlagetabelle mit bekannten optimalen Linealen zur Verifikation
- **Ausgabekompatibilität**: Ergebnisformat kompatibel mit den Original-C/Go-Versionen
- **Plattformübergreifend**: Läuft auf jeder Ruby-unterstützten Plattform

## Voraussetzungen

- **Ruby 2.5+**
- Keine externen Gems erforderlich

## Build & Ausführung

### Installation

```bash
# Mache Skripte ausführbar
chmod +x *.rb build.sh

# Führe das Build-Skript aus
./build.sh
```

### Verwendung

```bash
# Ausführen aus dem Ruby-Verzeichnis
./golomb.rb <marks> [options]

# Oder verwende den Binary-Link
../bin/golomb-ruby <marks> [options]

# Beispiele
./golomb.rb 5 -v -b
./golomb.rb 8 -mp -v -b
```

### Optionen

| Flag | Beschreibung |
|------|-------------|
| `-v` | Aktiviert ausführliche Ausgabe während der Suche |
| `-mp` | Nutzt Multi-Processing-Löser (parallele Prozesse) |
| `-b` | Verwendet die beste bekannte Lineallänge als Ausgangspunkt |
| `-o <file>` | Schreibt das Ergebnis in eine bestimmte Ausgabedatei |
| `-help` | Zeigt eine Hilfemeldung an und beendet das Programm |

## Algorithmus

Die Ruby-Implementation verwendet einen optimierten Backtracking-Algorithmus zum Finden optimaler Golomb-Lineale:

1. **Vollständige Backtracking-Suche**: Berechnet Lineale immer durch tatsächliche Suche, statt LUT-Lineale direkt zurückzugeben
2. **Intelligente LUT-Nutzung**:
   - Verwendet die LUT nur für Suchgrenzen und zur Ausgabekanonisierung
   - Mit `-b` Flag: Beschränkt die Suche auf die bekannte optimale Länge aus der LUT
   - Ohne `-b` Flag: Führt vollständige Suche von der Untergrenze bis zur bekannten optimalen Länge durch
   - Validiert Ergebnisse gegen kanonische optimale Lineale aus der LUT
3. **Fortschrittliches Pruning**:
   - Lehnt Teillösungen mit doppelten Distanzen mittels effizienter Bitset-Operationen ab
   - Frühe Validierung von Linealsprefixen, um ungültige Zweige schnell zu eliminieren
   - Adaptive Schrittgrößen basierend auf Markierungspositionen, um den Suchraum effizient zu erkunden

### Multi-Processing (`-mp`)

Das `-mp` Flag aktiviert die Parallelverarbeitung durch:

- Aufteilung des Suchraums nach den ersten beiden Markierungspositionen
- Verwendung eines Prozesses pro CPU-Kern (bis zur Größe des Suchraums)
- Sofortige Beendigung, sobald eine Lösung gefunden wurde
- Koordination der Arbeiter mittels des Parallel-Gems

## Ausgabeformat

Die Ruby-Implementation erzeugt Ausgabedateien im gleichen Format wie die C- und Go-Versionen:

```
length=<letzte-markierung>
marks=<n>
positions=<leerzeichen-getrennte-markierungspositionen>
distances=<alle-messbaren-distanzen>
missing=<distanzen-1..length-die-NICHT-messbar-sind>
seconds=<reine-laufzeit-floating-sekunden>
time=<formatierte-laufzeit>
options=<kommandozeilen-flags>
optimal=<yes|no>   # nur wenn Referenzlineal existierte
```

Ausgabedateien werden im `out/`-Verzeichnis gespeichert mit Namen nach dem Muster `GOL_n<marks>_<optionen>.txt`.

## Bekannte optimale Lineale

Die LUT enthält verifizierte optimale Lineale für Markierungen 2-28, darunter:
- 5 Markierungen: Länge 11 `[0, 1, 4, 9, 11]`
- 8 Markierungen: Länge 34 `[0, 1, 4, 9, 15, 22, 32, 34]`
- 10 Markierungen: Länge 55 `[0, 1, 6, 10, 23, 26, 34, 41, 53, 55]`
- 12 Markierungen: Länge 85 `[0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85]`

Alle Lineale in der LUT wurden verifiziert als gültige Golomb-Lineale mit eindeutigen Distanzen.

## Performance-Eigenschaften

Die Ruby-Implementation bietet:

- **Eleganter Code**: Verständlicher, wartbarer Ruby-Code
- **Multiprocessing**: Parallelisierung über mehrere CPU-Kerne hinweg
- **Bitset-Optimierungen**: Schnelle Distanzüberprüfungen mit Array-basierten Bitsets
- **Adaptive Schrittgrößen**: Optimierte Schrittgrößen je nach Markierungsposition

Im Vergleich zu den C- und Go-Implementierungen bietet Ruby den Vorteil der Lesbarkeit und schnellen Entwicklung, jedoch mit einigen Performance-Einbußen aufgrund der interpretierten Natur von Ruby.

## Architektur

Die Ruby-Implementation besteht aus:

- `golomb.rb`: Kommandozeilen-Schnittstelle und Programmeinstiegspunkt
- `solver.rb`: Kern-Suchalgorithmus mit Einzel- und Multi-Processing-Varianten
- `ruler.rb`: Lineal-Datenstruktur und Validierungslogik
- `lut.rb`: Nachschlagetabelle mit bekannten optimalen Linealen
- `build.sh`: Build-Skript für einfache Installation

## Lizenz

Gleiche Lizenz wie die Original-C-Version.
