#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

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
static int cmp_int(const void *a, const void *b) { return (*(const int *)a) - (*(const int *)b); }

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <marks> [-v] [-mp] [-d] [-b] [-e]\n", prog);
    fprintf(stderr, "  <marks>  order (number of marks) to search\n");
    fprintf(stderr, "  -v       verbose output\n  -mp      multithreaded solver (static split)\n  -d       dynamic OpenMP task solver\n  -b       better lower-bound start length\n  -e       SIMD-optimised bitset (experimental)\n");
    exit(EXIT_FAILURE);
}

static void print_help(const char *prog_name) {
    printf("Usage: %s <n> [options]\n\n", prog_name);
    printf("Finds an optimal Golomb ruler with <n> marks.\n\n");
    printf("Options:\n");
    printf("  -v, --verbose      Enable verbose output during search.\n");
    printf("  -s, --single       Force single-threaded solver.\n");
    printf("  -mp                Use multi-threaded solver with static work division (default).\n");
    printf("  -d                 Use multi-threaded solver with dynamic OpenMP tasks.\n");
    printf("  -c                 Use 'creative' multi-threaded solver with dynamic scheduling.\n");
    printf("  -b                 Use best-known ruler length as a starting point heuristic.\n");
    printf("  -e                 Enable SIMD (AVX2) optimizations where available.\n");
    printf("  -o <file>          Write the found ruler to a file.\n");
    printf("  --help             Display this help message and exit.\n");
}

static volatile int g_current_L = -1;
static volatile int g_done = 0;
static struct timespec g_ts_start;
static double g_vt_sec = 0.0;

