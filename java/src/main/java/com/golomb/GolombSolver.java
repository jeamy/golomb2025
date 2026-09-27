package com.golomb;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Solver for finding optimal Golomb rulers.
 *
 * <p>The search engine is a direct port of the C solver's endpoint-aware DFS
 * (src/solver_traditional_opt.c, the {@code -to} solver) and the technique
 * used by the CUDA variant (nvidia/golomb_bits.h): both ruler endpoints
 * ({@code 0} and {@code length}) are fixed before any inner mark is placed,
 * so every candidate mark's distance to the fixed right endpoint is checked
 * immediately instead of only once all marks are placed. The previous
 * version here placed marks left-to-right and only checked
 * {@code positions[marks - 1] == maxLength} at the deepest recursion level,
 * missing this pruning entirely.
 *
 * <p>Getting the parallel path (n=14, {@code -mp -b}, this machine) from the
 * pre-port baseline to Go/Rust parity took three iterations, not one:
 * <pre>
 *   before (left-to-right, no endpoint pruning): n=12 0.8s, n=13 23.0s (n=14 not run: too slow)
 *   endpoint DFS, still "second"-only parallel:  n=14 291s
 *   + flat candidate list, parallelStream()::findAny():  n=14 363s  (WORSE - see below)
 *   + preallocated scratch buffer (no per-node alloc):    n=14 373s  (no real change)
 *   + long[] bitset instead of java.util.BitSet:          n=14 290s (~22%, not the bottleneck)
 *   + AtomicInteger cursor, explicit worker threads:      n=14  58s (this fixed it)
 * </pre>
 * The single-threaded number was the tell: {@code -s -b} at n=13 was already
 * at parity with Go/Rust (~13s) once the buffer and bitset changes landed, so
 * the remaining n=14 gap could not be per-node cost — it had to be dispatch
 * order. {@code parallelStream().findAny()} splits the candidate array into
 * large contiguous leaves and runs each leaf's candidates strictly in order
 * on whichever thread claims it; since the winning {@code (second, third)}
 * prefix is essentially always near the front of the enumeration (low pairs
 * are both cheaper to reach and more likely to succeed, by construction of
 * the DFS bounds and symmetry break), one thread ends up serially grinding
 * through everything ahead of it in its leaf while every other thread sits
 * idle on leaves that contain no solution. An explicit {@link
 * java.util.concurrent.atomic.AtomicInteger} cursor (see {@link
 * #solveParallel}) lets every idle worker immediately pull the next
 * unclaimed candidate instead, which is what Go's channel and C's
 * {@code schedule(dynamic, 16)} already do.
 */
public class GolombSolver {

    private static final int MAX_MARKS = 32;
    private static final int MAX_LENGTH = 600;

    private final boolean verbose;
    private final boolean useParallel;
    private final AtomicBoolean cancelled = new AtomicBoolean(false);

    public GolombSolver(boolean verbose, boolean useParallel) {
        this.verbose = verbose;
        this.useParallel = useParallel;
    }

    /**
     * Attempts to find a Golomb ruler with the given number of marks and maximum length.
     */
    public GolombRuler solve(int marks, int maxLength) {
        if (marks < 2 || marks > MAX_MARKS) {
            throw new IllegalArgumentException("Marks must be between 2 and " + MAX_MARKS);
        }
        if (maxLength > MAX_LENGTH) {
            throw new IllegalArgumentException("Max length cannot exceed " + MAX_LENGTH);
        }

        cancelled.set(false);

        if (useParallel && marks > 3) {
            return solveParallel(marks, maxLength);
        } else {
            return solveSingle(marks, maxLength);
        }
    }

    private GolombRuler solveSingle(int marks, int maxLength) {
        int[] positions = new int[marks];
        long[] distanceSet = newBitset(maxLength);
        int[] buf = new int[marks * marks];

        positions[0] = 0;
        positions[marks - 1] = maxLength;
        setBit(distanceSet, maxLength);

        if (dfsEndpoint(1, marks, maxLength, positions, distanceSet, buf)) {
            return new GolombRuler(maxLength, marks, positions.clone());
        }

        return null;
    }

    /**
     * Enumerates every valid, distinct {@code (positions[1], positions[2])} prefix
     * up front and hands them out, in index order, to a fixed pool of
     * {@code availableProcessors()} worker threads pulling from a shared
     * {@link java.util.concurrent.atomic.AtomicInteger} cursor — mirroring
     * {@code solve_golomb_traditional_opt_mt} in the C code (same bounds:
     * {@code t <= tMax}, {@code s <= min(length/2, tMax - 1)}) and the Go/Rust
     * ports' worker-pull structure. See the class Javadoc for why the cursor
     * (rather than {@code parallelStream().findAny()}) was necessary.
     */
    private GolombRuler solveParallel(int marks, int maxLength) {
        int tMax = maxLength - (marks - 3);
        int secondMax = Math.min(maxLength / 2, tMax - 1);
        if (secondMax < 1) {
            return null;
        }

        java.util.List<int[]> candidateList = new java.util.ArrayList<>();
        for (int second = 1; second <= secondMax; second++) {
            for (int third = second + 1; third <= tMax; third++) {
                if (third - second == second) continue; // initial distances must differ
                candidateList.add(new int[] {second, third});
            }
        }
        if (candidateList.isEmpty()) {
            return null;
        }
        int[][] candidates = candidateList.toArray(new int[0][]);

        java.util.concurrent.atomic.AtomicInteger cursor = new java.util.concurrent.atomic.AtomicInteger(0);
        java.util.concurrent.atomic.AtomicReference<GolombRuler> found = new java.util.concurrent.atomic.AtomicReference<>();
        int numWorkers = Math.min(Runtime.getRuntime().availableProcessors(), candidates.length);

        Thread[] workers = new Thread[numWorkers];
        for (int w = 0; w < numWorkers; w++) {
            workers[w] = new Thread(() -> {
                int i;
                while (found.get() == null && !cancelled.get() && (i = cursor.getAndIncrement()) < candidates.length) {
                    int[] pair = candidates[i];
                    GolombRuler ruler = tryPrefix(marks, maxLength, pair[0], pair[1]);
                    if (ruler != null) {
                        found.compareAndSet(null, ruler);
                        return;
                    }
                }
            });
            workers[w].start();
        }
        for (Thread t : workers) {
            try {
                t.join();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        return found.get();
    }

    /**
     * Completes one {@code (second, third)} prefix with {@link #dfsEndpoint},
     * returning the ruler on success or {@code null} otherwise.
     */
    private GolombRuler tryPrefix(int marks, int maxLength, int second, int third) {
        int[] positions = new int[marks];
        long[] distanceSet = newBitset(maxLength);
        int[] buf = new int[marks * marks];

        positions[0] = 0;
        positions[1] = second;
        positions[2] = third;
        positions[marks - 1] = maxLength;

        int[] initial = {maxLength, second, third, third - second,
                          maxLength - second, maxLength - third};
        for (int d : initial) {
            if (testBit(distanceSet, d)) {
                return null;
            }
            setBit(distanceSet, d);
        }

        if (dfsEndpoint(3, marks, maxLength, positions, distanceSet, buf)) {
            return new GolombRuler(maxLength, marks, positions.clone());
        }
        return null;
    }

    /**
     * Recursive endpoint-aware DFS placing inner marks {@code positions[depth..marks-2]},
     * given that {@code positions[0] = 0} and {@code positions[marks-1] = length} are
     * already fixed and every distance implied by {@code positions[0..depth-1]} (plus
     * the endpoint distances they imply) is already set in {@code distanceSet}.
     */
    private boolean dfsEndpoint(int depth, int marks, int length, int[] positions, long[] distanceSet, int[] buf) {
        if (cancelled.get()) return false;

        // All inner marks placed -> the ruler is complete and valid.
        if (depth == marks - 1) {
            return true;
        }

        int last = positions[depth - 1];

        // After placing `next`, (marks-2-depth) more inner marks plus the endpoint
        // still need at least 1 apart each: next + (marks-1-depth) <= length.
        int maxNext = length - (marks - 1 - depth);

        // Symmetry break on the first inner mark: at most length/2 (eliminates
        // the mirror image of every ruler).
        if (depth == 1) {
            int limit = Math.max(length / 2, last + 1);
            if (maxNext > limit) {
                maxNext = limit;
            }
        }

        // buf[depth * marks .. depth * marks + depth) holds this level's new
        // left-side distances; it is a slice of a buffer allocated once per
        // top-level search (see solveSingle/tryPrefix) so descending into the
        // recursion never allocates on the hot path.
        int base = depth * marks;

        for (int next = last + 1; next <= maxNext; next++) {
            if (cancelled.get()) return false;

            int gap = next - last;
            if (testBit(distanceSet, gap)) continue;

            // Endpoint-aware pruning: check the distance to the fixed right
            // endpoint immediately, instead of only once all marks are placed.
            int dEnd = length - next;
            if (testBit(distanceSet, dEnd)) continue;

            boolean ok = true;
            for (int i = 0; i < depth; i++) {
                int d = next - positions[i];
                if (testBit(distanceSet, d)) {
                    ok = false;
                    break;
                }
                buf[base + i] = d;
            }
            if (!ok) continue;

            // Intra-step collision: a left-side distance introduced in this same
            // step might equal dEnd; the bitset alone can't catch that.
            boolean clash = false;
            for (int i = 0; i < depth; i++) {
                if (buf[base + i] == dEnd) {
                    clash = true;
                    break;
                }
            }
            if (clash) continue;

            positions[depth] = next;
            for (int i = 0; i < depth; i++) setBit(distanceSet, buf[base + i]);
            setBit(distanceSet, dEnd);

            if (verbose && depth <= 2) {
                System.out.printf("Trying depth %d, position %d%n", depth, next);
            }

            if (dfsEndpoint(depth + 1, marks, length, positions, distanceSet, buf)) {
                return true;
            }

            for (int i = 0; i < depth; i++) clearBit(distanceSet, buf[base + i]);
            clearBit(distanceSet, dEnd);
        }

        return false;
    }

    /**
     * Cancels the current search operation.
     */
    public void cancel() {
        cancelled.set(true);
    }

    // -------------------------------------------------------------------
    // Bitset helpers: one bit per distance value, exactly as in
    // src/solver_traditional_opt.c and nvidia/golomb_bits.h. A raw long[]
    // avoids java.util.BitSet's bounds-checked method calls. This was a
    // modest win on its own (~22%, see the class Javadoc for the measured
    // progression) — the real n=14 fix was the dispatch order in
    // solveParallel, not per-node cost.
    // -------------------------------------------------------------------

    private static long[] newBitset(int maxVal) {
        return new long[maxVal / 64 + 1];
    }

    private static void setBit(long[] bs, int idx) {
        bs[idx >> 6] |= 1L << (idx & 63);
    }

    private static void clearBit(long[] bs, int idx) {
        bs[idx >> 6] &= ~(1L << (idx & 63));
    }

    private static boolean testBit(long[] bs, int idx) {
        return (bs[idx >> 6] & (1L << (idx & 63))) != 0;
    }
}
