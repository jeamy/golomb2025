/* Bit-parallel endpoint-aware DFS shared by the CUDA kernel and the host.
 *
 * State at level d (marks m_0=0 < ... < m_{d-1}=x placed, L fixed):
 *   D      absolute set of used distances (pairs and each mark to L)
 *   LIST   bit k  <=> x-k is a placed mark (k >= 1)
 *   MARKS  bit p  <=> p is a placed mark (L excluded)
 *   F      bit g  <=> candidate y = x+g is illegal, i.e.
 *            (i)   y - m_j in D for some j       (left distances)
 *            (ii)  L - y   in D                  (endpoint distance)
 *            (iii) y - m_j == L - y for some j   (the two new kinds clash)
 *
 * Placing y = x+g0 (e = L-y) turns F into
 *   F' = (F >> g0) | D' | E | midbit,   D' = D | LIST' | {e},
 *   E  = { m_j + L - 2y }  (e applied at every older mark, and the new left
 *                           distances seen from the endpoint side),
 *   midbit = (L-y)/2 when L-y is even (clash (iii) for the new mark).
 * All three predicates shift uniformly with x, so a single mask per level
 * suffices and the next legal candidate is one find-first-zero away.
 *
 * Bound (OGR(k) = optimal length of a k-mark ruler, proven values):
 *   mark index d at y:  L - y >= OGR(n-d), since [y, L] holds n-d marks.
 *   (The prefix side y >= OGR(d+1) holds automatically for a valid prefix.)
 * Mirror symmetry (optional): first gap < last gap, i.e. pos[n-2] < L-pos[1].
 */
#ifndef GOLOMB_BITS_H
#define GOLOMB_BITS_H

#include <stdint.h>

#ifdef __CUDACC__
#define GB_HD __host__ __device__ __forceinline__
#else
#define GB_HD static inline
#endif

#if defined(__CUDA_ARCH__)
#define GB_UNROLL _Pragma("unroll")
#elif defined(__CUDACC__)
#define GB_UNROLL /* nvcc host pass: loops are small, gcc -O3 unrolls */
#else
#define GB_UNROLL _Pragma("GCC unroll 8")
#endif

#define GB_MAX_LEVELS 20 /* inner levels searched below a prefix */

GB_HD uint32_t gb_fsr(uint32_t lo, uint32_t hi, int r) /* low word of (hi:lo)>>r */
{
#ifdef __CUDA_ARCH__
    return __funnelshift_r(lo, hi, r);
#else
    return r ? (lo >> r) | (hi << (32 - r)) : lo;
#endif
}

GB_HD uint32_t gb_fsl(uint32_t lo, uint32_t hi, int r) /* high word of (hi:lo)<<r */
{
#ifdef __CUDA_ARCH__
    return __funnelshift_l(lo, hi, r);
#else
    return r ? (hi << r) | (lo >> (32 - r)) : hi;
#endif
}

GB_HD int gb_ctz(uint32_t v)
{
#ifdef __CUDA_ARCH__
    return __ffs((int)v) - 1;
#else
    return __builtin_ctz(v);
#endif
}

template <int K>
GB_HD void gb_shr(uint32_t (&a)[K], int s)
{
    if (s >= 32 * K) {
GB_UNROLL
        for (int i = 0; i < K; ++i) a[i] = 0u;
        return;
    }
    const int q = s >> 5, r = s & 31;
GB_UNROLL
    for (int step = 1; step < K; step <<= 1) {
        if (q & step) {
GB_UNROLL
            for (int i = 0; i < K; ++i) a[i] = (i + step < K) ? a[i + step] : 0u;
        }
    }
GB_UNROLL
    for (int i = 0; i < K; ++i) a[i] = gb_fsr(a[i], (i + 1 < K) ? a[i + 1] : 0u, r);
}

template <int K>
GB_HD void gb_shl(uint32_t (&a)[K], int s)
{
    if (s >= 32 * K) {
GB_UNROLL
        for (int i = 0; i < K; ++i) a[i] = 0u;
        return;
    }
    const int q = s >> 5, r = s & 31;
GB_UNROLL
    for (int step = 1; step < K; step <<= 1) {
        if (q & step) {
GB_UNROLL
            for (int i = K - 1; i >= 0; --i) a[i] = (i - step >= 0) ? a[i - step] : 0u;
        }
    }
GB_UNROLL
    for (int i = K - 1; i >= 0; --i) a[i] = gb_fsl((i >= 1) ? a[i - 1] : 0u, a[i], r);
}

