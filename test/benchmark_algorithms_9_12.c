#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *g_cp_path = NULL;
int g_cp_interval_sec = 60;

#define FIRST_N 9
#define LAST_N 12
#define RUNS_PER_SOLVER 8
#define RUN_TIMEOUT_MS 5000

typedef bool (*solver_fn)(int, int, ruler_t *, bool);

typedef struct {
    const char *name;
    solver_fn solve;
} solver_def_t;

typedef struct {
    double total_ms;
    double min_ms;
    double max_ms;
    int valid;
    int optimal;
    int same_length_as_ref;
    int timeouts;
} stats_t;

typedef struct {
    bool found;
    ruler_t ruler;
} child_result_t;

typedef struct {
    bool found;
    bool timeout;
    double elapsed_ms;
    ruler_t ruler;
} run_result_t;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static bool verify_ruler(const ruler_t *ruler, int n) {
    if (ruler->marks != n) {
        return false;
    }
    if (ruler->pos[0] != 0 || ruler->pos[n - 1] != ruler->length) {
        return false;
    }

    bool seen[MAX_LEN_BITSET + 1] = {false};
    for (int i = 1; i < n; i++) {
        if (ruler->pos[i] <= ruler->pos[i - 1]) {
            return false;
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d = ruler->pos[j] - ruler->pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET || seen[d]) {
                return false;
            }
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

static void add_time(stats_t *stats, double elapsed_ms) {
    stats->total_ms += elapsed_ms;
    if (elapsed_ms < stats->min_ms) {
        stats->min_ms = elapsed_ms;
    }
    if (elapsed_ms > stats->max_ms) {
        stats->max_ms = elapsed_ms;
    }
}

static run_result_t run_solver_timed(solver_fn solve, int n, int target_length) {
    run_result_t result = {0};
    int pipefd[2];

    double start = now_ms();
    if (pipe(pipefd) != 0) {
        result.elapsed_ms = now_ms() - start;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.elapsed_ms = now_ms() - start;
        return result;
    }

    if (pid == 0) {
        close(pipefd[0]);
        child_result_t child = {0};
        child.found = solve(n, target_length, &child.ruler, false);
        (void)write(pipefd[1], &child, sizeof(child));
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);

    int status = 0;
    for (;;) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }

        result.elapsed_ms = now_ms() - start;
        if (result.elapsed_ms >= RUN_TIMEOUT_MS) {
            kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            result.timeout = true;
            close(pipefd[0]);
            return result;
        }

        struct timespec pause = {0, 1000000L};
        nanosleep(&pause, NULL);
    }

    child_result_t child = {0};
    ssize_t bytes = read(pipefd[0], &child, sizeof(child));
    close(pipefd[0]);

    result.elapsed_ms = now_ms() - start;
    if (bytes == (ssize_t)sizeof(child) && WIFEXITED(status)) {
        result.found = child.found;
        result.ruler = child.ruler;
    }
    return result;
}

