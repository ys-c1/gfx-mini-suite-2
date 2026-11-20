#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;
int sat_overlap(const SatPoly* a, const SatPoly* b);
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);

/* Status of a circle/polygon pair (closed sets):
 *   SAT_CIRCLE_OUTSIDE       the closed shapes are disjoint.  External
 *                            tangency (distance == r) counts as OUTSIDE,
 *                            overlap area 0.
 *   SAT_CIRCLE_INTERSECTING  the closed shapes overlap but neither contains
 *                            the other: boundaries cross, or one lies inside
 *                            the other and touches its boundary (internal
 *                            tangency / boundary through the centre).
 *   SAT_CIRCLE_CONTAINED     one closed shape contains the other.  The
 *                            `contained` field says which:
 *                              +1  closed disk is a subset of the polygon
 *                              -1  polygon is a subset of the closed disk
 *                            (containment is inclusive of tangency).
 *   SAT_CIRCLE_POLY_EMPTY    polygon vertex count < 3: no interior exists.
 *   SAT_CIRCLE_POLY_DEGENERATE  vertices are collinear or coincident: no
 *                            interior exists.
 *   SAT_CIRCLE_EINVAL        NULL output, non-finite input, negative radius,
 *                            or |coordinate| > 1e15 (overflow guard).
 *
 * Radius 0 is valid: the "disk" is a point.  Overlap area is always 0 and the
 * status is CONTAINED if the point lies in the closed polygon (including its
 * boundary), OUTSIDE otherwise.
 *
 * sat_circle_polygon_area() returns 0.0 for EINVAL, so it always has a defined
 * result; call sat_circle_polygon() to see the status behind it.
 *
 * Numerical contract: all area terms are evaluated in double.  Polygon edge
 * pieces use the exact closed form (1/2)*cross(u,v); circle arcs use the
 * exact sector form (1/2)*r^2*dtheta.  The only transcendental steps are
 * sqrt() for the edge/disk clip and atan2() for the arc angles (a few ulp
 * each), so the result is analytic, never sampled.  For the shipped fixtures
 * the absolute error against an independent scipy.quad reference is < 1e-11;
 * the general relative error is O(eps) and stays below 1e-9 for coordinates
 * up to ~1e6 and areas up to ~1e12.  Extreme offset (centre far outside a
 * tiny polygon) degrades gracefully under plain double summation. */
enum {
    SAT_CIRCLE_EINVAL = -1,
    SAT_CIRCLE_POLY_EMPTY = 0,
    SAT_CIRCLE_POLY_DEGENERATE = 1,
    SAT_CIRCLE_OUTSIDE = 2,
    SAT_CIRCLE_INTERSECTING = 3,
    SAT_CIRCLE_CONTAINED = 4
};

/* Analytic total overlap area (polygon \cap closed disk). */
double sat_circle_polygon_area(const SatPoly* p, double cx, double cy, double r);

/* Full result: status, analytic area, signed polygon area, containment kind. */
typedef struct {
    int status;        /* SAT_CIRCLE_* */
    int contained;     /* 0 none, +1 polygon contains disk, -1 disk contains polygon */
    double area;       /* analytic overlap area */
    double poly_area;  /* signed area of p (units^2) */
} SatCirclePoly;

int sat_circle_polygon(const SatPoly* p, double cx, double cy, double r,
                       SatCirclePoly* out);
#endif
