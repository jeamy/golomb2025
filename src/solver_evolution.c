/* ==========================================================================
 * EVOLUTIONARY GOLOMB RULER SOLVER — Iterated Min-Conflicts with Crossover
 * ==========================================================================
 *
 * Overview:
 *   A memetic algorithm combining stochastic local search (min-conflicts
 *   heuristic) with periodic crossover-based diversification. The solver
 *   treats the Golomb ruler problem as a Constraint Satisfaction Problem
 *   (CSP) where the constraint is that all pairwise distances must be unique.
 *
 * Architecture:
 *   Outer loop (iterated restarts, runs until solution found):
 *     - Every 3rd restart: seed the next run via distance-aware crossover
 *       between the best-seen configuration and a fresh random individual.
 *     - Otherwise: pure random initialization.
 *
 *   Inner loop (min-conflicts local search, budget = 800*n iterations):
 *     - Select a conflicting mark (one involved in a duplicate distance).
 *     - Scan ALL free positions and pick the one minimizing new conflicts.
 *     - Use incremental dist_count[] updates (O(n) per move, not O(n²)).
 *     - Escape plateaus via random walk; escape stagnation via full restart.
 *
 * Key Features:
 *   - Fixed endpoints: marks[0] = 0, marks[n-1] = L throughout.
 *   - Incremental conflict tracking: dist_count[d] stores how many pairs
 *     produce distance d. A conflict exists when dist_count[d] > 1.
 *   - Self-collision detection: when evaluating a candidate position, the
 *     `seen_this[]` array detects if two NEW distances from the same mark
 *     would collide with each other (not just with existing distances).
 *   - Safety recount: if incremental total drifts to ≤ 0, a full O(n²)
 *     rebuild is triggered to correct any accumulated errors.
 *
 * This is NOT backtracking, NOT constraint propagation, NOT a SAT solver.
 * It is a stochastic incomplete method that may require multiple restarts.
 * ========================================================================== */

#include "golomb.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Thread-local RNG state for thread-safe parallel restarts. */
static __thread unsigned int evo_rng_state;

static inline void evo_srand(unsigned int seed) { evo_rng_state = seed; }
static inline int evo_rand(void) {
    /* Same LCG as glibc rand_r for portability. */
    evo_rng_state = evo_rng_state * 1103515245 + 12345;
    return (int)((evo_rng_state >> 16) & 0x7fff);
}
/* Uniform int in [0, max-1]. Avoid modulo bias for small ranges. */
static inline int evo_rand_range(int max) {
    return evo_rand() % max;
}

/* An individual represents a candidate Golomb ruler configuration.
 * - marks[]: the n mark positions (always kept sorted after local search).
 * - conflicts: number of duplicate pairwise distances (0 = valid ruler).
 * - fitness: evaluation score (higher = better; used for crossover selection).
 * - valid: true iff conflicts == 0 (a proper Golomb ruler). */
typedef struct {
    int marks[MAX_MARKS];
    int n;
    double fitness;
    int conflicts;
    int length;
    bool valid;
} individual_t;

/* ---------------------------------------------------------------------------
 * count_conflicts -- Count the number of duplicate pairwise distances.
 *
 * Uses a boolean seen[] array: for each distance d, if it was already seen
 * from a previous pair, increment the conflict counter.
 * Cost: O(n²).
 * --------------------------------------------------------------------------- */
static int count_conflicts(const individual_t *ind) {
    bool seen[MAX_LEN_BITSET + 1] = {false};
    int conflicts = 0;
    
    for (int i = 0; i < ind->n; i++) {
        for (int j = i + 1; j < ind->n; j++) {
            int d = abs(ind->marks[j] - ind->marks[i]);
            if (d > MAX_LEN_BITSET) continue;
            if (seen[d]) {
                conflicts++;
            } else {
                seen[d] = true;
            }
        }
    }
    return conflicts;
}

/* ---------------------------------------------------------------------------
 * evaluate_fitness -- Compute fitness score for an individual.
 *
 * Valid rulers (0 conflicts) receive a high positive fitness rewarding
 * shorter length. Invalid rulers are penalized proportionally to their
 * conflict count. This score is used to select the "primary parent" in
 * crossover operations.
 * --------------------------------------------------------------------------- */