/* Single-bit helpers.  Written as per-word arithmetic masks on purpose:
 * the obvious `if ((b >> 5) == i) a[i] |= m` loop is folded by nvcc into
 * a dynamically indexed a[b >> 5], which demotes the whole mask array to
 * local memory (hundreds of LDL/STL in the hot loop). */
GB_HD uint32_t gb_word_bit(int b, int i)
{
    const unsigned sh = (unsigned)(b - 32 * i);
    return sh < 32u ? (1u << sh) : 0u;
}

template <int K>
GB_HD void gb_set(uint32_t (&a)[K], int b)
{
GB_UNROLL
    for (int i = 0; i < K; ++i) a[i] |= gb_word_bit(b, i);
}

template <int K>
GB_HD void gb_clr(uint32_t (&a)[K], int b)
{
GB_UNROLL
    for (int i = 0; i < K; ++i) a[i] &= ~gb_word_bit(b, i);
}

template <int K>
GB_HD bool gb_test(const uint32_t (&a)[K], int b)
{
    uint32_t r = 0u;
GB_UNROLL
    for (int i = 0; i < K; ++i) r |= a[i] & gb_word_bit(b, i);
    return r != 0u;
}

/* Lowest clear bit of F within [lo, hi], or -1. */
template <int K>
GB_HD int gb_next_free(const uint32_t (&F)[K], int lo, int hi)
{
    int res = -1;
GB_UNROLL
    for (int i = K - 1; i >= 0; --i) {
        const int b0 = 32 * i;
        uint32_t m = ~F[i];
        if (b0 + 31 < lo || b0 > hi) m = 0u;
        else {
            if (b0 < lo) m &= ~0u << (lo - b0);
            if (b0 + 31 > hi) m &= ~0u >> (31 - (hi - b0));
        }
        if (m) res = b0 + gb_ctz(m);
    }
    return res;
}

/* Sum of the k smallest distances >= 1 not in D, capped: returns a value
 * > cap as soon as the partial sum exceeds cap.  The k gaps between the
 * next mark and L are distinct distances unused so far, hence
 * L - y >= this sum (D only grows, so the current D gives a valid bound). */
template <int K>
GB_HD int gb_sum_free(const uint32_t (&D)[K], int k, int cap)
{
    int sum = 0;
    for (int i = 0; i < K && k > 0; ++i) {
        uint32_t m = ~D[i];
        if (i == 0) m &= ~1u;
        while (m && k > 0) {
            sum += 32 * i + gb_ctz(m);
            m &= m - 1u;
            --k;
            if (sum > cap) return sum;
        }
    }
    return k > 0 ? cap + 1 : sum;
}

/* Complete the prefix pos[0..depth-1] (pos[0]==0) to an n-mark Golomb ruler
 * of length L, with L+1 <= 32*K.  ogr[k] must hold OGR(k) for k < n.
 * Returns 1 and fills pos[0..n-1] on success, 0 if the subtree is exhausted,
 * -1 if *flag became nonzero.  With count != nullptr every solution is
 * counted and the search continues (returns 0).  Mirror pruning keeps only
 * rulers whose first gap is smaller than their last gap. */
