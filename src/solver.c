#include "golomb.h"
#include <string.h>
#include <stdint.h>

/* Bitset helpers ---------------------------------------------------------*/
#define MAX_LEN_BITSET 600
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

    /* try next positions */
    for (int next = last + 1; next <= target_len - (n - depth - 1); ++next) {
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
