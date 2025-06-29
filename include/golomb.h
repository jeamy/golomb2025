#ifndef GOLOMB_H
#define GOLOMB_H

#include <stdio.h>
#include <stdbool.h>

#define MAX_MARKS 32   /* maximal unterstützte Markierungen */

/* Representation of a Golomb ruler */
typedef struct {
    int length;                 /* last mark position (ruler length) */
    int marks;                  /* number of marks */
    int pos[MAX_MARKS];         /* ascending mark positions (pos[0] == 0) */
} ruler_t;

/*--------- LUT API (lut.c) ----------------------------------------------*/

/* Returns pointer to optimal ruler by length or NULL if unknown */
const ruler_t *lut_lookup_by_length(int length);

/* Prints a ruler to stdout */
void print_ruler(const ruler_t *r);

/*--------- Solver API (solver.c) ----------------------------------------*/

/*
 * Attempts to find an optimal ruler of given mark count n and maximum length L.
 * On success, writes result into out and returns true; else returns false.
 */
bool solve_golomb(int n, int target_length, ruler_t *out, bool verbose);

#endif /* GOLOMB_H */