static void evaluate_fitness(individual_t *ind, int target_len) {
    ind->conflicts = count_conflicts(ind);
    
    if (ind->n > 0) {
        int max_pos = 0;
        for (int i = 0; i < ind->n; i++) {
            if (ind->marks[i] > max_pos) max_pos = ind->marks[i];
        }
        ind->length = max_pos;
    } else {
        ind->length = 0;
    }
    
    ind->valid = (ind->conflicts == 0);
    
    if (ind->valid) {
        double length_ratio = (double)ind->length / target_len;
        ind->fitness = 1000.0 / (1.0 + length_ratio);
    } else {
        double conflict_penalty = ind->conflicts * 100.0;
        double length_bonus = (target_len - ind->length) * 0.1;
        if (length_bonus < 0) length_bonus = 0;
        ind->fitness = length_bonus - conflict_penalty;
        if (ind->fitness < -10000) ind->fitness = -10000;
    }
}

/* ---------------------------------------------------------------------------
 * init_individual -- "Smart Random" initialization.
 *
 * Places marks around evenly-spaced ideal positions with ±15% gaussian noise.
 * Endpoints are fixed at 0 and target_len. After placement, marks are sorted
 * and any duplicates are resolved by shifting forward.
 *
 * This initialization tends to produce configurations with fewer initial
 * conflicts than purely uniform random placement, giving the local search
 * a head start.
 * --------------------------------------------------------------------------- */
static void init_individual(individual_t *ind, int n, int target_len) {
    ind->n = n;
    
    ind->marks[0] = 0;
    ind->marks[n-1] = target_len;
    
    /* Inner marks: perturb around evenly-spaced ideal positions. */
    for (int i = 1; i < n - 1; i++) {
        double ideal = (double)i * target_len / (n - 1);
        double variance = target_len * 0.15;
        double noise = ((double)evo_rand() / 0x7fff - 0.5) * 2.0 * variance;
        int pos = (int)(ideal + noise);
        
        if (pos < 1) pos = 1;
        if (pos > target_len - 1) pos = target_len - 1;
        
        ind->marks[i] = pos;
    }
    
    /* Sort into ascending order. */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (ind->marks[i] > ind->marks[j]) {
                int tmp = ind->marks[i];
                ind->marks[i] = ind->marks[j];
                ind->marks[j] = tmp;
            }
        }
    }
    
    /* Resolve duplicate positions by shifting forward. */
    for (int i = 1; i < n; i++) {
        if (ind->marks[i] <= ind->marks[i-1]) {
            ind->marks[i] = ind->marks[i-1] + 1;
        }
    }
    
    /* Ensure last mark is exactly at target_len. */
    ind->marks[n-1] = target_len;
}

/* ---------------------------------------------------------------------------
 * distance_aware_crossover -- Combine two parent rulers into a child.
 *
 * Strategy:
 *   1. Compute the set of unique distances each parent produces.
 *   2. The parent with more unique distances is designated "primary" (it
 *      contributes better distance diversity).
 *   3. For each mark position, inherit from the primary with 60% probability
 *      and from the secondary with 40%.
 *   4. Sort and repair duplicate positions.
 *
 * This crossover is used as a diversification mechanism every 3rd restart:
 * it injects structural information from the best-seen solution into the
 * initial configuration of the next local search run.
 * --------------------------------------------------------------------------- */
static void distance_aware_crossover(const individual_t *p1, const individual_t *p2,
                                      individual_t *child, int n) {
    child->n = n;
    
    /* Collect the distance sets produced by each parent. */
    bool p1_dists[MAX_LEN_BITSET + 1] = {false};
    bool p2_dists[MAX_LEN_BITSET + 1] = {false};
    
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d1 = abs(p1->marks[j] - p1->marks[i]);
            int d2 = abs(p2->marks[j] - p2->marks[i]);
            if (d1 <= MAX_LEN_BITSET) p1_dists[d1] = true;
            if (d2 <= MAX_LEN_BITSET) p2_dists[d2] = true;
        }
    }
    
    /* Count distances unique to each parent. */
    int unique_in_p1 = 0, unique_in_p2 = 0;
    for (int d = 1; d <= MAX_LEN_BITSET && d < 1000; d++) {
        if (p1_dists[d] && !p2_dists[d]) unique_in_p1++;
        else if (!p1_dists[d] && p2_dists[d]) unique_in_p2++;
    }
    
    /* Primary parent = the one with greater distance diversity. */
    const individual_t *primary = (unique_in_p1 >= unique_in_p2) ? p1 : p2;
    const individual_t *secondary = (unique_in_p1 >= unique_in_p2) ? p2 : p1;
    
    /* Biased uniform crossover: 60% from primary, 40% from secondary. */
    for (int i = 0; i < n; i++) {
        double bias = 0.6;
        if (evo_rand_range(100) < (int)(bias * 100)) {
            child->marks[i] = primary->marks[i];
        } else {
            child->marks[i] = secondary->marks[i];
        }
    }
    
    /* Repair: sort and resolve duplicate positions. */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (child->marks[i] > child->marks[j]) {
                int tmp = child->marks[i];
                child->marks[i] = child->marks[j];
                child->marks[j] = tmp;
            }
        }
    }
    
    for (int i = 1; i < n; i++) {
        if (child->marks[i] <= child->marks[i-1]) {
            child->marks[i] = child->marks[i-1] + 1;
        }
    }
}

