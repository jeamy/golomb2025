#include "glibc_c23_math_compat.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <omp.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>

extern "C" {
#include "../include/golomb.h"
}
#include "golomb_bits.h"

/* OGR(k) for k < n, taken from the LUT (proven optimal lengths).  Used as
 * lower bounds on sub-segment lengths: a segment holding k marks of a
 * Golomb ruler is itself a Golomb ruler, so it is at least OGR(k) long. */
static int h_ogr[MAX_MARKS + 1];
__constant__ int c_ogr[MAX_MARKS + 1];

static void init_ogr_table(int n)
{
    for (int k = 0; k <= MAX_MARKS; ++k) {
        int v = k * (k - 1) / 2; /* triangular fallback, always valid */
        if (k >= 2 && k < n) {
            const ruler_t *r = lut_lookup_by_marks(k);
            if (r && r->length > v) v = r->length;
        }
        if (k < 2) v = 0;
        h_ogr[k] = v;
    }
}

/* ---------------------------------------------------------------------------
 * Architecture (2026-09-27):
 *   1. frontier_kernel enumerates valid endpoint-aware depth-4 prefixes and
 *      ranks them by their number of legal depth-5 continuations (ordering
 *      only; completeness never depends on the score).  For n >= 14
 *      expand_kernel splits them to depth 5 (GOLOMB_D5_MIN overrides).
 *   2. dfs_bits_kernel completes the prefixes with the bit-parallel DFS of
 *      golomb_bits.h: one forbidden-candidate mask per level, so the next
 *      legal mark is a find-first-zero instead of a scan with d bit tests.
 *      Threads are persistent and pull prefixes from a device counter; the
 *      per-level masks live in a register shift-stack.
 *   3. Hybrid scheduling: the GPU takes large chunks from the front of the
 *      score-sorted list, OpenMP threads take single prefixes from the back
 *      (same DFS on the host).  A hit on either side stops the other: the
 *      host-mapped flag for the CPU, a device-memory flag for the kernel.
 *   4. With -H the LUT (s0,t0) pair seeds a "guided fast-lane": the prefix
 *      is expanded to depth 4..6 and searched by the same hybrid engine
 *      (without the mirror cut, which needs both mirror images in the list).
 *      The result is still constructed by exact DFS, never copied from the LUT.
 *
 * Pruning (all exact, see golomb_bits.h): the segment [y, L] holding m
 * marks is at least OGR(m) long (LUT values for m < n) and at least the sum
 * of the m-1 smallest unused distances; the mirror cut keeps only rulers
 * whose first gap is smaller than their last gap.
 *
 * Note: -b searches only L = LUT length.  "optimal=yes" in the log comes
 * from the LUT; this program does not prove that L-1 is infeasible (use
 * GOLOMB_TARGET_L=<L-1> for that exhaustive run).
 * ------------------------------------------------------------------------- */

/* ---------------- Checkpointing header ---------------- */
typedef struct {
    char     magic[4];   // "GRCP"
    uint32_t version;    // 1
    uint32_t n;
    uint32_t L;
    uint64_t total;
    uint32_t hint_s;
    uint32_t hint_t;
    uint32_t hint_used;  // 0/1
} cp_header_t;

static int cp_load_file(const char *path,
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

static int cp_save_file(const char *path,
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

/* ---------------- Host bitset helpers (match C solver) ---------------- */
static inline void set_bit64(uint64_t *bs, int idx) { bs[idx >> 6] |= 1ULL << (idx & 63); }
static inline int  test_bit64(const uint64_t *bs, int idx) { return (bs[idx >> 6] >> (idx & 63)) & 1ULL; }

/* ---------------- GPU candidate prefilter ---------------- */
struct Cand { int s, t, u_hint, score; };

/* Prefix marks: 0, s, t, u[, v[, w]].  A zero v/w means "absent" (marks are
 * always >= 1 after position 0).  depth = 4 + (v>0) + (w>0). */
struct FrontierPrefix { int s, t, u, v, w, score; };

static __host__ __device__ inline int prefix_depth(const FrontierPrefix &p)
{
    if (p.w > 0) return 6;
    if (p.v > 0) return 5;
    return 4;
}

/* Rebuild pos[] and the distance bitset of a prefix (marks + fixed L).
 * Returns false if the prefix is not a valid partial ruler. */
static bool build_prefix_state(const FrontierPrefix &p, int n, int L, int *pos, uint64_t *bs)
{
    const int depth = prefix_depth(p);
    if (depth >= n) return false;
    pos[0] = 0; pos[1] = p.s; pos[2] = p.t; pos[3] = p.u;
    if (p.v > 0) pos[4] = p.v;
    if (p.w > 0) pos[5] = p.w;
    pos[n - 1] = L;
    for (int w = 0; w < BS_WORDS; ++w) bs[w] = 0;
    for (int a = 0; a < depth; ++a) {
        for (int b = a + 1; b < depth; ++b) {
            const int d = pos[b] - pos[a];
            if (test_bit64(bs, d)) return false;
            set_bit64(bs, d);
        }
        const int dl = L - pos[a];
        if (test_bit64(bs, dl)) return false;
        set_bit64(bs, dl);
    }
    return true;
}

/* Full-ruler sanity check (used to validate GPU results before accepting). */
static bool validate_ruler(const ruler_t *r)
{
    const int n = r->marks;
    if (n < 2 || n > MAX_MARKS) return false;
    if (r->pos[0] != 0 || r->pos[n - 1] != r->length) return false;
    for (int i = 1; i < n; ++i)
        if (r->pos[i] <= r->pos[i - 1]) return false;
    unsigned char seen[MAX_LEN_BITSET + 1] = {0};
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const int d = r->pos[j] - r->pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET || seen[d]) return false;
            seen[d] = 1;
        }
    return true;
}

/* Expand every endpoint-aware (s,t) root into depth-4 prefixes on the
 * device.  The CPU receives only valid prefixes and continues the exact
 * endpoint-aware DFS from depth 4. */
__global__ void frontier_kernel(int n, int L, const Cand *cands, int64_t total,
                                FrontierPrefix *out, unsigned int *count,
                                unsigned int capacity)
{
    const int64_t i = blockIdx.x * 1LL * blockDim.x + threadIdx.x;
    if (i >= total) return;
    const int s = cands[i].s;
    const int t = cands[i].t;
    if (t - s == s) return;

    /* Root feasibility: [t, L] holds n-2 marks of a Golomb ruler, so
     * L - t >= OGR(n-2) (>= the triangular bound (n-3)(n-2)/2). */
    if (L - t < c_ogr[n - 2]) return;

    int base[6] = {L, s, t, t - s, L - s, L - t};
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < a; ++b)
            if (base[a] == base[b]) return;

    /* [u, L] holds n-3 marks: u <= L - OGR(n-3). */
    int max_u = L - c_ogr[n - 3];
    if (max_u >= L) max_u = L - 1;
    for (int u = t + 1; u <= max_u; ++u) {
        int du[4] = {u, u - s, u - t, L - u};
        bool valid = true;
        for (int a = 0; a < 4 && valid; ++a) {
            for (int b = 0; b < a; ++b)
                if (du[a] == du[b]) { valid = false; break; }
            for (int b = 0; b < 6 && valid; ++b)
                if (du[a] == base[b]) { valid = false; break; }
        }
        if (!valid) continue;

        /* This is an ordering heuristic only: count legal depth-5 marks.
         * It neither removes nor changes a prefix, so exact completeness is
         * independent of the score.  A high continuation count gets the CPU
         * DFS to promising subtrees before a large number of dead prefixes. */
        int score = 0;
        const int max_v = L - c_ogr[n - 4]; /* must match expand_kernel */
        for (int v = u + 1; v <= max_v; ++v) {
            int dv[5] = {v, v - s, v - t, v - u, L - v};
            bool v_valid = true;
            for (int a = 0; a < 5 && v_valid; ++a) {
                for (int b = 0; b < a; ++b)
                    if (dv[a] == dv[b]) { v_valid = false; break; }
                for (int b = 0; b < 6 && v_valid; ++b)
                    if (dv[a] == base[b]) { v_valid = false; break; }
                for (int b = 0; b < 4 && v_valid; ++b)
                    if (dv[a] == du[b]) { v_valid = false; break; }
            }
            if (v_valid) ++score;
        }
        const unsigned int slot = atomicAdd(count, 1u);
        if (slot < capacity) out[slot] = FrontierPrefix{s, t, u, 0, 0, score};
    }
}

/* Second frontier pass: expand every valid depth-4 prefix into depth-5
 * prefixes.  A single depth-4 subtree is far too coarse as one GPU work
 * item (the heaviest can occupy a whole stream for minutes at n>=15);
 * after expansion each item covers ~1/20 of that work.  The host sizes
 * `out` to the sum of the depth-4 scores (each score counts exactly the
 * legal v continuations), so the atomic slot can never overflow.
 * score = legal-w continuation count: ordering heuristic only. */
__global__ void expand_kernel(int n, int L,
                              const FrontierPrefix *__restrict__ in, long long in_count,
                              FrontierPrefix *__restrict__ out,
                              unsigned int *__restrict__ out_count,
                              unsigned int capacity)
{
    const long long i = (long long)blockIdx.x * (long long)blockDim.x + threadIdx.x;
    if (i >= in_count) return;
    const FrontierPrefix p = in[i];
    /* Distances already present: pairs among {0,s,t,u} and each-to-L. */
    int known[15];
    known[0] = L;       known[1] = p.s;       known[2] = p.t;
    known[3] = p.t - p.s; known[4] = L - p.s; known[5] = L - p.t;
    known[6] = p.u;     known[7] = p.u - p.s; known[8] = p.u - p.t;
    known[9] = L - p.u;
    const int max_v = L - c_ogr[n - 4]; /* must match frontier_kernel's score */
    const int rem_w = n - 6;
    const int max_w = L - c_ogr[n - 5];
    for (int v = p.u + 1; v <= max_v; ++v) {
        const int dv[5] = {v, v - p.s, v - p.t, v - p.u, L - v};
        bool ok = true;
        for (int a = 0; a < 5 && ok; ++a) {
            for (int b = 0; b < a; ++b)
                if (dv[a] == dv[b]) { ok = false; break; }
            for (int b = 0; b < 10 && ok; ++b)
                if (dv[a] == known[b]) { ok = false; break; }
        }
        if (!ok) continue;
        int score = 0;
        if (rem_w >= 1) {
            for (int k = 0; k < 5; ++k) known[10 + k] = dv[k];
            for (int w = v + 1; w <= max_w; ++w) {
                const int dw[6] = {w, w - p.s, w - p.t, w - p.u, w - v, L - w};
                bool ok2 = true;
                for (int a = 0; a < 6 && ok2; ++a) {
                    for (int b = 0; b < a; ++b)
                        if (dw[a] == dw[b]) { ok2 = false; break; }
                    for (int b = 0; b < 15 && ok2; ++b)
                        if (dw[a] == known[b]) { ok2 = false; break; }
                }
                if (ok2) ++score;
            }
        }
        const unsigned int slot = atomicAdd(out_count, 1u);
        if (slot < capacity) out[slot] = FrontierPrefix{p.s, p.t, p.u, v, 0, score};
    }
}

