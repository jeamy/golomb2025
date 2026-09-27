# Golomb Ruler Finder - Java 25 Edition

A Java 25 port of the optimal Golomb ruler finder with multi-processing support (`-mp`).

## Overview

This is a modern Java implementation of the Golomb ruler search algorithm, ported from the original C version. A Golomb ruler is a set of marks where every pair of marks defines a unique distance. This implementation finds optimal rulers (shortest possible for a given number of marks) using branch-and-bound search with parallel processing capabilities.

## Features

- **Java 25 Support**: Uses modern Java features including records, pattern matching, and enhanced APIs
- **Multi-threaded Search**: `availableProcessors()` worker threads pulling from a shared atomic cursor (`-mp`, the default — see Algorithm below)
- **Built-in LUT**: Look-up table with known optimal rulers for verification
- **Comprehensive Testing**: Full test suite with JUnit 5
- **Maven Build**: Standard Maven project structure
- **Output Compatibility**: Results format compatible with original C version

## Requirements

- **Java 25**
- **Maven 3.8+**

## Build & Run

### Using the Script (Recommended)

A convenience script `golomb.sh` is provided to run the Java implementation with the same command-line syntax as the C version:

```bash
# Run from the java directory
./golomb.sh <marks> [options]

# Or use the symlink in the bin directory (golomb.sh creates it on first run)
../bin/golomb-java <marks> [options]

# Examples
./golomb.sh 5 -v
./golomb.sh 8 -mp -v
```

The script automatically adds the `-b` flag to use the best-known ruler length as a starting point, ensuring optimal performance.

### Manual Build & Run

```bash
# Build the project
mvn clean compile

# Run tests
mvn test

# Run with specific parameters
mvn exec:java -Dexec.args="5 -v -mp"

# Run via classpath (no shade/assembly plugin is configured, so `mvn package`
# does NOT produce an executable jar — running it with `java -jar` fails with
# "no main manifest attribute"; confirmed by running it)
java -cp target/classes com.golomb.GolombMain 5 -v -mp

# Or via the exec plugin
mvn -q exec:java -Dexec.args="5 -v -mp"
```

## Usage

```bash
java -cp target/classes com.golomb.GolombMain <marks> [options]
```

### Options

| Flag | Description |
|------|-------------|
| `-v, --verbose` | Enable verbose output during search |
| `-s, --single` | Force single-threaded solver |
| `-mp` | Use multi-threaded solver (default) |
| `-b` | Use best-known ruler length as starting point heuristic |
| `-o <file>` | Write result to specific output file |
| `-vt <min>` | Periodic heartbeat every `<min>` minutes |
| `--help` | Display help message |

### Examples

```bash
# Find optimal ruler with 5 marks
java -cp target/classes com.golomb.GolombMain 5

# Verbose multi-threaded search for 8 marks
java -cp target/classes com.golomb.GolombMain 8 -v -mp

# Use heuristic and save to file
java -cp target/classes com.golomb.GolombMain 10 -b -o ruler10.txt

# Single-threaded with heartbeat every 2 minutes
java -cp target/classes com.golomb.GolombMain 12 -s -vt 2
```

## Architecture

### Core Classes

- **`GolombRuler`**: Record representing a ruler with positions, length, and utility methods
- **`GolombSolver`**: Main search algorithm with single-threaded and parallel implementations
- **`GolombLUT`**: Look-up table containing known optimal rulers for verification
- **`GolombMain`**: Command-line interface and application entry point

### Algorithm

The search engine is a direct port of the C solver's endpoint-aware DFS
(`src/solver_traditional_opt.c`, the `-to` solver) and the technique used by
the CUDA variant (`nvidia/golomb_bits.h`): both ruler endpoints (`0` and
`length`) are fixed before any inner mark is placed, so every candidate
mark's distance to the fixed right endpoint is checked immediately instead
of only once all marks are placed. Used distances live in a `long[]` bitset
(one bit per value), updated incrementally on descent and rolled back on
backtrack. An earlier version here placed marks left-to-right and only
checked `positions[marks - 1] == maxLength` at the deepest recursion level,
missing this pruning entirely — see `GolombSolver`'s class Javadoc for the
full before/after story, including two dead ends that didn't help.

1. **Endpoint-aware pruning**: for each candidate mark, the distance to the
   fixed endpoint is checked before anything else.
2. **Symmetry breaking**: the first inner mark is limited to `<= length/2`.
3. **Bitset distance tracking**: `long[]`, incrementally set/cleared.

### Multi-threading (`-mp`, the default)

`-mp` enumerates every valid, distinct `(positions[1], positions[2])` prefix
up front (same bounds as the C `-mp` path) and hands them out, in index
order, to a fixed pool of `availableProcessors()` worker threads pulling
from a shared `AtomicInteger` cursor — mirroring the Go/Rust ports and C's
`schedule(dynamic, 16)`. This matters more than it sounds: an earlier
version here parallelized only over `positions[1]` with `positions[2]`
nested sequentially inside, and a later attempt used
`parallelStream().findAny()` over the full candidate list — both left one
thread grinding serially through a large chunk of candidates while every
other thread sat idle, because the winning prefix is essentially always
near the front of the enumeration. The explicit cursor lets every idle
worker immediately pull the next unclaimed candidate instead.

## Performance

### Benchmarks (2026-09-27, same machine as the root README's CPU benchmarks; `-mp` is the default)

`java -cp target/classes com.golomb.GolombMain <n> -b`, wall clock:

