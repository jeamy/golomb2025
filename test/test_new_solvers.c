#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

/* Externe Solver-Funktionen */
extern bool solve_golomb_physics(int n, int target_length, ruler_t *out, bool verbose);
extern bool solve_golomb_evolutionary(int n, int target_length, ruler_t *out, bool verbose);
extern const ruler_t *lut_lookup_by_marks(int marks);

/* Testergebnis */
typedef struct {
    const char *name;
    int n;
    int found_length;
    int expected_length;
    bool found;
    bool optimal;
    double time_ms;
} test_result_t;

/* Einen einzelnen Solver testen */
static bool test_solver(const char *solver_name,
                        bool (*solver_func)(int, int, ruler_t*, bool),
                        int n, int expected_length, 
                        test_result_t *result) {
    ruler_t out;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Versuche mit erwarteter Länge zu lösen */
    bool found = solver_func(n, expected_length, &out, false);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                        (end.tv_nsec - start.tv_nsec) / 1e6;
    
    result->name = solver_name;
    result->n = n;
    result->found = found;
    result->expected_length = expected_length;
    result->time_ms = elapsed_ms;
    
    if (found) {
        result->found_length = out.length;
        result->optimal = (out.length == expected_length);
        
        /* Verifiziere Gültigkeit */
        bool seen[MAX_LEN_BITSET + 1] = {false};
        bool valid = true;
        for (int i = 0; i < n && valid; i++) {
            for (int j = i + 1; j < n; j++) {
                int d = out.pos[j] - out.pos[i];
                if (d <= 0 || d > MAX_LEN_BITSET || seen[d]) {
                    valid = false;
                    break;
                }
                seen[d] = true;
            }
        }
        
        if (!valid) {
            printf("  [ERROR] %s(n=%d): Ungültiges Lineal!\n", solver_name, n);
            result->optimal = false;
        }
        
        return valid && result->optimal;
    } else {
        result->found_length = -1;
        result->optimal = false;
        return false;
    }
}

/* Vergleiche zwei Lineale */
static bool rulers_equal(const ruler_t *a, const ruler_t *b) {
    if (a->marks != b->marks) return false;
    if (a->length != b->length) return false;
    
    for (int i = 0; i < a->marks; i++) {
        if (a->pos[i] != b->pos[i]) return false;
    }
    return true;
}