__global__ void prefilter_kernel(int n, int L, const Cand *cands, int64_t total,
                                 unsigned char *ok, int *u_hints)
{
    int64_t i = blockIdx.x * 1LL * blockDim.x + threadIdx.x;
    if (i >= total) return;
    int s = cands[i].s;
    int t = cands[i].t;
    int best_u = 0;
    /* The root itself must be a Golomb ruler: {s, t, t-s} are distinct.
     * Rejecting this here avoids both GPU work and a later CPU-only check. */
    if (t - s == s) { ok[i] = 0; u_hints[i] = 0; return; }
    // Endpoint-aware depth=3 state: {0, s, t, L}.  L is a fixed mark,
    // not a hint, so every comparison below is a necessary condition.
    int rem = n - 3; // remaining marks including final
    // The rem gaps after t are pairwise distinct => sum >= rem*(rem+1)/2.
    int tri_needed = rem * (rem + 1) / 2; // minimal additional length needed after 't'
    if (t + tri_needed > L) { ok[i] = 0; u_hints[i] = 0; return; }

    int base[6] = {L, s, t, t - s, L - s, L - t};
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < a; ++b)
            if (base[a] == base[b]) { ok[i] = 0; u_hints[i] = 0; return; }

    // First next bound using triangular after-placing bound
    int rem_after1 = rem - 1;                    // after choosing u
    int tri_after1 = rem_after1 * (rem_after1 + 1) / 2;
    int max_u = L - tri_after1;
    if (max_u >= L) max_u = L - 1;               // L is already fixed
    if (max_u <= t) { ok[i] = 0; u_hints[i] = 0; return; }

    unsigned char ok1 = 0, ok2 = 0;
    for (int u = t + 1; u <= max_u; ++u) {
        int du[4] = {u, u - s, u - t, L - u};
        bool u_valid = true;
        for (int a = 0; a < 4 && u_valid; ++a) {
            for (int b = 0; b < a; ++b)
                if (du[a] == du[b]) { u_valid = false; break; }
            for (int b = 0; b < 6 && u_valid; ++b)
                if (du[a] == base[b]) { u_valid = false; break; }
        }
        if (!u_valid) continue;
        ok1 = 1; // one-step feasible
        if (n == 5) { best_u = u; break; }

        // Two-step feasibility: try to place v > u.
        // After v, (n-5) marks remain; their gaps are distinct, so
        // v <= L - (n-5)(n-4)/2, and v > u must be possible.
        int rem2 = rem_after1 - 1;                 // remaining after placing u
        int tri_needed2 = rem2 * (rem2 + 1) / 2;  // (n-5)(n-4)/2
        if (u + tri_needed2 > L) continue;
        int max_v = L - tri_needed2;
        if (max_v >= L) max_v = L - 1;           // L is already fixed
        if (max_v <= u) continue;

        // Distances present after u: base plus all four distances to u.
        for (int v = u + 1; v <= max_v; ++v) {
            int dv[5] = {v, v - s, v - t, v - u, L - v};
            bool v_valid = true;
            for (int a = 0; a < 5 && v_valid; ++a) {
                for (int b = 0; b < a; ++b)
                    if (dv[a] == dv[b]) { v_valid = false; break; }
                for (int b = 0; b < 6 && v_valid; ++b)
                    if (dv[a] == base[b]) { v_valid = false; break; }
                for (int b = 0; b < 4 && v_valid; ++b)
                    if (dv[a] == du[b]) { v_valid = false; break; }
            }
            if (!v_valid) continue;
            ok2 = 2; best_u = (best_u == 0 ? u : best_u); break;
        }
        if (ok2) break;
        if (!best_u) best_u = u; // remember first one-step-feasible u as fallback
    }
    // ok1 sets bit0, ok2 sets bit1: combine into a single status byte.
    ok[i] = (unsigned char)(ok1 | ok2);
    u_hints[i] = best_u;
}

/* ---------------- Device-side exact DFS ----------------
 * One thread completes one prefix with an iterative endpoint-aware DFS.
 * found_flag lives in host-mapped memory so CPU and GPU share it.
 *
 * The distance bitset is kept in registers: every access goes through
 * fully-unrolled word selection (template W), so no local-memory
 * round trips occur in the hot loop.  Backtracking needs no resume
 * stack: after returning from depth d+1 the next candidate at depth d
 * is pos[d] + 1, and pos[] still holds the removed mark. */
template <int W>
__device__ __forceinline__ bool bs_test(const uint64_t (&bs)[W], int d)
{
    bool r = false;
#pragma unroll
    for (int k = 0; k < W; ++k)
        if ((d >> 6) == k) r = ((bs[k] >> (d & 63)) & 1ULL) != 0ULL;
    return r;
}

template <int W>
__device__ __forceinline__ void bs_set(uint64_t (&bs)[W], int d)
{
    const uint64_t mask = 1ULL << (d & 63);
#pragma unroll
    for (int k = 0; k < W; ++k)
        if ((d >> 6) == k) bs[k] |= mask;
}

template <int W>
__device__ __forceinline__ void bs_clr(uint64_t (&bs)[W], int d)
{
    const uint64_t mask = ~(1ULL << (d & 63));
#pragma unroll
    for (int k = 0; k < W; ++k)
        if ((d >> 6) == k) bs[k] &= mask;
}

/* Fast path for L <= 255: the bitset lives in four scalar words, so every
 * bit test/set/clear is pure register ALU with no local-memory traffic. */
__global__ void __launch_bounds__(256)
frontier_dfs_kernel_fast(int n, int L, const FrontierPrefix *__restrict__ list, long long count,
                          volatile int *__restrict__ found_flag,
                          int *__restrict__ result, int *__restrict__ winner)
{
    const long long i = (long long)blockIdx.x * (long long)blockDim.x + threadIdx.x;
    if (i >= count) return;
    if (*found_flag) return;

    const FrontierPrefix p = list[i];
    const int depth = prefix_depth(p);

    int pos[MAX_MARKS];
    uint64_t b0 = 0ULL, b1 = 0ULL, b2 = 0ULL, b3 = 0ULL;

#define FAST_BS_TEST(d) \
    (((d) < 64) ? ((b0 >> ((d) & 63)) & 1ULL) \
     : ((d) < 128) ? ((b1 >> ((d) & 63)) & 1ULL) \
     : ((d) < 192) ? ((b2 >> ((d) & 63)) & 1ULL) \
     : ((b3 >> ((d) & 63)) & 1ULL))
#define FAST_BS_SET(d) do { \
        const uint64_t m_ = 1ULL << ((d) & 63); \
        if ((d) < 64) b0 |= m_; else if ((d) < 128) b1 |= m_; \
        else if ((d) < 192) b2 |= m_; else b3 |= m_; \
    } while (0)
#define FAST_BS_CLR(d) do { \
        const uint64_t m_ = ~(1ULL << ((d) & 63)); \
        if ((d) < 64) b0 &= m_; else if ((d) < 128) b1 &= m_; \
        else if ((d) < 192) b2 &= m_; else b3 &= m_; \
    } while (0)

    pos[0] = 0; pos[1] = p.s; pos[2] = p.t; pos[3] = p.u;
    if (p.v > 0) pos[4] = p.v;
    if (p.w > 0) pos[5] = p.w;
    pos[n - 1] = L;

    bool ok = true;
    for (int a = 0; a < depth && ok; ++a) {
        for (int b = a + 1; b < depth && ok; ++b) {
            const int d = pos[b] - pos[a];
            if (FAST_BS_TEST(d)) ok = false;
            else FAST_BS_SET(d);
        }
        if (ok) {
            const int dl = L - pos[a];
            if (FAST_BS_TEST(dl)) ok = false;
            else FAST_BS_SET(dl);
        }
    }
    if (!ok) return; /* defensive: generator proved validity already */

    int d = depth;
    int c = pos[d - 1] + 1; /* first candidate at this level */
    unsigned int steps = 0;
    while (d >= depth) {
        if (d == n - 1) {
            /* All inner marks placed: pos[n-1] == L is preset. */
            if (atomicCAS(winner, 0, 1) == 0) {
                for (int k = 0; k < n; ++k) result[k] = pos[k];
                __threadfence();
            }
            *found_flag = 1;
            return;
        }
        const int last = pos[d - 1];
        const int max_next = L - (n - 1 - d);
        bool placed = false;
        while (c <= max_next) {
            ++steps;
            if ((steps & 1023u) == 0u && *found_flag) return;
            const int gap = c - last; /* distance to predecessor */
            if (FAST_BS_TEST(gap)) { ++c; continue; }
            const int d_end = L - c; /* distance to the fixed endpoint */
            if (FAST_BS_TEST(d_end)) { ++c; continue; }
            bool ok2 = true;
            for (int j = 0; j < d; ++j) {
                if (FAST_BS_TEST(c - pos[j])) { ok2 = false; break; }
            }
            if (!ok2) { ++c; continue; }
            bool clash = false; /* a left distance may equal d_end */
            for (int j = 0; j < d; ++j) {
                if (c - pos[j] == d_end) { clash = true; break; }
            }
            if (clash) { ++c; continue; }
            /* commit */
            pos[d] = c;
            for (int j = 0; j < d; ++j) FAST_BS_SET(c - pos[j]);
            FAST_BS_SET(d_end);
            ++d;
            c = c + 1; /* first candidate of the deeper level */
            placed = true;
            break;
        }
        if (!placed) {
            /* level exhausted: backtrack and remove pos[d]'s distances */
            --d;
            if (d >= depth) {
                const int mk = pos[d];
                for (int j = 0; j < d; ++j) FAST_BS_CLR(mk - pos[j]);
                FAST_BS_CLR(L - mk);
                c = mk + 1; /* resume after the removed mark */
            }
        }
    }
#undef FAST_BS_TEST
#undef FAST_BS_SET
#undef FAST_BS_CLR
}

