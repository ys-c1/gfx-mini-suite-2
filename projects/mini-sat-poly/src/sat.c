#include "sat.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* Original boolean overlap test.  Implementation deliberately kept     */
/* byte-for-byte identical: every sample, including exact contact,      */
/* must keep its previous answer.                                       */
/* ------------------------------------------------------------------ */

void sat_centroid(const SatPoly* a, float* cx, float* cy) {
    float sx = 0, sy = 0;
    for (int i = 0; i < a->n; ++i) { sx += a->p[i].x; sy += a->p[i].y; }
    *cx = sx / (float)a->n; *cy = sy / (float)a->n;
}

static void project(const SatPoly* a, float ax, float ay, float* lo, float* hi) {
    float mn = 1e30f, mx = -1e30f;
    for (int i = 0; i < a->n; ++i) {
        float d = a->p[i].x * ax + a->p[i].y * ay;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }
    *lo = mn; *hi = mx;
}

static int axes_overlap(const SatPoly* a, const SatPoly* b) {
    for (int e = 0; e < a->n; ++e) {
        int i2 = (e + 1) % a->n;
        float ex = a->p[i2].x - a->p[e].x, ey = a->p[i2].y - a->p[e].y;
        float ax = -ey, ay = ex;
        float len = sqrtf(ax * ax + ay * ay);
        if (len < 1e-9f) continue;
        ax /= len; ay /= len;
        float alo, ahi, blo, bhi;
        project(a, ax, ay, &alo, &ahi);
        project(b, ax, ay, &blo, &bhi);
        if (ahi < blo || bhi < alo) return 0;
    }
    return 1;
}

int sat_overlap(const SatPoly* a, const SatPoly* b) {
    if (!a || !b || a->n < 3 || b->n < 3) return -1;
    return axes_overlap(a, b) && axes_overlap(b, a);
}