/* Print ruler */
static void print_ruler_compact(const ruler_t *r) {
    printf("[%d", r->pos[0]);
    for (int i = 1; i < r->marks; i++) {
        printf(",%d", r->pos[i]);
    }
    printf("]");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("=================================================================\n");
    printf("Test der neuen Golomb-Solver (Physics, Evolutionary)\n");
    printf("=================================================================\n\n");
    
    /* Maximales n zum Testen */
    int max_n = 14;
    
    test_result_t results[2][14]; /* 2 Solver, n=2..14 */
    int passed[2] = {0, 0};
    int failed[2] = {0, 0};
    int timeouts[2] = {0, 0};
    
    const char *solver_names[2] = {"Physics", "Evolutionary"};
    bool (*solvers[2])(int, int, ruler_t*, bool) = {
        solve_golomb_physics,
        solve_golomb_evolutionary
    };
    
    /* Header */
    printf("n  | Expected | ");
    for (int s = 0; s < 2; s++) {
        printf("%-12s | ", solver_names[s]);
    }
    printf("\n");
    printf("---|----------|");
    for (int s = 0; s < 2; s++) {
        printf("---------------|");
    }
    printf("\n");
    
    /* Teste für n = 2 bis 14 */
    for (int n = 2; n <= max_n; n++) {
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) {
            printf("%2d |  (skip)  | LUT-Eintrag fehlt\n", n);
            continue;
        }
        
        int expected_len = expected->length;
        
        printf("%2d |    %3d   | ", n, expected_len);
        
        /* Teste alle drei Solver */
        for (int s = 0; s < 2; s++) {
            test_result_t *res = &results[s][n-2];
            
            /* Timeout-Mechanismus: Wenn Solver zu lange braucht, abbrechen */
            /* Wir nutzen einen einfachen Ansatz: Suche nur mit max 5 Sekunden */
            
            bool ok = test_solver(solver_names[s], solvers[s], n, expected_len, res);
            
            if (res->time_ms > 5000) {
                printf("TIMEOUT      | ");
                timeouts[s]++;
            } else if (!res->found) {
                printf("NOT FOUND    | ");
                failed[s]++;
            } else if (ok) {
                printf("OK (%5.1fms) | ", res->time_ms);
                passed[s]++;
            } else {
                printf("WRONG LEN    | ");
                failed[s]++;
            }
        }
        printf("\n");
    }
    
    /* Zusammenfassung */
    printf("\n=================================================================\n");
    printf("ZUSAMMENFASSUNG\n");
    printf("=================================================================\n");
    
    for (int s = 0; s < 2; s++) {
        printf("\n%s:\n", solver_names[s]);
        printf("  Erfolgreich: %d/%d\n", passed[s], max_n - 1);
        printf("  Fehlgeschlagen: %d\n", failed[s]);
        printf("  Timeouts: %d\n", timeouts[s]);
        
        if (passed[s] == max_n - 1) {
            printf("  Status: ALLE TESTS BESTANDEN ✓\n");
        } else if (passed[s] >= (max_n - 1) / 2) {
            printf("  Status: TEILWEISE BESTANDEN (~)\n");
        } else {
            printf("  Status: VIELE FEHLER ✗\n");
        }
    }
    
    /* Detaillierte Ergebnisse für fehlgeschlagene Tests */
    printf("\n=================================================================\n");
    printf("DETAILS FEHLGESCHLAGENER TESTS\n");
    printf("=================================================================\n");
    
    bool any_failures = false;
    for (int s = 0; s < 2; s++) {
        for (int n = 2; n <= max_n; n++) {
            test_result_t *res = &results[s][n-2];
            if (res->found && !res->optimal) {
                if (!any_failures) {
                    any_failures = true;
                }
                printf("\n%s (n=%d):\n", solver_names[s], n);
                printf("  Erwartet: L=%d\n", res->expected_length);
                printf("  Gefunden: L=%d\n", res->found_length);
                printf("  Zeit: %.2f ms\n", res->time_ms);
            } else if (!res->found && res->expected_length > 0) {
                if (!any_failures) {
                    any_failures = true;
                }
                printf("\n%s (n=%d): NICHT GEFUNDEN\n", solver_names[s], n);
            }
        }
    }
    
    if (!any_failures) {
        printf("\nKeine Fehler! Alle Solver funktionieren korrekt.\n");
    }
    
    /* Vergleich mit LUT-Einträgen */
    printf("\n=================================================================\n");
    printf("VERGLEICH MIT OPTIMALEN LÖSUNGEN (LUT)\n");
    printf("=================================================================\n");
    
    for (int n = 2; n <= max_n && n <= 8; n++) { /* Nur kleine n für Übersicht */
        const ruler_t *expected = lut_lookup_by_marks(n);
        if (!expected) continue;
        
        printf("\nn=%d (erwartet L=%d): ", n, expected->length);
        print_ruler_compact(expected);
        printf("\n");
        
        for (int s = 0; s < 2; s++) {
            test_result_t *res = &results[s][n-2];
            if (res->found) {
                printf("  %s: L=%d ", solver_names[s], res->found_length);
                if (res->optimal) {
                    printf("[OPTIMAL]\n");
                } else {
                    printf("[suboptimal, delta=%d]\n", 
                           res->found_length - res->expected_length);
                }
            }
        }
    }
    
    /* Gesamtergebnis */
    int total_passed = passed[0] + passed[1] + passed[2];
    int total_tests = 3 * (max_n - 1);
    
    printf("\n=================================================================\n");
    printf("GESAMTERGEBNIS: %d/%d (%.1f%%)\n", 
           total_passed, total_tests, 100.0 * total_passed / total_tests);
    printf("=================================================================\n");
    
    return (total_passed == total_tests) ? 0 : 1;
}