template <int W>
__global__ void __launch_bounds__(256)
frontier_dfs_kernel_w(int n, int L, const FrontierPrefix *__restrict__ list, long long count,
                      volatile int *__restrict__ found_flag,
                      int *__restrict__ result, int *__restrict__ winner)
{
    const long long i = (long long)blockIdx.x * (long long)blockDim.x + threadIdx.x;
    if (i >= count) return;
    if (*found_flag) return;

    const FrontierPrefix p = list[i];
    const int depth = prefix_depth(p);

    int pos[MAX_MARKS];
    uint64_t bs[W];

    pos[0] = 0; pos[1] = p.s; pos[2] = p.t; pos[3] = p.u;
    if (p.v > 0) pos[4] = p.v;
    if (p.w > 0) pos[5] = p.w;
    pos[n - 1] = L;
#pragma unroll
    for (int w = 0; w < W; ++w) bs[w] = 0ULL;

    bool ok = true;
    for (int a = 0; a < depth && ok; ++a) {
        for (int b = a + 1; b < depth && ok; ++b) {
            const int d = pos[b] - pos[a];
            if (bs_test(bs, d)) ok = false;
            else bs_set(bs, d);
        }
        if (ok) {
            const int dl = L - pos[a];
            if (bs_test(bs, dl)) ok = false;
            else bs_set(bs, dl);
        }
    }
    if (!ok) return; /* defensive: generator proved validity already */

    int d = depth;
    int c = pos[d - 1] + 1; /* first candidate at this level */
    unsigned int steps = 0;
    while (d >= depth) {
        if (d == n - 1) {
            /* All inner marks placed: pos[n-1] == L is preset. */
            if (atomicCAS(winner, 0, 1) == 0) {
                for (int k = 0; k < n; ++k) result[k] = pos[k];
                __threadfence();
            }
            *found_flag = 1;
            return;
        }
        const int last = pos[d - 1];
        const int max_next = L - (n - 1 - d);
        bool placed = false;
        while (c <= max_next) {
            ++steps;
            if ((steps & 1023u) == 0u && *found_flag) return;
            const int gap = c - last; /* distance to predecessor */
            if (bs_test(bs, gap)) { ++c; continue; }
            const int d_end = L - c; /* distance to the fixed endpoint */
            if (bs_test(bs, d_end)) { ++c; continue; }
            bool ok2 = true;
            for (int j = 0; j < d; ++j) {
                if (bs_test(bs, c - pos[j])) { ok2 = false; break; }
            }
            if (!ok2) { ++c; continue; }
            bool clash = false; /* a left distance may equal d_end */
            for (int j = 0; j < d; ++j) {
                if (c - pos[j] == d_end) { clash = true; break; }
            }
            if (clash) { ++c; continue; }
            /* commit */
            pos[d] = c;
            for (int j = 0; j < d; ++j) bs_set(bs, c - pos[j]);
            bs_set(bs, d_end);
            ++d;
            c = c + 1; /* first candidate of the deeper level */
            placed = true;
            break;
        }
        if (!placed) {
            /* level exhausted: backtrack and remove pos[d]'s distances */
            --d;
            if (d >= depth) {
                const int mk = pos[d];
                for (int j = 0; j < d; ++j) bs_clr(bs, mk - pos[j]);
                bs_clr(bs, L - mk);
                c = mk + 1; /* resume after the removed mark */
            }
        }
    }
}

/* Persistent bit-parallel kernel (L <= 255): every thread pulls prefixes
 * from a device-side counter until the chunk is drained, so a slow subtree
 * only blocks its own thread, not a whole launch.  The DFS polls `stop` in
 * device memory (an L2 hit); polling the host-mapped flag from ~24k threads
 * saturates PCIe and stalled the whole GPU.  The host propagates a CPU hit
 * into `stop` with an async copy; a GPU hit sets both flags. */
template <int K, int LV>
__global__ void __launch_bounds__(256)
dfs_bits_kernel(int n, int L, const FrontierPrefix *__restrict__ list, long long count,
                unsigned long long *__restrict__ next, int mirror,
                volatile int *__restrict__ stop, volatile int *__restrict__ found_flag,
                int *__restrict__ result, int *__restrict__ winner,
                unsigned long long *__restrict__ nsol)
{
    for (;;) {
        if (*stop) return;
        const unsigned long long i = atomicAdd(next, 1ULL);
        if (i >= (unsigned long long)count) return;
        const FrontierPrefix p = list[i];
        const int depth = prefix_depth(p);
        int pos[MAX_MARKS];
        pos[0] = 0; pos[1] = p.s; pos[2] = p.t; pos[3] = p.u;
        if (p.v > 0) pos[4] = p.v;
        if (p.w > 0) pos[5] = p.w;
        /* launch_bits_dfs picks LV >= n-2-depth for every prefix */
        const int r = gb_dfs_reg<K, LV>(n, L, depth, pos, c_ogr, mirror != 0, stop, nsol);
        if (r == 1) {
            if (atomicCAS(winner, 0, 1) == 0) {
                for (int k = 0; k < n; ++k) result[k] = pos[k];
                __threadfence_system();
            }
            *stop = 1;
            *found_flag = 1;
            return;
        }
        if (r < 0) return;
    }
}

static int bits_words(int L) { return (L + 1 + 31) / 32; }

static bool bits_supported(int n, int L)
{
    /* the kernel register stack holds up to 16 levels below a depth-4 prefix */
    return L + 1 <= 256 && n - 1 - 4 <= GB_MAX_LEVELS && n - 2 - 4 <= 16;
}

static int g_bits_grid = 0; /* persistent grid size (blocks), set at startup */

template <int LV>
static void launch_bits_dfs_lv(int n, int L, int blocks, const FrontierPrefix *d_list, long long cnt,
                               unsigned long long *d_next, int mirror, volatile int *d_stop,
                               volatile int *flag, int *d_result, int *d_winner,
                               unsigned long long *d_count, cudaStream_t stream)
{
    switch (bits_words(L)) {
        case 1: case 2: dfs_bits_kernel<2, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        case 3: dfs_bits_kernel<3, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        case 4: dfs_bits_kernel<4, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        case 5: dfs_bits_kernel<5, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        case 6: dfs_bits_kernel<6, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        case 7: dfs_bits_kernel<7, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
        default: dfs_bits_kernel<8, LV><<<blocks, 256, 0, stream>>>(n, L, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count); break;
    }
}

/* lv_need = deepest register stack any prefix of the list requires
 * (n-2-min_depth); the shallowest fitting instantiation is used. */
static void launch_bits_dfs(int n, int L, int lv_need, const FrontierPrefix *d_list, long long cnt,
                            unsigned long long *d_next, int mirror, volatile int *d_stop,
                            volatile int *flag, int *d_result, int *d_winner,
                            unsigned long long *d_count, cudaStream_t stream)
{
    const int threads = 256;
    long long need = (cnt + threads - 1) / threads;
    int blocks = g_bits_grid > 0 ? g_bits_grid : 24;
    if (need < blocks) blocks = (int)need;
    if (lv_need <= 8) launch_bits_dfs_lv<8>(n, L, blocks, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count, stream);
    else if (lv_need <= 12) launch_bits_dfs_lv<12>(n, L, blocks, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count, stream);
    else launch_bits_dfs_lv<16>(n, L, blocks, d_list, cnt, d_next, mirror, d_stop, flag, d_result, d_winner, d_count, stream);
}

static void init_bits_grid(void)
{
    /* cudaDeviceGetAttribute is ABI-stable; cudaDeviceProp's layout differs
     * between toolkit headers (13.x) and an older linked runtime (12.9). */
    int sms = 0;
    if (cudaDeviceGetAttribute(&sms, cudaDevAttrMultiProcessorCount, 0) != cudaSuccess || sms < 1) return;
    int per_sm = 0;
    if (cudaOccupancyMaxActiveBlocksPerMultiprocessor(&per_sm, dfs_bits_kernel<5, 8>, 256, 0) != cudaSuccess || per_sm < 1)
        per_sm = 2;
    const char *env_g = getenv("GOLOMB_GRID_PER_SM");
    if (env_g && *env_g && atoi(env_g) > 0 && atoi(env_g) < per_sm) per_sm = atoi(env_g);
    g_bits_grid = sms * per_sm;
    fprintf(stderr, "[CUDA] Persistent DFS grid: %d SMs x %d blocks x 256 threads.\n", sms, per_sm);
}

/* Host counterpart: the same bit-parallel DFS for the CPU threads. */
static int cpu_bits_dfs(int n, int L, int depth, int *pos, bool mirror, volatile int *flag,
                        long long *count = nullptr)
{
    switch (bits_words(L)) {
        case 1: case 2: return gb_dfs<2>(n, L, depth, pos, h_ogr, mirror, flag, count);
        case 3: return gb_dfs<3>(n, L, depth, pos, h_ogr, mirror, flag, count);
        case 4: return gb_dfs<4>(n, L, depth, pos, h_ogr, mirror, flag, count);
        case 5: return gb_dfs<5>(n, L, depth, pos, h_ogr, mirror, flag, count);
        case 6: return gb_dfs<6>(n, L, depth, pos, h_ogr, mirror, flag, count);
        case 7: return gb_dfs<7>(n, L, depth, pos, h_ogr, mirror, flag, count);
        default: return gb_dfs<8>(n, L, depth, pos, h_ogr, mirror, flag, count);
    }
}

/* Dispatch on the number of bitset words implied by L (legacy kernels, used
 * only for L > 255 where the bit-parallel masks do not fit). */
/* Dispatch on the number of bitset words implied by L.  L <= 255 uses the
 * register-word fast kernel (covers every LUT entry up to n=17). */
static void launch_frontier_dfs(int n, int L, const FrontierPrefix *d_list, long long base_cnt,
                                volatile int *flag, int *d_result, int *d_winner,
                                cudaStream_t stream)
{
    const int threads = 256;
    const int blocks = (int)((base_cnt + threads - 1) / threads);
    if (L <= 255) {
        frontier_dfs_kernel_fast<<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner);
        return;
    }
    const int W = (L >> 6) + 1; /* L <= 600 -> W <= 10 */
    switch (W) {
        case 1: frontier_dfs_kernel_w<1><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 2: frontier_dfs_kernel_w<2><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 3: frontier_dfs_kernel_w<3><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 4: frontier_dfs_kernel_w<4><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 5: frontier_dfs_kernel_w<5><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 6: frontier_dfs_kernel_w<6><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 7: frontier_dfs_kernel_w<7><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 8: frontier_dfs_kernel_w<8><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        case 9: frontier_dfs_kernel_w<9><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
        default: frontier_dfs_kernel_w<10><<<blocks, threads, 0, stream>>>(n, L, d_list, base_cnt, flag, d_result, d_winner); break;
    }
}

/* ---------------- Heartbeat ---------------- */
static volatile int g_done = 0;
static volatile int g_current_L = -1;
static double g_vt_sec = 0.0;
static struct timespec g_ts_start;

static void *heartbeat_thread(void *)
{
    while (!g_done) {
        struct timespec ts_now; clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double since = (ts_now.tv_sec - g_ts_start.tv_sec) + (ts_now.tv_nsec - g_ts_start.tv_nsec) / 1e9;
        int L = g_current_L;
        if (g_vt_sec > 0.0 && L >= 0) {
            // format mm:ss.mmm
            int minutes = (int)(since / 60.0);
            double seconds = since - minutes * 60.0;
            if (minutes > 0) fprintf(stderr, "[VT] %02d:%06.3f elapsed – current L=%d\n", minutes, seconds, L);
            else              fprintf(stderr, "[VT] %.3f s elapsed – current L=%d\n", seconds, L);
            fflush(stderr);
        }
        struct timespec req = { (time_t)g_vt_sec, (long)((g_vt_sec - (time_t)g_vt_sec) * 1e9) };
        nanosleep(&req, NULL);
    }
    return NULL;
}