int main(void) {
    mkdir("benchmark_logs", 0755);

    char log_path[256];
    snprintf(log_path, sizeof(log_path),
             "benchmark_logs/algorithms_n%d_%d_runs%d_%ld.txt",
             FIRST_N, LAST_N, RUNS_PER_SOLVER, (long)time(NULL));

    FILE *log = fopen(log_path, "w");
    if (!log) {
        perror("fopen");
        return 1;
    }

    solver_def_t solvers[] = {
        {"Physics", solve_golomb_physics},
        {"Evolutionary", solve_golomb_evolutionary},
        {"TradOpt", solve_golomb_traditional_opt},
        {"Traditional", solve_golomb},
    };
    const int solver_count = (int)(sizeof(solvers) / sizeof(solvers[0]));

    fprintf(log, "Golomb solver benchmark\n");
    fprintf(log, "Range: n=%d..%d\n", FIRST_N, LAST_N);
    fprintf(log, "Runs per solver: %d\n", RUNS_PER_SOLVER);
    fprintf(log, "Timeout per run: %d ms\n", RUN_TIMEOUT_MS);
    fprintf(log, "Reference: LUT/out-derived optimal length plus validation against Golomb distances\n");
    fprintf(log, "Important: solvers are timed through their public solve_* functions; no solver result is injected by this benchmark.\n\n");

    printf("Benchmark n=%d..%d, %d runs per solver\n",
           FIRST_N, LAST_N, RUNS_PER_SOLVER);
    printf("Log: %s\n\n", log_path);
    printf("n  expected | physics       evolutionary  tradopt       traditional\n");
    printf("--- --------|--------------------------------------------------------------\n");

    int failures = 0;

    for (int n = FIRST_N; n <= LAST_N; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) {
            fprintf(log, "n=%d: missing LUT reference\n", n);
            failures++;
            continue;
        }

        stats_t stats[8];
        for (int s = 0; s < solver_count; s++) {
            init_stats(&stats[s]);
        }

        fprintf(log, "============================================================\n");
        fprintf(log, "n=%d expected_length=%d expected_marks=", n, expected->length);
        log_ruler(log, expected);
        fprintf(log, "\n");
        fflush(log);

        for (int s = 0; s < solver_count; s++) {
            fprintf(log, "\n%s:\n", solvers[s].name);
            fflush(log);
            for (int run = 1; run <= RUNS_PER_SOLVER; run++) {
                fprintf(log, "  run=%02d start\n", run);
                fflush(log);

                run_result_t rr = run_solver_timed(solvers[s].solve, n, expected->length);

                add_time(&stats[s], rr.elapsed_ms);

                bool valid = rr.found && verify_ruler(&rr.ruler, n);
                bool optimal = valid && rr.ruler.length == expected->length;
                if (valid) {
                    stats[s].valid++;
                }
                if (optimal) {
                    stats[s].optimal++;
                    stats[s].same_length_as_ref++;
                }
                if (rr.timeout) {
                    stats[s].timeouts++;
                }

                fprintf(log, "  run=%02d timeout=%s found=%s valid=%s length=%d optimal=%s elapsed_ms=%.6f marks=",
                        run,
                        rr.timeout ? "yes" : "no",
                        rr.found ? "yes" : "no",
                        valid ? "yes" : "no",
                        rr.found ? rr.ruler.length : -1,
                        optimal ? "yes" : "no",
                        rr.elapsed_ms);
                if (rr.found) {
                    log_ruler(log, &rr.ruler);
                } else {
                    fprintf(log, "[]");
                }
                fprintf(log, "\n");
                fflush(log);
            }

            fprintf(log,
                    "  summary valid=%d/%d optimal=%d/%d timeouts=%d/%d avg_ms=%.6f min_ms=%.6f max_ms=%.6f\n",
                    stats[s].valid, RUNS_PER_SOLVER,
                    stats[s].optimal, RUNS_PER_SOLVER,
                    stats[s].timeouts, RUNS_PER_SOLVER,
                    stats[s].total_ms / RUNS_PER_SOLVER,
                    stats[s].min_ms,
                    stats[s].max_ms);
            fflush(log);
        }

        printf("%2d %8d |", n, expected->length);
        for (int s = 0; s < solver_count; s++) {
            printf(" %3d/%d %7.3f",
                   stats[s].optimal,
                   RUNS_PER_SOLVER,
                   stats[s].total_ms / RUNS_PER_SOLVER);
        }
        printf("\n");

        double traditional_avg = stats[solver_count - 1].total_ms / RUNS_PER_SOLVER;
        fprintf(log, "\nPerformance vs Traditional:\n");
        for (int s = 0; s < solver_count - 1; s++) {
            double avg = stats[s].total_ms / RUNS_PER_SOLVER;
            fprintf(log, "  %s avg_ms=%.6f speedup=%.3fx\n",
                    solvers[s].name,
                    avg,
                    avg > 0.0 ? traditional_avg / avg : 0.0);

            if (stats[s].optimal != RUNS_PER_SOLVER || avg >= traditional_avg) {
                failures++;
            }
        }

        if (stats[solver_count - 1].optimal != RUNS_PER_SOLVER) {
            failures++;
        }

        fprintf(log, "\n");
    }

    fprintf(log, "Overall status: %s\n", failures == 0 ? "PASS" : "FAIL");
    fclose(log);

    printf("\nStatus: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
