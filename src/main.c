#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <length> [-v]\n", prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc < 2) usage(argv[0]);
    int length = atoi(argv[1]);
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    bool verbose = false;
    if (argc >= 3 && strcmp(argv[2], "-v") == 0) verbose = true;

    const ruler_t *ref = lut_lookup_by_length(length);

    ruler_t result;
    bool solved = false;
    bool compared = false;
    bool optimal = false;

    if (ref) {
        if (verbose) {
            printf("Reference optimal ruler from LUT:\n");
            print_ruler(ref);
        }
        solved = solve_golomb(ref->marks, length, &result, verbose);
        compared = true;
        optimal = solved && (result.length == ref->length);
    } else {
        /* Kein LUT-Eintrag: Versuche steigende n */
        for (int n = 2; n <= MAX_MARKS; ++n) {
            if (solve_golomb(n, length, &result, verbose)) { solved = true; break; }
        }
    }

    if (!solved) {
        fprintf(stderr, "Konnte kein Golomb-Lineal der Länge %d finden (n ≤ %d).\n", length, MAX_MARKS);
        return EXIT_FAILURE;
    }

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec)/1e9;
    printf("Gefundenes Lineal: ");
    print_ruler(&result);
    printf("Berechnungszeit: %.6f s\n", elapsed);

    char fname[32];
    snprintf(fname, sizeof fname, "GOL_%d.txt", length);
    FILE *fp = fopen(fname, "w");
    if (fp) {
        fprintf(fp, "length=%d\nmarks=%d\npositions=", result.length, result.marks);
        for (int i = 0; i < result.marks; ++i) {
            fprintf(fp, "%d%s", result.pos[i], (i == result.marks-1) ? "" : " ");
        }
        fprintf(fp, "\nseconds=%.6f\n", elapsed);
        if (compared) fprintf(fp, "optimal=%s\n", optimal ? "yes" : "no");
        fclose(fp);
    } else {
        perror("fopen");
    }
    if (compared) {
        printf("Status: %s\n", optimal ? "Optimal ✅" : "Nicht optimal ❌");
        return optimal ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        puts("Kein Vergleich möglich (LUT-Eintrag fehlt).\n");
        return EXIT_SUCCESS;
    }
}
