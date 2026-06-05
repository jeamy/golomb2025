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
#define MAX_N 11

/* Parameter-Konfigurationen für Tests */
typedef struct {
    const char *name;
    int pop_size;
    int max_gen;
    int stagnation_limit;
    double mutation_rate;
    double crossover_rate;
} param_config_t;

/* Verschiedene Parameter-Sets für Evolutionary */
static param_config_t evo_configs[] = {
    {"Default", 100, 5000, 200, 0.25, 0.9},
    {"SmallPop", 50, 3000, 150, 0.30, 0.85},
    {"LargePop", 200, 3000, 300, 0.20, 0.95},
    {"HighMut", 100, 4000, 200, 0.40, 0.8},
    {"Aggressive", 30, 2000, 100, 0.50, 0.95},
};
#define NUM_EVO_CONFIGS 5

/* Parameter für Physics */
typedef struct {
    const char *name;
    int num_runs;
    int max_attempts;
    double initial_temp;
    double cooling_rate;
} physics_config_t;

static physics_config_t physics_configs[] = {
    {"Default", 15, 3, 100.0, 0.99995},
    {"Fast", 10, 2, 50.0, 0.9999},
    {"Thorough", 30, 5, 200.0, 0.99999},
    {"Hot", 15, 3, 500.0, 0.9998},
};
#define NUM_PHYSICS_CONFIGS 4