/* ---------------- Optional async prefilter worker ---------------- */
struct PrefilterJob {
    int n;
    int L;
    long long total;
    const Cand *cands; // input snapshot for device copy
    std::vector<unsigned char> ok_out;
    size_t ok2_cnt{0}, ok1_cnt{0};
    volatile int done{0};
    int success{0};
    std::vector<int> u_hints_out;
};

static bool cuda_check(cudaError_t status, const char *operation)
{
    if (status == cudaSuccess) return true;
    fprintf(stderr, "[CUDA] %s failed: %s (%d)\n", operation,
            cudaGetErrorString(status), (int)status);
    return false;
}

static bool build_frontier(int n, int L, const std::vector<Cand> &cands,
                           std::vector<FrontierPrefix> &frontier)
{
    if (n < 5 || cands.empty()) return false;
    const size_t capacity_size = cands.size() * (size_t)L;
    if (capacity_size == 0 || capacity_size > UINT32_MAX) return false;
    Cand *d_cands = nullptr;
    FrontierPrefix *d_frontier = nullptr;
    unsigned int *d_count = nullptr;
    bool success = cuda_check(cudaMalloc(&d_cands, cands.size() * sizeof(Cand)), "cudaMalloc(frontier candidates)");
    if (success) success = cuda_check(cudaMalloc(&d_frontier, capacity_size * sizeof(FrontierPrefix)), "cudaMalloc(frontier output)");
    if (success) success = cuda_check(cudaMalloc(&d_count, sizeof(unsigned int)), "cudaMalloc(frontier count)");
    if (success) success = cuda_check(cudaMemcpy(d_cands, cands.data(), cands.size() * sizeof(Cand), cudaMemcpyHostToDevice), "cudaMemcpy(frontier candidates H2D)");
    if (success) success = cuda_check(cudaMemset(d_count, 0, sizeof(unsigned int)), "cudaMemset(frontier count)");
    if (success) {
        const int threads = 256;
        const int blocks = (int)((cands.size() + threads - 1) / threads);
        frontier_kernel<<<blocks, threads>>>(n, L, d_cands, (int64_t)cands.size(), d_frontier, d_count, (unsigned int)capacity_size);
        success = cuda_check(cudaGetLastError(), "frontier kernel launch") &&
                  cuda_check(cudaDeviceSynchronize(), "frontier kernel execution");
    }
    unsigned int count = 0;
    if (success) success = cuda_check(cudaMemcpy(&count, d_count, sizeof(count), cudaMemcpyDeviceToHost), "cudaMemcpy(frontier count D2H)");
    int d5_min = 14;
    {
        const char *env_d5 = getenv("GOLOMB_D5_MIN");
        if (env_d5 && *env_d5) d5_min = atoi(env_d5);
    }
    if (success && count <= capacity_size && count > 0 && n >= d5_min && n >= 7) {
        /* Pass 2: expand depth-4 prefixes to depth-5 on the device.  The
         * score of each depth-4 prefix is exactly its legal-v count, so
         * the host-side sum is the exact output capacity.  Depth-5 items
         * are ~20x smaller subtrees, which keeps GPU chunk latency in the
         * millisecond range instead of minutes.  On any failure the
         * coarse depth-4 list is still valid, so it is the fallback. */
        std::vector<FrontierPrefix> l4(count);
        if (cuda_check(cudaMemcpy(l4.data(), d_frontier, count * sizeof(FrontierPrefix),
                                  cudaMemcpyDeviceToHost), "cudaMemcpy(frontier d4 D2H)")) {
            uint64_t cap5 = 0;
            for (size_t i = 0; i < count; ++i) cap5 += (unsigned int)l4[i].score;
            const uint64_t CAP5_MAX = 48ull * 1024 * 1024; /* ~1.1GB output cap */
            bool expanded = false;
            FrontierPrefix *d_out5 = nullptr;
            unsigned int *d_count5 = nullptr;
            bool ok5 = cap5 > 0 && cap5 <= CAP5_MAX;
            if (ok5) ok5 = cuda_check(cudaMalloc(&d_out5, (size_t)cap5 * sizeof(FrontierPrefix)), "cudaMalloc(frontier depth-5)");
            if (ok5) ok5 = cuda_check(cudaMalloc(&d_count5, sizeof(unsigned int)), "cudaMalloc(frontier count5)");
            if (ok5) ok5 = cuda_check(cudaMemset(d_count5, 0, sizeof(unsigned int)), "cudaMemset(count5)");
            unsigned int count5 = 0;
            if (ok5) {
                const int threads = 256;
                const long long blocks = (count + threads - 1) / threads;
                expand_kernel<<<(unsigned)blocks, threads>>>(n, L, d_frontier, count,
                                                             d_out5, d_count5, (unsigned int)cap5);
                ok5 = cuda_check(cudaGetLastError(), "expand kernel launch") &&
                      cuda_check(cudaDeviceSynchronize(), "expand kernel execution") &&
                      cuda_check(cudaMemcpy(&count5, d_count5, sizeof(count5), cudaMemcpyDeviceToHost), "cudaMemcpy(count5 D2H)");
            }
            if (ok5 && count5 > 0) {
                frontier.resize(count5);
                expanded = cuda_check(cudaMemcpy(frontier.data(), d_out5,
                                                 count5 * sizeof(FrontierPrefix), cudaMemcpyDeviceToHost),
                                      "cudaMemcpy(frontier d5 D2H)");
            }
            if (d_out5) cudaFree(d_out5);
            if (d_count5) cudaFree(d_count5);
            if (!expanded) frontier.assign(l4.begin(), l4.end());
            else fprintf(stderr, "[CUDA] Expanded frontier to %u depth-5 prefixes.\n", count5);
        }
        if (frontier.empty()) success = false;
        else success = true;
    } else if (success && count <= capacity_size) {
        frontier.resize(count);
        success = count == 0 || cuda_check(cudaMemcpy(frontier.data(), d_frontier,
                                                       count * sizeof(FrontierPrefix), cudaMemcpyDeviceToHost),
                                             "cudaMemcpy(frontier D2H");
    } else if (success) {
        fprintf(stderr, "[CUDA] frontier overflow: %u > %zu\n", count, capacity_size);
        success = false;
    }
    if (success && !frontier.empty()) {
        std::stable_sort(frontier.begin(), frontier.end(),
                         [](const FrontierPrefix &a, const FrontierPrefix &b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.s != b.s) return a.s < b.s;
            if (a.t != b.t) return a.t < b.t;
            if (a.u != b.u) return a.u < b.u;
            return a.v < b.v;
        });
    }
    cudaFree(d_cands); cudaFree(d_frontier); cudaFree(d_count);
    if (!success) frontier.clear();
    return success;
}

static void *prefilter_worker(void *arg)
{
    PrefilterJob *job = (PrefilterJob*)arg;
    long long total = job->total;
    Cand *d_cands = nullptr; unsigned char *d_ok = nullptr; int *d_u_hints = nullptr;
    unsigned char *h_ok = nullptr; int *h_u_hints = nullptr;
    bool success = cuda_check(cudaSetDevice(0), "cudaSetDevice");
    if (success) success = cuda_check(cudaMalloc(&d_cands, sizeof(Cand) * (size_t)total), "cudaMalloc(candidates)");
    if (success) success = cuda_check(cudaMalloc(&d_ok, (size_t)total), "cudaMalloc(status)");
    if (success) success = cuda_check(cudaMalloc(&d_u_hints, sizeof(int) * (size_t)total), "cudaMalloc(hints)");
    if (success) success = cuda_check(cudaMemcpy(d_cands, job->cands, sizeof(Cand) * (size_t)total, cudaMemcpyHostToDevice), "cudaMemcpy(candidates H2D)");
    if (success) {
        const int threads = 256;
        const int blocks = (int)((total + threads - 1) / threads);
        prefilter_kernel<<<blocks, threads>>>(job->n, job->L, d_cands, total, d_ok, d_u_hints);
        success = cuda_check(cudaGetLastError(), "prefilter kernel launch") &&
                  cuda_check(cudaDeviceSynchronize(), "prefilter kernel execution");
    }
    if (success) {
        h_ok = (unsigned char*)malloc((size_t)total);
        h_u_hints = (int*)malloc(sizeof(int) * (size_t)total);
        success = h_ok && h_u_hints;
        if (!success) fprintf(stderr, "[CUDA] host allocation for prefilter result failed\n");
    }
    if (success) success = cuda_check(cudaMemcpy(h_ok, d_ok, (size_t)total, cudaMemcpyDeviceToHost), "cudaMemcpy(status D2H)");
    if (success) success = cuda_check(cudaMemcpy(h_u_hints, d_u_hints, sizeof(int) * (size_t)total, cudaMemcpyDeviceToHost), "cudaMemcpy(hints D2H)");
    if (success) {
        size_t ok2 = 0, ok1 = 0;
        for (size_t i = 0; i < (size_t)total; ++i) { if (h_ok[i] >= 2) ++ok2; else if (h_ok[i] == 1) ++ok1; }
        job->ok2_cnt = ok2; job->ok1_cnt = ok1;
        job->ok_out.assign(h_ok, h_ok + (size_t)total);
        job->u_hints_out.assign(h_u_hints, h_u_hints + (size_t)total);
    } else {
        job->ok_out.clear();
        job->u_hints_out.clear();
    }
    cudaFree(d_cands); cudaFree(d_ok); cudaFree(d_u_hints); free(h_ok); free(h_u_hints);
    job->success = success ? 1 : 0;
    job->done = 1;
    return NULL;
}

/* ---------------- Hybrid GPU+CPU prefix search ---------------- */
static bool g_gpu_available = false;
static long long g_gpu_chunk = 0; /* 0 = auto (GOLOMB_GPU_CHUNK overrides) */
static int g_chunk_debug = 0;     /* GOLOMB_DEBUG=1 enables per-chunk stderr logs */
/* GOLOMB_COUNT=1 (testing): count every ruler in the frontier instead of
 * stopping at the first; GPU and CPU totals are reported separately. */
static bool g_count_mode = false;
static std::atomic<long long> g_cpu_count{0};
static unsigned long long g_gpu_count = 0;

/* Two-ended work queue over the score-sorted prefix list.  The GPU takes
 * large chunks from the front (best-scored prefixes), the CPU threads take
 * single prefixes from the back, so the slower CPU never holds a promising
 * subtree the GPU would finish far sooner.  Every prefix is handed out
 * exactly once. */
struct PrefixQueue {
    std::mutex mu;
    long long front = 0;
    long long back = 0;
    bool take_front(long long want, long long &base, long long &cnt)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (front >= back) return false;
        base = front;
        cnt = back - front < want ? back - front : want;
        front += cnt;
        return true;
    }
    bool take_back(long long &j)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (front >= back) return false;
        j = --back;
        return true;
    }
    long long front_claimed()
    {
        std::lock_guard<std::mutex> lk(mu);
        return front;
    }
};

