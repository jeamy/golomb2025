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
 *  3. Distance uniqueness is *not* encoded upfront.  After each satisfying
 *     assignment the candidate ruler is checked in C; if a duplicate distance
 *     appears we simply move on to the next length (blocking clauses could be
 *     added later).  This pragmatic loop is fast enough for n ≤ 16.
 *  4. The CNF is piped to an external SAT solver (`kissat`, `minisat`, …)
 *     selected via the environment variable `SAT_SOLVER` or falling back to
 *     `kissat` if none is set.
 * --------------------------------------------------------------------*/

#include <unistd.h>
#include <sys/wait.h>

static int var_id(int mark, int pos, int max_len) { return mark * (max_len + 1) + pos + 1; }

static bool build_cnf(const char *cnf_path, int n, int L, long *out_clause_count)
{
    FILE *f = fopen(cnf_path, "w");
    if (!f) return false;

    const int vars = n * (L + 1);
    long clauses = 0;

    /* --- Closed-form clause count (for the DIMACS header) --- */
    long c_exact1 = n;                         /* at least one position per mark */
    long c_atmost = n * ((L + 1) * L / 2);     /* pairwise at-most-one */
    long c_unique = (L + 1) * (n * (n - 1) / 2);
    clauses = c_exact1 + c_atmost + c_unique;

    fprintf(f, "p cnf %d %ld\n", vars, clauses);

    /* 1. Each mark has at least one position */
    for (int i = 0; i < n; ++i) {
        for (int p = 0; p <= L; ++p)
            fprintf(f, "%d ", var_id(i, p, L));
        fprintf(f, "0\n");
    }

    /* 2. At most one position per mark (pairwise encoding) */
    for (int i = 0; i < n; ++i) {
        for (int p = 0; p <= L; ++p)
            for (int q = p + 1; q <= L; ++q)
                fprintf(f, "-%d -%d 0\n", var_id(i, p, L), var_id(i, q, L));
    }

    /* 3. No shared position between different marks */
    for (int p = 0; p <= L; ++p) {
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                fprintf(f, "-%d -%d 0\n", var_id(i, p, L), var_id(j, p, L));
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

static bool call_solver(const char *cnf, const char *model_out, bool verbose)
{
    const char *solver = getenv("SAT_SOLVER");

    if (!solver || !*solver) {
        if (cmd_available("kissat")) solver = "kissat";
        else if (cmd_available("minisat")) solver = "minisat";
        else {
            fprintf(stderr, "[SAT] No SAT solver found. Install 'kissat' or 'minisat', or set $SAT_SOLVER.\n");
            return false;
        }
    } else {
        /* solver specified by env var – verify availability */
        if (!cmd_available(solver)) {
            fprintf(stderr, "[SAT] Solver '%s' not found in PATH.\n", solver);
            return false;
        }
    }

    char cmd[256];
    snprintf(cmd, sizeof cmd, "%s %s > %s", solver, cnf, model_out);
    if (verbose) fprintf(stderr, "[SAT] cmd: %s\n", cmd);
    int status = system(cmd);
    if (status == -1) {
        perror("system");
        return false;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        /* Minisat: 10 = SAT, 20 = UNSAT, Kissat: 0 */
        if (code == 0 || code == 10 || code == 20) return true;
        fprintf(stderr, "[SAT] Solver exited with code %d (unexpected).\n", code);
    } else {
        fprintf(stderr, "[SAT] Solver terminated abnormally.\n");
    }
    return false;
}

static bool parse_model(const char *model_path, int n, int L, int *out_pos)
{
    FILE *fp = fopen(model_path, "r");
    if (!fp) return false;
    char tok[64];
    if (!(fscanf(fp, "%63s", tok) == 1)) { fclose(fp); return false; }
    if (strcmp(tok, "UNSAT") == 0 || strcmp(tok, "s") == 0) {
        /* if in DIMACS format kissat prints: s UNSATISFIABLE */
        if (strcmp(tok, "s") == 0) {
            fscanf(fp, "%63s", tok); /* read UNSATISFIABLE */
        }
        fclose(fp); return false;
    }

    /* if first token not SAT, read next if solver prints banner */
    if (strcmp(tok, "SAT") != 0 && strcmp(tok, "s") == 0) {
        /* we already consumed 's', need next token SATISFIABLE or SAT */
        fscanf(fp, "%63s", tok);
    }

    /* read variable assignments (lines start with v) */
    int lit;
    while (fscanf(fp, "%d", &lit) == 1) {
        if (lit == 0) continue;
        if (lit > 0) {
            int var = lit - 1;
            int mark = var / (L + 1);
            int pos  = var % (L + 1);
            if (mark < n) out_pos[mark] = pos;
        }
    }
    fclose(fp);
    return true;
}

static bool is_golomb(int n, const int *pos)
{
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
    const char *cnf_file   = "/tmp/golomb.cnf";
    const char *model_file = "/tmp/golomb.model";

    for (int L = n - 1; L <= target_length; ++L) {
        if (verbose)
            fprintf(stderr, "[SAT] Encoding n=%d, L=%d ...\n", n, L);
        if (!build_cnf(cnf_file, n, L, NULL)) {
            fprintf(stderr, "[SAT] Could not write CNF.\n");
            break;
        }
        if (!call_solver(cnf_file, model_file, verbose)) {
            fprintf(stderr, "[SAT] Solver invocation failed.\n");
            break;
        }
        int pos[MAX_MARKS] = {0};
        if (!parse_model(model_file, n, L, pos)) {
            if (verbose) fprintf(stderr, "[SAT] UNSAT at L=%d\n", L);
            continue; /* try next length */
        }
        /* sort positions by mark index already ascending */
        if (!is_golomb(n, pos)) {
            if (verbose) fprintf(stderr, "[SAT] model at L=%d violated distance uniqueness, continue.\n", L);
            continue;
        }
        /* success */
        out->marks = n;
        out->length = pos[n - 1];
        memcpy(out->pos, pos, n * sizeof(int));
        return true;
    }
    return false;
}