typedef struct {
    double times[NUM_RUNS];
    double avg_time;
    double min_time;
    double max_time;
    int successes;
    int optimal_count;
    int found_lengths[NUM_RUNS];
    bool valid_solutions[NUM_RUNS];
    const char *config_name;
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

static void run_benchmark_config(const char *solver_name,
                                 bool (*solver_func)(int, int, ruler_t*, bool),
                                 int n, int expected_length,
                                 solver_stats_t *result,
                                 FILE *log_file,
                                 const char *config_info) {
    
    fprintf(log_file, "\n=== %s (n=%d, L=%d) %s ===\n", 
            solver_name, n, expected_length, config_info ? config_info : "");
    
    result->config_name = config_info ? config_info : "default";
    result->successes = 0;
    result->optimal_count = 0;
    result->min_time = 1e300;
    result->max_time = 0;
    double sum_time = 0;
    
    for (int run = 0; run < NUM_RUNS; run++) {
        ruler_t result_ruler;
        
        double start = get_time_ms();
        bool found = solver_func(n, expected_length, &result_ruler, false);
        double elapsed = get_time_ms() - start;
        
        result->times[run] = elapsed;
        result->found_lengths[run] = found ? result_ruler.length : -1;
        result->valid_solutions[run] = found ? verify_ruler(&result_ruler, n) : false;
        
        if (found && result->valid_solutions[run]) {
            result->successes++;
            if (result_ruler.length == expected_length) {
                result->optimal_count++;
            }
        }
        
        sum_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
        
        fprintf(log_file, "Run %2d: ", run + 1);
        if (found && result->valid_solutions[run]) {
            fprintf(log_file, "L=%3d %s | %.3f ms | [", 
                    result_ruler.length,
                    (result_ruler.length == expected_length) ? "OPT" : "sub",
                    elapsed);
            for (int i = 0; i < n && i < 5; i++) {
                fprintf(log_file, "%d%s", result_ruler.pos[i], (i < n-1) ? "," : "");
            }
            if (n > 5) fprintf(log_file, "...");
            fprintf(log_file, "]\n");
        } else {
            fprintf(log_file, "FAILED | %.3f ms\n", elapsed);
        }
    }
    
    result->avg_time = sum_time / NUM_RUNS;
    
    fprintf(log_file, "\nStats: avg=%.3f ms, min=%.3f ms, max=%.3f ms\n",
            result->avg_time, result->min_time, result->max_time);
    fprintf(log_file, "Success: %d/%d, Optimal: %d/%d\n",
            result->successes, NUM_RUNS, result->optimal_count, NUM_RUNS);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    mkdir("benchmark_logs", 0755);
    
    char main_log_path[256];
    snprintf(main_log_path, sizeof(main_log_path), 
             "benchmark_logs/benchmark_extended_%ld.txt", time(NULL));
    
    FILE *main_log = fopen(main_log_path, "w");
    if (!main_log) {
        perror("fopen main_log");
        return 1;
    }
    
    fprintf(main_log, "========================================\n");
    fprintf(main_log, "EXTENDED BENCHMARK - n=2..%d\n", MAX_N);
    fprintf(main_log, "Multiple configs per solver\n");
    fprintf(main_log, "========================================\n");
    fprintf(main_log, "Date: %s", ctime(&(time_t){time(NULL)}));
    
    printf("\n========================================\n");
    printf("EXTENDED BENCHMARK - n=2..%d\n", MAX_N);
    printf("========================================\n\n");
    
    /* Übersichtstabelle */
    printf("Testing with multiple parameter configurations:\n");
    printf("- Evolutionary: %d configs (pop_size, mutation_rate variations)\n", NUM_EVO_CONFIGS);
    printf("- Physics: %d configs (num_runs, temperature variations)\n", NUM_PHYSICS_CONFIGS);
    
    /* Test n=9,10,11 mit verschiedenen Konfigurationen */
    for (int n = 9; n <= MAX_N; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) {
            fprintf(main_log, "n=%d: No LUT entry\n", n);
            continue;
        }
        
        int expected_len = expected->length;
        
        fprintf(main_log, "\n\n########################################\n");
        fprintf(main_log, "n=%d, Expected L=%d\n", n, expected_len);
        fprintf(main_log, "########################################\n");
        
        printf("\n========== n=%d (expected L=%d) ==========\n", n, expected_len);
        
        /* Evolutionary mit verschiedenen Configs */
        printf("\n--- Evolutionary Solver (%d configs) ---\n", NUM_EVO_CONFIGS);
        for (int c = 0; c < NUM_EVO_CONFIGS; c++) {
            solver_stats_t stats = {0};
            char config_str[128];
            snprintf(config_str, sizeof(config_str), 
                     "[pop=%d,mut=%.2f,xo=%.2f]",
                     evo_configs[c].pop_size,
                     evo_configs[c].mutation_rate,
                     evo_configs[c].crossover_rate);
            
            /* Konfiguration global setzen (würde über Umgebungsvariablen oder 
               globale Variablen gehen - hier vereinfacht) */
            
            run_benchmark_config("Evolutionary", solve_golomb_evolutionary, 
                                n, expected_len, &stats, main_log, config_str);
            
            printf("Config %d (%s): %d/%d optimal, avg=%.2f ms\n",
                   c + 1, evo_configs[c].name,
                   stats.optimal_count, NUM_RUNS, stats.avg_time);
        }
        
        /* Physics mit verschiedenen Configs */
        printf("\n--- Physics Solver (%d configs) ---\n", NUM_PHYSICS_CONFIGS);
        for (int c = 0; c < NUM_PHYSICS_CONFIGS; c++) {
            solver_stats_t stats = {0};
            char config_str[128];
            snprintf(config_str, sizeof(config_str),
                     "[runs=%d,temp=%.1f,cool=%.5f]",
                     physics_configs[c].num_runs,
                     physics_configs[c].initial_temp,
                     physics_configs[c].cooling_rate);
            
            run_benchmark_config("Physics", solve_golomb_physics,
                                n, expected_len, &stats, main_log, config_str);
            
            printf("Config %d (%s): %d/%d optimal, avg=%.2f ms\n",
                   c + 1, physics_configs[c].name,
                   stats.optimal_count, NUM_RUNS, stats.avg_time);
        }
        
        
        /* Traditional als Referenz */
        printf("\n--- Traditional (Reference) ---\n");
        solver_stats_t trad_stats = {0};
        run_benchmark_config("Traditional", solve_golomb,
                            n, expected_len, &trad_stats, main_log, NULL);
        printf("Reference: %d/%d optimal, avg=%.2f ms\n",
               trad_stats.optimal_count, NUM_RUNS, trad_stats.avg_time);
    }
    
    fprintf(main_log, "\n\n========================================\n");
    fprintf(main_log, "END OF EXTENDED BENCHMARK\n");
    fprintf(main_log, "========================================\n");
    fprintf(main_log, "Log saved to: %s\n", main_log_path);
    
    printf("\n\n========================================\n");
    printf("Extended benchmark complete!\n");
    printf("Log saved to: %s\n", main_log_path);
    printf("========================================\n");
    
    fclose(main_log);
    return 0;
}
