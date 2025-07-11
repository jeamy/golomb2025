# Golomb Ruler Finder - Go Implementation

A modern Go port of the Golomb ruler search algorithm, featuring multi-processing capabilities and compatibility with the original C implementation.

## Overview

This Go implementation provides the same functionality as the C version with the following features:

- **Multi-processing Support**: Parallel search using goroutines (`-mp` flag)
- **Built-in LUT**: Look-up table with known optimal rulers for verification
- **Output Compatibility**: Results format compatible with original C version
- **Cross-platform**: Runs on any platform supported by Go

## Requirements

- **Go 1.23+**

## Build & Run

### Build

```bash
# Build the project
./build.sh

# Or manually
go build -o ../bin/golomb-go .
```

### Usage

```bash
# Run from the go directory
./golomb <marks> [options]

# Or use the binary directly
../bin/golomb-go <marks> [options]

# Examples
./golomb 5 -v -b
./golomb 8 -mp -v -b
```

### Options

| Flag | Description |
|------|-------------|
| `-v` | Enable verbose output during search |
| `-mp` | Use multi-processing solver (parallel goroutines) |
| `-b` | Use best-known ruler length as starting point heuristic |
| `-o <file>` | Write result to a specific output file |
| `-help` | Display help message and exit |

## Algorithm

The Go implementation uses the same recursive backtracking algorithm as the C version:

1. **Backtracking Search**: Systematically explores all possible ruler configurations
2. **Early Pruning**: Rejects partial solutions with duplicate distances
3. **Multi-processing**: Splits search space across multiple goroutines for parallel execution
4. **LUT Integration**: Uses known optimal rulers for verification and heuristic starting points

### Multi-processing (`-mp`)

The `-mp` flag enables parallel processing by:
- Splitting the search space by the second mark position
- Using one goroutine per CPU core (up to the search space size)
- Implementing early termination when a solution is found
- Coordinating workers using channels and context cancellation

## Output Format

The Go implementation produces output files in the same format as the C version:

```
length=<last-mark>
marks=<n>
positions=<space-separated mark positions>
distances=<all measurable distances>
missing=<distances 1..length that are NOT measurable>
seconds=<raw runtime, floating seconds>
time=<pretty runtime>
options=<command-line flags>
optimal=<yes|no>   # only if reference ruler existed
```

Output files are saved to the `out/` directory with names following the pattern `GOL_n<marks>_<options>.txt`.

## Known Optimal Rulers

The LUT includes verified optimal rulers for marks 2-28, including:
- 5 marks: length 11 `[0, 1, 4, 9, 11]`
- 8 marks: length 34 `[0, 1, 4, 9, 15, 22, 32, 34]`
- 10 marks: length 55 `[0, 1, 6, 10, 23, 26, 34, 41, 53, 55]`
- 11 marks: length 72 `[0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72]`

All rulers in the LUT have been verified to be valid Golomb rulers with unique distances.

## Performance Characteristics

### Advantages
- **Excellent Concurrency**: Go's goroutines provide efficient parallelization
- **Memory Safety**: Automatic garbage collection prevents memory leaks
- **Cross-platform**: Single binary runs on multiple operating systems
- **Fast Compilation**: Quick build times for development iteration

### Compared to C Implementation
- **Startup Time**: Slightly faster startup due to no compilation step
- **Memory Usage**: Higher memory usage due to garbage collection overhead
- **Single-threaded**: Comparable performance to C for small problems
- **Multi-threaded**: Excellent scaling with goroutines, potentially better than C's OpenMP

## Comparing with C Implementation

To compare results between Go and C implementations:

```bash
# Run C version
../bin/golomb 5 -v -b

# Run Go version  
./golomb 5 -v -b

# Compare outputs
diff ../out/GOL_n5_b_v.txt out/GOL_n5_b_v.txt
```

Both implementations should find identical optimal rulers, though performance characteristics may differ.

## Architecture

The Go implementation consists of:

- `main.go`: Command-line interface and program entry point
- `solver.go`: Core search algorithm with single-threaded and multi-processing variants
- `ruler.go`: Ruler data structure and validation logic
- `lut.go`: Look-up table with known optimal rulers
- `build.sh`: Build script for easy compilation

## Future Enhancements

- **WebAssembly Support**: Compile to WASM for browser execution
- **gRPC API**: Network service for distributed computation
- **Benchmarking Suite**: Performance comparison tools
- **Advanced Algorithms**: Integration with constraint solvers
- **Visualization**: Web interface for interactive ruler exploration

## License

Same license as the original C version.
