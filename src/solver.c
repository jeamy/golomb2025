#include "golomb.h"
#include <string.h>
#include <immintrin.h> /* AVX2 intrinsics */
#include <stdint.h>

/* Optional hand-written assembler version (FASM). Will be NULL if not linked. */
extern int test_any_dup8_avx2_asm(const uint64_t *bs, const int *dist8)
        __attribute__((weak));
extern int test_any_dup8_avx2_gather(const uint64_t *bs, const int *dist8)
        __attribute__((weak));
extern int test_any_dup8_avx512(const uint64_t *bs, const int *dist8)
        __attribute__((weak));
extern bool g_use_asm;

/* intrinsic fallback prototype */
static inline int test_any_dup8_avx2(const uint64_t *bs, const int *dist8);



/* Select best available implementation at runtime */
static inline int test_any_dup8(const uint64_t *bs, const int *dist8)
{
    /* Prefer AVX2 gather (widely supported), then AVX-512 if explicitly selected via env */
    if (g_use_simd && test_any_dup8_avx2_gather)
        return test_any_dup8_avx2_gather(bs, dist8);
    if (g_use_simd && test_any_dup8_avx512 && getenv("GOLOMB_USE_AVX512"))
        return test_any_dup8_avx512(bs, dist8);
    if (g_use_asm && test_any_dup8_avx2_asm)
        return test_any_dup8_avx2_asm(bs, dist8);
    return test_any_dup8_avx2(bs, dist8); /* intrinsic fallback */
}


/* Bitset helpers ---------------------------------------------------------*/
/* Global runtime flag set by main.c */
bool g_use_simd = false;
bool g_use_asm  = false;

