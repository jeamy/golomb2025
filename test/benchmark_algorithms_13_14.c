#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

const char *g_cp_path = NULL;
int g_cp_interval_sec = 60;

#define FIRST_N 13
#define LAST_N 14
#define NEW_RUNS 8
#define TRADITIONAL_RUNS 1

typedef bool (*solver_fn)(int, int, ruler_t *, bool);

typedef struct {
    const char *name;
    solver_fn solve;
} solver_def_t;

typedef struct {
    double total_ms;
    double min_ms;
    double max_ms;
    double cold_ms;
    int valid;
    int optimal;
} stats_t;

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

static void init_stats(stats_t *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->min_ms = 1e300;
}

static void add_time(stats_t *stats, double elapsed_ms, int run) {
    stats->total_ms += elapsed_ms;
    if (run == 1) stats->cold_ms = elapsed_ms;
    if (elapsed_ms < stats->min_ms) stats->min_ms = elapsed_ms;
    if (elapsed_ms > stats->max_ms) stats->max_ms = elapsed_ms;
}

static void run_solver_block(FILE *log, const solver_def_t *solver, int n, int expected_len,
                             int runs, stats_t *stats) {
    init_stats(stats);

    fprintf(log, "\n%s runs=%d:\n", solver->name, runs);
    fflush(log);

    for (int run = 1; run <= runs; run++) {
        ruler_t result = {0};
        double start = now_ms();
        bool found = solver->solve(n, expected_len, &result, false);
        double elapsed = now_ms() - start;

        bool valid = found && verify_ruler(&result, n);
        bool optimal = valid && result.length == expected_len;
        add_time(stats, elapsed, run);
        if (valid) stats->valid++;
        if (optimal) stats->optimal++;

        fprintf(log, "  run=%02d found=%s valid=%s length=%d optimal=%s elapsed_ms=%.6f marks=",
                run,
                found ? "yes" : "no",
                valid ? "yes" : "no",
                found ? result.length : -1,
                optimal ? "yes" : "no",
                elapsed);
        if (found) log_ruler(log, &result);
        else fprintf(log, "[]");
        fprintf(log, "\n");
        fflush(log);
    }

    fprintf(log,
            "  summary valid=%d/%d optimal=%d/%d cold_ms=%.6f avg_ms=%.6f min_ms=%.6f max_ms=%.6f\n",
            stats->valid, runs, stats->optimal, runs, stats->cold_ms,
            stats->total_ms / runs, stats->min_ms, stats->max_ms);
    fflush(log);
}

int main(void) {
    mkdir("benchmark_logs", 0755);

    char log_path[256];
    snprintf(log_path, sizeof(log_path),
             "benchmark_logs/algorithms_n%d_%d_newruns%d_tradruns%d_%ld.txt",
             FIRST_N, LAST_N, NEW_RUNS, TRADITIONAL_RUNS, (long)time(NULL));

    FILE *log = fopen(log_path, "w");
    if (!log) {
        perror("fopen");
        return 1;
    }

    solver_def_t new_solvers[] = {
        {"Physics", solve_golomb_physics},
        {"Evolutionary", solve_golomb_evolutionary},
    };
    solver_def_t traditional = {"Traditional", solve_golomb};

    fprintf(log, "Golomb solver benchmark\n");
    fprintf(log, "Range: n=%d..%d\n", FIRST_N, LAST_N);
    fprintf(log, "New solver runs per n: %d\n", NEW_RUNS);
    fprintf(log, "Traditional runs per n: %d\n", TRADITIONAL_RUNS);
    fprintf(log, "Reference only supplies expected optimal length; no positions are injected.\n");
    fprintf(log, "For new solvers, cold_ms is run 1 after clearing the exact-core cache.\n\n");

    printf("Benchmark n=%d..%d, new runs=%d, traditional runs=%d\n",
           FIRST_N, LAST_N, NEW_RUNS, TRADITIONAL_RUNS);
    printf("Log: %s\n\n", log_path);
    printf("n  L   | physics cold/avg   evolutionary cold/avg traditional cold\n");
    printf("-------|----------------------------------------------------------------\n");

    int failures = 0;

    for (int n = FIRST_N; n <= LAST_N; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) {
            fprintf(log, "n=%d missing LUT reference\n", n);
            failures++;
            continue;
        }

        fprintf(log, "============================================================\n");
        fprintf(log, "n=%d expected_length=%d expected_marks=", n, expected->length);
        log_ruler(log, expected);
        fprintf(log, "\n");

        stats_t new_stats[3];
        for (int s = 0; s < 3; s++) {
            run_solver_block(log, &new_solvers[s], n, expected->length, NEW_RUNS, &new_stats[s]);
        }

        stats_t traditional_stats;
        run_solver_block(log, &traditional, n, expected->length, TRADITIONAL_RUNS, &traditional_stats);

        fprintf(log, "\nPerformance vs Traditional cold run:\n");
        for (int s = 0; s < 3; s++) {
            double avg = new_stats[s].total_ms / NEW_RUNS;
            double cold_speedup = new_stats[s].cold_ms > 0.0 ? traditional_stats.cold_ms / new_stats[s].cold_ms : 0.0;
            double avg_speedup = avg > 0.0 ? traditional_stats.cold_ms / avg : 0.0;
            fprintf(log, "  %s cold_speedup=%.3fx avg_speedup=%.3fx\n",
                    new_solvers[s].name, cold_speedup, avg_speedup);
            if (new_stats[s].optimal != NEW_RUNS || new_stats[s].cold_ms >= traditional_stats.cold_ms) {
                failures++;
            }
        }
        fprintf(log, "\n");

        printf("%2d %3d |", n, expected->length);
        for (int s = 0; s < 3; s++) {
            printf(" %8.1f/%-8.1f", new_stats[s].cold_ms, new_stats[s].total_ms / NEW_RUNS);
        }
        printf(" %10.1f\n", traditional_stats.cold_ms);
    }

    fprintf(log, "Overall status: %s\n", failures == 0 ? "PASS" : "FAIL");
    fclose(log);

    printf("\nStatus: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
