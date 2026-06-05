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
extern bool solve_golomb_evolutionary(int n, int target_length, ruler_t *out, bool verbose);
extern bool solve_golomb(int n, int target_length, ruler_t *out, bool verbose);
extern const ruler_t *lut_lookup_by_marks(int marks);

#define NUM_RUNS 10

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

int main() {
    mkdir("benchmark_logs", 0755);
    
    char log_path[256];
    snprintf(log_path, sizeof(log_path), 
             "benchmark_logs/benchmark_n10_11_%ld.txt", time(NULL));
    
    FILE *log = fopen(log_path, "w");
    if (!log) { perror("fopen"); return 1; }
    
    fprintf(log, "========================================\n");
    fprintf(log, "n=10,11 TEST - Best Evolutionary Config\n");
    fprintf(log, "Config: pop=30, mut=0.50, xo=0.95\n");
    fprintf(log, "========================================\n\n");
    
    printf("\n========================================\n");
    printf("n=10,11 TEST - Best Evolutionary Config\n");
    printf("Config: pop=30, mut=0.50, xo=0.95\n");
    printf("========================================\n\n");
    
    /* Test n=10 und n=11 */
    for (int n = 10; n <= 11; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) continue;
        
        int expected_len = expected->length;
        
        fprintf(log, "\n========== n=%d, Expected L=%d ==========\n", n, expected_len);
        printf("\n========== n=%d, Expected L=%d ==========\n", n, expected_len);
        
        /* Evolutionary mit bester Config */
        double times[NUM_RUNS];
        int successes = 0;
        int optimal = 0;
        double min_t = 1e300, max_t = 0, sum_t = 0;
        
        fprintf(log, "\nEvolutionary [pop=30,mut=0.50,xo=0.95]:\n");
        printf("\nEvolutionary [pop=30,mut=0.50,xo=0.95]:\n");
        
        for (int run = 0; run < NUM_RUNS; run++) {
            ruler_t result;
            double start = get_time_ms();
            bool found = solve_golomb_evolutionary(n, expected_len, &result, false);
            double elapsed = get_time_ms() - start;
            
            times[run] = elapsed;
            sum_t += elapsed;
            if (elapsed < min_t) min_t = elapsed;
            if (elapsed > max_t) max_t = elapsed;
            
            bool valid = found ? verify_ruler(&result, n) : false;
            if (valid) {
                successes++;
                if (result.length == expected_len) optimal++;
            }
            
            fprintf(log, "Run %2d: ", run + 1);
            printf("Run %2d: ", run + 1);
            
            if (valid) {
                fprintf(log, "L=%3d %s | %.2f ms\n", 
                        result.length, 
                        (result.length == expected_len) ? "OPT" : "sub",
                        elapsed);
                printf("L=%3d %s | %.2f ms\n", 
                       result.length,
                       (result.length == expected_len) ? "OPT" : "sub",
                       elapsed);
            } else {
                fprintf(log, "FAILED | %.2f ms\n", elapsed);
                printf("FAILED | %.2f ms\n", elapsed);
            }
        }
        
        double avg_t = sum_t / NUM_RUNS;
        
        fprintf(log, "\nStats: avg=%.2f ms, min=%.2f ms, max=%.2f ms\n", avg_t, min_t, max_t);
        fprintf(log, "Success: %d/%d, Optimal: %d/%d\n", successes, NUM_RUNS, optimal, NUM_RUNS);
        
        printf("\nStats: avg=%.2f ms, min=%.2f ms, max=%.2f ms\n", avg_t, min_t, max_t);
        printf("Success: %d/%d, Optimal: %d/%d\n", successes, NUM_RUNS, optimal, NUM_RUNS);
        
        /* Traditional als Referenz */
        fprintf(log, "\nTraditional (Reference):\n");
        printf("\nTraditional (Reference):\n");
        
        double t_start = get_time_ms();
        ruler_t trad_result;
        bool trad_found = solve_golomb(n, expected_len, &trad_result, false);
        double t_elapsed = get_time_ms() - t_start;
        
        if (trad_found && verify_ruler(&trad_result, n)) {
            fprintf(log, "L=%d OPT | %.2f ms\n", trad_result.length, t_elapsed);
            printf("L=%d OPT | %.2f ms\n", trad_result.length, t_elapsed);
        } else {
            fprintf(log, "FAILED | %.2f ms\n", t_elapsed);
            printf("FAILED | %.2f ms\n", t_elapsed);
        }
    }
    
    fprintf(log, "\n\nLog saved to: %s\n", log_path);
    printf("\n\nLog saved to: %s\n", log_path);
    
    fclose(log);
    return 0;
}
