#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;

/* ---- sat_overlap: boolean test, semantics unchanged ------------------- */
/* 1 = overlapping (touching counts as overlapping), 0 = disjoint,
 * -1 = invalid input (NULL or fewer than three vertices). */
int sat_overlap(const SatPoly* a, const SatPoly* b);
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);

/* ---- sat_overlap_mtv: overlap test plus minimum translation vector --- */
typedef enum {
    SAT_MTV_OVERLAP = 0,   /* overlapping convex polygons; dir/dist valid   */
    SAT_MTV_DISJOINT = 1,  /* polygons are separated; no MTV exists         */
    SAT_MTV_DEGENERATE = 2,/* an input has n < 3 or zero area; no MTV exists */
    SAT_MTV_NONCONVEX = 3, /* an input is not convex; MTV not defined        */
} SatMtvStatus;

typedef struct {
    SatMtvStatus status;
    /* Unit separation direction such that moving B (the second argument)
     * by dist*(dirx, diry) makes the pair touch and moving it any further
     * separates them. This is the minimum-translation direction, not
     * necessarily the shortest displacement to a disjoint state (which is
     * dist plus an infinitesimal). Unspecified unless status ==
     * SAT_MTV_OVERLAP. */
    float dirx, diry;
    /* Minimum distance to separate the pair along (dirx, diry), i.e. the
     * smallest t >= 0 such that translating B by t*(dirx, diry) leaves the
     * pair touching. Zero when the inputs only touch already. */
    float dist;
} SatMtv;

/* Computes an MTV for overlapping convex polygons. Every edge normal of A
 * and of B is a candidate axis; for each one the 1-D intervals are
 * projected and the smaller of the two escapes is kept (push B by
 * a_hi - b_lo, or by b_hi - a_lo). The global result is the smallest such
 * distance over all candidates. For convex polygons this is the exact
 * minimum translation distance.
 *
 * Equal distances are resolved deterministically by candidate order:
 *   1. a candidate axis from A beats one from B,
 *   2. the earlier edge index beats the later one,
 *   3. per axis, the +normal escape beats the -normal escape.
 * The +normal/-normal choice only matters when the two escapes are exactly
 * equal; it never changes dist.
 *
 * Touching polygons (distance zero) report SAT_MTV_OVERLAP with dist 0,
 * matching sat_overlap's contact rule.
 *
 * The edge-normal construction is exact only for CONVEX polygons, so a
 * non-convex input that the boolean calls overlapping is rejected with
 * SAT_MTV_NONCONVEX rather than being given an MTV that may not actually
 * separate. A non-convex pair the boolean calls disjoint still reports
 * SAT_MTV_DISJOINT, since a separating axis is a valid proof of
 * disjointness regardless of convexity. (The boolean sat_overlap keeps
 * its historical behaviour for concave polygons.)
 *
 * Degenerate input (n < 3, non-finite coordinates, or zero signed area
 * relative to the bounding box) returns SAT_MTV_DEGENERATE rather than a
 * plausible-looking vector. */
SatMtv sat_overlap_mtv(const SatPoly* a, const SatPoly* b);
#endif