static void *heartbeat_thread(void *arg) {
    while (!g_done) {
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double since = (ts_now.tv_sec - g_ts_start.tv_sec) + (ts_now.tv_nsec - g_ts_start.tv_nsec)/1e9;
        char tbuf[32];
        format_elapsed(since, tbuf, sizeof tbuf);
        int L = g_current_L;
        if (L >= 0)
            fprintf(stdout, "[VT] %s elapsed – current L=%d\n", tbuf, L);
        fflush(stdout);
        struct timespec req = { (time_t)g_vt_sec, (long)((g_vt_sec - (time_t)g_vt_sec)*1e9) };
        nanosleep(&req, NULL);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    // Check for --help first
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    if (argc < 2)
        usage(argv[0]);
    int n = atoi(argv[1]);
    if (n < 2 || n > MAX_MARKS)
    {
        fprintf(stderr, "Marks must be between 2 and %d.\n", MAX_MARKS);
        return EXIT_FAILURE;
    }
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    g_ts_start = ts_start;
    time_t t_start_wall = time(NULL);
    char start_iso[32]; strftime(start_iso, sizeof start_iso, "%F %T", localtime(&t_start_wall));
    printf("Start time: %s\n", start_iso);

    bool verbose = false;
    bool use_mp = false;
    bool use_mt_dyn = false;
    double vt_sec = 0.0;          /* heartbeat interval in seconds (0 = disabled) */
    pthread_t hb_thread;
    bool use_heuristic_start = false;
    bool use_creative = false;
    bool use_simd = false; /* -e flag */
    char *output_file = NULL;
    bool force_single_thread = false;
    /* parse optional flags */
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "-v") == 0)
        {
            verbose = true;
        }
        else if (strcmp(argv[i], "-s") == 0)
        {
            force_single_thread = true;
        }
        else if (strcmp(argv[i], "-o") == 0)
        {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -o option requires a filename.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "-mp") == 0)
        {
            use_mp = true;
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            use_mt_dyn = true;
            use_mp = false;
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            use_heuristic_start = true;
        }
        else if (strcmp(argv[i], "-c") == 0)
        {
            use_creative = true;
        }
        else if (strcmp(argv[i], "-vt") == 0) {
            if (i + 1 < argc) {
                vt_sec = atof(argv[++i]) * 60.0;
                g_vt_sec = vt_sec;
                if (vt_sec < 0.01) vt_sec = 0.0;
            } else {
                fprintf(stderr, "Error: -vt option requires minutes argument.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "-e") == 0)
        {
            use_simd = true;
        }
        else
            usage(argv[0]);
    }

    const ruler_t *ref = lut_lookup_by_marks(n);

    ruler_t result;
    extern bool g_use_simd;
    g_use_simd = use_simd;

    bool solved = false;
    bool compared = false;
    bool optimal = false;

    int target_len_start;
    if (ref)
    {
        target_len_start = ref->length;
    }
    else
    {
        int base = n * (n - 1) / 2;
        if (use_heuristic_start && n > 3)
            base += (n - 3) / 2; /* simple Hasse-style improvement */
        target_len_start = base;
    }

    if (ref && verbose)
    {
        printf("Reference optimal ruler from LUT:\n");
        print_ruler(ref);
    }

    g_current_L = target_len_start;
    if (vt_sec > 0.0) pthread_create(&hb_thread, NULL, heartbeat_thread, NULL);
    for (int L = target_len_start; L <= MAX_LEN_BITSET; ++L)
    {
        bool ok;
        if (force_single_thread)
            ok = solve_golomb(n, L, &result, verbose);
        else if (use_creative)
            ok = solve_golomb_creative(n, L, &result, verbose);
        else if (use_mt_dyn)
            ok = solve_golomb_mt_dyn(n, L, &result, verbose);
        else if (use_mp)
            ok = solve_golomb_mt(n, L, &result, verbose);
        else
            ok = solve_golomb(n, L, &result, verbose);

        if (ok)
        {
            solved = true;
            if (use_heuristic_start || ref) {
                break;
            }
            target_len_start = result.length - 1;
        }
        if (ref && L >= result.length) {
            break;
        }
    }

    if (ref)
    {
        compared = true;
        optimal = solved && (result.length == ref->length);
    }

    g_done = 1;
    if (vt_sec > 0.0) pthread_join(hb_thread, NULL);

    if (!solved)
    {
        fprintf(stderr, "Could not find a Golomb ruler with %d marks within length limit.\n", n);
        return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
    char tbuf[32];
    format_elapsed(elapsed, tbuf, sizeof tbuf);
    time_t t_end_wall = time(NULL);
    char end_iso[32]; strftime(end_iso, sizeof end_iso, "%F %T", localtime(&t_end_wall));
    printf("End time:   %s\n", end_iso);

    printf("Found ruler: ");
    print_ruler(&result);
    printf("Elapsed time: %s\n", tbuf);

    /* compute all pairwise distances */
    int dist[(MAX_MARKS * (MAX_MARKS - 1)) / 2];
    int dcnt = 0;
    for (int i = 0; i < result.marks; ++i)
        for (int j = i + 1; j < result.marks; ++j)
            dist[dcnt++] = result.pos[j] - result.pos[i];
    qsort(dist, dcnt, sizeof(int), cmp_int);

    /* Determine missing distances */
    int miss[MAX_LEN_BITSET];
    int mcnt = 0;
    char present[MAX_LEN_BITSET + 1] = {0};
    for (int i = 0; i < dcnt; ++i)
        present[dist[i]] = 1;
    for (int d = 1; d <= result.length; ++d)
    {
        if (!present[d])
            miss[mcnt++] = d;
    }

    printf("Distances (%d): ", dcnt);
    for (int i = 0; i < dcnt; ++i)
        printf("%d%s", dist[i], (i == dcnt - 1) ? "" : " ");
    printf("\nMissing (%d): ", mcnt);
    for (int i = 0; i < mcnt; ++i)
        printf("%d%s", miss[i], (i == mcnt - 1) ? "" : " ");
    printf("\n");

    /* build option string and filename suffix from flags */
    char opts[64] = "";
    char fsuffix[64] = "";

    // Solver type flags (mutually exclusive, reflects solver choice)
    if (force_single_thread) {
        strcat(opts, "-s ");
        strcat(fsuffix, "_s");
    } else if (use_creative) {
        strcat(opts, "-c ");
        strcat(fsuffix, "_c");
    } else if (use_mt_dyn) {
        strcat(opts, "-d ");
        strcat(fsuffix, "_d");
    } else if (use_mp) {
        strcat(opts, "-mp ");
        strcat(fsuffix, "_mp");
    }

    // Additive flags
    if (use_heuristic_start) {
        strcat(opts, "-b ");
        strcat(fsuffix, "_b");
    }
    if (use_simd) {
        strcat(opts, "-e ");
        strcat(fsuffix, "_e");
    }
    if (verbose) {
        strcat(opts, "-v ");
        strcat(fsuffix, "_v");
    }

    size_t optlen = strlen(opts);
    if (optlen > 0 && opts[optlen - 1] == ' ') {
        opts[--optlen] = '\0';
    }

    char fname[128];
    if (output_file != NULL) {
        strncpy(fname, output_file, sizeof(fname) - 1);
        fname[sizeof(fname) - 1] = '\0';
    } else {
        if (mkdir("out", 0755) == -1 && errno != EEXIST) {
            perror("mkdir out");
        }
        snprintf(fname, sizeof fname, "out/GOL_n%d%s.txt", n, fsuffix);
    }
    FILE *fp = fopen(fname, "w");
    if (fp)
    {
        fprintf(fp, "length=%d\nmarks=%d\npositions=", result.length, result.marks);
        for (int i = 0; i < result.marks; ++i)
        {
            fprintf(fp, "%d%s", result.pos[i], (i == result.marks - 1) ? "" : " ");
        }
        fprintf(fp, "\ndistances=");
        for (int i = 0; i < dcnt; ++i)
            fprintf(fp, "%d%s", dist[i], (i == dcnt - 1) ? "" : " ");
        fprintf(fp, "\nmissing=");
        for (int i = 0; i < mcnt; ++i)
            fprintf(fp, "%d%s", miss[i], (i == mcnt - 1) ? "" : " ");
        fprintf(fp, "\nseconds=%.6f\ntime=%s\noptions=%s\n", elapsed, tbuf, optlen ? opts : "none");
        if (compared)
            fprintf(fp, "optimal=%s\n", optimal ? "yes" : "no");
        fclose(fp);
    }
    else
    {
        perror("fopen");
    }
    if (compared)
    {
        printf("Status: %s\n", optimal ? "Optimal ✅" : "Not optimal ❌");
        return optimal ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else
    {
        puts("No comparison possible (length missing from LUT).\n");
        return EXIT_SUCCESS;
    }
}
