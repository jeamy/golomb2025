#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "golomb.h"

#ifdef _OPENMP
#include <omp.h>
#endif

/* NASM DFS entry point */
extern int dfs_asm(int depth, int n, int target_len, int *pos, uint64_t *dist_bs, int verbose);

/* local bit helpers (mirror solver.c) */
static inline void set_bit_local(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline int  test_bit_local(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

typedef struct { int s, t, score; } cand_t;

static int cmp_cand(const void *a, const void *b)
{
    const cand_t *x = (const cand_t*)a;
    const cand_t *y = (const cand_t*)b;
    if (x->score != y->score) return x->score - y->score;
    if (x->s != y->s) return x->s - y->s;
    return x->t - y->t;
}

bool solve_golomb_mt_asm(int n, int target_length, ruler_t *out, int verbose)
{
#ifdef _OPENMP
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET)
        return false;

    if (n <= 3)
        return solve_golomb(n, target_length, out, verbose);

    volatile int found = 0;
    ruler_t res_local;

    int half = target_length / 2;
    int T = target_length - (n - 2);
    int second_max = half;
    if (second_max > T - 1) second_max = T - 1;
    if (second_max < 1) second_max = 1;

    const ruler_t *ref = lut_lookup_by_marks(n);

    /* Fast lane: try exact LUT (second,third) first */
    if (ref && !getenv("GOLOMB_NO_HINTS")) {
        int s0 = ref->pos[1];
        int t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            uint64_t dist_bs0[BS_WORDS] = {0};
            int pos0[MAX_MARKS];
            pos0[0] = 0; pos0[1] = s0; pos0[2] = t0;
            set_bit_local(dist_bs0, s0);
            int d13 = t0;
            int d23 = t0 - s0;
            if (!test_bit_local(dist_bs0, d13) && !test_bit_local(dist_bs0, d23)) {
                set_bit_local(dist_bs0, d13);
                set_bit_local(dist_bs0, d23);
                if (dfs_asm(3, n, target_length, pos0, dist_bs0, 0)) {
                    out->marks = n;
                    out->length = pos0[n - 1];
                    memcpy(out->pos, pos0, n * sizeof(int));
                    return true;
                }
            }
        }
    }

    long long total = 0;
    for (int s = 1; s <= second_max; ++s) {
        int cnt = T - s;
        if (cnt > 0) total += cnt;
    }
    if (total <= 0)
        return false;

    cand_t *cands = (cand_t*)malloc((size_t)total * sizeof(cand_t));
    if (!cands)
        return false;

    int use_hint_order = (ref && !getenv("GOLOMB_NO_HINTS")) ? 1 : 0;
    long long k = 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            int score = 0;
            if (use_hint_order) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands[k++] = (cand_t){ s, t, score };
        }
    }

    if (use_hint_order && total > 1)
        qsort(cands, (size_t)total, sizeof(cand_t), cmp_cand);

#pragma omp parallel
    {
#pragma omp single
        {
#pragma omp taskgroup
            {
#pragma omp taskloop grainsize(1)
                for (long long i = 0; i < total; ++i)
                {
                    if (found)
                        continue;

                    int second = cands[i].s;
                    int third = cands[i].t;

                    uint64_t dist_bs[BS_WORDS] = {0};
                    int pos[MAX_MARKS];
                    pos[0] = 0;
                    pos[1] = second;
                    pos[2] = third;

                    set_bit_local(dist_bs, second);
                    int d13 = third;
                    int d23 = third - second;
                    if (test_bit_local(dist_bs, d13) || test_bit_local(dist_bs, d23))
                        continue;
                    set_bit_local(dist_bs, d13);
                    set_bit_local(dist_bs, d23);

                    if (dfs_asm(3, n, target_length, pos, dist_bs, 0))
                    {
                        int old_found;
#pragma omp atomic capture
                        {
                            old_found = found;
                            found = 1;
                        }
                        if (old_found == 0)
                        {
                            res_local.marks = n;
                            res_local.length = pos[n - 1];
                            memcpy(res_local.pos, pos, n * sizeof(int));
#pragma omp cancel taskgroup
                        }
                    }
                }
            }
        }
    }

    free(cands);
    if (found) {
        *out = res_local;
        return true;
    }
    return false;
#else
    return solve_golomb(n, target_length, out, verbose);
#endif
}