template <int K>
GB_HD int gb_dfs(int n, int L, int depth, int *pos, const int *ogr, bool mirror,
                 volatile int *flag, long long *count)
{
    if (depth < 1 || depth > n - 1 || L + 1 > 32 * K || n - 1 - depth > GB_MAX_LEVELS) return 0;

    uint32_t D[K], LIST[K], MARKS[K], F[K];
GB_UNROLL
    for (int i = 0; i < K; ++i) { D[i] = 0u; LIST[i] = 0u; MARKS[i] = 0u; F[i] = 0u; }

    for (int a = 0; a < depth; ++a) {
        if (a > 0 && pos[a] <= pos[a - 1]) return 0;
        for (int b = a + 1; b < depth; ++b) {
            const int dd = pos[b] - pos[a];
            if (gb_test(D, dd)) return 0;
            gb_set(D, dd);
        }
        const int dl = L - pos[a];
        if (dl <= 0 || gb_test(D, dl)) return 0;
        gb_set(D, dl);
        gb_set(MARKS, pos[a]);
    }
    int x = pos[depth - 1];
    for (int j = 0; j < depth - 1; ++j) gb_set(LIST, x - pos[j]);

    if (depth == n - 1) {
        const bool ok = !mirror || n < 3 || pos[1] < L - pos[n - 2];
        if (!ok) return 0;
        pos[n - 1] = L;
        if (count) { ++*count; return 0; }
        return 1;
    }

    for (int g = 1; g < L - x; ++g) {
        const int y = x + g, e = L - y;
        bool bad = gb_test(D, e);
        for (int j = 0; j < depth && !bad; ++j) {
            const int dj = y - pos[j];
            if (gb_test(D, dj) || dj == e) bad = true;
        }
        if (bad) gb_set(F, g);
    }

#ifndef GB_NO_FREE_BOUND
    const bool use_free = true;
#else
    const bool use_free = false;
#endif
    uint32_t Fst[GB_MAX_LEVELS][K];
    int d = depth;
    int g = 0;
    unsigned int steps = 0;
    for (;;) {
        if (flag && ((++steps & 1023u) == 0u) && *flag) return -1;
        const int lo = g + 1;
        int need = ogr[n - d];
        if (mirror) {
            /* [y, pos[n-2]] holds n-d-1 marks and the last gap exceeds pos[1] */
            const int nm = ogr[n - d - 1] + pos[1] + 1;
            if (need < nm) need = nm;
        }
        if (use_free) {
            const int sf = gb_sum_free(D, n - 1 - d, L - x);
            if (need < sf) need = sf;
        }
        const int hi = L - need - x;
        const int ng = (lo <= hi) ? gb_next_free(F, lo, hi) : -1;
        if (ng >= 0) {
            const int y = x + ng;
            if (d == n - 2) {
                pos[d] = y;
                pos[n - 1] = L;
                if (!count) return 1;
                ++*count;
                g = ng;
                continue;
            }
GB_UNROLL
            for (int i = 0; i < K; ++i) Fst[d - depth][i] = F[i];
            pos[d] = y;
            gb_shl(LIST, ng);
            gb_set(LIST, ng);
            const int e = L - y;
GB_UNROLL
            for (int i = 0; i < K; ++i) D[i] |= LIST[i];
            gb_set(D, e);
            uint32_t E[K];
GB_UNROLL
            for (int i = 0; i < K; ++i) E[i] = MARKS[i];
            if (L - 2 * y >= 0) gb_shl(E, L - 2 * y);
            else gb_shr(E, 2 * y - L);
            gb_set(MARKS, y);
            gb_shr(F, ng);
GB_UNROLL
            for (int i = 0; i < K; ++i) F[i] |= D[i] | E[i];
            if ((e & 1) == 0) gb_set(F, e >> 1);
            x = y;
            ++d;
            g = 0;
        } else {
            if (d == depth) return 0;
            --d;
            const int y = x;
            const int gg = y - pos[d - 1];
GB_UNROLL
            for (int i = 0; i < K; ++i) { F[i] = Fst[d - depth][i]; D[i] &= ~LIST[i]; }
            gb_clr(D, L - y);
            gb_clr(MARKS, y);
            gb_clr(LIST, gg);
            gb_shr(LIST, gg);
            x = y - gg;
            g = gg;
        }
    }
}

GB_HD void gb_count_add(unsigned long long *c)
{
#ifdef __CUDA_ARCH__
    atomicAdd(c, 1ULL);
#else
    ++*c;
#endif
}

/* Register-stack variant for the GPU.  The per-level masks live in a
 * statically indexed shift register (LV levels deep) instead of a
 * dynamically indexed local array: with ~1000 threads per SM the local
 * stack no longer fits L1 and every push/pop became a DRAM round trip.
 * A push/pop costs LV*K register moves instead.  Requires
 * n-2-depth <= LV (caller falls back to gb_dfs otherwise).  Same contract
 * as gb_dfs (no counting mode). */
