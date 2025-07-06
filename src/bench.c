#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/*
 * run_benchmarks – executes a fixed set of flag variants for a given order `n`.
 * Results are printed to stdout and written to "out/bench_n<N>.txt".
 */
void run_benchmarks(const char *prog, int n)
{
    const char *variants[] = {
        "-mp",
        "-mp -b",
        "-mp -e",
        "-mp -a",
        "-mp -e -a",
        "-mp -b -a",
        "-d",
        "-d -e",
        "-d -a",
        "-c",
        NULL
    };

    /* ensure output directory exists */
    if (mkdir("out", 0755) == -1 && errno != EEXIST) {
        perror("mkdir out");
    }

    char outfname[128];
    snprintf(outfname, sizeof outfname, "out/bench_n%d.txt", n);
    FILE *fp = fopen(outfname, "w");
    if (!fp)
        perror("fopen bench file");

    printf("\nRunning benchmark suite for n=%d\n", n);
    if (fp)
        fprintf(fp, "Flags\tSeconds\n");

    for (int i = 0; variants[i]; ++i) {
        const char *v = variants[i];
        printf("\n>>> Running %s ...\n", v);
        fflush(stdout);
        char cmd[256];
        snprintf(cmd, sizeof cmd, "%s %d %s", prog, n, v);

        struct timespec ts1, ts2;
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        int ret = system(cmd);
        (void)ret;
        clock_gettime(CLOCK_MONOTONIC, &ts2);

        double sec = (ts2.tv_sec - ts1.tv_sec) +
                     (ts2.tv_nsec - ts1.tv_nsec) / 1e9;

        printf("%15s : %.3f s\n", v, sec);
        if (fp)
            fprintf(fp, "%s\t%.3f\n", v, sec);
    }
    if (fp)
        fclose(fp);
    printf("Results written to %s\n", outfname);
}