static inline void set_bit(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline void clr_bit(uint64_t *bs, int idx) { bs[idx >> 6] &= ~(1ULL << (idx & 63)); }
/* Scalar fallback */
static inline int  test_bit_scalar(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

/* AVX2: check 4 distances in parallel via gather + bitmask test. Returns 1 if ANY duplicate seen */
static inline int test_any_dup_avx2(const uint64_t *bs, const int *dist4)
{
#if defined(__AVX2__)
    /* Load distances */
    __m128i idx32   = _mm_loadu_si128((const __m128i*)dist4);        /* 4 x int32 */
    __m128i words32 = _mm_srli_epi32(idx32, 6);                      /* distance>>6 (word index) */

    /* Gather corresponding 64-bit words from the bitset */
    __m256i words64 = _mm256_i32gather_epi64((const long long*)bs, words32, 8);

    /* words32 is always < BS_WORDS thanks to guard; build masks */
    uint64_t masks_arr[4];
    for (int i = 0; i < 4; ++i) masks_arr[i] = 1ULL << (dist4[i] & 63);
    __m256i masks = _mm256_loadu_si256((const __m256i*)masks_arr);

    /* AND to extract the tested bits, then OR-reduce by testz */
    __m256i dup = _mm256_and_si256(words64, masks);
    return !_mm256_testz_si256(dup, dup);
#else
    (void)bs; (void)dist4; return 0;
#endif
}

/* Wrapper for 8 distances using two 4-distance calls */
static inline int test_any_dup8_avx2(const uint64_t *bs, const int *dist8)
{
#if defined(__AVX2__)
    return test_any_dup_avx2(bs, dist8) || test_any_dup_avx2(bs, dist8 + 4);
#else
    (void)bs; (void)dist8; return 0;
#endif
}

static inline int test_bit(const uint64_t *bs, int idx)
{
    return test_bit_scalar(bs, idx);
}

/* Recursive branch&bound with distance bitset */
bool dfs(int depth, int n, int target_len, int *pos, uint64_t *dist_bs, bool verbose)
{
    if (depth == n)
    {
        return pos[n - 1] == target_len;
    }
    int last = pos[depth - 1];

    /* minimal possible final length if we place marks 1 apart */
    if (last + (n - depth) > target_len)
        return false;

    int max_next = target_len - (n - depth - 1);
    if (depth == 1)
    {
        int limit = target_len / 2; /* symmetry break */
        if (limit < last + 1)
            limit = last + 1; /* ensure at least one candidate */
        if (max_next > limit)
            max_next = limit;
    }

    /* try next positions */
    for (int next = last + 1; next <= max_next; ++next)
    {
        int gap = next - last;
        /* Fast pre-check: most likely duplicate is the newest smallest gap (next-last). */
        if (test_bit_scalar(dist_bs, gap))
            continue;
        /* check unique distances */
        bool ok = true;
        if (g_use_simd && depth >= 6) { /* Use SIMD earlier (helps n<=12) */
                        int dist8[8];
            int idx8 = 0;
            for (int i = 0; i < depth; ++i) {
                dist8[idx8++] = next - pos[i];
                if (idx8 == 8) {
                    if (test_any_dup8(dist_bs, dist8)) { ok = false; break; }
                    idx8 = 0;
                }
            }
            if (ok && idx8 > 0) {
                for (int j = 0; j < idx8; ++j) {
                    if (test_bit_scalar(dist_bs, dist8[j])) { ok = false; break; }
                }
            }
        } else {
            for (int i = 0; i < depth; ++i) {
                int d = next - pos[i];
                if (test_bit_scalar(dist_bs, d)) {
                    ok = false;
                    break;
                }
            }
        }
        if (!ok)
            continue;
        /* commit */
        pos[depth] = next;
        for (int i = 0; i < depth; ++i)
        {
            int d = next - pos[i];
            set_bit(dist_bs, d);
        }
        if (verbose && depth < 6)
        {
            printf("depth %d add %d\n", depth, next);
        }
        if (dfs(depth + 1, n, target_len, pos, dist_bs, verbose))
            return true;
        /* rollback */
        for (int i = 0; i < depth; ++i)
        {
            int d = next - pos[i];
            clr_bit(dist_bs, d);
        }
    }
    return false;
}

bool solve_golomb(int n, int target_length, ruler_t *out, bool verbose)
{
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET)
        return false;
    int pos[MAX_MARKS] = {0};
    uint64_t dist_bs[BS_WORDS] = {0};

    if (!dfs(1, n, target_length, pos, dist_bs, verbose))
        return false;

    out->marks = n;
    out->length = pos[n - 1];
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
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET)
        return false;
    /* Very small orders (<=3) are faster single-threaded */
    if (n <= 3)
    {
        return solve_golomb(n, target_length, out, verbose);
    }
    volatile int found = 0; /* shared flag */
    ruler_t res_local;

    int half = target_length / 2; /* symmetry break */
    int T = target_length - (n - 2);
    int second_max = half;
    if (second_max > T - 1) second_max = T - 1;
    if (second_max < 1) second_max = 1;

    /* If we have a LUT reference for this n, prefer candidates near its (second,third) */
    extern const ruler_t *lut_lookup_by_marks(int);
    const ruler_t *ref = lut_lookup_by_marks(n);

    /* Fast lane: try the exact LUT (second,third) first (positions still solved via DFS). */
    if (ref && !getenv("GOLOMB_NO_HINTS")) {
        int s0 = ref->pos[1];
        int t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            uint64_t dist_bs0[BS_WORDS] = {0};
            int pos0[MAX_MARKS];
            pos0[0] = 0; pos0[1] = s0; pos0[2] = t0;
            set_bit(dist_bs0, s0);
            int d13 = t0;
            int d23 = t0 - s0;
            if (!test_bit(dist_bs0, d13) && !test_bit(dist_bs0, d23)) {
                set_bit(dist_bs0, d13);
                set_bit(dist_bs0, d23);
                if (dfs(3, n, target_length, pos0, dist_bs0, false)) {
                    out->marks = n;
                    out->length = pos0[n - 1];
                    memcpy(out->pos, pos0, n * sizeof(int));
                    return true;
                }
            }
        }
    }
    typedef struct { int s, t, score; } cand_t;
    long long total = 0;
    /* Worst-case pairs is ~second_max*(T-second_max/2) < 1e6 for our ranges, malloc ok */
    cand_t *cands = NULL;
    if (second_max >= 1) {
        /* count total */
        for (int s = 1; s <= second_max; ++s) {
            int cnt = T - s;
            if (cnt > 0) total += cnt;
        }
        cands = (cand_t*)malloc((size_t)total * sizeof(cand_t));
    }
    long long k = 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            if (t <= s) continue;
            int score = 0;
            if (ref) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands[k++] = (cand_t){ s, t, score };
        }
    }
    if (ref && cands && total > 1) {
        /* stable sort by score ascending */
        int cmp(const void *a, const void *b) {
            const cand_t *x = (const cand_t*)a, *y = (const cand_t*)b;
            if (x->score != y->score) return x->score - y->score;
            if (x->s != y->s) return x->s - y->s;
            return x->t - y->t;
        }
        qsort(cands, (size_t)total, sizeof(cand_t), cmp);
    }

    /* Parallelise across ordered candidate list */
