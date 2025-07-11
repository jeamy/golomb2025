# Golomb Ruler Finder - Java 24 Edition

A Java 24 port of the optimal Golomb ruler finder with multi-processing support (`-mp`).

## Overview

This is a modern Java implementation of the Golomb ruler search algorithm, ported from the original C version. A Golomb ruler is a set of marks where every pair of marks defines a unique distance. This implementation finds optimal rulers (shortest possible for a given number of marks) using branch-and-bound search with parallel processing capabilities.

## Features

- **Java 24 Support**: Uses modern Java features including records, pattern matching, and enhanced APIs
- **Multi-threaded Search**: Parallel processing with Fork-Join framework (`-mp` flag)
- **Built-in LUT**: Look-up table with known optimal rulers for verification
- **Comprehensive Testing**: Full test suite with JUnit 5
- **Maven Build**: Standard Maven project structure
- **Output Compatibility**: Results format compatible with original C version

## Requirements

- **Java 24** (with preview features enabled)
- **Maven 3.8+**

## Build & Run

### Using the Script (Recommended)

A convenience script `golomb.sh` is provided to run the Java implementation with the same command-line syntax as the C version:

```bash
# Run from the java directory
./golomb.sh <marks> [options]

# Or use the symlink in the bin directory
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

# Build JAR
mvn package

# Run JAR
java --enable-preview -jar target/golomb-ruler-1.0.0.jar 5 -v -mp
```

## Usage

```bash
java --enable-preview -jar golomb-ruler.jar <marks> [options]
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
java --enable-preview -jar golomb-ruler.jar 5

# Verbose multi-threaded search for 8 marks
java --enable-preview -jar golomb-ruler.jar 8 -v -mp

# Use heuristic and save to file
java --enable-preview -jar golomb-ruler.jar 10 -b -o ruler10.txt

# Single-threaded with heartbeat every 2 minutes
java --enable-preview -jar golomb-ruler.jar 12 -s -vt 2
```

## Architecture

### Core Classes

- **`GolombRuler`**: Record representing a ruler with positions, length, and utility methods
- **`GolombSolver`**: Main search algorithm with single-threaded and parallel implementations
- **`GolombLUT`**: Look-up table containing known optimal rulers for verification
- **`GolombMain`**: Command-line interface and application entry point

### Algorithm

The solver uses a recursive branch-and-bound depth-first search:

1. **Branch**: Try each possible position for the next mark
2. **Bound**: Prune branches that cannot lead to optimal solutions
3. **Parallel**: Distribute top-level branches across multiple threads
4. **Optimization**: Use symmetry breaking and distance bitsets for efficiency

### Multi-threading

The `-mp` flag enables parallel processing using Java's Fork-Join framework:
- Distributes search space across available CPU cores
- Uses work-stealing for load balancing
- Supports early termination when solution is found
- Maintains thread-safe result collection

## Performance

Performance characteristics compared to the original C version:

- **Single-threaded**: ~2-3x slower due to JVM overhead
- **Multi-threaded**: Comparable or better on multi-core systems
- **Memory usage**: Higher due to JVM, but with better garbage collection
- **Scalability**: Better thread scaling on high-core systems

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

The LUT includes optimal rulers for marks 2-27, with lengths:
- 4 marks: length 6
- 5 marks: length 11  
- 6 marks: length 17
- 7 marks: length 25
- 8 marks: length 34
- And more...

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
- Slightly slower single-threaded performance

### Future Enhancements
- Vector API integration for SIMD-like operations
- Native compilation with GraalVM
- Additional search algorithms (SAT solver, creative solver)
- Web interface for interactive usage

## License

Same license as the original C version.
