#include "golomb.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

/* ----------------------------------------------------------------------
 * SAT-based Golomb ruler solver (flag `-x`)
 * ----------------------------------------------------------------------
 *  1. Boolean variables v(i,p) encode that mark i is placed at absolute
 *     position p where 0 ≤ p ≤ L.
 *  2. Hard constraints emitted in DIMACS CNF:
 *       a) Exactly one position per mark (exact-one).
 *       b) No two marks share the same position.
 *       c) All distances between marks must be unique (encoded directly in CNF).
 *  3. Uses CaDiCaL as the SAT solver for better performance.
 * --------------------------------------------------------------------*/

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Variablen-IDs: mark_i ist auf Position p -> var[i,p] = i*(L+1) + p + 1 */
static int var_id(int mark, int pos, int L) { return mark * (L + 1) + pos + 1; }

static bool build_cnf(const char *cnf_path, int n, int L, long *out_clause_count)
{
    FILE *f = fopen(cnf_path, "w");
    if (!f) return false;

    /* Calculate number of variables */
    int num_pairs = n * (n - 1) / 2;
    /* Variables: mark positions + distance variables */
    int vars = n * (L + 1) + num_pairs * (L + 1);
    long clauses = 0;

    /* 1. Each mark has exactly one position */
    clauses += n;                          /* at-least-one */
    clauses += n * (L * (L + 1) / 2);      /* at-most-one */

    /* 2. No shared positions */
    clauses += (L + 1) * (n * (n - 1) / 2);

    /* 3. Distance uniqueness */
    /* For each pair of distances, we need one clause to ensure they're different */
    clauses += num_pairs * (num_pairs - 1) / 2;
    /* Plus clauses to link distance variables to positions */
    clauses += num_pairs * L * (L + 1) / 2;

    fprintf(f, "p cnf %d %ld\n", vars, clauses);

    /* 1. Each mark has exactly one position */
    for (int i = 0; i < n; ++i) {
        /* At least one position */
        for (int p = 0; p <= L; ++p)
            fprintf(f, "%d ", var_id(i, p, L + 1));
        fprintf(f, "0\n");

        /* At most one position */
        for (int p = 0; p <= L; ++p)
            for (int q = p + 1; q <= L; ++q)
                fprintf(f, "-%d -%d 0\n", var_id(i, p, L + 1), var_id(i, q, L + 1));
    }

    /* 2. No shared positions */
    for (int p = 0; p <= L; ++p)
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                fprintf(f, "-%d -%d 0\n", var_id(i, p, L + 1), var_id(j, p, L + 1));

    /* 3. Distance uniqueness */
    /* First, create distance variables for each pair */
    int pair_idx = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            /* For each possible distance d, create clauses that force
             * the distance variable to be true if and only if marks i and j
             * are at positions that create that distance */
            for (int d = 1; d <= L; ++d) {
                int dist_var = vars + pair_idx * (L + 1) + d;
                for (int p = 0; p + d <= L; ++p) {
                    /* If marks are at positions creating distance d,
                     * then distance variable must be true */
                    fprintf(f, "-%d -%d %d 0\n",
                        var_id(i, p, L + 1),
                        var_id(j, p + d, L + 1),
                        dist_var);
                }
            }
            pair_idx++;
        }
    }

    /* Now ensure all distance variables are different */
    pair_idx = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            int pair_idx2 = pair_idx + 1;
            for (int k = i; k < n; ++k) {
                for (int l = (k == i ? j + 1 : k + 1); l < n; ++l) {
                    if (!(i == k && j == l)) {
                        /* For each distance d, the two pairs can't both have distance d */
                        for (int d = 1; d <= L; ++d) {
                            int dist_var1 = vars + pair_idx * (L + 1) + d;
                            int dist_var2 = vars + pair_idx2 * (L + 1) + d;
                            fprintf(f, "-%d -%d 0\n", dist_var1, dist_var2);
                        }
                    }
                    pair_idx2++;
                }
            }
            pair_idx++;
        }
    }

    fclose(f);
    if (out_clause_count) *out_clause_count = clauses;
    return true;
}

static bool cmd_available(const char *name)
{
    char check[128];
    snprintf(check, sizeof check, "command -v %s >/dev/null 2>&1", name);
    int r = system(check);
    return r == 0;
}

static bool call_solver(const char *cnf_path, const char *model_out, bool verbose)
{
    if (!cmd_available("cadical")) {
        fprintf(stderr, "[SAT] CaDiCaL solver not found.\n");
        return false;
    }

    if (verbose) {
        FILE *debug = fopen(cnf_path, "r");
        if (debug) {
            char line[256];
            int lines = 0;
            while (fgets(line, sizeof(line), debug) && lines < 10) {
                fprintf(stderr, "CNF: %s", line);
                lines++;
            }
            fclose(debug);
        }
    }

    char cmd[256];
    snprintf(cmd, sizeof cmd, "cadical %s > %s 2>/dev/null", cnf_path, model_out);
    if (verbose) fprintf(stderr, "[SAT] cmd: %s\n", cmd);
    
    int status = system(cmd);
    if (status == -1) {
        perror("system");
        return false;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        /* CaDiCaL: 10 = SAT, 20 = UNSAT */
        if (code == 10 || code == 20) return true;
        if (verbose)
            fprintf(stderr, "[SAT] Solver exited with code %d.\n", code);
    } else {
        if (verbose)
            fprintf(stderr, "[SAT] Solver terminated abnormally.\n");
    }
    return false;
}

