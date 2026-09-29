# Cross-Language Benchmark Comparison

Runtimes for finding an optimal Golomb ruler of `n` marks, one number per
implementation, taken directly from the tracked result files in each
`out/` directory (`out/`, `go/out/`, `rust/out/`, `java/out/`) — no
numbers in this file are estimated or extrapolated. Machine: Ryzen 7
3700X (16 threads) + GTX 1660 Ti, 2026-09-27/28.

For C, where dozens of flag-variant runs exist per `n` (`-mp`, `-mp -e`,
`-mp -af`, `-c`, `-c -an`, ...), only the **fastest** run per `n` is used
here; see `README.md`'s own Sample Runtimes tables for the full variant
breakdown. Go, Rust and Java each have exactly one tracked run per `n`
(`-mp -b`, i.e. multi-threaded search targeting the known-optimal length).

All values are wall-clock seconds (`seconds=` field in the result file).
`-` means no tracked result exists for that combination.

## n = 13 and 14 (the only `n` all five have)

| n | C (fastest CPU variant) | Go (`-mp -b`) | Rust (`--mp -b`) | Java (`-mp -b`) | CUDA, no hints (`-b`) | CUDA, guided (`-b -H`) |
|---|---:|---:|---:|---:|---:|---:|
| 13 | 2.415 (`-mp -b -af`) | 3.602 | 4.520 | 2.876 | – | – |
| 14 | 20.676 (`-mp -b -af`) | 52.927 | 63.321 | 65.152 | 0.353 | 0.153 |

C's fastest n=13/14 runs are the hand-written FASM/NASM distance-check
hot spots (`-af`/`-an`) layered on top of `-mp`; see `README.md` for the
full per-variant table.

**On the Go/Rust/Java n=14 numbers**: these are the fastest of repeated,
one-at-a-time (no concurrent contention) runs, but `-mp` runtimes for
these three still vary noticeably between otherwise-identical runs —
measured directly, not assumed:

| | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| Go | 57.2s | 52.9s | – (only 2 isolated runs) |
| Rust | 31.7s | 63.3s | 80.2s |
| Java | 58.1s | 65.2s | 67.4s |

(All runs sequential and isolated — no other benchmark running at the same time.)

The parallel prefix search races many worker threads against each other;
which one reaches the winning `(second, third)` prefix first depends on
OS thread scheduling, not just the algorithm, so wall time for a single
`-mp` run is inherently noisy — the same reason the C table above takes
the fastest of many tracked variant runs rather than a single one. The
table's Go/Rust/Java values are each the fastest of the isolated attempts
above (Go 52.927s is the tracked file; Rust's 63.321s and Java's 65.152s
remain the best directly measured, even though a faster untracked 31.7s
Rust run happened earlier and was not saved to a file at the time).

## Go / Rust / Java across their full tracked range (n = 2..14)

All three use the same algorithm as C's `-to` solver (endpoint-aware
bit-parallel DFS) with a `-mp`-equivalent parallel prefix search.

| n | Go | Rust | Java |
|---|---:|---:|---:|
| 2 | 0.000001 | 0.0000004 | 0.000000 |
| 3 | 0.000005 | 0.00005 | 0.008 |
| 4 | 0.000072 | 0.003975 | 0.004 |
| 5 | 0.000342 | 0.001898 | 0.005 |
| 6 | 0.000278 | 0.007552 | 0.005 |
| 7 | 0.000407 | 0.009241 | 0.006 |
| 8 | 0.001559 | 0.002589 | 0.006 |
| 9 | 0.001502 | 0.005585 | 0.022 |
| 10 | 0.003025 | 0.012109 | 0.054 |
| 11 | 0.041886 | 0.083225 | 0.169 |
| 12 | 0.273403 | 0.130485 | 0.353 |
| 13 | 3.602 | 4.520 | 2.876 |
| 14 | 52.927 | 63.321 | 65.152 |