int sat_point_inside(const SatPoly* a, float x, float y) {
    int inside = 0;
    for (int i = 0, j = a->n - 1; i < a->n; j = i++) {
        float xi = a->p[i].x, yi = a->p[i].y, xj = a->p[j].x, yj = a->p[j].y;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return inside;
}

/* ------------------------------------------------------------------ */
/* Minimum translation vector.                                         */
/*                                                                     */
/* For every edge normal of A, then of B, project both polygons and    */
/* compute the smallest 1-D translation of B that clears A:            */
/*     t_plus  = a_hi - b_lo   (push B along +axis)                    */
/*     t_minus = b_hi - a_lo   (push B along -axis)                    */
/* The per-axis distance is min(t_plus, t_minus); the global MTV is    */
/* the minimum over axes.  For convex polygons this is the exact       */
/* minimum translation distance (the Minkowski difference's boundary   */
/* nearest the origin always lies on a face parallel to an edge of A   */
/* or B).  Note that the distance is not the interval intersection     */
/* length: for a contained interval the correct value is the distance  */
/* to the nearer end of the containing interval.                       */
/* ------------------------------------------------------------------ */

/* Upper bound on vertices is fixed by SatPoly, but don't hard-code it. */
#define SAT_AXIS_EPS2 1e-18f /* same threshold as the boolean: len < 1e-9 */

typedef struct {
    float dist;
    float dirx, diry;
    int   src;   /* 0 = edge of A, 1 = edge of B */
    int   idx;
    int   valid;
} SatCand;

static int poly_finite(const SatPoly* p) {
    for (int i = 0; i < p->n; ++i) {
        if (!isfinite(p->p[i].x) || !isfinite(p->p[i].y)) return 0;
    }
    return 1;
}

/* Twice the absolute signed area, accumulated in double so that exact
 * collinearity of float coordinates is not mistaken for real area. */
static double poly_area2(const SatPoly* p) {
    double s = 0.0;
    for (int i = 0; i < p->n; ++i) {
        int j = (i + 1) % p->n;
        s += (double)p->p[i].x * (double)p->p[j].y
           - (double)p->p[j].x * (double)p->p[i].y;
    }
    return fabs(s);
}

/* Zero area relative to the bounding box.  A point/line has bbox area 0.
 * The 1e-12 factor is far above double rounding of float inputs and far
 * below any physically meaningful sliver. */
static int poly_zero_area(const SatPoly* p) {
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    for (int i = 0; i < p->n; ++i) {
        if (p->p[i].x < mnx) mnx = p->p[i].x;
        if (p->p[i].x > mxx) mxx = p->p[i].x;
        if (p->p[i].y < mny) mny = p->p[i].y;
        if (p->p[i].y > mxy) mxy = p->p[i].y;
    }
    double dx = mxx - mnx, dy = mxy - mny;
    return poly_area2(p) <= 1e-12 * (dx * dx + dy * dy);
}

/* Convexity: the signed cross product of consecutive edges must have one
 * consistent sign (zero-area inputs are rejected before this runs, so no
 * edge is degenerate).  Tolerance is relative to the edge lengths so that
 * a slightly non-convex draw still counts as non-convex, while convex
 * input with rounded float coordinates is not rejected. */
static int poly_convex(const SatPoly* p) {
    int sign = 0;
    for (int i = 0; i < p->n; ++i) {
        int j = (i + 1) % p->n, k = (i + 2) % p->n;
        double e1x = (double)p->p[j].x - p->p[i].x;
        double e1y = (double)p->p[j].y - p->p[i].y;
        double e2x = (double)p->p[k].x - p->p[j].x;
        double e2y = (double)p->p[k].y - p->p[j].y;
        double c = e1x * e2y - e1y * e2x;
        double scale = sqrt((e1x * e1x + e1y * e1y) * (e2x * e2x + e2y * e2y));
        if (fabs(c) <= 1e-9 * scale) continue; /* collinear vertex */
        int s = c > 0.0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return 0;
    }
    return 1;
}

/* Candidates are ordered: every edge of A (index ascending), then every
 * edge of B (index ascending).  Because the boolean predicate is passed
 * before this is called, every candidate interval intersects and both
 * escape distances are >= 0; the minimum is the exact MTV for convex
 * polygons. */
static void consider_axis(const SatPoly* a, const SatPoly* b,
                          float ex, float ey, int src, int idx,
                          SatCand* best) {
    float len2 = ex * ex + ey * ey;
    if (len2 < SAT_AXIS_EPS2) return;
    float inv = 1.0f / sqrtf(len2);
    float dx = -ey * inv, dy = ex * inv;

    /* Canonical orientation of the axis itself.  This only decides which
     * of two *equal* escapes is reported; it does not change dist. */
    if (dy < 0.0f || (dy == 0.0f && dx < 0.0f)) { dx = -dx; dy = -dy; }

    float alo, ahi, blo, bhi;
    project(a, dx, dy, &alo, &ahi);
    project(b, dx, dy, &blo, &bhi);

    float tp = ahi - blo; /* push B along +d */
    float tm = bhi - alo; /* push B along -d */
    float t, sx, sy;
    if (tp <= tm) {       /* equal: the canonical +d escape is used */
        t = tp; sx = dx; sy = dy;
    } else {
        t = tm; sx = -dx; sy = -dy;
    }

    if (t < 0.0f) t = 0.0f; /* guard; unreachable once overlap holds */

    if (!best->valid || t < best->dist ||
        (t == best->dist &&
         (src < best->src || (src == best->src && idx < best->idx)))) {
        best->valid = 1;
        best->dist = t;
        best->dirx = sx;
        best->diry = sy;
        best->src = src;
        best->idx = idx;
    }
}

SatMtv sat_overlap_mtv(const SatPoly* a, const SatPoly* b) {
    SatMtv r;
    r.status = SAT_MTV_DEGENERATE;
    r.dirx = 0.0f; r.diry = 0.0f; r.dist = 0.0f;

    if (!a || !b || a->n < 3 || b->n < 3) return r;
    if (!poly_finite(a) || !poly_finite(b)) return r;
    if (poly_zero_area(a) || poly_zero_area(b)) return r;

    /* Keep the status consistent with the (unchanged) boolean predicate:
     * a separated pair has no MTV, and exact contact counts as overlap
     * with distance zero.  A disjoint answer is always trustworthy, even
     * for concave input: if some axis separates the projections, the
     * polygons really are disjoint. */
    if (sat_overlap(a, b) != 1) { r.status = SAT_MTV_DISJOINT; return r; }

    /* The edge-normal MTV is exact only for convex polygons.  sat_overlap
     * can report a false overlap for concave input, so a concave pair that
     * reaches this point has no trustworthy separating vector.  Refuse
     * rather than return a number that may not separate. */
    if (!poly_convex(a) || !poly_convex(b)) {
        r.status = SAT_MTV_NONCONVEX;
        return r;
    }

    SatCand best;
    best.valid = 0; best.dist = 0.0f; best.dirx = 0.0f; best.diry = 0.0f;
    best.src = 0; best.idx = 0;

    for (int e = 0; e < a->n; ++e) {
        int e2 = (e + 1) % a->n;
        consider_axis(a, b, a->p[e2].x - a->p[e].x, a->p[e2].y - a->p[e].y,
                      0, e, &best);
    }
    for (int e = 0; e < b->n; ++e) {
        int e2 = (e + 1) % b->n;
        consider_axis(a, b, b->p[e2].x - b->p[e].x, b->p[e2].y - b->p[e].y,
                      1, e, &best);
    }

    if (!best.valid) return r; /* every edge was zero-length */

    r.status = SAT_MTV_OVERLAP;
    r.dirx = best.dirx;
    r.diry = best.diry;
    r.dist = best.dist;
    return r;
}
