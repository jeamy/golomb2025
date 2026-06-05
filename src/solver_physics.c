/* ==========================================================================
 * PHYSICS-BASED GOLOMB RULER SOLVER — Discrete Simulated Annealing
 * ==========================================================================
 *
 * Thermodynamic analogy:
 *   - Marks are "particles" on integer lattice positions 0..L.
 *   - "Energy" = number of duplicate (conflicting) pairwise distances.
 *   - Goal: reach energy = 0 (a valid Golomb ruler).
 *
 * Algorithm:
 *   Iterated Simulated Annealing with conflict-oriented neighborhood:
 *   1. Random initialization of n marks on [0, L] with fixed endpoints.
 *   2. Each SA iteration selects a CONFLICTING mark (one involved in a
 *      duplicate distance) and samples k random alternative positions.
 *   3. The best candidate is accepted or rejected via the Metropolis
 *      criterion: improvements always accepted, worsenings accepted with
 *      probability exp(-delta/T).
 *   4. Temperature is cooled geometrically (T *= 0.99995) with reheating
 *      when the search stalls.
 *   5. If a full SA run fails, the solver restarts with a fresh random
 *      configuration and tries again (indefinitely until a solution is found).
 *
 * Key design decisions:
 *   - Endpoints marks[0]=0 and marks[n-1]=L remain fixed throughout.
 *   - The neighborhood sampling size k is temperature-dependent:
 *     k = 8 + 2*T. At high T (exploration), few samples → more random.
 *     At low T (exploitation), more samples → more greedy.
 *   - Incremental conflict counting via dist_count[] avoids O(n²) per move.
 *   - A safety recount is triggered when total ≤ 0 (drift correction).
 *
 * This is NOT backtracking, NOT a SAT solver, NOT constraint programming.
 * It is a pure metaheuristic that may fail on any single run but succeeds
 * with high probability given enough restarts.
 * ========================================================================== */

#include "golomb.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif


/* ---------------------------------------------------------------------------
 * sa_count_conflicts -- Full O(n²) rebuild of the distance frequency array.
 *
 * For every pair (i,j), computes |marks[j] - marks[i]| and increments
 * dist_count[d]. Returns the total number of conflicts (distances that
 * appear more than once).
 * --------------------------------------------------------------------------- */
static int sa_count_conflicts(const int *marks, int n, int L, int *dist_count) {
    memset(dist_count, 0, (size_t)(L + 1) * sizeof(int));
    int conflicts = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d = marks[j] - marks[i];
            if (d < 0) d = -d;
            if (d <= L) {
                if (dist_count[d] > 0) conflicts++;
                dist_count[d]++;
            }
        }
    }
    return conflicts;
}

/* ---------------------------------------------------------------------------
 * sa_remove_counted -- Remove all distances involving marks[idx] from
 * dist_count. Returns the number of conflicts that were resolved by this
 * removal (i.e., distances whose count dropped from >1 to exactly 1).
 * Cost: O(n) per call.
 * --------------------------------------------------------------------------- */
static int sa_remove_counted(const int *marks, int n, int L, int *dist_count, int idx) {
    int removed = 0;
    for (int j = 0; j < n; j++) {
        if (j == idx) continue;
        int d = marks[idx] - marks[j];
        if (d < 0) d = -d;
        if (d > L) continue;
        if (dist_count[d] > 1) removed++;
        dist_count[d]--;
    }
    return removed;
}

/* ---------------------------------------------------------------------------
 * sa_add -- Insert all distances from position `pos` (at index idx) into
 * dist_count. Returns the number of NEW conflicts created (distances whose
 * count went from 1 to >1).
 * Cost: O(n) per call.
 * --------------------------------------------------------------------------- */
static int sa_add(const int *marks, int n, int L, int *dist_count, int idx, int pos) {
    int added = 0;
    for (int j = 0; j < n; j++) {
        if (j == idx) continue;
        int d = pos - marks[j];
        if (d < 0) d = -d;
        if (d <= L) {
            if (dist_count[d] > 0) added++;
            dist_count[d]++;
        }
    }
    return added;
}

/* ==========================================================================
 * PUBLIC ENTRY POINT: Discrete Simulated Annealing solver.
 *
 * Runs indefinitely (iterated restarts) until a zero-conflict configuration
 * is found, which constitutes a valid Golomb ruler of length target_length.
 * ========================================================================== */
bool solve_golomb_physics(int n, int target_length, ruler_t *out, bool verbose) {
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) {
        return false;
    }

    int L = target_length;
    volatile int found = 0;  /* Shared flag: set by first thread to find solution */
    int result_marks[MAX_MARKS];
    int result_restart = 0;