/* Exact CPU completion of one prefix: 1 = found (pos filled), 0 = subtree
 * exhausted, -1 = aborted because another worker found a ruler. */
static int cpu_complete_prefix(int n, int L, const FrontierPrefix &p, bool mirror,
                               volatile int *vflag, int *pos, bool verbose)
{
    const int depth = prefix_depth(p);
    if (bits_supported(n, L)) {
        pos[0] = 0; pos[1] = p.s; pos[2] = p.t; pos[3] = p.u;
        if (p.v > 0) pos[4] = p.v;
        if (p.w > 0) pos[5] = p.w;
        if (g_count_mode) {
            long long c = 0;
            cpu_bits_dfs(n, L, depth, pos, mirror, nullptr, &c);
            g_cpu_count += c;
            return 0;
        }
        return cpu_bits_dfs(n, L, depth, pos, mirror, vflag);
    }
    uint64_t bs[BS_WORDS] = {0};
    if (!build_prefix_state(p, n, L, pos, bs)) return 0;
    return dfs_endpoint_from_state(depth, n, L, pos, bs, verbose) ? 1 : 0;
}

/* Exact CPU DFS over a contiguous slice of the prefix list (failure
 * fallback).  Sets *vflag and fills *res when a ruler is found. */
static bool cpu_prefix_dfs_range(int n, int L, const FrontierPrefix *list,
                                 long long begin, long long end, bool mirror,
                                 volatile int *vflag, ruler_t *res, bool verbose)
{
    if (end <= begin) return false;
    int local_found = 0;
    ruler_t local{};
#pragma omp parallel for schedule(dynamic, 1)
    for (long long j = begin; j < end; ++j) {
        if (*vflag) continue;
        int pos[MAX_MARKS] = {0};
        if (cpu_complete_prefix(n, L, list[j], mirror, vflag, pos, verbose) == 1) {
#pragma omp critical(set_result)
            {
                if (!local_found) {
                    local.marks = n;
                    local.length = L;
                    memcpy(local.pos, pos, n * sizeof(int));
                    local_found = 1;
                }
                *vflag = 1;
            }
        }
    }
    if (local_found) { *res = local; return true; }
    return false;
}

struct GpuDfsJob {
    int n;
    int L;
    int mirror;
    int lv_need;                 /* n - 2 - (shallowest prefix depth) */
    const FrontierPrefix *list;  /* host array */
    PrefixQueue *queue;
    volatile int *flag;          /* shared found flag (host-mapped) */
    int winner;                  /* 1 = GPU wrote a valid-looking result */
    int result[MAX_MARKS];
    int cpu_found;               /* worker's CPU fallback found a ruler */
    ruler_t cpu_res;
};

/* GPU worker: pulls chunks from the front of the queue.  Two streams keep
 * one chunk queued behind the running one; since the persistent blocks of
 * a drained chunk retire individually, the next chunk fills the SMs as the
 * previous one tails off. */
static void *gpu_dfs_worker(void *arg)
{
    GpuDfsJob *job = (GpuDfsJob*)arg;
    job->winner = 0; job->cpu_found = 0;
    if (cudaSetDevice(0) != cudaSuccess) return NULL;
    const bool bits = bits_supported(job->n, job->L);
    long long chunk = g_gpu_chunk;
    if (chunk <= 0) chunk = bits ? (long long)(g_bits_grid > 0 ? g_bits_grid : 96) * 256 * 4 : 8192;
    enum { NS = 2 };
    FrontierPrefix *d_list[NS] = {nullptr, nullptr};
    unsigned long long *d_next = nullptr;
    int *d_stop = nullptr;
    unsigned long long *d_count = nullptr;
    int *h_one = nullptr;
    bool stop_sent = false;
    cudaStream_t ctl;
    cudaStreamCreateWithFlags(&ctl, cudaStreamNonBlocking);
    int *d_result = nullptr;
    int *d_winner = nullptr;
    cudaStream_t stream[NS];
    for (int k = 0; k < NS; ++k) cudaStreamCreate(&stream[k]);
    bool ok = true;
    for (int k = 0; k < NS && ok; ++k)
        ok = cuda_check(cudaMalloc(&d_list[k], (size_t)chunk * sizeof(FrontierPrefix)), "cudaMalloc(dfs list)");
    if (ok) ok = cuda_check(cudaMalloc(&d_next, NS * sizeof(unsigned long long)), "cudaMalloc(dfs cursor)");
    if (ok && g_count_mode) {
        ok = cuda_check(cudaMalloc(&d_count, sizeof(unsigned long long)), "cudaMalloc(count)") &&
             cuda_check(cudaMemset(d_count, 0, sizeof(unsigned long long)), "cudaMemset(count)");
    }
    if (ok) ok = cuda_check(cudaMalloc(&d_stop, sizeof(int)), "cudaMalloc(dfs stop)");
    if (ok) ok = cuda_check(cudaMemset(d_stop, 0, sizeof(int)), "cudaMemset(dfs stop)");
    if (ok) ok = cuda_check(cudaMallocHost(&h_one, sizeof(int)), "cudaMallocHost(stop value)");
    if (ok) *h_one = 1;
    if (ok) ok = cuda_check(cudaMalloc(&d_result, MAX_MARKS * sizeof(int)), "cudaMalloc(dfs result)");
    if (ok) ok = cuda_check(cudaMalloc(&d_winner, sizeof(int)), "cudaMalloc(dfs winner)");
    if (ok) ok = cuda_check(cudaMemset(d_winner, 0, sizeof(int)), "cudaMemset(dfs winner)");
    if (ok) ok = cuda_check(cudaMemset(d_result, 0, MAX_MARKS * sizeof(int)), "cudaMemset(dfs result)");

    int flip = 0;
    long long slot_base[NS] = {-1, -1}, slot_cnt[NS] = {0, 0};
    struct timespec ts_w0; clock_gettime(CLOCK_MONOTONIC, &ts_w0);
    auto send_stop = [&]() {
        if (!stop_sent && d_stop && h_one) {
            cudaMemcpyAsync(d_stop, h_one, sizeof(int), cudaMemcpyHostToDevice, ctl);
            stop_sent = true;
        }
    };
    while (ok) {
        if (*job->flag) break;
        while (cudaStreamQuery(stream[flip]) == cudaErrorNotReady) {
            if (*job->flag) break;
            usleep(500);
        }
        if (*job->flag) break;
        if (g_chunk_debug && slot_base[flip] >= 0) {
            struct timespec ts_d; clock_gettime(CLOCK_MONOTONIC, &ts_d);
            const double tel = (ts_d.tv_sec - ts_w0.tv_sec) + (ts_d.tv_nsec - ts_w0.tv_nsec) / 1e9;
            fprintf(stderr, "[CUDA] chunk [%lld, %lld) done at t=%.2fs\n",
                    slot_base[flip], slot_base[flip] + slot_cnt[flip], tel);
        }
        long long base = 0, cnt = 0;
        if (!job->queue->take_front(chunk, base, cnt)) break;
        const int idx = flip;
        flip = (flip + 1) % NS;
        bool launched = cuda_check(cudaMemcpyAsync(d_list[idx], job->list + base,
                                                   (size_t)cnt * sizeof(FrontierPrefix),
                                                   cudaMemcpyHostToDevice, stream[idx]), "cudaMemcpy(dfs chunk H2D)");
        if (launched && bits) {
            launched = cuda_check(cudaMemsetAsync(d_next + idx, 0, sizeof(unsigned long long), stream[idx]), "cudaMemset(dfs cursor)");
            if (launched) launch_bits_dfs(job->n, job->L, job->lv_need, d_list[idx], cnt, d_next + idx, job->mirror,
                                          d_stop, job->flag, d_result, d_winner, d_count, stream[idx]);
        } else if (launched && g_count_mode) {
            launched = false; /* legacy kernel cannot count: CPU completes the chunk */
        } else if (launched) {
            launch_frontier_dfs(job->n, job->L, d_list[idx], cnt, job->flag, d_result, d_winner, stream[idx]);
        }
        if (launched) launched = cuda_check(cudaGetLastError(), "dfs kernel launch");
        if (!launched) {
            /* this chunk never ran on the device: complete it on the CPU */
            job->cpu_found = cpu_prefix_dfs_range(job->n, job->L, job->list, base, base + cnt, job->mirror != 0,
                                                  job->flag, &job->cpu_res, false) || job->cpu_found;
            break;
        }
        slot_base[idx] = base; slot_cnt[idx] = cnt;
        if (g_chunk_debug) {
            struct timespec ts_c; clock_gettime(CLOCK_MONOTONIC, &ts_c);
            const double t0 = (ts_c.tv_sec - ts_w0.tv_sec) + (ts_c.tv_nsec - ts_w0.tv_nsec) / 1e9;
            fprintf(stderr, "[CUDA] chunk [%lld, %lld) launched at t=%.2fs\n", base, base + cnt, t0);
        }
    }
    /* Running kernels only see a CPU hit through d_stop. */
    if (*job->flag) send_stop();
    for (int k = 0; k < NS; ++k) {
        while (cudaStreamQuery(stream[k]) == cudaErrorNotReady) {
            if (*job->flag) send_stop();
            usleep(500);
        }
        if (cudaStreamSynchronize(stream[k]) != cudaSuccess) ok = false;
    }
    cudaStreamSynchronize(ctl);
    if (ok && d_count) {
        cuda_check(cudaMemcpy(&g_gpu_count, d_count, sizeof(g_gpu_count), cudaMemcpyDeviceToHost), "cudaMemcpy(count D2H)");
    }
    if (ok) {
        int w = 0;
        if (cuda_check(cudaMemcpy(&w, d_winner, sizeof(int), cudaMemcpyDeviceToHost), "cudaMemcpy(dfs winner D2H)") && w) {
            job->winner = 1;
            cuda_check(cudaMemcpy(job->result, d_result, MAX_MARKS * sizeof(int), cudaMemcpyDeviceToHost), "cudaMemcpy(dfs result D2H)");
        }
    } else if (!(*job->flag)) {
        /* A stream failed mid-flight: conservatively re-run everything the
         * GPU ever claimed (always the prefix [0, front)).  Redundant work
         * is wasted time, never wrong. */
        fprintf(stderr, "[CUDA] GPU DFS stream failed; CPU re-covers the claimed range.\n");
        const long long covered = job->queue->front_claimed();
        if (covered > 0)
            job->cpu_found = cpu_prefix_dfs_range(job->n, job->L, job->list, 0, covered, job->mirror != 0,
                                                  job->flag, &job->cpu_res, false) || job->cpu_found;
    }
    for (int k = 0; k < NS; ++k) { cudaFree(d_list[k]); cudaStreamDestroy(stream[k]); }
    cudaFree(d_next); cudaFree(d_stop); cudaFree(d_result); cudaFree(d_winner);
    if (d_count) cudaFree(d_count);
    if (h_one) cudaFreeHost(h_one);
    cudaStreamDestroy(ctl);
    return NULL;
}

