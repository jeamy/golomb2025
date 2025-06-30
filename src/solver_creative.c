#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _OPENMP
#include <omp.h>
#endif

// Bitset helpers, copied for locality
static inline void set_bit(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline int test_bit(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

bool solve_golomb_creative(int n, int target_length, ruler_t *out, bool verbose) {
#ifdef _OPENMP
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) return false;
    if (n <= 2) return solve_golomb(n, target_length, out, verbose);

    volatile bool found = false;
    ruler_t res_local;
    int half = target_length / 2;

    #pragma omp parallel for schedule(dynamic, 1)
    for (int m2 = 1; m2 <= half; ++m2) {
        if (found) continue;

        for (int m3 = m2 + 1; m3 <= target_length - (n - 3); ++m3) {
            if (found) break; 
            if (m3 - m2 == m2) continue;

            int pos[MAX_MARKS] = {0};
            uint64_t dist_bs[BS_WORDS] = {0};

            pos[1] = m2;
            pos[2] = m3;
            set_bit(dist_bs, m2);
            set_bit(dist_bs, m3);
            set_bit(dist_bs, m3 - m2);

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
                break; 
            }
        }
    }

    if (found) {
        *out = res_local;
        return true;
    }
    return false;
#else
    return solve_golomb(n, target_length, out, verbose);
#endif
}
