#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

/* Format seconds into h:mm:ss.mmm or mm:ss.mmm or s.mmm */
static void format_elapsed(double sec, char *out, size_t len)
{
    int hours = (int)(sec / 3600);
    int minutes = (int)((sec - hours * 3600) / 60);
    double seconds = sec - hours * 3600 - minutes * 60;
    if (hours > 0)
        snprintf(out, len, "%d:%02d:%06.3f", hours, minutes, seconds);
    else if (minutes > 0)
        snprintf(out, len, "%02d:%06.3f", minutes, seconds);
    else
        snprintf(out, len, "%.3f s", seconds);
}

/* qsort helper */
static int cmp_int(const void *a, const void *b) { return (*(const int*)a) - (*(const int*)b); }

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
    char tbuf[32];
    format_elapsed(elapsed, tbuf, sizeof tbuf);
    printf("Found ruler: ");
    print_ruler(&result);
    printf("Elapsed time: %s\n", tbuf);

    /* compute all pairwise distances */
    int dist[(MAX_MARKS*(MAX_MARKS-1))/2];
    int dcnt = 0;
    for (int i = 0; i < result.marks; ++i)
        for (int j = i + 1; j < result.marks; ++j)
            dist[dcnt++] = result.pos[j] - result.pos[i];
    qsort(dist, dcnt, sizeof(int), cmp_int);

    /* Determine missing distances */
    int miss[MAX_LEN_BITSET];
    int mcnt = 0;
    char present[MAX_LEN_BITSET+1] = {0};
    for (int i = 0; i < dcnt; ++i) present[dist[i]] = 1;
    for (int d = 1; d <= result.length; ++d) {
        if (!present[d]) miss[mcnt++] = d;
    }

    printf("Distances (%d): ", dcnt);
    for (int i = 0; i < dcnt; ++i) printf("%d%s", dist[i], (i==dcnt-1)?"":" ");
    printf("\nMissing (%d): ", mcnt);
    for (int i = 0; i < mcnt; ++i) printf("%d%s", miss[i], (i==mcnt-1)?"":" ");
    printf("\n");

    /* build option string */
    char opts[16] = "";
    if (verbose) strcat(opts, "-v ");
    if (use_mp) strcat(opts, "-mp ");
    size_t optlen = strlen(opts);
    if (optlen && opts[optlen-1] == ' ') opts[--optlen] = '\0';

    /* ensure output directory exists */
    if (mkdir("out", 0755) == -1 && errno != EEXIST) {
        perror("mkdir out");
    }
    char fname[64];
    snprintf(fname, sizeof fname, "out/GOL_n%d.txt", n);
    FILE *fp = fopen(fname, "w");
    if (fp) {
        fprintf(fp, "length=%d\nmarks=%d\npositions=", result.length, result.marks);
        for (int i = 0; i < result.marks; ++i) {
            fprintf(fp, "%d%s", result.pos[i], (i == result.marks-1) ? "" : " ");
        }
        fprintf(fp, "\ndistances=");
        for (int i = 0; i < dcnt; ++i) fprintf(fp, "%d%s", dist[i], (i==dcnt-1)?"":" ");
        fprintf(fp, "\nmissing=");
        for (int i = 0; i < mcnt; ++i) fprintf(fp, "%d%s", miss[i], (i==mcnt-1)?"":" ");
        fprintf(fp, "\nseconds=%.6f\ntime=%s\noptions=%s\n", elapsed, tbuf, optlen ? opts : "none");
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
