#ifndef EXACT_REF_H
#define EXACT_REF_H
/* Independent exact reference for the minimum translation vector between
 * two CONVEX polygons, computed by per-vertex brute force:
 *
 *   1. form the Minkowski difference K = A - B as every pairwise vertex
 *      difference  a_i - b_j   (up to 16*16 points);
 *   2. take the convex hull of those points (Andrew monotone chain);
 *   3. the pair overlaps exactly when the origin lies in or on the hull;
 *   4. the minimum separation distance is the distance from the origin to
 *      the hull boundary; the direction is the unit vector to the closest
 *      boundary point.
 *
 * This shares no code path with sat.c: no edge normals, no SAT, no
 * projections.  It is O(n*m log(n*m)) and exact up to double rounding.
 */
#include "../src/sat.h"

typedef struct {
    int   overlaps;      /* origin inside/on the Minkowski difference hull */
    int   dist_defined;  /* 0 when the origin is exactly on the boundary    */
    double dist;         /* minimum separation distance                     */
    double dirx, diry;   /* unit direction of the closest boundary point    */
    /* Number of distinct optimal boundary directions found (within 1e-9).
     * >1 means the optimum is not unique, so an angle comparison against
     * a single reference direction is not meaningful. */
    int   n_opt_dirs;
    /* Degrees of each distinct optimal direction, sorted, for reporting. */
    double opt_deg[16];
    int   n_opt_deg;
} ExactMtv;

ExactMtv exact_mtv(const SatPoly* a, const SatPoly* b);
#endif