#pragma omp parallel
    {
#pragma omp for schedule(dynamic, 16)
        for (long long i = 0; i < total; ++i)
        {
            if (found)
                continue;
            int second = cands[i].s;
            int third  = cands[i].t;

            /* local state per iteration */
            uint64_t dist_bs[BS_WORDS] = {0};
            int pos[MAX_MARKS];
            pos[0] = 0;
            pos[1] = second;
            pos[2] = third;
            set_bit(dist_bs, second);
            int d13 = third;           /* third - 0 */
            int d23 = third - second;  /* third - second */
            /* quick duplicate tests before committing */
            if (test_bit(dist_bs, d13) || test_bit(dist_bs, d23))
                continue;
            set_bit(dist_bs, d13);
            set_bit(dist_bs, d23);

            if (dfs(3, n, target_length, pos, dist_bs, false))
            {
#pragma omp critical
                {
                    if (!found)
                    {
                        res_local.marks = n;
                        res_local.length = pos[n - 1];
                        memcpy(res_local.pos, pos, n * sizeof(int));
                        found = 1;
                        #pragma omp flush(found)
                    }
                }
            }
        }
    }
    if (found)
    {
        *out = res_local;
        return true;
    }
    return false;
#else /* !_OPENMP */
    return solve_golomb(n, target_length, out, verbose);
#endif

}

/* ----------------------------------------------------------------------
 * Dynamic task-based OpenMP solver (finer workload balancing)
 * --------------------------------------------------------------------*/
bool solve_golomb_mt_dyn(int n, int target_length,
                         ruler_t *out, bool verbose)
{
#ifdef _OPENMP
    if (n > MAX_MARKS || target_length > MAX_LEN_BITSET)
        return false;
    if (n <= 3)
        return solve_golomb(n, target_length, out, verbose);

    volatile int found = 0; /* shared flag  */
    ruler_t local;          /* winning ruler */

#pragma omp parallel
    {
#pragma omp single
        {
            const int half = target_length / 2; /* symmetry break */

#pragma omp taskgroup
            {
                /* flatten (second, third) space; batch 64 combos per task */
#pragma omp taskloop grainsize(32) firstprivate(n, target_length, verbose) shared(found, local)
                for (int second = 1; second <= half; ++second)
                    for (int third = second + 1; third <= target_length - (n - 2); ++third)
                    {
                        if (found) continue; /* early poll */

                        int pos[MAX_MARKS];
                        uint64_t bs[BS_WORDS] = {0};
                        pos[0] = 0;
                        pos[1] = second;
                        pos[2] = third;
                        set_bit(bs, second);
                        int d13 = third;
                        int d23 = third - second;
                        if (d13 == second || d23 == second || test_bit(bs, d13) || test_bit(bs, d23))
                            continue;
                        set_bit(bs, d13);
                        set_bit(bs, d23);
                        if (dfs(3, n, target_length, pos, bs, false)) {
                            int old;
#pragma omp atomic capture
                            { old = found; found = 1; }
                            if (old == 0) {
                                local.marks = n;
                                local.length = pos[n - 1];
                                memcpy(local.pos, pos, n * sizeof(int));
                                /* stop remaining tasks */
#pragma omp cancel taskgroup
                            }
                        }
                    }
            } /* taskgroup */
        } /* single */
    } /* parallel */

    if (found)
    {
        *out = local;
        return true;
    }
    return false;
#else
    return solve_golomb(n, target_length, out, verbose);
#endif

}