/* CPU side of the hybrid: OpenMP threads pull single prefixes from the back
 * of the shared queue.  The DFS polls *vflag, so a GPU hit stops the CPU
 * within microseconds instead of after its current subtree. */
static bool cpu_prefix_dfs_queue(int n, int L, const FrontierPrefix *list, PrefixQueue &q,
                                 bool mirror, volatile int *vflag, ruler_t *res, bool verbose)
{
    int local_found = 0;
    ruler_t local{};
#pragma omp parallel
    {
        long long j;
        while (!*vflag && q.take_back(j)) {
            int pos[MAX_MARKS] = {0};
            if (cpu_complete_prefix(n, L, list[j], mirror, vflag, pos, verbose) == 1) {
#pragma omp critical(set_result)
                {
                    if (!local_found) {
                        local.marks = n;
                        local.length = L;
                        memcpy(local.pos, pos, n * sizeof(int));
                        local_found = 1;
                    }
                    *vflag = 1;
                }
                break;
            }
        }
    }
    if (local_found) { *res = local; return true; }
    return false;
}

/* Hybrid search over a sorted prefix list.  mirror=true enables the
 * first-gap < last-gap symmetry cut; it is only valid when the list covers
 * both mirror images (the full frontier), not for the guided (s0,t0) lane. */
static bool run_hybrid_prefix_search(int n, int L, const std::vector<FrontierPrefix> &list, bool mirror,
                                     volatile int *vflag, ruler_t *res, bool verbose)
{
    const long long F = (long long)list.size();
    if (F <= 0) return false;

    PrefixQueue q;
    q.front = 0;
    q.back = F;
    GpuDfsJob job{};
    job.n = n; job.L = L; job.mirror = mirror ? 1 : 0;
    {
        int min_depth = 6;
        for (const FrontierPrefix &p : list) {
            const int dp = prefix_depth(p);
            if (dp < min_depth) min_depth = dp;
        }
        job.lv_need = n - 2 - min_depth;
    }
    job.list = list.data();
    job.queue = &q;
    job.flag = vflag;

    pthread_t th;
    int gpu_started = 0;
    if (g_gpu_available && getenv("GOLOMB_NO_GPU_DFS") == NULL) {
        if (pthread_create(&th, NULL, gpu_dfs_worker, &job) == 0) gpu_started = 1;
    }

    struct timespec ts_h0; clock_gettime(CLOCK_MONOTONIC, &ts_h0);
    bool cpu_found = cpu_prefix_dfs_queue(n, L, list.data(), q, mirror, vflag, res, verbose);
    const long long cpu_items = F - q.back;

    if (gpu_started) pthread_join(th, NULL);
    {
        struct timespec ts_h1; clock_gettime(CLOCK_MONOTONIC, &ts_h1);
        const double dt = (ts_h1.tv_sec - ts_h0.tv_sec) + (ts_h1.tv_nsec - ts_h0.tv_nsec) / 1e9;
        fprintf(stderr, "[CUDA] Hybrid search: %.3f s, %lld of %lld prefixes on the CPU.\n", dt, cpu_items, F);
        if (g_count_mode)
            fprintf(stderr, "[COUNT] n=%d L=%d mirror=%d rulers=%lld (gpu=%llu cpu=%lld)\n", n, L, mirror ? 1 : 0,
                    (long long)g_gpu_count + g_cpu_count.load(), g_gpu_count, g_cpu_count.load());
    }

    if (*vflag) {
        if (cpu_found) return true; /* *res already filled */
        if (job.winner) {
            ruler_t cand{};
            cand.marks = n;
            cand.length = L;
            memcpy(cand.pos, job.result, n * sizeof(int));
            if (validate_ruler(&cand)) { *res = cand; return true; }
            fprintf(stderr, "[CUDA] GPU result failed validation; falling back to CPU.\n");
        }
        if (job.cpu_found) { *res = job.cpu_res; return true; }
        /* GPU reported a hit that did not survive validation: redo the
         * whole list on the CPU (cannot happen with a correct kernel). */
        *vflag = 0;
        return cpu_prefix_dfs_range(n, L, list.data(), 0, F, mirror, vflag, res, verbose);
    }
    return false;
}

/* ---------------- Guided fast-lane prefix builder ---------------- */
/* Expand the LUT (s0,t0) prefix into depth-4..6 subprefixes on the CPU.
 * Every level enumerates the next mark with the same necessary conditions
 * the device frontier uses, so the union of all tasks is exactly the DFS
 * subtree below (s0,t0). */
static bool build_guided_prefixes(int n, int L, int s0, int t0,
                                  std::vector<FrontierPrefix> &out)
{
    out.clear();
    if (n < 5) return false;

    /* seed validity: distances among {0, s0, t0, L} must be distinct */
    {
        const int seed_d[6] = {s0, t0, t0 - s0, L, L - s0, L - t0};
        uint64_t bs[BS_WORDS] = {0};
        for (int a = 0; a < 6; ++a) {
            if (seed_d[a] <= 0 || test_bit64(bs, seed_d[a])) return false;
            set_bit64(bs, seed_d[a]);
        }
    }

    long long target = 32768;
    const char *env = getenv("GOLOMB_GUIDED_TARGET");
    if (env && *env) {
        long long v = atoll(env);
        if (v >= 64) target = v;
    }

    struct Item { int depth; int m[8]; };
    std::vector<Item> level;
    {
        Item seed{};
        seed.depth = 3;
        seed.m[0] = 0; seed.m[1] = s0; seed.m[2] = t0;
        level.push_back(seed);
    }

    uint64_t bs[BS_WORDS];
    for (int depth = 3; depth < 6 && depth < n - 1; ++depth) {
        if (depth > 3 && (long long)level.size() >= target) break;
        std::vector<Item> next;
        for (const Item &it : level) {
            /* rebuild the distance bitset of this prefix */
            for (int w = 0; w < BS_WORDS; ++w) bs[w] = 0;
            bool ok = true;
            for (int a = 0; a < it.depth && ok; ++a) {
                for (int b = a + 1; b < it.depth && ok; ++b) {
                    const int d = it.m[b] - it.m[a];
                    if (test_bit64(bs, d)) ok = false;
                    else set_bit64(bs, d);
                }
                if (ok) {
                    const int dl = L - it.m[a];
                    if (test_bit64(bs, dl)) ok = false;
                    else set_bit64(bs, dl);
                }
            }
            if (!ok) continue; /* defensive: generator proved validity */
            const int last = it.m[it.depth - 1];
            /* After placing c, (n-1-depth) marks remain (including L):
             * their gaps are distinct => c <= L - (n-1-depth)(n-depth)/2. */
            const int rem = n - 1 - depth;
            const int max_c = L - rem * (rem + 1) / 2;
            for (int c = last + 1; c <= max_c; ++c) {
                const int d_end = L - c;
                if (test_bit64(bs, d_end)) continue;
                bool ok2 = true;
                for (int j = 0; j < it.depth && ok2; ++j) {
                    const int dist = c - it.m[j];
                    if (test_bit64(bs, dist)) ok2 = false;
                    if (dist == d_end) ok2 = false;
                }
                if (!ok2) continue;
                Item child = it;
                child.m[depth] = c;
                child.depth = depth + 1;
                next.push_back(child);
            }
        }
        if (next.empty()) break; /* subtree below (s0,t0) is dead */
        level.swap(next);
    }

    for (const Item &it : level) {
        FrontierPrefix p{};
        p.s = it.m[1]; p.t = it.m[2]; p.u = it.m[3];
        if (it.depth > 4) p.v = it.m[4];
        if (it.depth > 5) p.w = it.m[5];
        out.push_back(p);
    }
    return !out.empty();
}

static bool prefix_lex_cmp(const FrontierPrefix &a, const FrontierPrefix &b)
{
    if (a.s != b.s) return a.s < b.s;
    if (a.t != b.t) return a.t < b.t;
    if (a.u != b.u) return a.u < b.u;
    if (a.v != b.v) return a.v < b.v;
    return a.w < b.w;
}

