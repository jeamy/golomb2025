#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>

/* Für solver.c benötigte externe Variablen */
const char *g_cp_path = NULL;
int g_cp_interval_sec = 60;

/* Externe Solver-Funktionen */
extern bool solve_golomb_physics(int n, int target_length, ruler_t *out, bool verbose);
extern bool solve_golomb_evolutionary(int n, int target_length, ruler_t *out, bool verbose);
extern bool solve_golomb(int n, int target_length, ruler_t *out, bool verbose);
extern const ruler_t *lut_lookup_by_marks(int marks);

#define NUM_RUNS 10
#define MAX_N 8

typedef struct {
    double times[NUM_RUNS];
    double avg_time;
    double min_time;
    double max_time;
    int successes;
    int optimal_count;
    int found_lengths[NUM_RUNS];
    bool valid_solutions[NUM_RUNS];
} solver_stats_t;

static double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static bool verify_ruler(const ruler_t *ruler, int n) {
    if (ruler->marks != n) return false;
    
    bool seen[MAX_LEN_BITSET + 1] = {false};
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int d = ruler->pos[j] - ruler->pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET) return false;
            if (seen[d]) return false;
            seen[d] = true;
        }
    }
    return true;
}

static void run_benchmark(const char *solver_name,
                        bool (*solver_func)(int, int, ruler_t*, bool),
                        int n, int expected_length,
                        solver_stats_t *stats,
                        FILE *log_file) {
    
    fprintf(log_file, "\n=== %s (n=%d, expected L=%d) ===\n", solver_name, n, expected_length);
    
    stats->successes = 0;
    stats->optimal_count = 0;
    stats->min_time = 1e300;
    stats->max_time = 0;
    double sum_time = 0;
    
    for (int run = 0; run < NUM_RUNS; run++) {
        ruler_t result;
        
        double start = get_time_ms();
        bool found = solver_func(n, expected_length, &result, false);
        double elapsed = get_time_ms() - start;
        
        stats->times[run] = elapsed;
        stats->found_lengths[run] = found ? result.length : -1;
        stats->valid_solutions[run] = found ? verify_ruler(&result, n) : false;
        
        if (found && stats->valid_solutions[run]) {
            stats->successes++;
            if (result.length == expected_length) {
                stats->optimal_count++;
            }
        }
        
        sum_time += elapsed;
        if (elapsed < stats->min_time) stats->min_time = elapsed;
        if (elapsed > stats->max_time) stats->max_time = elapsed;
        
        fprintf(log_file, "Run %2d: ", run + 1);
        if (found && stats->valid_solutions[run]) {
            fprintf(log_file, "L=%3d %s | %.3f ms | [", 
                    result.length,
                    (result.length == expected_length) ? "OPT" : "sub",
                    elapsed);
            for (int i = 0; i < n && i < 6; i++) {
                fprintf(log_file, "%d%s", result.pos[i], (i < n-1) ? "," : "");
            }
            if (n > 6) fprintf(log_file, "...");
            fprintf(log_file, "]\n");
        } else {
            fprintf(log_file, "FAILED | %.3f ms\n", elapsed);
        }
    }
    
    stats->avg_time = sum_time / NUM_RUNS;
    
    fprintf(log_file, "\nStats: avg=%.3f ms, min=%.3f ms, max=%.3f ms\n",
            stats->avg_time, stats->min_time, stats->max_time);
    fprintf(log_file, "Success: %d/%d, Optimal: %d/%d\n",
            stats->successes, NUM_RUNS, stats->optimal_count, NUM_RUNS);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Verzeichnis für Logs erstellen */
    mkdir("benchmark_logs", 0755);
    
    /* Haupt-Logdatei */
    char main_log_path[256];
    snprintf(main_log_path, sizeof(main_log_path), 
             "benchmark_logs/benchmark_%ld.txt", time(NULL));
    
    FILE *main_log = fopen(main_log_path, "w");
    if (!main_log) {
        perror("fopen main_log");
        return 1;
    }
    
    fprintf(main_log, "========================================\n");
    fprintf(main_log, "SOLVER BENCHMARK - 10 Runs per Algorithm\n");
    fprintf(main_log, "========================================\n");
    fprintf(main_log, "Date: %s", ctime(&(time_t){time(NULL)}));
    fprintf(main_log, "Testing n=2..%d\n\n", MAX_N);
    
    /* Solver-Definitionen */
    struct {
        const char *name;
        bool (*func)(int, int, ruler_t*, bool);
        solver_stats_t stats[MAX_N + 1];
    } solvers[] = {
        {"Physics", solve_golomb_physics, {}},
        {"Evolutionary", solve_golomb_evolutionary, {}},
        {"Traditional", solve_golomb, {}}  /* Referenz */
    };
    int num_solvers = sizeof(solvers) / sizeof(solvers[0]);
    
    /* Ergebnis-Tabelle */
    printf("\nBENCHMARK: 10 runs per solver, n=2..%d\n", MAX_N);
    printf("=====================================================\n");
    printf("n  | Expected | ");
    for (int s = 0; s < num_solvers; s++) {
        printf("%-12s | ", solvers[s].name);
    }
    printf("\n");
    printf("---|----------|");
    for (int s = 0; s < num_solvers; s++) {
        printf("---------------|");
    }
    printf("\n");
    
    /* Benchmark für jedes n */
    for (int n = 2; n <= MAX_N; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) continue;
        
        int expected_len = expected->length;
        
        printf("%2d |    %3d   | ", n, expected_len);
        fprintf(main_log, "\n\n########################################\n");
        fprintf(main_log, "n=%d, Expected L=%d\n", n, expected_len);
        fprintf(main_log, "########################################\n");
        
        for (int s = 0; s < num_solvers; s++) {
            run_benchmark(solvers[s].name, solvers[s].func, n, expected_len,
                         &solvers[s].stats[n], main_log);
            
            solver_stats_t *st = &solvers[s].stats[n];
            if (st->optimal_count == NUM_RUNS) {
                printf("OK (%5.1fms) | ", st->avg_time);
            } else if (st->optimal_count > 0) {
                printf("~%d/%d (%5.1f)| ", st->optimal_count, NUM_RUNS, st->avg_time);
            } else if (st->successes > 0) {
                printf("sub (%5.1f) | ", st->avg_time);
            } else {
                printf("FAIL        | ");
            }
        }
        printf("\n");
    }
    
    /* Zusammenfassung */
    fprintf(main_log, "\n\n========================================\n");
    fprintf(main_log, "SUMMARY\n");
    fprintf(main_log, "========================================\n");
    
    printf("\n=====================================================\n");
    printf("SUMMARY\n");
    printf("=====================================================\n");
    
    for (int s = 0; s < num_solvers; s++) {
        fprintf(main_log, "\n%s:\n", solvers[s].name);
        printf("\n%s:\n", solvers[s].name);
        
        int total_optimal = 0;
        int total_success = 0;
        double total_time = 0;
        int count = 0;
        
        for (int n = 2; n <= MAX_N; n++) {
            solver_stats_t *st = &solvers[s].stats[n];
            total_optimal += st->optimal_count;
            total_success += st->successes;
            total_time += st->avg_time;
            count++;
            
            fprintf(main_log, "  n=%d: %d/%d optimal, avg=%.3f ms\n",
                    n, st->optimal_count, NUM_RUNS, st->avg_time);
        }
        
        fprintf(main_log, "  TOTAL: %d/%d optimal (%.1f%%), avg time=%.3f ms\n",
                total_optimal, count * NUM_RUNS,
                100.0 * total_optimal / (count * NUM_RUNS),
                total_time / count);
        
        printf("  Optimal: %d/%d (%.1f%%)\n",
               total_optimal, count * NUM_RUNS,
               100.0 * total_optimal / (count * NUM_RUNS));
        printf("  Avg time: %.3f ms\n", total_time / count);
    }
    
    /* Vergleich mit Traditionell */
    fprintf(main_log, "\n\nPERFORMANCE COMPARISON (vs Traditional):\n");
    printf("\nPERFORMANCE COMPARISON (vs Traditional):\n");
    
    double trad_avg = 0;
    for (int n = 2; n <= MAX_N; n++) {
        trad_avg += solvers[3].stats[n].avg_time;
    }
    trad_avg /= (MAX_N - 1);
    
    for (int s = 0; s < 3; s++) {  /* Nur die neuen Solver */
        double avg = 0;
        for (int n = 2; n <= MAX_N; n++) {
            avg += solvers[s].stats[n].avg_time;
        }
        avg /= (MAX_N - 1);
        
        double ratio = avg / trad_avg;
        fprintf(main_log, "  %s: %.2fx of traditional (%.3f ms vs %.3f ms)\n",
                solvers[s].name, ratio, avg, trad_avg);
        printf("  %s: %.2fx of traditional\n", solvers[s].name, ratio);
    }
    
    fprintf(main_log, "\nLog saved to: %s\n", main_log_path);
    printf("\nDetailed log saved to: %s\n", main_log_path);
    
    fclose(main_log);
    return 0;
}
