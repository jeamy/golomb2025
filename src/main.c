#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <marks> [-v] [-mp]\n", prog);
    fprintf(stderr, "  <marks>  order (number of marks) to search\n");
    fprintf(stderr, "  -v       verbose output\n  -mp      use OpenMP multithreaded solver\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc < 2) usage(argv[0]);
    int n = atoi(argv[1]);
    if (n < 2 || n > MAX_MARKS) {
        fprintf(stderr, "Marks must be between 2 and %d.\n", MAX_MARKS);
        return EXIT_FAILURE;
    }
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    bool verbose = false;
    bool use_mp = false;

    /* parse optional flags */
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "-mp") == 0) use_mp = true;
        else usage(argv[0]);
    }

    const ruler_t *ref = lut_lookup_by_marks(n);

    ruler_t result;
    bool solved = false;
    bool compared = false;
    bool optimal = false;

    int target_len_start = ref ? ref->length : (n * (n - 1) / 2); /* simple lower bound */

    if (ref && verbose) {
        printf("Reference optimal ruler from LUT:\n");
        print_ruler(ref);
    }

    for (int L = target_len_start; L <= MAX_LEN_BITSET; ++L) {
        bool ok = use_mp ? solve_golomb_mt(n, L, &result, verbose)
                         : solve_golomb   (n, L, &result, verbose);
        if (ok) { solved = true; break; }
    }

    if (ref) {
        compared = true;
        optimal = solved && (result.length == ref->length);
    }

    if (!solved) {
        fprintf(stderr, "Could not find a Golomb ruler with %d marks within length limit.\n", n);
        return EXIT_FAILURE;
    }

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec)/1e9;
    printf("Found ruler: ");
    print_ruler(&result);
    printf("Elapsed time: %.6f s\n", elapsed);

    char fname[32];
    snprintf(fname, sizeof fname, "GOL_n%d.txt", n);
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
        printf("Status: %s\n", optimal ? "Optimal ✅" : "Not optimal ❌");
        return optimal ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        puts("No comparison possible (length missing from LUT).\n");
        return EXIT_SUCCESS;
    }
}
