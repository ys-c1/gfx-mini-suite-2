#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;

/*
 * Extended overlap query: same verdict as sat_overlap, but also reports the
 * axis that decided the query and the depth measured along it.
 *
 * Return value (identical to sat_overlap):
 *   1  polygons overlap
 *   0  polygons are separated
 *  -1  degenerate input (NULL or fewer than 3 vertices)
 *
 * On return 0, *axis is the first separating axis found (unit length,
 * oriented from a toward b) and *depth is the positive gap between the
 * projections along it. A separating axis proves disjointness for ANY
 * polygon, convex or concave, so this verdict is exact for concave input.
 *
 * On return 1, *axis is the minimum-overlap axis (unit length, oriented
 * from a toward b) and *depth is the overlap along it, so translating b by
 * axis*depth is a minimal separating translation. For concave input the
 * overlap verdict is conservative (exact for convex polygons only), as
 * before.
 *
 * On return -1, *axis and *depth are left untouched.
 *
 * The verdict never depends on the order in which axes are visited; which
 * axis is reported on return 0 does (first one found). axis/depth may be
 * NULL if the caller does not need them.
 */
int sat_overlap_axis(const SatPoly* a, const SatPoly* b, SatVec* axis, float* depth);

int sat_overlap(const SatPoly* a, const SatPoly* b);
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);
#endif