/* ---------------- Main (CUDA-enhanced mp) ---------------- */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <n> [-b] [-v] [-H] [-f <file>] [-fi <sec>] [-vt <min>] [-wu <N>] [-dh] [-dw <W>] [-ap]\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    bool verbose = false;
    bool use_b = false;
    bool hints = false; // enable LUT hint order and fast-lane only with -H
    const char *cp_path = NULL;
    int cp_interval = 60;
    double vt_min = 0.0;
    // Tunables: warmup size and depth-3 hinting
    long long warmup_limit = 8192; // default
    const char *env_wu = getenv("GOLOMB_WARMUP");
    if (env_wu && *env_wu) {
        long long v = atoll(env_wu); if (v > 0) warmup_limit = v;
    }
    int dfs3_hint = (getenv("GOLOMB_DFS3_HINT") != NULL) ? 1 : 0;
    int u_win = 16; const char *env_uw = getenv("GOLOMB_UWIN"); if (env_uw && *env_uw) { int v = atoi(env_uw); if (v > 0) u_win = v; }
    int async_pref = (getenv("GOLOMB_ASYNC_PREF") != NULL) ? 1 : 0;
    {
        const char *env_ck = getenv("GOLOMB_GPU_CHUNK");
        if (env_ck && *env_ck) {
            long long v = atoll(env_ck);
            if (v >= 1024) g_gpu_chunk = v;
        }
        g_chunk_debug = (getenv("GOLOMB_DEBUG") != NULL) ? 1 : 0;
        g_count_mode = getenv("GOLOMB_COUNT") != NULL;
    }

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "-b") == 0) use_b = true;
        else if (strcmp(argv[i], "-H") == 0) hints = true;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) { cp_path = argv[++i]; }
        else if (strcmp(argv[i], "-fi") == 0 && i + 1 < argc) { cp_interval = atoi(argv[++i]); if (cp_interval <= 0) cp_interval = 60; }
        else if (strcmp(argv[i], "-vt") == 0 && i + 1 < argc) { vt_min = atof(argv[++i]); }
        else if (strcmp(argv[i], "-wu") == 0 && i + 1 < argc) { long long v = atoll(argv[++i]); if (v > 0) warmup_limit = v; }
        else if (strcmp(argv[i], "-dh") == 0) { dfs3_hint = 1; }
        else if (strcmp(argv[i], "-dw") == 0 && i + 1 < argc) { int v = atoi(argv[++i]); if (v > 0) u_win = v; }
        else if (strcmp(argv[i], "-ap") == 0) { async_pref = 1; }
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }

    /* Enable SIMD in C dfs unless explicitly disabled. solver.c was built with -march=native. */
    g_use_simd = (getenv("GOLOMB_NO_SIMD") == NULL);

    const ruler_t *ref = lut_lookup_by_marks(n);
    if (!use_b || !ref) {
        fprintf(stderr, "This CUDA variant currently requires -b and a known LUT length for n=%d.\n", n);
        return 3;
    }
    int target_length = ref->length;
    {
        /* Testing aid: GOLOMB_TARGET_L=<L> searches another length, e.g.
         * L_opt-1 for a luck-free exhaustive throughput measurement. */
        const char *env_l = getenv("GOLOMB_TARGET_L");
        if (env_l && *env_l) {
            int v = atoi(env_l);
            if (v > 0 && v <= MAX_LEN_BITSET) target_length = v;
        }
    }
    init_ogr_table(n);
    const bool use_mirror = getenv("GOLOMB_NO_MIRROR") == NULL;

    // Start time and heartbeat
    clock_gettime(CLOCK_MONOTONIC, &g_ts_start);
    time_t t_start_wall = time(NULL);
    char start_iso[32]; strftime(start_iso, sizeof start_iso, "%F %T", localtime(&t_start_wall));
    fprintf(stderr, "Start time: %s\n", start_iso);
    g_current_L = target_length;
    g_vt_sec = vt_min > 0 ? vt_min * 60.0 : 0.0;
    pthread_t hb_th; if (g_vt_sec > 0.0) pthread_create(&hb_th, NULL, heartbeat_thread, NULL);

    // Build candidates like -mp. After pos[2]=t, (n-3) marks remain -> t <= L - (n-3).
    int half = target_length / 2;
    int T = target_length - (n - 3);
    int second_max = half; if (second_max > T - 1) second_max = T - 1; if (second_max < 1) second_max = 1;

    long long total = 0;
    for (int s = 1; s <= second_max; ++s) {
        int cnt = T - s; if (cnt > 0) total += cnt;
    }
    std::vector<Cand> cands; cands.reserve((size_t)total);
    int use_hint_order = (hints && ref && getenv("GOLOMB_NO_HINTS") == NULL) ? 1 : 0;
    for (int s = 1; s <= second_max; ++s) {
        for (int t = s + 1; t <= T; ++t) {
            int score = 0;
            if (use_hint_order) {
                int ds = s - ref->pos[1]; if (ds < 0) ds = -ds;
                int dt = t - ref->pos[2]; if (dt < 0) dt = -dt;
                score = ds + dt;
            }
            cands.push_back({s, t, 0, score});
        }
    }
    if (use_hint_order && cands.size() > 1) {
        std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b){
            if (a.score != b.score) return a.score < b.score;
            if (a.s != b.s) return a.s < b.s;
            return a.t < b.t;
        });
    }

    // Checkpoint bitset
    size_t words = (size_t)((total + 31) / 32); if (words == 0) words = 1;
    std::vector<uint32_t> done_words(words, 0);
    if (cp_path && *cp_path) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_load_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
        (void)cp_save_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
    }

    // Optional GPU prefilter: mark candidates that can advance one or two steps without immediate duplicates
    std::vector<unsigned char> ok_host; ok_host.reserve((size_t)total);
    int device_count = 0;
    cudaError_t derr = cudaGetDeviceCount(&device_count);
    int rt_ver = 0, dr_ver = 0; cudaRuntimeGetVersion(&rt_ver); cudaDriverGetVersion(&dr_ver);
    const char *cvd = getenv("CUDA_VISIBLE_DEVICES");
    fprintf(stderr, "[CUDA] Runtime=%d Driver=%d CUDA_VISIBLE_DEVICES=%s\n", rt_ver, dr_ver, cvd ? cvd : "(unset)");
    if (derr != cudaSuccess) {
        fprintf(stderr, "[CUDA] cudaGetDeviceCount error: %s (%d)\n", cudaGetErrorString(derr), (int)derr);
    }
    if (device_count > 0) {
        int dev = 0;
        cudaError_t sderr = cudaSetDevice(dev);
        if (sderr != cudaSuccess) {
            fprintf(stderr, "[CUDA] cudaSetDevice(%d) failed: %s (%d)\n", dev, cudaGetErrorString(sderr), (int)sderr);
        }
        // create context
        cudaFree(0);
        cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
        fprintf(stderr, "[CUDA] Using device %d: %s\n", dev, prop.name);
        if (!cuda_check(cudaMemcpyToSymbol(c_ogr, h_ogr, sizeof(h_ogr)), "cudaMemcpyToSymbol(ogr)"))
            device_count = 0;
        init_bits_grid();
    } else {
        fprintf(stderr, "[CUDA] No CUDA device found – running CPU-only prefilter.\n");
    }

    /* Shared found flag: host-mapped so GPU kernels and CPU threads can
     * both poll and set it.  Falls back to a stack variable without GPU. */
    int found_stack = 0;
    int *mapped_flag = nullptr;
    volatile int *VF = (volatile int *)&found_stack;
    g_gpu_available = false;
    if (device_count > 0) {
        if (cudaHostAlloc((void **)&mapped_flag, sizeof(int), cudaHostAllocMapped) == cudaSuccess) {
            void *dev_ptr = nullptr;
            if (cudaHostGetDevicePointer(&dev_ptr, mapped_flag, 0) == cudaSuccess) {
                *mapped_flag = 0;
                VF = (volatile int *)mapped_flag;
                g_gpu_available = true;
            } else {
                cudaFreeHost(mapped_flag);
                mapped_flag = nullptr;
            }
        } else {
            fprintf(stderr, "[CUDA] mapped flag allocation failed; GPU DFS disabled.\n");
        }
    }

    /* The frontier is exact and device-generated.  Checkpoint files encode
     * root-candidate progress, so retain the older root filter for resumes. */
    std::vector<FrontierPrefix> frontier;
    bool frontier_ready = false;
    if (!use_hint_order && n >= 5 && device_count > 0 && total > 0 && !(cp_path && *cp_path)) {
        frontier_ready = build_frontier(n, target_length, cands, frontier);
        if (frontier_ready)
            fprintf(stderr, "[CUDA] Frontier ready: %zu endpoint-aware prefixes.\n", frontier.size());
        else
            fprintf(stderr, "[CUDA] Frontier generation failed; using root prefilter.\n");
    }

    // Enable GPU prefilter only when the frontier is unavailable and -H is not set.
    pthread_t pf_th; PrefilterJob pf_job{}; int pf_started = 0;
    if (!frontier_ready && !use_hint_order && n > 4 && device_count > 0 && total > 0) {
        if (async_pref) {
            pf_job.n = n; pf_job.L = target_length; pf_job.total = total; pf_job.cands = cands.data();
            // Ensure CUDA context exists for the thread
            cudaFree(0);
            pthread_create(&pf_th, NULL, prefilter_worker, &pf_job);
            pf_started = 1;
        } else {
            pf_job.n = n; pf_job.L = target_length; pf_job.total = total; pf_job.cands = cands.data();
            prefilter_worker(&pf_job);
            if (pf_job.success) {
                fprintf(stderr, "[CUDA] Prefiltered %lld candidates: %zu two-step, %zu one-step.\n", total, pf_job.ok2_cnt, pf_job.ok1_cnt);
                ok_host.swap(pf_job.ok_out);
                for (size_t i = 0; i < (size_t)total; ++i) cands[i].u_hint = pf_job.u_hints_out[i];
            } else {
                fprintf(stderr, "[CUDA] Prefilter failed; retaining all CPU candidates.\n");
            }
        }
    }

    ruler_t res_local{};
    /* -H guided fast-lane: split the LUT (s0,t0) prefix into depth-4..6
     * subprefixes and search them with the hybrid GPU+CPU engine.  This
     * replaces the former single-threaded fast-lane; the search remains
     * exact and constructive (the LUT ruler itself is never copied). */
    if (use_hint_order && !*VF) {
        int s0 = ref->pos[1], t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            std::vector<FrontierPrefix> guided;
            if (build_guided_prefixes(n, target_length, s0, t0, guided)) {
                std::stable_sort(guided.begin(), guided.end(), prefix_lex_cmp);
                fprintf(stderr, "[CUDA] Guided fast-lane: %zu subprefixes under LUT pair (%d,%d).\n",
                        guided.size(), s0, t0);
                if (run_hybrid_prefix_search(n, target_length, guided, false, VF, &res_local, verbose))
                    *VF = 1;
            }
        }
    }

    /* Classic single-thread fast-lane as a fallback (guidance failed). */
    if (use_hint_order && !*VF) {
        int s0 = ref->pos[1], t0 = ref->pos[2];
        if (s0 >= 1 && s0 <= second_max && t0 > s0 && t0 <= T) {
            uint64_t bs[BS_WORDS] = {0};
            int pos[MAX_MARKS]; pos[0] = 0; pos[1] = s0; pos[2] = t0;
            set_bit64(bs, s0);
            int d13 = t0; int d23 = t0 - s0;
            if (!test_bit64(bs, d13) && !test_bit64(bs, d23)) {
                set_bit64(bs, d13); set_bit64(bs, d23);
                if (dfs(3, n, target_length, pos, bs, verbose)) {
                    res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); *VF = 1;
                }
            }
        }
    }

    /* While an asynchronous prefilter is running, use a disjoint CPU prefix.
     * Every prefix item is recorded so the main pass never searches it twice. */
    std::vector<unsigned char> warm_done((size_t)total, 0);
    if (!*VF && pf_started && total > 0) {
        long long warmup = total < warmup_limit ? total : warmup_limit;
        std::vector<long long> warm_idx; warm_idx.reserve((size_t)warmup);
        for (long long i = 0; i < warmup; ++i) warm_idx.push_back(i);
        #pragma omp parallel for schedule(dynamic, 16)
        for (long long j = 0; j < (long long)warm_idx.size(); ++j) {
            long long i = warm_idx[(size_t)j];
            warm_done[(size_t)i] = 1;
            if (*VF) continue;
            int second = cands[(size_t)i].s;
            int third  = cands[(size_t)i].t;
            if (third - second == second) continue; // skip isosceles triangle
            uint64_t bs[BS_WORDS] = {0};
            int pos[MAX_MARKS]; pos[0] = 0; pos[1] = second; pos[2] = third;
            set_bit64(bs, second);
            int d13 = third; int d23 = third - second;
            if (test_bit64(bs, d13) || test_bit64(bs, d23)) continue;
            set_bit64(bs, d13); set_bit64(bs, d23);
            if (dfs(3, n, target_length, pos, bs, verbose)) {
                #pragma omp critical(set_result)
                {
                    if (!*VF) { res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); *VF = 1; }
                }
            }
        }
    }

    // If prefilter was launched asynchronously, wait here so u_hint values are ready for the main loop
    if (pf_started) {
        pthread_join(pf_th, NULL);
        if (pf_job.success) {
            fprintf(stderr, "[CUDA] Prefiltered %lld candidates: %zu two-step, %zu one-step.\n", total, pf_job.ok2_cnt, pf_job.ok1_cnt);
            ok_host.swap(pf_job.ok_out);
            for (size_t i = 0; i < (size_t)total; ++i) cands[i].u_hint = pf_job.u_hints_out[i];
        } else {
            fprintf(stderr, "[CUDA] Prefilter failed; retaining all CPU candidates.\n");
        }
    }

    /* A status of zero proves that this DFS(3) prefix cannot place even its
     * next mark, so it is safe to remove it from an exact search.  Keep the
     * original candidate index for checkpoint compatibility. */
    std::vector<long long> search_idx;
    search_idx.reserve((size_t)total);
    const bool have_prefilter = !use_hint_order && ok_host.size() == (size_t)total;
    if (frontier_ready) {
        /* The frontier is exhaustive for valid roots, so root DFS is skipped. */
    } else if (have_prefilter) {
        if (cp_path && *cp_path) {
            for (long long i = 0; i < total; ++i) {
                if (ok_host[(size_t)i] == 0 || warm_done[(size_t)i]) {
                    const size_t wi = (size_t)(i >> 5);
                    const uint32_t mask = 1u << (i & 31);
                    __sync_fetch_and_or(&done_words[wi], mask);
                }
            }
        }
        /* ok is a bitmask: bit 0 means one-step feasible and bit 1 means
         * two-step feasible.  A two-step candidate therefore has value 3. */
        for (long long i = 0; i < total; ++i)
            if (!warm_done[(size_t)i] && (ok_host[(size_t)i] & 2u))
                search_idx.push_back(i);
        for (long long i = 0; i < total; ++i)
            if (!warm_done[(size_t)i] && ok_host[(size_t)i] == 1u)
                search_idx.push_back(i);
    } else {
        for (long long i = 0; i < total; ++i)
            if (!warm_done[(size_t)i])
                search_idx.push_back(i);
    }

    struct timespec ts_last_flush; clock_gettime(CLOCK_MONOTONIC, &ts_last_flush);

    if (frontier_ready && !*VF) {
        /* Hybrid completion: the GPU drains the score-sorted list from the
         * front in chunks, OpenMP threads drain it from the back. */
        if (run_hybrid_prefix_search(n, target_length, frontier, use_mirror, VF, &res_local, verbose))
            *VF = 1;
    }

    // Parallel CPU search across the remaining, GPU-feasible candidates.
    #pragma omp parallel for schedule(dynamic, 16)
    for (long long j = 0; j < (long long)search_idx.size(); ++j) {
        const long long i = search_idx[(size_t)j];
        if (*VF) continue;
        // skip processed
        if (cp_path && *cp_path) {
            size_t wi = (size_t)(i >> 5); uint32_t mask = 1u << (i & 31);
            if (done_words[wi] & mask) continue;
        }
        int second = cands[(size_t)i].s;
        int third  = cands[(size_t)i].t;
        if (third - second == second) goto checkpoint_update; // skip isosceles triangle
        // Initialize base state for depth=3. Optionally bias depth-3 by trying u near u_hint first, then call dfs(3)
        {
            uint64_t bs[BS_WORDS] = {0};
            set_bit64(bs, second);
            int d13 = third; int d23 = third - second;
            if (!test_bit64(bs, d13) && !test_bit64(bs, d23)) {
                set_bit64(bs, d13); set_bit64(bs, d23);
                int pos[MAX_MARKS]; pos[0] = 0; pos[1] = second; pos[2] = third;
                bool ok_found = false;
                if (dfs3_hint) {
                    // Compute bound for u (depth=4)
                    const int rem_after2 = n - 4;
                    const int tri_after2 = rem_after2 * (rem_after2 + 1) / 2;
                    const int max_u = target_length - tri_after2;
                    const int u_hint = cands[(size_t)i].u_hint;
                    auto try_u = [&](int u)->bool {
                        const int du0 = u;
                        const int du1 = u - second;
                        const int du2 = u - third;
                        if (du1 <= 0 || du2 <= 0) return false;
                        if (test_bit64(bs, du0) || test_bit64(bs, du1) || test_bit64(bs, du2)) return false;
                        if (du0 == du1 || du0 == du2 || du1 == du2) return false;
                        uint64_t bs_u[BS_WORDS]; memcpy(bs_u, bs, sizeof(bs_u));
                        set_bit64(bs_u, du0); set_bit64(bs_u, du1); set_bit64(bs_u, du2);
                        int pos_u[MAX_MARKS];
                        pos_u[0] = pos[0]; pos_u[1] = pos[1]; pos_u[2] = pos[2]; pos_u[3] = u;
                        if (dfs(4, n, target_length, pos_u, bs_u, verbose)) {
                            #pragma omp critical(set_result)
                            {
                                if (!*VF) { res_local.marks = n; res_local.length = pos_u[n - 1]; memcpy(res_local.pos, pos_u, n * sizeof(int)); *VF = 1; }
                            }
                            return true;
                        }
                        return false;
                    };
                    if (!*VF && max_u > third && u_hint > third && u_hint <= max_u) {
                        if (try_u(u_hint)) ok_found = true;
                    }
                    if (!*VF && !ok_found && max_u > third && u_hint > 0) {
                        int start = std::max(third + 1, u_hint - u_win);
                        int end   = std::min(max_u, u_hint + u_win);
                        for (int u = start; u <= end && !*VF; ++u) {
                            if (u == u_hint) continue;
                            if (try_u(u)) { ok_found = true; break; }
                        }
                    }
                }
                if (!ok_found && dfs(3, n, target_length, pos, bs, verbose)) {
                    #pragma omp critical(set_result)
                    {
                        if (!*VF) { res_local.marks = n; res_local.length = pos[n - 1]; memcpy(res_local.pos, pos, n * sizeof(int)); *VF = 1; }
                    }
                }
            }
        }
checkpoint_update:
        if (cp_path && *cp_path) {
            size_t wi = (size_t)(i >> 5); uint32_t mask = 1u << (i & 31);
            __sync_fetch_and_or(&done_words[wi], mask);
            struct timespec ts_now; clock_gettime(CLOCK_MONOTONIC, &ts_now);
            time_t dt = ts_now.tv_sec - ts_last_flush.tv_sec;
            if (dt >= cp_interval) {
                #pragma omp critical(cp_io)
                {
                    struct timespec ts_chk; clock_gettime(CLOCK_MONOTONIC, &ts_chk);
                    if (ts_chk.tv_sec - ts_last_flush.tv_sec >= cp_interval) {
                        int hs2 = use_hint_order && ref ? ref->pos[1] : 0;
                        int ht2 = use_hint_order && ref ? ref->pos[2] : 0;
                        (void)cp_save_file(cp_path, n, target_length, total, hs2, ht2, use_hint_order, done_words.data(), words);
                        ts_last_flush = ts_chk;
                    }
                }
            }
        }
    }

    if (cp_path && *cp_path) {
        int hs = use_hint_order && ref ? ref->pos[1] : 0;
        int ht = use_hint_order && ref ? ref->pos[2] : 0;
        (void)cp_save_file(cp_path, n, target_length, total, hs, ht, use_hint_order, done_words.data(), words);
    }

    g_done = 1; if (g_vt_sec > 0.0) pthread_join(hb_th, NULL);

    if (*VF) {
        /* Validate before reporting; the CPU DFS is trusted, the GPU result
         * was already validated, this guards the plumbing in between. */
        if (!validate_ruler(&res_local)) {
            fprintf(stderr, "Internal error: found ruler failed validation at L=%d.\n", target_length);
            return 4;
        }
        // Print in the same format as the C variant:
        // length=..\nmarks=..\npositions=..\ndistances=..\nmissing=..
        int L = res_local.length;
        int m = res_local.marks;
        printf("length=%d\nmarks=%d\npositions=", L, m);
        for (int i = 0; i < m; ++i) {
            printf("%d%s", res_local.pos[i], (i == m - 1) ? "" : " ");
        }
        // Build distance presence 1..L
        std::vector<unsigned char> present((size_t)L + 1, 0);
        for (int j = 0; j < m; ++j) {
            for (int i = 0; i < j; ++i) {
                int d = res_local.pos[j] - res_local.pos[i];
                if (d >= 1 && d <= L) present[(size_t)d] = 1;
            }
        }
        // distances line
        printf("\ndistances=");
        for (int d = 1, first = 1; d <= L; ++d) {
            if (present[(size_t)d]) {
                if (!first) putchar(' ');
                printf("%d", d);
                first = 0;
            }
        }
        // missing line
        printf("\nmissing=");
        for (int d = 1, first = 1; d <= L; ++d) {
            if (!present[(size_t)d]) {
                if (!first) putchar(' ');
                printf("%d", d);
                first = 0;
            }
        }
        putchar('\n');

        /* Write the out/ log in the same format as the C variant. */
        {
            char opts[64] = "nv";
            char fsuffix[32] = "_nv";
            if (use_b) strncat(opts, " -b", sizeof(opts) - strlen(opts) - 1);
            if (hints) {
                strncat(opts, " -H", sizeof(opts) - strlen(opts) - 1);
                strncat(fsuffix, "_H", sizeof(fsuffix) - strlen(fsuffix) - 1);
            }
            if (verbose) strncat(opts, " -v", sizeof(opts) - strlen(opts) - 1);
            struct timespec ts_end; clock_gettime(CLOCK_MONOTONIC, &ts_end);
            double elapsed = (ts_end.tv_sec - g_ts_start.tv_sec) + (ts_end.tv_nsec - g_ts_start.tv_nsec) / 1e9;
            if (mkdir("out", 0755) == -1 && errno != EEXIST)
                fprintf(stderr, "mkdir out: %s\n", strerror(errno));
            char fname[128];
            snprintf(fname, sizeof fname, "out/GOL_n%d%s.txt", n, fsuffix);
            FILE *fp = fopen(fname, "w");
            if (fp) {
                fprintf(fp, "length=%d\nmarks=%d\npositions=", L, m);
                for (int i = 0; i < m; ++i)
                    fprintf(fp, "%d%s", res_local.pos[i], (i == m - 1) ? "" : " ");
                fprintf(fp, "\ndistances=");
                for (int d = 1, first = 1; d <= L; ++d) {
                    if (present[(size_t)d]) {
                        if (!first) fputc(' ', fp);
                        fprintf(fp, "%d", d);
                        first = 0;
                    }
                }
                fprintf(fp, "\nmissing=");
                for (int d = 1, first = 1; d <= L; ++d) {
                    if (!present[(size_t)d]) {
                        if (!first) fputc(' ', fp);
                        fprintf(fp, "%d", d);
                        first = 0;
                    }
                }
                const int hh = (int)(elapsed / 3600.0);
                const int mm = (int)((elapsed - hh * 3600.0) / 60.0);
                const double ss = elapsed - hh * 3600.0 - mm * 60.0;
                fprintf(fp, "\nseconds=%.6f\ntime=%d:%02d:%06.3f\noptions=%s\n", elapsed, hh, mm, ss, opts);
                if (ref) fprintf(fp, "optimal=yes\n");
                fclose(fp);
            } else {
                fprintf(stderr, "Could not write %s: %s\n", fname, strerror(errno));
            }
        }
        if (mapped_flag) cudaFreeHost(mapped_flag);
        return 0;
    }
    if (mapped_flag) cudaFreeHost(mapped_flag);
    fprintf(stderr, "No ruler found at L=%d (unexpected for LUT-verified -b).\n", target_length);
    return 1;
}
