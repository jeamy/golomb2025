#include "golomb.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#ifdef _OPENMP
#include <omp.h>
#endif

// Bitset helpers
static inline void set_bit(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline void clr_bit(uint64_t *bs, int idx) { bs[idx >> 6] &= ~(1ULL << (idx & 63)); }
static inline int test_bit(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

// Shared state for the best ruler found so far
static volatile int g_min_len = INT_MAX;
static ruler_t g_best_ruler;
static omp_lock_t g_ruler_lock; // Use a lock for safer updates

/**
 * @brief Creative recursive solver (Bounded Projective Search)
 */
static void dfs_creative(int depth, int n, int *pos, uint64_t *dist_bs)
{
    // Pruning: if the current path can't possibly be better, stop.
    if (pos[depth - 1] >= g_min_len) {
        return;
    }

    // Solution found
    if (depth == n) {
        int current_len = pos[n - 1];
        
        // Check if this solution is a new global best
        if (current_len < g_min_len) {
            omp_set_lock(&g_ruler_lock);
            // Re-check inside the lock to handle race conditions
            if (current_len < g_min_len) {
                g_min_len = current_len;
                g_best_ruler.length = current_len;
                g_best_ruler.marks = n;
                memcpy(g_best_ruler.pos, pos, n * sizeof(int));
            }
            omp_unset_lock(&g_ruler_lock);
        }
        return;
    }

    int last_pos = pos[depth - 1];
    // Aggressively prune based on the current best-known length
    int max_next = g_min_len - (n - depth - 1);

    // Try to place the next mark
    for (int next = last_pos + 1; next <= max_next; ++next) {
        
        // Check for duplicate distances
        bool ok = true;
        int new_dists[MAX_MARKS];
        int dist_count = 0;

        for (int i = 0; i < depth; ++i) {
            int d = next - pos[i];
            if (test_bit(dist_bs, d)) {
                ok = false;
                break;
            }
            new_dists[dist_count++] = d;
        }

        if (ok) {
            // Commit: Add new mark and its distances
            pos[depth] = next;
            for (int i = 0; i < dist_count; ++i) {
                set_bit(dist_bs, new_dists[i]);
            }

            dfs_creative(depth + 1, n, pos, dist_bs);

            // Rollback: Remove distances to backtrack
            for (int i = 0; i < dist_count; ++i) {
                clr_bit(dist_bs, new_dists[i]);
            }
        }
    }
}

/**
 * @brief Entry point for the creative solver
 */
bool solve_golomb_creative(int n, ruler_t *out, bool verbose)
{
    (void)verbose; // verbose is not used in this implementation yet

    if (n > MAX_MARKS) return false;
    if (n <= 1) {
        out->marks = n;
        out->length = 0;
        if (n == 1) out->pos[0] = 0;
        return true;
    }

    const ruler_t *lut_ruler = lut_lookup_by_marks(n);
    // Start with a reasonable upper bound, or the known optimal length if available.
    g_min_len = (lut_ruler) ? lut_ruler->length + 1 : INT_MAX;
    int initial_len = g_min_len;

    omp_init_lock(&g_ruler_lock);

    #pragma omp parallel
    {
        #pragma omp single
        {
            // The search space is split by the position of the second mark.
            // Symmetry breaking: pos[1] <= (L-1)/2. We use a generous upper bound.
            int search_limit = g_min_len / 2;

            #pragma omp taskloop grainsize(1)
            for (int m2 = 1; m2 <= search_limit; ++m2) {
                int pos[MAX_MARKS] = {0};
                uint64_t dist_bs[BS_WORDS] = {0};

                pos[1] = m2;
                set_bit(dist_bs, m2);
                
                dfs_creative(2, n, pos, dist_bs);
            }
        }
    }

    omp_destroy_lock(&g_ruler_lock);

    // If we found a ruler that is better than the initial bound
    if (g_min_len < initial_len) {
        *out = g_best_ruler;
        return true;
    }

    return false;
}
