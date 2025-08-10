#include "golomb.h"
#include <string.h>
#include <immintrin.h> /* AVX2 intrinsics */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

/* ---------------- Checkpointing helpers ---------------- */
typedef struct {
    char magic[4];      /* "GRCP" */
    uint32_t version;   /* 1 */
    uint32_t n;
    uint32_t L;
    uint64_t total;
    uint32_t hint_s;
    uint32_t hint_t;
    uint32_t hint_used; /* 0/1 */
} cp_header_t;

static __attribute__((unused)) int cp_load_file(const char *path,
                        int n,
                        int target_length,
                        long long total,
                        int hint_s,
                        int hint_t,
                        int hint_used,
                        uint32_t *done_words,
                        size_t words)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    cp_header_t h;
    size_t r = fread(&h, 1, sizeof h, fp);
    if (r != sizeof h || memcmp(h.magic, "GRCP", 4) != 0 || h.version != 1) { fclose(fp); return 0; }
    if (h.n != (uint32_t)n || h.L != (uint32_t)target_length || h.total != (uint64_t)total) { fclose(fp); return 0; }
    if (h.hint_s != (uint32_t)hint_s || h.hint_t != (uint32_t)hint_t || h.hint_used != (uint32_t)hint_used) { fclose(fp); return 0; }
    size_t want = words * sizeof(uint32_t);
    r = fread(done_words, 1, want, fp);
    fclose(fp);
    return r == want;
}

static __attribute__((unused)) int cp_save_file(const char *path,
                        int n,
                        int target_length,
                        long long total,
                        int hint_s,
                        int hint_t,
                        int hint_used,
                        const uint32_t *done_words,
                        size_t words)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return 0;
    cp_header_t h;
    memcpy(h.magic, "GRCP", 4);
    h.version = 1;
    h.n = (uint32_t)n;
    h.L = (uint32_t)target_length;
    h.total = (uint64_t)total;
    h.hint_s = (uint32_t)hint_s;
    h.hint_t = (uint32_t)hint_t;
    h.hint_used = (uint32_t)hint_used;
    size_t w1 = fwrite(&h, 1, sizeof h, fp);
    size_t w2 = fwrite(done_words, 1, words * sizeof(uint32_t), fp);
    int ok = (w1 == sizeof h) && (w2 == words * sizeof(uint32_t));
    if (fclose(fp) != 0) ok = 0;
    if (!ok) { remove(tmp); return 0; }
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

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
    int use_hint_order = (ref && !getenv("GOLOMB_NO_HINTS")) ? 1 : 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            if (t <= s) continue;
            int score = 0;
            if (use_hint_order) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands[k++] = (cand_t){ s, t, score };
        }
    }
    if (use_hint_order && cands && total > 1) {
        /* stable sort by score ascending */
        int cmp(const void *a, const void *b) {
            const cand_t *x = (const cand_t*)a, *y = (const cand_t*)b;
            if (x->score != y->score) return x->score - y->score;
            if (x->s != y->s) return x->s - y->s;
            return x->t - y->t;
        }
        qsort(cands, (size_t)total, sizeof(cand_t), cmp);
    }

    /* ---------------- Checkpoint/Resume setup (only for -mp) ---------------- */
    extern const char *g_cp_path;
    extern int g_cp_interval_sec;
    uint32_t *done_words = NULL; /* bitset: 1 = candidate processed */
    size_t words = (size_t)((total + 31) / 32);
    if (words == 0) words = 1;
    done_words = (uint32_t*)calloc(words, sizeof(uint32_t));
    if (!done_words) { free(cands); return false; }

    int use_cp = (g_cp_path && *g_cp_path) ? 1 : 0;
    if (use_cp) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_load_file(g_cp_path, n, target_length, total, hs, ht, use_hint_order, done_words, words);
        /* Create or refresh the checkpoint file immediately so users can see it early */
        (void)cp_save_file(g_cp_path, n, target_length, total, hs, ht, use_hint_order, done_words, words);
    }
    int interval = (g_cp_interval_sec > 0) ? g_cp_interval_sec : 60;
    struct timespec ts_last_flush; clock_gettime(CLOCK_MONOTONIC, &ts_last_flush);

    /* Parallelise across ordered candidate list */
#pragma omp parallel
    {
#pragma omp for schedule(dynamic, 16)
        for (long long i = 0; i < total; ++i)
        {
            if (found)
                continue;
            /* skip already processed candidate if resuming */
            if (use_cp) {
                size_t wi = (size_t)(i >> 5);
                uint32_t mask = 1u << (i & 31);
                if (done_words[wi] & mask) continue;
            }
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

            /* mark candidate processed and possibly flush checkpoint */
            if (use_cp) {
                size_t wi = (size_t)(i >> 5);
                uint32_t mask = 1u << (i & 31);
                __sync_fetch_and_or(&done_words[wi], mask);
                struct timespec ts_now; clock_gettime(CLOCK_MONOTONIC, &ts_now);
                time_t dt = ts_now.tv_sec - ts_last_flush.tv_sec;
                if (dt >= interval) {
#pragma omp critical(cp_io)
                    {
                        /* re-check inside critical to avoid thundering herd */
                        struct timespec ts_chk; clock_gettime(CLOCK_MONOTONIC, &ts_chk);
                        if (ts_chk.tv_sec - ts_last_flush.tv_sec >= interval) {
                            int hs2 = use_hint_order && ref ? ref->pos[1] : 0;
                            int ht2 = use_hint_order && ref ? ref->pos[2] : 0;
                            (void)cp_save_file(g_cp_path, n, target_length, total, hs2, ht2, use_hint_order, done_words, words);
                            ts_last_flush = ts_chk;
                        }
                    }
                }
            }
        }
    }
    if (use_cp) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_save_file(g_cp_path, n, target_length, total, hs, ht, use_hint_order, done_words, words);
    }
    if (found)
    {
        *out = res_local;
        free(cands);
        free(done_words);
        return true;
    }
    free(cands);
    free(done_words);
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