/* ==========================================================================
 * LOCAL SEARCH: Min-Conflicts Heuristic (CSP-style, NO backtracking)
 * ==========================================================================
 *
 * The classic min-conflicts heuristic (known from N-Queens):
 *   1. Select a variable (mark) that participates in a constraint violation
 *      (a duplicate distance).
 *   2. Move it to the value (position) that minimizes the total violations.
 *   3. Repeat until solved or iteration budget exhausted.
 *
 * Endpoints marks[0]=0 and marks[n-1]=L remain fixed; only the n-2 inner
 * marks are optimized. This is a standalone local search, not DFS.
 *
 * Incremental updates:
 *   dist_count[] is maintained persistently. When a mark moves, only the
 *   n-1 distances involving that mark are removed and re-added.
 *   Cost per iteration: O(n + L) for the best-move scan, O(n) for the
 *   incremental update. Total per iteration: O(n*L) for the full scan.
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 * mc_build_full -- Full O(n²) rebuild of dist_count[] and conflict total.
 *
 * Called at initialization and when incremental tracking might have drifted.
 * --------------------------------------------------------------------------- */
static int mc_build_full(const int *marks, int n, int L, int *dist_count) {
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
 * mc_remove_mark -- Remove all distances from marks[idx] to every other mark.
 *
 * Decrements dist_count[d] for each such distance d. Returns the number of
 * conflicts that were resolved (i.e., dist_count[d] dropped from >1 to 1).
 * Cost: O(n).
 * --------------------------------------------------------------------------- */
static int mc_remove_mark(const int *marks, int n, int L,
                          int *dist_count, int idx) {
    int removed = 0;
    for (int j = 0; j < n; j++) {
        if (j == idx) continue;
        int d = marks[idx] - marks[j];
        if (d < 0) d = -d;
        if (d > L) continue;
        if (dist_count[d] > 1) removed++;  /* vorher Konflikt -> einer weniger */
        dist_count[d]--;
    }
    return removed;
}

/* ---------------------------------------------------------------------------
 * mc_add_mark -- Insert all distances from `pos` (at index idx) into dist_count.
 *
 * Increments dist_count[d] for each distance d from pos to marks[j] (j≠idx).
 * Returns the number of NEW conflicts created (dist_count[d] went from 1 to >1).
 * Cost: O(n).
 * --------------------------------------------------------------------------- */
static int mc_add_mark(const int *marks, int n, int L,
                       int *dist_count, int idx, int pos) {
    int added = 0;
    for (int j = 0; j < n; j++) {
        if (j == idx) continue;
        int d = pos - marks[j];
        if (d < 0) d = -d;
        if (d > L) continue;
        if (dist_count[d] > 0) added++;
        dist_count[d]++;
    }
    return added;
}

/* ---------------------------------------------------------------------------
 * min_conflicts_search -- Core local search engine.
 *
 * Attempts to find a zero-conflict configuration by iteratively moving
 * conflicting marks to their best positions. Returns true if a valid
 * Golomb ruler is found within max_iters iterations.
 *
 * The search combines:
 *   - Greedy best-move selection (scan all L-2 free positions per step)
 *   - Random walk for plateau escape (after 12 stuck iterations)
 *   - Full randomization restart for stagnation (after 4*n no-improvement)
 * --------------------------------------------------------------------------- */
static bool min_conflicts_search(individual_t *ind, int n, int L, int max_iters) {
    if (n < 2) return false;
    ind->marks[0] = 0;
    ind->marks[n - 1] = L;

    int *dist_count = malloc((size_t)(L + 1) * sizeof(int));
    bool *occupied = malloc((size_t)(L + 1) * sizeof(bool));
    int *seen_this = calloc((size_t)(L + 1), sizeof(int));
    int touched[MAX_MARKS];
    if (!dist_count || !occupied || !seen_this) {
        free(dist_count); free(occupied); free(seen_this); return false;
    }

    int total_conflicts = mc_build_full(ind->marks, n, L, dist_count);

    bool solved = false;
    int plateau = 0;        /* Consecutive iterations where best move = no-op */
    int best_conf = total_conflicts;
    int stall = 0;          /* Iterations without improvement */
    int restart_after = 4 * n; /* Stagnation threshold for internal restart */

    for (int iter = 0; iter < max_iters; iter++) {
        /* Safety: correct potential drift in incremental conflict counting.
         * If total somehow becomes <= 0 without a true solution, rebuild. */
        if (total_conflicts <= 0) {
            total_conflicts = mc_build_full(ind->marks, n, L, dist_count);
            if (total_conflicts == 0) { solved = true; break; }
        }

        /* Stagnation detection: if no improvement for restart_after iterations,
         * randomize all inner marks and start fresh (internal restart). */
        if (total_conflicts < best_conf) { best_conf = total_conflicts; stall = 0; }
        else if (++stall > restart_after) {
            /* Randomize all inner marks (keep endpoints fixed). */
            memset(occupied, 0, (size_t)(L + 1) * sizeof(bool));
            occupied[0] = occupied[L] = true;
            for (int i = 1; i < n - 1; i++) {
                int p;
                do { p = 1 + evo_rand_range(L - 1); } while (occupied[p]);
                occupied[p] = true;
                ind->marks[i] = p;
            }
            total_conflicts = mc_build_full(ind->marks, n, L, dist_count);
            best_conf = total_conflicts; stall = 0; plateau = 0;
            continue;
        }

        /* Variable selection: collect all inner marks involved in at least
         * one conflict (a distance d where dist_count[d] > 1). */
        int conflicting[MAX_MARKS];
        int nc = 0;
        for (int i = 1; i < n - 1; i++) {
            bool part = false;
            for (int j = 0; j < n && !part; j++) {
                if (j == i) continue;
                int d = ind->marks[i] - ind->marks[j];
                if (d < 0) d = -d;
                if (d <= L && dist_count[d] > 1) part = true;
            }
            if (part) conflicting[nc++] = i;
        }
        if (nc == 0) break; /* No conflicts -> solution found */

        /* Randomly select one conflicting mark to move. */
        int idx = conflicting[evo_rand_range(nc)];

        /* Build occupied[] array (all positions taken by marks OTHER than idx). */
        memset(occupied, 0, (size_t)(L + 1) * sizeof(bool));
        for (int j = 0; j < n; j++) {
            if (j != idx) occupied[ind->marks[j]] = true;
        }

        /* Incrementally remove mark idx from dist_count. */
        int old_mark_pos = ind->marks[idx];
        int removed = mc_remove_mark(ind->marks, n, L, dist_count, idx);
        total_conflicts -= removed;

        /* Plateau escape: if the best move hasn't changed the mark's position
         * for 12 consecutive iterations, do a random walk instead. */
        if (plateau > 12) {
            int p;
            do { p = 1 + evo_rand_range(L - 1); } while (occupied[p]);
            ind->marks[idx] = p;
            int rw_added = mc_add_mark(ind->marks, n, L, dist_count, idx, p);
            total_conflicts += rw_added;
            plateau = 0;
            continue;
        }

        /* Best-move scan: evaluate ALL L-2 free positions for mark idx.
         *
         * For each candidate position p, count the conflicts it would create:
         *   - dist_count[d] > 0: collision with an existing distance (from
         *     other mark pairs that are still in the configuration).
         *   - seen_this[d]: self-collision -- two distances from p to different
         *     marks are equal (these wouldn't be caught by dist_count since
         *     both are new).
         *
         * Ties are broken randomly (reservoir sampling: accept with prob 1/k
         * where k is the number of equally-good positions seen so far). */
        int best_added = 1 << 30;
        int best_pos = ind->marks[idx];
        int best_count = 0;
        for (int p = 1; p <= L - 1; p++) {
            if (occupied[p]) continue;
            int added = 0;
            int tc = 0;
            for (int j = 0; j < n; j++) {
                if (j == idx) continue;
                int d = p - ind->marks[j];
                if (d < 0) d = -d;
                if (d > L) continue;
                if (dist_count[d] > 0) {
                    added++; /* Collision with existing distances */
                } else if (seen_this[d]) {
                    added++; /* Self-collision: two new distances are equal */
                } else {
                    seen_this[d] = 1;
                    touched[tc++] = d;
                }
            }
            /* Clean up seen_this[] for the next candidate. */
            for (int t = 0; t < tc; t++) seen_this[touched[t]] = 0;

            if (added < best_added) {
                best_added = added; best_pos = p; best_count = 1;
            } else if (added == best_added) {
                best_count++;
                if (evo_rand_range(best_count) == 0) best_pos = p;
            }
        }

        /* Commit the chosen position: incrementally add its distances. */
        ind->marks[idx] = best_pos;
        int move_added = mc_add_mark(ind->marks, n, L, dist_count, idx, best_pos);
        total_conflicts += move_added;

        /* Track whether the mark actually moved (for plateau detection). */
        if (best_pos == old_mark_pos)
            plateau++;
        else
            plateau = 0;
    }

    /* Sort marks into canonical ascending order before returning. */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (ind->marks[i] > ind->marks[j]) {
                int tmp = ind->marks[i]; ind->marks[i] = ind->marks[j]; ind->marks[j] = tmp;
            }
        }
    }

    free(dist_count);
    free(occupied);
    free(seen_this);
    return solved;
}

