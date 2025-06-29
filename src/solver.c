#include "golomb.h"
#include <string.h>
#include <stdint.h>

/* Bitset helpers ---------------------------------------------------------*/
static inline void set_bit(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline void clr_bit(uint64_t *bs, int idx) { bs[idx >> 6] &= ~(1ULL << (idx & 63)); }
static inline int  test_bit(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

/* Recursive branch&bound with distance bitset */
static bool dfs(int depth, int n, int target_len, int *pos, uint64_t *dist_bs, bool verbose)
{
    if (depth == n) {
        return pos[n-1] == target_len;
    }
    int last = pos[depth-1];

    /* minimal possible final length if we place marks 1 apart */
    if (last + (n - depth) > target_len) return false;

    int max_next = target_len - (n - depth - 1);
    if (depth == 1 && max_next > target_len/2) max_next = target_len/2; /* symmetry break */

    /* try next positions */
    for (int next = last + 1; next <= max_next; ++next) {
        /* check unique distances */
        bool ok = true;
        for (int i = 0; i < depth; ++i) {
            int d = next - pos[i];
            if (test_bit(dist_bs, d)) { ok = false; break; }
        }
        if (!ok) continue;
        /* commit */
        pos[depth] = next;
        for (int i = 0; i < depth; ++i) {
            int d = next - pos[i];
            set_bit(dist_bs, d);
        }
        if (verbose && depth < 6) {
            printf("depth %d add %d\n", depth, next);
        }
        if (dfs(depth+1, n, target_len, pos, dist_bs, verbose)) return true;
        /* rollback */
        for (int i = 0; i < depth; ++i) {
            int d = next - pos[i];
            clr_bit(dist_bs, d);
        }
    }
    return false;
}

bool solve_golomb(int n, int target_length, ruler_t *out, bool verbose)
{
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) return false;
    int pos[MAX_MARKS] = {0};
    uint64_t dist_bs[(MAX_LEN_BITSET >> 6) + 1] = {0};

    if (!dfs(1, n, target_length, pos, dist_bs, verbose)) return false;

    out->marks = n;
    out->length = pos[n-1];
    memcpy(out->pos, pos, n * sizeof(int));
    return true;
}

/* ----------------------------------------------------------------------
 * Multi-threaded top-level search (OpenMP)
 * --------------------------------------------------------------------*/
#ifdef _OPENMP
#include <omp.h>
#endif

bool solve_golomb_mt(int n, int target_length, ruler_t *out, bool verbose)
{
#ifdef _OPENMP
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET) return false;
    volatile int found = 0; /* shared flag */
    ruler_t res_local;

    int half = target_length / 2; /* symmetry break */

    /* Parallelise on second and third mark (two-level) */
    #pragma omp parallel
    {
        uint64_t dist_bs[(MAX_LEN_BITSET >> 6) + 1];
        int pos[MAX_MARKS];

        #pragma omp for schedule(dynamic,1) nowait
        for (int second = 1; second <= half; ++second) {
            for (int third = second + 1; third <= target_length - (n - 2); ++third) {
                if (found) continue;
                /* init */
                memset(dist_bs, 0, sizeof(dist_bs));
                pos[0] = 0;
                pos[1] = second;
                pos[2] = third;
                set_bit(dist_bs, second);
                int d13 = third;         /* third - 0 */
                int d23 = third - second;
                if (d13 == second || d23 == second || test_bit(dist_bs, d13) || test_bit(dist_bs, d23)) continue;
                set_bit(dist_bs, d13);
                set_bit(dist_bs, d23);

                if (dfs(3, n, target_length, pos, dist_bs, verbose && omp_get_thread_num()==0)) {
                    #pragma omp critical
                    {
                        if (!found) {
                            res_local.marks = n;
                            res_local.length = pos[n-1];
                            memcpy(res_local.pos, pos, n * sizeof(int));
                            found = 1;
                        }
                    }
                }
            }
        }
    }
    if (found) {
        *out = res_local;
        return true;
    }
    return false;
#else
    /* OpenMP not available, fallback to single-thread */
    return solve_golomb(n, target_length, out, verbose);
#endif
}