static bool parse_model(const char *model_path, int n, int L, int *out_pos, bool verbose)
{
    FILE *fp = fopen(model_path, "r");
    if (!fp) return false;

    char tok[64];
    bool sat = false;
    while (fscanf(fp, "%63s", tok) == 1) {
        if (strcmp(tok, "s") == 0) {
            if (fscanf(fp, "%63s", tok) != 1) { /* read SAT/UNSAT */
                fclose(fp);
                return false;
            }
            if (strcmp(tok, "UNSATISFIABLE") == 0) {
                if (verbose) fprintf(stderr, "[SAT] UNSAT\n");
                fclose(fp);
                return false;
            }
            sat = true;
        }
        if (strcmp(tok, "v") == 0) break;
    }
    if (!sat) {
        if (verbose) fprintf(stderr, "[SAT] No solution status found\n");
        fclose(fp);
        return false;
    }

    /* Parse model line */
    memset(out_pos, 0, n * sizeof(int));
    int var;
    while (fscanf(fp, "%d", &var) == 1) {
        if (var == 0) break;
        if (var < 0) continue;
        /* var_id(i,p,L) = i*(L+1) + p + 1 */
        int mark = (var - 1) / (L + 1);
        int pos  = (var - 1) % (L + 1);
        if (mark < n)
            out_pos[mark] = pos;
    }
    fclose(fp);
    return true;
}

static bool is_golomb(int n, const int *pos)
{
    /* Distanz-Uniqueness wird jetzt direkt in der CNF geprüft */
    /* Diese Funktion dient nur noch als Verifikation */
    bool seen[MAX_LEN_BITSET + 1] = {0};
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            int d = pos[j] - pos[i];
            if (d <= 0 || d > MAX_LEN_BITSET) return false;
            if (seen[d]) return false;
            seen[d] = true;
        }
    }
    return true;
}

bool solve_golomb_sat(int n, int target_length, ruler_t *out, bool verbose)
{
    if (n < 1 || n > MAX_MARKS) return false;

    const char *cnf_file = "/tmp/golomb.cnf";
    const char *model_file = "/tmp/golomb.model";

    /* Get optimal length from LUT if available */
    const ruler_t *known = lut_lookup_by_marks(n);
    int start_L = n - 1;  /* Start with minimal possible length */
    int max_L = target_length > 0 ? target_length : 
               (known ? known->length : n * n);
    if (verbose) fprintf(stderr, "[SAT] Using L=%d to %d\n", start_L, max_L);

    /* Try increasing lengths until we find a solution or hit max */
    for (int L = start_L; L <= max_L; ++L) {
        if (verbose)
            fprintf(stderr, "[SAT] Encoding n=%d, L=%d ...\n", n, L);

        /* Erzeuge CNF mit korrekten Variablen-IDs */
        if (!build_cnf(cnf_file, n, L, NULL)) {
            fprintf(stderr, "[SAT] Could not write CNF.\n");
            break;
        }

        /* Rufe CaDiCaL auf */
        if (!call_solver(cnf_file, model_file, verbose)) {
            if (verbose) fprintf(stderr, "[SAT] Solver invocation failed.\n");
            continue;
        }

        /* Parse Lösung */
        int pos[MAX_MARKS];
        if (!parse_model(model_file, n, L, pos, verbose)) {
            if (verbose) fprintf(stderr, "[SAT] Could not parse model at L=%d.\n", L);
            continue;
        }

        /* Prüfe Distanz-Uniqueness */
        if (!is_golomb(n, pos)) {
            if (verbose) fprintf(stderr, "[SAT] model at L=%d violated distance uniqueness.\n", L);
            continue;
        }

        /* Erfolg! */
        out->marks = n;
        out->length = pos[n - 1];
        memcpy(out->pos, pos, n * sizeof(int));
        
        /* Berechne Distanzen */
        int dist[MAX_MARKS * MAX_MARKS];
        int dcnt = 0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                dist[dcnt++] = pos[j] - pos[i];

        /* Berechne fehlende Distanzen */
        int miss[MAX_LEN_BITSET];
        int mcnt = 0;
        bool seen[MAX_LEN_BITSET] = {0};
        for (int i = 0; i < dcnt; ++i)
            seen[dist[i]] = true;
        for (int i = 1; i < pos[n-1]; ++i)
            if (!seen[i])
                miss[mcnt++] = i;

        /* Terminal-Ausgabe */
        printf("Length: %d\n", pos[n-1]);
        printf("Marks: ");
        for (int i = 0; i < n; ++i)
            printf("%d%s", pos[i], (i == n-1) ? "" : " ");
        printf("\nDistances: ");
        for (int i = 0; i < dcnt; ++i)
            printf("%d%s", dist[i], (i == dcnt-1) ? "" : " ");
        printf("\nMissing: ");
        for (int i = 0; i < mcnt; ++i)
            printf("%d%s", miss[i], (i == mcnt-1) ? "" : " ");
        printf("\n");

        if (verbose) {
            fprintf(stderr, "[SAT] Found solution at L=%d\n", L);
        }
        return true;
    }

    return false;
}
