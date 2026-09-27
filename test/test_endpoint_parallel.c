#include "golomb.h"

#include <stdbool.h>
#include <stdio.h>

/* solver.c references these globals even though this test only exercises the
 * endpoint-aware implementations. */
const char *g_cp_path = NULL;
int g_cp_interval_sec = 60;

static bool valid_ruler(const ruler_t *r, int n, int length)
{
    if (r->marks != n || r->length != length || r->pos[0] != 0 ||
        r->pos[n - 1] != length)
        return false;

    bool seen[MAX_LEN_BITSET + 1] = {false};
    for (int i = 1; i < n; ++i)
        if (r->pos[i] <= r->pos[i - 1])
            return false;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const int d = r->pos[j] - r->pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET || seen[d])
                return false;
            seen[d] = true;
        }
    return true;
}

int main(void)
{
    int failures = 0;
    for (int n = 4; n <= 12; ++n) {
        const ruler_t *ref = lut_lookup_by_marks(n);
        ruler_t single = {0};
        ruler_t parallel = {0};
        if (!ref ||
            !solve_golomb_traditional_opt(n, ref->length, &single, false) ||
            !solve_golomb_traditional_opt_mt(n, ref->length, &parallel, false) ||
            !valid_ruler(&single, n, ref->length) ||
            !valid_ruler(&parallel, n, ref->length)) {
            fprintf(stderr, "n=%d failed at L=%d\n", n, ref ? ref->length : -1);
            ++failures;
        }
    }

    /* The LUT is only an oracle for the accepted endpoint; both exact paths
     * must reject shorter lengths without reading any reference positions. */
    for (int n = 4; n <= 10; ++n) {
        const ruler_t *ref = lut_lookup_by_marks(n);
        ruler_t result = {0};
        if (!ref || solve_golomb_traditional_opt_mt(n, ref->length - 1, &result, false)) {
            fprintf(stderr, "n=%d unexpectedly solved L=%d\n", n, ref ? ref->length - 1 : -1);
            ++failures;
        }
    }

    if (failures) return 1;
    puts("endpoint parallel regression: PASS");
    return 0;
}