template <int K, int LV>
GB_HD int gb_dfs_reg(int n, int L, int depth, int *pos, const int *ogr, bool mirror,
                     volatile int *flag, unsigned long long *count = nullptr)
{
    if (depth < 3 || depth > n - 1 || L + 1 > 32 * K || n - 2 - depth > LV) return 0;

    uint32_t D[K], LIST[K], MARKS[K], F[K];
    GB_UNROLL
    for (int i = 0; i < K; ++i) { D[i] = 0u; LIST[i] = 0u; MARKS[i] = 0u; F[i] = 0u; }

    for (int a = 0; a < depth; ++a) {
        if (a > 0 && pos[a] <= pos[a - 1]) return 0;
        for (int b = a + 1; b < depth; ++b) {
            const int dd = pos[b] - pos[a];
            if (gb_test(D, dd)) return 0;
            gb_set(D, dd);
        }
        const int dl = L - pos[a];
        if (dl <= 0 || gb_test(D, dl)) return 0;
        gb_set(D, dl);
        gb_set(MARKS, pos[a]);
    }
    const int x0 = pos[depth - 1];
    const int s1 = pos[1];
    int x = x0;
    for (int j = 0; j < depth - 1; ++j) gb_set(LIST, x - pos[j]);

    if (depth == n - 1) {
        if (mirror && !(s1 < L - pos[n - 2])) return 0;
        pos[n - 1] = L;
        if (count) { gb_count_add(count); return 0; }
        return 1;
    }

    for (int g = 1; g < L - x; ++g) {
        const int y = x + g, e = L - y;
        bool bad = gb_test(D, e);
        for (int j = 0; j < depth && !bad; ++j) {
            const int dj = y - pos[j];
            if (gb_test(D, dj) || dj == e) bad = true;
        }
        if (bad) gb_set(F, g);
    }

    uint32_t st[LV][K];
    int gs[LV];
    GB_UNROLL
    for (int k = 0; k < LV; ++k) { gs[k] = 0; GB_UNROLL for (int i = 0; i < K; ++i) st[k][i] = 0u; }
    int d = depth;
    int g = 0;
    unsigned int steps = 0;
    for (;;) {
        if (flag && ((++steps & 1023u) == 0u) && *flag) return -1;
        const int lo = g + 1;
        int need = ogr[n - d];
        if (mirror) {
            const int nm = ogr[n - d - 1] + s1 + 1;
            if (need < nm) need = nm;
        }
#ifndef GB_REG_NO_FREE_BOUND
        const int sf = gb_sum_free(D, n - 1 - d, L - x);
        if (need < sf) need = sf;
#endif
        const int hi = L - need - x;
        const int ng = (lo <= hi) ? gb_next_free(F, lo, hi) : -1;
        if (ng >= 0) {
            const int y = x + ng;
            if (d == n - 2 && count) {
                gb_count_add(count);
                g = ng;
                continue;
            }
            if (d == n - 2) {
                /* rebuild pos[depth..n-2] from the gap stack */
                int p = y;
                pos[n - 2] = y;
                GB_UNROLL
                for (int k = 0; k < LV; ++k) {
                    const int idx = n - 3 - k;
                    if (idx >= depth) { p -= (k == 0 ? ng : gs[k - 1]); pos[idx] = p; }
                }
                pos[n - 1] = L;
                return 1;
            }
            GB_UNROLL
            for (int k = LV - 1; k > 0; --k) {
                gs[k] = gs[k - 1];
                GB_UNROLL for (int i = 0; i < K; ++i) st[k][i] = st[k - 1][i];
            }
            gs[0] = ng;
            GB_UNROLL for (int i = 0; i < K; ++i) st[0][i] = F[i];
            gb_shl(LIST, ng);
            gb_set(LIST, ng);
            const int e = L - y;
            GB_UNROLL
            for (int i = 0; i < K; ++i) D[i] |= LIST[i];
            gb_set(D, e);
            uint32_t E[K];
            GB_UNROLL
            for (int i = 0; i < K; ++i) E[i] = MARKS[i];
            if (L - 2 * y >= 0) gb_shl(E, L - 2 * y);
            else gb_shr(E, 2 * y - L);
            gb_set(MARKS, y);
            gb_shr(F, ng);
            GB_UNROLL
            for (int i = 0; i < K; ++i) F[i] |= D[i] | E[i];
            if ((e & 1) == 0) gb_set(F, e >> 1);
            x = y;
            ++d;
            g = 0;
        } else {
            if (d == depth) return 0;
            --d;
            const int y = x;
            const int gg = gs[0];
            GB_UNROLL
            for (int i = 0; i < K; ++i) { F[i] = st[0][i]; D[i] &= ~LIST[i]; }
            GB_UNROLL
            for (int k = 0; k < LV - 1; ++k) {
                gs[k] = gs[k + 1];
                GB_UNROLL for (int i = 0; i < K; ++i) st[k][i] = st[k + 1][i];
            }
            gb_clr(D, L - y);
            gb_clr(MARKS, y);
            gb_clr(LIST, gg);
            gb_shr(LIST, gg);
            x = y - gg;
            g = gg;
        }
    }
}

#endif /* GOLOMB_BITS_H */
