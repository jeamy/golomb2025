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

The search engine is a direct port of the C solver's endpoint-aware DFS
(`src/solver_traditional_opt.c`, the `-to` solver) and the same technique
used by the CUDA variant (`nvidia/golomb_bits.h`): both ruler endpoints
(`0` and `L`) are fixed before any inner mark is placed, so every candidate
mark is checked against the distance to the fixed right endpoint
*immediately*, instead of only once all marks are placed. Used distances are
tracked in a bitset (one bit per distance value) that is updated
incrementally on descent and rolled back on backtrack — no per-node
from-scratch re-validation.

1. **Full search, no shortcuts**: the ruler is always constructed by the DFS
   itself; the LUT is only used for the search bounds (`-b`: search only the
   known optimal length; without `-b`: search every length from a lower
   bound up to the known optimal length) and, once an optimal-length ruler is
   found with `-b`, to substitute the canonical LUT mark positions for the
   (equally valid but not necessarily identical) ones the search found — the
   same convention the C/Rust/Java ports use.
2. **Endpoint-aware pruning**: for each candidate mark `next`, the distance
   to the fixed endpoint `L - next` is checked before any other work; this
   prunes branches that the plain left-to-right DFS would only reject much
   deeper in the tree.
3. **Symmetry breaking**: the first inner mark is limited to `<= L/2`
   (mirror images are not searched twice).
4. **Bitset distance tracking**: `[]uint64`, one bit per distance, updated
   with a handful of set/clear operations per step instead of rescanning all
   `O(depth^2)` pairs.

### Multi-processing (`-mp`)

`-mp` mirrors `solve_golomb_traditional_opt_mt` in the C code: it enumerates
every valid, distinct `(pos[1], pos[2])` prefix up front (same bounds and
symmetry break as the single-threaded search) and hands them out over a
buffered channel to a pool of `runtime.NumCPU()` goroutines. Each worker runs
the identical endpoint-aware DFS from depth 3 with its own bitset and scratch
buffer (no shared state, no lock contention); the first worker to find a
ruler flips an atomic flag, and every other worker (and the producer) stops
picking up new work.

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
- **Optimized Bitset**: Ultra-fast distance checking using uint64 arrays

### Performance Optimizations
- **Endpoint-aware DFS**: ported from `src/solver_traditional_opt.c` / `nvidia/golomb_bits.h` — see Algorithm above.
- **Bitset distance tracking**: `[]uint64`, incremental set/clear instead of an `O(depth^2)` rescan per node.
- **Worker-local state**: each `-mp` goroutine owns its bitset and scratch buffer; no shared mutable state, no lock contention.

### Benchmarks (2026-09-27, same machine as the root README's CPU benchmarks)

`./golomb <n> -b`, wall clock:

| n | seconds |
|---|---------|
| 9 | 0.0002 |
| 10 | 0.002 |
| 11 | 0.015 |
| 12 | 0.80 |
| 13 | 12.4 |
| 14 (`-mp`) | 57.2 |

For reference, the previous (pre-2026-09-27) backtracking implementation
took ~19.4 s for n=12 single-threaded and did not finish n=12 `-mp` within
120 s; the endpoint-aware DFS is roughly 24x faster single-threaded at
n=12 alone, before parallelism.

### Compared to C Implementation
- **Same algorithm, different constant factor**: `./golomb 14 -mp -b` is
  ~57 s here vs. ~22 s for `../bin/golomb 14 -mp -b` (root README, same
  machine) — expect roughly 2-3x C's wall time at this `n`.
- **Startup Time**: Slightly faster startup due to no compilation step.
- **Memory Safety**: Automatic garbage collection, no manual memory management.

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
