#define _POSIX_C_SOURCE 200809L
#include "golomb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include "bench.h"

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

static void print_help(const char *prog_name);

static void usage(const char *prog)
{
    (void)prog;
    print_help(prog);
    exit(EXIT_FAILURE);
}

static void print_help(const char *prog_name)
{
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
    printf("  -af                Use FASM assembler (unrolled scalar).\n");
    printf("  -an                Use NASM assembler (AVX2 gather).\n");
    printf("  -t                 Run built-in benchmark suite for given <n>.\n");
    printf("  -o <file>          Write the found ruler to a file.\n");
    printf("  -f <file>          Enable checkpointing (mp solver) and save/resume progress at <file>.\n");
    printf("  -fi <sec>          Checkpoint flush interval in seconds (default 60).\n");
    printf("  --help             Display this help message and exit.\n");
}

static volatile int g_current_L = -1;
static volatile int g_done = 0;
static struct timespec g_ts_start;
static double g_vt_sec = 0.0;

/* Solver dispatch helper to avoid code duplication */
typedef enum { SOLVER_SINGLE, SOLVER_MP, SOLVER_DYN, SOLVER_CREATIVE } solver_type_t;

static bool run_solver(solver_type_t type, int n, int L, ruler_t *result, bool verbose)
{
    switch (type) {
        case SOLVER_CREATIVE: return solve_golomb_creative(n, L, result, verbose);
        case SOLVER_DYN:      return solve_golomb_mt_dyn(n, L, result, verbose);
        case SOLVER_MP:       return solve_golomb_mt(n, L, result, verbose);
        case SOLVER_SINGLE:
        default:              return solve_golomb(n, L, result, verbose);
    }
}

/* Checkpointing globals (declared in golomb.h) */
const char *g_cp_path = NULL;
int g_cp_interval_sec = 60; /* default 60s */