| n | seconds |
|---|---------|
| 9 | 0.0003 |
| 12 | 0.35 |
| 13 (`-mp`) | 2.9 |
| 13 (`-s`, single-threaded) | 13.4 |
| 14 (`-mp`) | 58 |

For reference, the pre-port solver (left-to-right, no endpoint pruning,
parallelized only over `positions[1]`) took 0.8s for n=12 and 23.0s for
n=13 `-mp -b`; n=14 was never run against it (see `GolombSolver`'s class
Javadoc for the full progression, including the two changes that didn't
help before the actual fix landed).

Compared to the C version at n=14 `-mp -b` (~22s, root README) and the Go
(57s) and Rust (32s) ports: Java's single-threaded search is now at parity
with Go/Rust (13.4s vs. 12.4s/12.2s at n=13), and `-mp` lands close to Go.

- **Memory usage**: higher due to the JVM, but with automatic garbage collection.
- **No SIMD/inline-assembly paths**: unlike the C version's `-e`/`-af`/`-an`.

## Output Format

Results are written in the same format as the original C version:

```
length=11
marks=5
positions=0 1 4 9 11
distances=1 3 4 5 7 8 9 10 11
missing=2 6
seconds=0.123456
time=0.123 s
options=-mp -v
optimal=yes
```

## Testing

The project includes comprehensive tests:

```bash
# Run all tests
mvn test

# Run specific test class
mvn test -Dtest=GolombSolverTest

# Run with verbose output
mvn test -Dtest=GolombSolverTest -Dorg.slf4j.simpleLogger.defaultLogLevel=debug
```

## Known Optimal Rulers

The LUT includes verified optimal rulers for marks 2-28, with lengths:
- 4 marks: length 6 `[0, 1, 4, 6]`
- 5 marks: length 11 `[0, 1, 4, 9, 11]`
- 6 marks: length 17 `[0, 1, 4, 10, 12, 17]`
- 7 marks: length 25 `[0, 1, 4, 10, 18, 23, 25]`
- 8 marks: length 34 `[0, 1, 4, 9, 15, 22, 32, 34]`
- 9 marks: length 44 `[0, 1, 5, 12, 25, 27, 35, 41, 44]`
- 10 marks: length 55 `[0, 1, 6, 10, 23, 26, 34, 41, 53, 55]`
- 11 marks: length 72 `[0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72]`
- And more...

All rulers in the LUT have been verified to be valid Golomb rulers with unique distances.

## Differences from C Version

### Additions
- Object-oriented design with records and classes
- Enhanced error handling and validation
- Comprehensive test suite
- Maven build system
- Better command-line parsing

### Limitations
- No SIMD optimizations (AVX2/AVX512)
- No inline assembly optimizations
- Higher memory usage
- `-b` does not canonicalize the result to the LUT's mark positions at the
  optimal length the way the C, Go and Rust ports do — it returns whatever
  (equally valid) positions the search itself found

### Recent Improvements (2026-09-27)
- Upgraded from Java 24 to Java 25 (the current LTS) and dropped
  `--enable-preview` everywhere (pom.xml, golomb.sh, docs): the codebase
  never actually used any preview feature — `javac --release 24` (no
  `--enable-preview`) already compiled it cleanly, confirmed by trying it.
  Java 24 is a non-LTS release and has been out of Oracle/OpenJDK support
  since Java 25 shipped in September 2025, so this wasn't just a version
  bump; the project was targeting an already-unsupported release for no
  functional reason.
- Ported the C/CUDA endpoint-aware bit-parallel DFS (see Algorithm above);
  `-mp` now uses an explicit atomic-cursor worker pool instead of
  parallelizing over `positions[1]` alone. n=14 `-mp -b`: not run before
  (n=13 alone was already 23s) -> 58s now.
- `golomb.sh`: fixed a hardcoded `JAVA_HOME=~/programming/java24` that only
  worked on the original author's machine; it now derives `JAVA_HOME` from
  `java` on `PATH` if unset.
- Fixed LUT data for 10-mark and 11-mark rulers to match the verified optimal rulers from the C implementation
- Added convenience script (`golomb.sh`) for easier execution with automatic best-known ruler length usage
- Comprehensive validation of all rulers in the LUT
- Improved test suite with validation for all ruler lengths

### Future Enhancements
- Vector API integration for SIMD-like operations
- Native compilation with GraalVM
- Additional search algorithms (SAT solver, creative solver)
- Web interface for interactive usage
- Automated validation against C implementation for all new rulers

## Output Format

The Java implementation produces output files in the same format as the C version for easy comparison:

```
length=<last-mark>
marks=<n>
positions=<space-separated mark positions>
distances=<all measurable distances>
missing=<distances 1..length that are NOT measurable>
seconds=<raw runtime, floating seconds>
time=<pretty runtime, h:mm:ss.mmm>
options=<command-line flags>
optimal=<yes|no>   # only if reference ruler existed
```

Output files are saved to the `out/` directory with names following the pattern `GOL_n<marks>_<options>.txt`.

## Comparing Java and C Implementations

To compare the results between the Java and C implementations:

```bash
# Run C version
../bin/golomb 5 -v

# Run Java version
./golomb.sh 5 -v

# Compare outputs
diff ../out/GOL_n5_v.txt out/GOL_n5_mp_b_v.txt
```

Both implementations should find the same optimal rulers for all mark counts, though performance characteristics may differ.

## License

Same license as the original C version.
