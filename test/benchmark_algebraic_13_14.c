#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

const char *g_cp_path = NULL;
int g_cp_interval_sec = 60;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static bool verify_ruler(const ruler_t *ruler, int n) {
    if (ruler->marks != n) return false;
    if (ruler->pos[0] != 0 || ruler->pos[n - 1] != ruler->length) return false;
    bool seen[MAX_LEN_BITSET + 1] = {false};
    for (int i = 1; i < n; i++) {
        if (ruler->pos[i] <= ruler->pos[i - 1]) return false;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d = ruler->pos[j] - ruler->pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET || seen[d]) return false;
            seen[d] = true;
        }
    }
    return true;
}

static void log_ruler(FILE *fp, const ruler_t *ruler) {
    fprintf(fp, "[");
    for (int i = 0; i < ruler->marks; i++) {
        fprintf(fp, "%d%s", ruler->pos[i], (i + 1 == ruler->marks) ? "" : " ");
    }
    fprintf(fp, "]");
}

static double run_one(FILE *log, const char *name,
                      bool (*solve)(int, int, ruler_t *, bool),
                      int n, int expected_len, bool *ok_out) {
    ruler_t result = {0};
    double start = now_ms();
    bool found = solve(n, expected_len, &result, false);
    double elapsed = now_ms() - start;
    bool valid = found && verify_ruler(&result, n);
    bool optimal = valid && result.length == expected_len;
    *ok_out = optimal;

    fprintf(log, "%s found=%s valid=%s length=%d optimal=%s elapsed_ms=%.6f marks=",
            name, found ? "yes" : "no", valid ? "yes" : "no",
            found ? result.length : -1, optimal ? "yes" : "no", elapsed);
    if (found) log_ruler(log, &result);
    else fprintf(log, "[]");
    fprintf(log, "\n");
    fflush(log);

    return elapsed;
}

int main(void) {
    mkdir("benchmark_logs", 0755);
    char log_path[256];
    snprintf(log_path, sizeof(log_path),
             "benchmark_logs/algebraic_vs_traditional_n13_14_%ld.txt", (long)time(NULL));

    FILE *log = fopen(log_path, "w");
    if (!log) {
        perror("fopen");
        return 1;
    }

    fprintf(log, "Algebraic vs Traditional cold benchmark n=13..14\n");
    fprintf(log, "Reference supplies expected length only; no output positions are injected.\n\n");
    printf("Algebraic vs Traditional n=13..14\nLog: %s\n", log_path);
    printf("n  L   algebraic_ms traditional_ms speedup\n");

    int failures = 0;
    for (int n = 13; n <= 14; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) {
            fprintf(log, "n=%d missing reference\n", n);
            failures++;
            continue;
        }
        fprintf(log, "============================================================\n");
        fprintf(log, "n=%d expected_length=%d\n", n, expected->length);
        fflush(log);

        bool alg_ok = false, trad_ok = false;
        double alg_ms = run_one(log, "Algebraic", solve_golomb_algebraic,
                                n, expected->length, &alg_ok);
        double trad_ms = run_one(log, "Traditional", solve_golomb,
                                 n, expected->length, &trad_ok);
        double speedup = alg_ms > 0.0 ? trad_ms / alg_ms : 0.0;
        fprintf(log, "speedup=%.6f\n\n", speedup);
        printf("%2d %3d %12.3f %14.3f %7.3fx\n",
               n, expected->length, alg_ms, trad_ms, speedup);
        if (!alg_ok || !trad_ok || alg_ms >= trad_ms) failures++;
    }

    fprintf(log, "Overall status: %s\n", failures == 0 ? "PASS" : "FAIL");
    fclose(log);
    printf("Status: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