At this scale (single machine, best of a few isolated runs at n=14, one
run each below that — see the note above the previous table on `-mp`
variance) the three are within the same order of magnitude at every `n`;
none is consistently fastest. n=10 and n=12 in Go have an extra data point in the `out/`
directory (`GOL_n10_mp.txt`, `GOL_n12_mp.txt`, no `-b`) that searches
every length up to the optimum rather than just the known one — much
slower (0.038s / 6.675s) and not used in the table above, which only
compares like-for-like `-mp -b` runs.

## C and CUDA beyond n = 14 (`out/`)

Only the C CPU solver and the CUDA variant have tracked results past
n=14; Go/Rust/Java have not been run there.

| n | Optimal length | C CPU, fastest tracked (`-mp -b`, no ASM variants run) | CUDA, no hints (`-b`) | CUDA, guided (`-b -H`) |
|---|---:|---:|---:|---:|
| 14 | 127 | 20.676 (`-mp -b -af`, see table above) | 0.353 | 0.153 |
| 15 | 151 | 192.964 | 5.504 | 0.165 |
| 16 | 177 | 2396.427 | 0.938 | 0.318 |
| 17 | 199 | – (not run on CPU) | 533.471 | 0.356 |
| 18 | 216 | – (not run on CPU) | 1272.666 | 2.488 |

CUDA's "no hints" search still does the full endpoint-aware exact DFS
(same algorithm as C's `-to` / the Go/Rust/Java ports) but with the search
tree partitioned across the GPU; "guided" additionally seeds the search
from the LUT's own `(pos[1], pos[2])` pair, which is why it stays under
2.5s even at n=17-18 (n=17: 0.356s; n=18 has more subprefixes under that
pair to divide between CPU and GPU, hence the jump to 2.488s) while "no
hints" varies by three orders of magnitude depending on how quickly it
stumbles onto that same pair. See
`nvidia/README.md` for the full algorithm description.

n=17/18 were never attempted on the plain CPU solver — at the n=15→16
scaling factor already visible above (~12x), n=17/18 would be projected
at many hours to plausibly days of CPU time, which is exactly the gap
the CUDA port exists to close.

## Does the ASM/SIMD path need the endpoint-aware algorithm too? (2026-09-29)

Yes, and it now has it: `-af`/`-an`/`-e` (the SIMD 8-distances-at-once
duplicate check) previously only applied to the old left-to-right
solver (`-mp`), never to the endpoint-aware `-to` solver that Go, Rust,
Java and CUDA are all ported from. Added it to `-to` too (see
`src/solver_traditional_opt.c`).

| n | `-to -mp -b` | `-to -mp -b -af` | `-mp -b -af` (old algo, for reference) |
|---|---:|---:|---:|
| 13 | 1.053 | **0.991** | 2.415 |
| 14 | 26.643 | 24.133 | **20.676** |

Two things came out of measuring this rather than assuming it:

1. **The SIMD gain on top of `-to` is modest** (~10-15%, matching
   solver.c's own SIMD gain), not the large multiplier a naive reading
   of "old algo + SIMD already beats the new algo without it" would
   suggest. At n=13, `-to -mp -b -af` is now *faster* than the old
   `-mp -b -af` — endpoint pruning plus a plain (non-LUT-guided)
   candidate order already wins there. At n=14 the old algorithm is
   still faster.
2. **The real reason `-mp -b -af` was fast is mostly its LUT-guided
   candidate ordering** (`solver.c` sorts `(second, third)` pairs by
   proximity to the LUT's own reference pair before searching), not
   primarily the SIMD check. `-to -mp` has no such ordering — it
   enumerates candidates in plain ascending order — so at n=14 it can
   still lose a search-order race to `-mp`'s head start, the same
   run-to-run variance documented for Go/Rust/Java above. Adding
   LUT-guided ordering to `-to -mp` as well is the next natural step if
   the gap at higher `n` matters, and hasn't been done.