#ifdef _OPENMP
    /* Parallel restarts: each thread runs independent SA restarts with its
     * own RNG state and working memory. First thread to find a zero-conflict
     * configuration sets `found` and all others terminate. */
    #pragma omp parallel
    {
        unsigned int rng_state = (unsigned)time(NULL) ^ (unsigned)omp_get_thread_num();
#else
    {
        unsigned int rng_state = (unsigned)time(NULL);
#endif
        int *dist_count = malloc((size_t)(L + 1) * sizeof(int));
        bool *occupied = malloc((size_t)(L + 1) * sizeof(bool));
        if (!dist_count || !occupied) {
            free(dist_count); free(occupied);
            goto thread_exit;
        }

        int marks[MAX_MARKS];
        int restart = 0;

        while (!found) {
            /* Random initialization with fixed endpoints 0 and L. */
            memset(occupied, 0, (size_t)(L + 1) * sizeof(bool));
            marks[0] = 0; occupied[0] = true;
            marks[n - 1] = L; occupied[L] = true;
            for (int i = 1; i < n - 1; i++) {
                int p;
                do { p = 1 + (int)(rand_r(&rng_state) % (L - 1)); } while (occupied[p]);
                occupied[p] = true;
                marks[i] = p;
            }
            /* Sort marks (needed for consistent distance computation). */
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (marks[i] > marks[j]) { int t = marks[i]; marks[i] = marks[j]; marks[j] = t; }

            int total = sa_count_conflicts(marks, n, L, dist_count);
            if (total == 0) {
                goto sa_done;
            }

            /* Simulated Annealing with conflict-oriented neighborhood. */
            double temp = 2.0;
            double cooling = 0.99995;
            double min_temp = 0.005;
            int sa_iters = 600 * n * n;
            int stall = 0;

            for (int iter = 0; iter < sa_iters && !found; iter++) {

                /* Step 1: Identify conflicting inner marks. */
                int conflicting[MAX_MARKS];
                int nc = 0;
                for (int i = 1; i < n - 1; i++) {
                    for (int j = 0; j < n; j++) {
                        if (j == i) continue;
                        int d = marks[i] - marks[j];
                        if (d < 0) d = -d;
                        if (d <= L && dist_count[d] > 1) { conflicting[nc++] = i; break; }
                    }
                }
                if (nc == 0) break; /* Solution found */

                /* Step 2: Randomly select one conflicting mark. */
                int idx = conflicting[rand_r(&rng_state) % nc];

                /* Remove selected mark's distances. */
                int old_pos = marks[idx];
                int original_total = total;
                int removed = sa_remove_counted(marks, n, L, dist_count, idx);
                int base = total - removed;
                occupied[old_pos] = false;

                /* Step 3: Sample k random free positions. */
                int k_samples = 8 + (int)(temp * 2);
                if (k_samples > L - 2) k_samples = L - 2;
                int best_cand_pos = -1;
                int best_cand_added = 1 << 30;
                for (int s = 0; s < k_samples; s++) {
                    int p = 1 + (int)(rand_r(&rng_state) % (L - 1));
                    if (occupied[p]) continue;
                    int cand_added = 0;
                    for (int j = 0; j < n; j++) {
                        if (j == idx) continue;
                        int d = p - marks[j];
                        if (d < 0) d = -d;
                        if (d <= L && dist_count[d] > 0) cand_added++;
                    }
                    if (cand_added < best_cand_added) {
                        best_cand_added = cand_added;
                        best_cand_pos = p;
                    }
                }

                if (best_cand_pos < 0) {
                    occupied[old_pos] = true;
                    sa_add(marks, n, L, dist_count, idx, old_pos);
                    total = original_total;
                    continue;
                }

                /* Step 4: Insert candidate, compute exact delta. */
                marks[idx] = best_cand_pos;
                occupied[best_cand_pos] = true;
                int actual_added = sa_add(marks, n, L, dist_count, idx, best_cand_pos);
                int new_total = base + actual_added;
                int delta_conf = new_total - original_total;

                /* Step 5: Metropolis acceptance. */
                double r = (double)rand_r(&rng_state) / RAND_MAX;
                if (delta_conf <= 0 || r < exp(-(double)delta_conf / temp)) {
                    total = new_total;
                    stall = 0;
                } else {
                    sa_remove_counted(marks, n, L, dist_count, idx);
                    marks[idx] = old_pos;
                    occupied[best_cand_pos] = false;
                    occupied[old_pos] = true;
                    sa_add(marks, n, L, dist_count, idx, old_pos);
                    total = original_total;
                    stall++;
                }

                /* Safety recount. */
                if (total <= 0) {
                    total = sa_count_conflicts(marks, n, L, dist_count);
                    if (total == 0) break;
                }

                /* Reheating on stagnation. */
                if (stall > 4 * n) {
                    temp = 2.0;
                    stall = 0;
                }

                temp *= cooling;
                if (temp < min_temp) temp = min_temp;
            }

            if (total == 0) {
            sa_done:
                /* This thread found a solution. Atomically claim the result. */
                ;
                int old_found = 0;
#ifdef _OPENMP
                #pragma omp atomic capture
                { old_found = found; found = 1; }
#else
                old_found = found; found = 1;
#endif
                if (old_found == 0) {
                    /* Sort and store result. */
                    for (int i = 0; i < n; i++)
                        for (int j = i + 1; j < n; j++)
                            if (marks[i] > marks[j]) { int t = marks[i]; marks[i] = marks[j]; marks[j] = t; }
                    memcpy(result_marks, marks, n * sizeof(int));
                    result_restart = restart;
                }
            }

            restart++;
        }

        free(dist_count);
        free(occupied);
    thread_exit: ;
    } /* end parallel / sequential block */

    if (found) {
        out->marks = n;
        out->length = result_marks[n - 1];
        memcpy(out->pos, result_marks, n * sizeof(int));
        if (verbose) {
            printf("[PHYSICS] Valid solution found after %d restarts, length=%d\n",
                   result_restart, out->length);
        }
        return true;
    }

    if (verbose) {
        printf("[PHYSICS] No valid solution found.\n");
    }
    return false;
}

/* Legacy alias: the "hybrid" entry point simply delegates to the SA solver.
 * No DFS fallback is used -- this keeps the solver honest as a pure
 * physics-inspired metaheuristic. */
bool solve_golomb_hybrid_physics(int n, int target_length, ruler_t *out, bool verbose) {
    return solve_golomb_physics(n, target_length, out, verbose);
}