/* ==========================================================================
 * PUBLIC ENTRY POINT: Evolutionary (memetic) Golomb ruler solver.
 *
 * Runs indefinitely with iterated restarts until a valid ruler is found.
 * Every 3rd restart uses crossover from the best-seen solution; all others
 * start from a fresh random configuration.
 * ========================================================================== */
bool solve_golomb_evolutionary(int n, int target_length, ruler_t *out, bool verbose) {
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) {
        return false;
    }
    
    int ls_iters = 800 * n;  /* Iteration budget per local search run */

    volatile int found = 0;  /* Shared flag: first thread to solve sets this */
    int result_marks[MAX_MARKS];
    int result_length = 0;
    int result_restart = 0;

#ifdef _OPENMP
    /* Parallel restarts: each thread runs independent min-conflicts restarts
     * with its own thread-local RNG state and best_seen individual. First
     * thread to find a valid ruler sets `found` and all others terminate. */
    #pragma omp parallel
    {
        evo_srand((unsigned)time(NULL) ^ (unsigned)omp_get_thread_num());
#else
    {
        evo_srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)out);
#endif
        individual_t candidate;
        individual_t best_seen;
        best_seen.conflicts = 1 << 30;
        best_seen.n = n;

        int restart = 0;
        while (!found) {
            /* Initialization: every 3rd restart (after the first 2), use crossover
             * between the thread-local best-seen and a fresh random individual. */
            if (restart > 2 && best_seen.conflicts < (1 << 30) && (restart % 3) == 0) {
                individual_t fresh;
                init_individual(&fresh, n, target_length);
                distance_aware_crossover(&best_seen, &fresh, &candidate, n);
            } else {
                init_individual(&candidate, n, target_length);
            }

            if (min_conflicts_search(&candidate, n, target_length, ls_iters)) {
                evaluate_fitness(&candidate, target_length);
                if (candidate.valid) {
                    int old_found = 0;
#ifdef _OPENMP
                    #pragma omp atomic capture
                    { old_found = found; found = 1; }
#else
                    old_found = found; found = 1;
#endif
                    if (old_found == 0) {
                        memcpy(result_marks, candidate.marks, n * sizeof(int));
                        result_length = candidate.length;
                        result_restart = restart;
                    }
                    break;
                }
            }

            evaluate_fitness(&candidate, target_length);
            if (candidate.conflicts < best_seen.conflicts) {
                best_seen = candidate;
            }

            restart++;
        }
    } /* end parallel / sequential block */

    if (found) {
        out->marks = n;
        out->length = result_length;
        memcpy(out->pos, result_marks, n * sizeof(int));
        if (verbose) {
            printf("[EVOLUTION] Found valid solution at restart %d, length=%d\n",
                   result_restart, out->length);
        }
        return true;
    }

    if (verbose) {
        printf("[EVOLUTION] No perfect solution found.\n");
    }
    return false;
}