static void *heartbeat_thread(void *arg)
{
    while (!g_done)
    {
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double since = (ts_now.tv_sec - g_ts_start.tv_sec) + (ts_now.tv_nsec - g_ts_start.tv_nsec) / 1e9;
        char tbuf[32];
        format_elapsed(since, tbuf, sizeof tbuf);
        int L = g_current_L;
        if (L >= 0)
            fprintf(stdout, "[VT] %s elapsed – current L=%d\n", tbuf, L);
        fflush(stdout);
        struct timespec req = {(time_t)g_vt_sec, (long)((g_vt_sec - (time_t)g_vt_sec) * 1e9)};
        nanosleep(&req, NULL);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    // Check for --help first
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
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
    char start_iso[32];
    strftime(start_iso, sizeof start_iso, "%F %T", localtime(&t_start_wall));
    printf("Start time: %s\n", start_iso);

    bool verbose = false;
    bool run_tests = false;
    bool use_mp = false;
    bool use_mt_dyn = false;
    double vt_sec = 0.0; /* heartbeat interval in seconds (0 = disabled) */
    pthread_t hb_thread;
    bool use_heuristic_start = false;
    bool use_creative = false;
    bool use_simd = false;     /* -e flag (forces on); default decided later by HW */
    bool use_asm_fasm = false; /* -af flag: FASM unrolled scalar */
    bool use_asm_nasm = false; /* -an flag: NASM AVX2 gather */
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
            if (i + 1 < argc)
            {
                output_file = argv[++i];
            }
            else
            {
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
        else if (strcmp(argv[i], "-vt") == 0)
        {
            if (i + 1 < argc)
            {
                vt_sec = atof(argv[++i]) * 60.0;
                g_vt_sec = vt_sec;
                if (vt_sec < 0.01)
                    vt_sec = 0.0;
            }
            else
            {
                fprintf(stderr, "Error: -vt option requires minutes argument.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "-e") == 0)
        {
            use_simd = true;
        }
        else if (strcmp(argv[i], "-af") == 0)
        {
            use_asm_fasm = true;
        }
        else if (strcmp(argv[i], "-an") == 0)
        {
            use_asm_nasm = true;
        }
        else if (strcmp(argv[i], "-t") == 0)
        {
            run_tests = true;
        }
        else if (strcmp(argv[i], "-f") == 0)
        {
            if (i + 1 < argc)
            {
                g_cp_path = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: -f option requires a filename.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "-fi") == 0)
        {
            if (i + 1 < argc)
            {
                int sec = atoi(argv[++i]);
                if (sec > 0) g_cp_interval_sec = sec; /* <=0 keeps default 60 via solver */
            }
            else
            {
                fprintf(stderr, "Error: -fi option requires seconds.\n");
                return EXIT_FAILURE;
            }
        }
        else
            usage(argv[0]);
    }

    if (run_tests)
    {
        run_benchmarks(argv[0], n);
        return 0;
    }

    const ruler_t *ref = lut_lookup_by_marks(n);

    ruler_t result;
    extern bool g_use_simd;
    extern bool g_use_asm_fasm;
    extern bool g_use_asm_nasm;
/* function pointers to detect which impl is linked (weak) */
extern int test_any_dup8_avx512(const uint64_t *, const int *) __attribute__((weak));
extern int test_any_dup8_avx2_gather(const uint64_t *, const int *) __attribute__((weak));
extern int test_any_dup8_avx2_asm(const uint64_t *, const int *) __attribute__((weak));
extern int test_any_dup8_avx2_nasm(const uint64_t *, const int *) __attribute__((weak));
    /* Default-enable SIMD if compiled with AVX2/AVX512, or when -e is passed */
    g_use_asm_fasm = use_asm_fasm;
    g_use_asm_nasm = use_asm_nasm;
#if defined(__AVX512F__) || defined(__AVX2__)
    g_use_simd = true;
#else
    g_use_simd = false;
#endif
    if (use_simd) g_use_simd = true;
        /* Ensure OpenMP cancellation is enabled unless the user already set it */
        if (!getenv("OMP_CANCELLATION"))
            setenv("OMP_CANCELLATION", "TRUE", 1);
        /* Inform user which distance duplicate implementation will be used
         * (must match dispatch order in solver.c test_any_dup8) */
        const char *dup_impl = "scalar (intrinsic)";
        if (g_use_asm_fasm && test_any_dup8_avx2_asm)
            dup_impl = "FASM (AVX2 gather asm)";
        else if (g_use_asm_nasm && test_any_dup8_avx2_nasm)
            dup_impl = "NASM (AVX2 gather asm)";
        else if (g_use_simd && test_any_dup8_avx512 && getenv("GOLOMB_USE_AVX512"))
            dup_impl = "AVX-512 gather";
        else if (g_use_simd && test_any_dup8_avx2_gather)
            dup_impl = "AVX2 gather (C)";
        else if (g_use_simd)
            dup_impl = "AVX2 intrinsics";
        printf("[Info] Distance duplicate test implementation: %s\n", dup_impl);

    bool solved = false;
    bool compared = false;
    bool optimal = false;

    /* Determine solver type once */
    solver_type_t solver_type = SOLVER_SINGLE;
    if (!force_single_thread) {
        if (use_creative)       solver_type = SOLVER_CREATIVE;
        else if (use_mt_dyn)    solver_type = SOLVER_DYN;
        else if (use_mp)        solver_type = SOLVER_MP;
    }

    int base = n * (n - 1) / 2;
    int target_len_start;
    if (use_heuristic_start) {
        if (ref) {
            target_len_start = ref->length;
        } else {
            if (n > 3) base += (n - 3) / 2;
            target_len_start = base;
        }
    } else {
        target_len_start = base;
    }

    if (ref && verbose)
    {
        printf("Reference optimal ruler from LUT:\n");
        print_ruler(ref);
    }

    g_current_L = target_len_start;
    if (vt_sec > 0.0)
        pthread_create(&hb_thread, NULL, heartbeat_thread, NULL);

    /* Pre-check: if LUT exists and -b is NOT used, try the LUT length once */
    if (ref && !use_heuristic_start)
    {
        if (run_solver(solver_type, n, ref->length, &result, verbose))
            solved = true;
    }

    for (int L = target_len_start; !solved && L <= MAX_LEN_BITSET; ++L)
    {
        if (run_solver(solver_type, n, L, &result, verbose))
        {
            solved = true;
            break;
        }
    }

    if (ref)
    {
        compared = true;
        optimal = solved && (result.length == ref->length);
    }

    g_done = 1;
    if (vt_sec > 0.0)
        pthread_join(hb_thread, NULL);

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
    char end_iso[32];
    strftime(end_iso, sizeof end_iso, "%F %T", localtime(&t_end_wall));
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
    if (force_single_thread)
    {
        strcat(opts, "-s ");
        strcat(fsuffix, "_s");
    }
    else if (use_creative)
    {
        strcat(opts, "-c ");
        strcat(fsuffix, "_c");
    }
    else if (use_mt_dyn)
    {
        strcat(opts, "-d ");
        strcat(fsuffix, "_d");
    }
    else if (use_mp)
    {
        strcat(opts, "-mp ");
        strcat(fsuffix, "_mp");
    }

    // Additive flags
    if (use_heuristic_start)
    {
        strcat(opts, "-b ");
        strcat(fsuffix, "_b");
    }
    if (use_simd)
    {
        strcat(opts, "-e ");
        strcat(fsuffix, "_e");
    }
    if (use_asm_fasm)
    {
        strcat(opts, "-af ");
        strcat(fsuffix, "_af");
    }
    if (use_asm_nasm)
    {
        strcat(opts, "-an ");
        strcat(fsuffix, "_an");
    }
    if (verbose)
    {
        strcat(opts, "-v ");
        strcat(fsuffix, "_v");
    }

    size_t optlen = strlen(opts);
    if (optlen > 0 && opts[optlen - 1] == ' ')
    {
        opts[--optlen] = '\0';
    }

    char fname[128];
    if (output_file != NULL)
    {
        strncpy(fname, output_file, sizeof(fname) - 1);
        fname[sizeof(fname) - 1] = '\0';
    }
    else
    {
        if (mkdir("out", 0755) == -1 && errno != EEXIST)
        {
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
