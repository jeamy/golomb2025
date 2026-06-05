/* ==========================================================================
 * CREATIVE SOLVER — OpenMP Dynamic-Scheduled Parallel DFS
 * ==========================================================================
 *
 * Strategy:
 *   This solver parallelizes the Golomb ruler search by distributing the
 *   top-level decision (choosing the 2nd and 3rd mark positions) across
 *   threads using OpenMP's dynamic scheduling.
 *
 * How it works:
 *   1. The first mark is always at position 0.
 *   2. The second mark m2 ranges from 1 to L/2 (symmetry break).
 *   3. For each m2, the third mark m3 ranges from m2+1 up to L-(n-3)
 *      (leaving room for the remaining n-3 marks).
 *   4. Trivial duplicates are filtered: if m3 - m2 == m2 then the distances
 *      {m2, m3, m3-m2} would contain a duplicate, so we skip.
 *   5. Once m2 and m3 are fixed (with their 3 distances committed to the
 *      bitset), the remaining marks are found via recursive DFS (dfs()).
 *
 * Parallelism:
 *   - The outer loop over m2 uses `schedule(dynamic, 1)` so that threads
 *     pick up work units one at a time. This balances the load because
 *     different m2 values lead to vastly different subtree sizes.
 *   - A shared volatile `found` flag enables early termination: once any
 *     thread finds a solution, all others stop exploring.
 *
 * Trade-offs vs other solvers:
 *   - Simpler than `-mp` (no candidate sorting, no checkpointing).
 *   - Less overhead than `-d` (no recursive task spawning).
 *   - Good for medium-sized n (14-16) where subtrees are irregular.
 *
 * Falls back to single-threaded solve_golomb() if OpenMP is unavailable.
 * ========================================================================== */

#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Bitset helpers (duplicated here for compilation-unit locality). */
static inline void set_bit(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline int test_bit(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

bool solve_golomb_creative(int n, int target_length, ruler_t *out, bool verbose) {
#ifdef _OPENMP
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) return false;
    if (n <= 2) return solve_golomb(n, target_length, out, verbose);

    volatile bool found = false;
    ruler_t res_local;
    int half = target_length / 2; /* Symmetry break: m2 <= L/2 */

    /* Dynamic scheduling: each m2 value is one work unit. Threads steal
     * the next available m2 when they finish their current subtree. */
    #pragma omp parallel for schedule(dynamic, 1)
    for (int m2 = 1; m2 <= half; ++m2) {
        for (int m3 = m2 + 1; m3 <= target_length - (n - 3); ++m3) {
            if (found) break; /* Early exit: another thread found a solution */
            if (m3 - m2 == m2) continue; /* Skip trivial duplicate distance */

            /* Thread-local search state */
            int pos[MAX_MARKS] = {0};
            uint64_t dist_bs[BS_WORDS] = {0};

            pos[1] = m2;
            pos[2] = m3;
            set_bit(dist_bs, m2);        /* distance 0→m2 */
            set_bit(dist_bs, m3);        /* distance 0→m3 */
            set_bit(dist_bs, m3 - m2);   /* distance m2→m3 */

            /* Recursive DFS from depth 3 onwards */
            if (dfs(3, n, target_length, pos, dist_bs, false)) {
                #pragma omp critical
                {
                    if (!found) {
                        found = true;
                        res_local.marks = n;
                        res_local.length = target_length;
                        memcpy(res_local.pos, pos, n * sizeof(int));
                    }
                }
                break; /* This thread is done */
            }
        }
    }

    if (found) {
        *out = res_local;
        return true;
    }
    return false;
#else
    /* No OpenMP: fall back to single-threaded solver */
    return solve_golomb(n, target_length, out, verbose);
#endif
}
