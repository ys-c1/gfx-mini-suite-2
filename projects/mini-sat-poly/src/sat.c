#include "sat.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/* =====================================================================
 * Circle vs simple polygon: analytic overlap area.
 *
 * Method (Green's theorem on the boundary of U = polygon cap disk):
 *
 *     area = 1/2 * closed_integral ( x dy - y dx )   over dU,
 *
 * evaluated with the circle centre moved to the origin.  dU consists of
 *   (a) the pieces of polygon edges that lie inside the disk, and
 *   (b) the arcs of the circle that lie inside the polygon.
 * Both are traversed in the polygon's own orientation, so the signed sums
 * add directly.  On a segment u -> v the integral is exactly
 * (1/2) cross(u, v).  On an arc of angle dtheta it is exactly
 * (1/2) r^2 dtheta.  No sampling and no look-up tables are involved.
 *
 * How the intersection is split into pieces (the "segment" question):
 *   1. Each polygon edge is clipped against the disk.  Solving
 *      |a + t d|^2 = r^2 gives the parameter interval [t0, t1] inside the
 *      disk (empty / one piece / no crossing).  Every surviving piece is
 *      either a full edge, a piece from an endpoint to a crossing, or the
 *      middle piece between two crossings -- the three cases the area sum
 *      handles separately, with a stable quadratic root formula so that
 *      near-tangent edges do not lose precision.
 *   2. The crossing points are converted to angles around the centre and
 *      sorted.  Consecutive angles delimit arcs of the circle on which the
 *      disk lies entirely inside or entirely outside the polygon (the
 *      polygon boundary only crosses the circle at those angles).  One
 *      point-in-polygon test on the arc midpoint classifies each arc.
 *   3. The total is sum(edge pieces) + sign * sum(inside arcs).  The sign
 *      comes from the polygon winding, so clockwise polygons give the same
 *      positive area.
 *   Duplicate crossing angles are kept: a circle through a polygon vertex
 *   reports that angle from both adjacent edges, and the zero-length arc
 *   between the copies contributes exactly 0.  Merging them with a fixed
 *   angular tolerance would silently drop genuinely short arcs.
 *
 * Classification is decided analytically, independently of the area:
 *   dmin = distance(centre, polygon boundary), dmax = max vertex distance
 *   - centre on the boundary            -> INTERSECTING
 *   - dmin >= r, centre inside          -> CONTAINED (+1)
 *   - dmax <= r                         -> CONTAINED (-1)
 *   - dmin >= r (otherwise)             -> OUTSIDE  (externally tangent
 *                                          counts as OUTSIDE: area is 0)
 *   - otherwise                         -> INTERSECTING
 * Both containments use closed sets, so internal tangency is CONTAINED.
 * Boundaries are measured against the closed polygon (the disk side is
 * treated as closed) with a 1e-12 * coordinate-scale band for exact input.
 *
 * Degenerate inputs are pinned down:
 *   n < 3              -> EMPTY (area 0)
 *   collinear/coincident vertices -> DEGENERATE (area 0)
 *   r == 0             -> area 0, CONTAINED if the point is in the closed
 *                         polygon, otherwise OUTSIDE
 *   non-finite / r < 0 / |coord| > 1e15 -> EINVAL (area 0)
 *
 * Accuracy: everything feeding the area is double precision.  The segment
 * terms are exact rational expressions in the input doubles; the only
 * transcendental operations are atan2() for the arc angle and sqrt() for
 * the clip roots, each with relative error of a few ulp.  The reference
 * check in tests/verify_circle.py (independent vertical-slice quadrature
 * with scipy.quad) measures the absolute error of the fixtures at < 1e-11.
 * The general bound is O(eps * (area + r^2 * total_arc_angle)) plus the
 * conditioning of the roots; for coordinates up to ~1e6 and areas up to
 * ~1e12 that is far below 1e-9 relative, and below 1e-9 absolute for the
 * shipped fixtures.  (Plain double summation is used, so a centre extremely
 * far from a tiny polygon degrades the bound gracefully.)
 * ===================================================================== */

static int sat_inside_d(const SatPoly* p, double x, double y) {
    int inside = 0, i, j;
    for (i = 0, j = p->n - 1; i < p->n; j = i++) {
        double xi = (double)p->p[i].x, yi = (double)p->p[i].y;
        double xj = (double)p->p[j].x, yj = (double)p->p[j].y;
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

static double sat_dist_seg2(double px, double py,
                            double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay;
    double L = dx * dx + dy * dy;
    double t = 0.0, qx, qy;
    if (L > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / L;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    qx = ax + t * dx - px;
    qy = ay + t * dy - py;
    return qx * qx + qy * qy;
}

static double sat_poly_signed_area(const SatPoly* p) {
    double ox = (double)p->p[0].x, oy = (double)p->p[0].y, s = 0.0;
    int i;
    for (i = 0; i < p->n; ++i) {
        int j = (i + 1) % p->n;
        double ax = (double)p->p[i].x - ox, ay = (double)p->p[i].y - oy;
        double bx = (double)p->p[j].x - ox, by = (double)p->p[j].y - oy;
        s += ax * by - ay * bx;
    }
    return 0.5 * s;
}

/* True when all vertices are collinear (or coincident): the polygon has no
 * interior and no boundary area can be swept.  The test uses the extreme
 * vertex pair as the reference line, so it is translation- and
 * rotation-invariant.  The relative tolerance is far below float resolution,
 * so a polygon that is merely thin is not misclassified. */
static int sat_poly_degenerate(const SatPoly* p) {
    int i, j, i0 = 0, i1 = 1;
    double best = -1.0, vx, vy, d2, maxdev2 = 0.0;
    for (i = 0; i < p->n; ++i) {
        for (j = i + 1; j < p->n; ++j) {
            double dx = (double)p->p[j].x - (double)p->p[i].x;
            double dy = (double)p->p[j].y - (double)p->p[i].y;
            double d = dx * dx + dy * dy;
            if (d > best) { best = d; i0 = i; i1 = j; }
        }
    }
    if (best <= 0.0) return 1;                 /* all vertices coincide */
    vx = (double)p->p[i1].x - (double)p->p[i0].x;
    vy = (double)p->p[i1].y - (double)p->p[i0].y;
    d2 = vx * vx + vy * vy;
    for (i = 0; i < p->n; ++i) {
        double px = (double)p->p[i].x - (double)p->p[i0].x;
        double py = (double)p->p[i].y - (double)p->p[i0].y;
        double cross = vx * py - vy * px;
        double dev2 = (cross * cross) / d2;    /* squared distance to line */
        if (dev2 > maxdev2) maxdev2 = dev2;
    }
    /* deviation <= 1e-12 * reference length */
    return maxdev2 <= 1e-24 * d2;
}

static int sat_cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static double sat_norm_angle(double a) {
    const double two_pi = 6.283185307179586476925286766559;
    while (a < 0.0) a += two_pi;
    while (a >= two_pi) a -= two_pi;
    return a;
}

/* Area contributed by the disk boundary arcs that lie inside p.
 * `ang` holds the sorted, de-duplicated angles (relative to the centre) of
 * every crossing between the polygon boundary and the circle.  Between two
 * consecutive crossings the circle is entirely inside or entirely outside p,
 * so one point-in-polygon test per arc is enough.  The returned value is the
 * total CCW angular measure of the inside arcs, in [0, 2*pi]. */
static double sat_arc_measure(const SatPoly* p, double cx, double cy, double r,
                              const double* ang, int m) {
    const double two_pi = 6.283185307179586476925286766559;
    double W = 0.0;
    int k;
    if (m == 0) {
        /* no crossing at all: the whole circle is on one side of p */
        return sat_inside_d(p, cx + r, cy) ? two_pi : 0.0;
    }
    for (k = 0; k < m; ++k) {
        double a0 = ang[k];
        double a1 = (k + 1 < m) ? ang[k + 1] : ang[0] + two_pi;
        double dth = a1 - a0;
        double mid = a0 + 0.5 * dth;
        double px = cx + r * cos(mid);
        double py = cy + r * sin(mid);
        if (sat_inside_d(p, px, py)) W += dth;
    }
    return W;
}

int sat_circle_polygon(const SatPoly* p, double cx, double cy, double r,
                       SatCirclePoly* out) {
    int i, n, status;
    double area = 0.0;
    double poly_area = 0.0;
    double ang[64];
    int m = 0;

    if (!out) return SAT_CIRCLE_EINVAL;
    out->status = SAT_CIRCLE_EINVAL;
    out->contained = 0;
    out->area = 0.0;
    out->poly_area = 0.0;

    if (!p || !isfinite(cx) || !isfinite(cy) || !isfinite(r) || r < 0.0)
        return SAT_CIRCLE_EINVAL;
    if (fabs(cx) > 1.0e15 || fabs(cy) > 1.0e15 || r > 1.0e15)
        return SAT_CIRCLE_EINVAL;   /* keeps squares/areas inside DBL_MAX */

    n = p->n;
    if (n < 3) {
        out->status = SAT_CIRCLE_POLY_EMPTY;
        return SAT_CIRCLE_POLY_EMPTY;
    }

    poly_area = sat_poly_signed_area(p);
    out->poly_area = poly_area;

    if (sat_poly_degenerate(p)) {
        out->status = SAT_CIRCLE_POLY_DEGENERATE;
        return SAT_CIRCLE_POLY_DEGENERATE;
    }

    /* ---- classification (analytic, independent of the area) ---- */
    {
        double dmin2 = 1e300, mx = 1.0, amax2 = 0.0;
        int inside;
        double dmin, eps;
        int poly_contains_disk, disk_contains_poly;

        for (i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            double d2 = sat_dist_seg2(cx, cy,
                                      (double)p->p[i].x, (double)p->p[i].y,
                                      (double)p->p[j].x, (double)p->p[j].y);
            double vx = (double)p->p[i].x - cx, vy = (double)p->p[i].y - cy;
            double v2 = vx * vx + vy * vy;
            if (d2 < dmin2) dmin2 = d2;
            if (v2 > amax2) amax2 = v2;
            if (fabs((double)p->p[i].x) > mx) mx = fabs((double)p->p[i].x);
            if (fabs((double)p->p[i].y) > mx) mx = fabs((double)p->p[i].y);
        }
        if (fabs(cx) > mx) mx = fabs(cx);
        if (fabs(cy) > mx) mx = fabs(cy);
        if (r > mx) mx = r;
        dmin = sqrt(dmin2);
        inside = sat_inside_d(p, cx, cy);
        eps = 1e-12 * mx;      /* "on the boundary" band, ~1e-12 of the scale */

        /* The disk is convex; all vertices inside the disk therefore put the
         * whole closed polygon inside it.  Conversely one vertex outside puts
         * some of the polygon outside. */
        poly_contains_disk = (r == 0.0)
            ? (inside || dmin <= eps)
            : (inside && dmin >= r - eps);
        disk_contains_poly = (r > 0.0) && (sqrt(amax2) <= r + eps);

        if (r == 0.0) {
            /* the disk is a point: contained in the closed polygon iff the
             * point is in the closed polygon, and the overlap is empty */
            status = poly_contains_disk ? SAT_CIRCLE_CONTAINED
                                        : SAT_CIRCLE_OUTSIDE;
            out->contained = poly_contains_disk ? 1 : 0;
        } else if (poly_contains_disk) {
            status = SAT_CIRCLE_CONTAINED;
            out->contained = 1;
        } else if (disk_contains_poly) {
            status = SAT_CIRCLE_CONTAINED;
            out->contained = -1;
        } else if (r > 0.0 && dmin >= r - eps) {
            /* the closed shapes only touch (external tangency) or are apart;
             * overlap area is 0 either way */
            status = SAT_CIRCLE_OUTSIDE;
            out->contained = 0;
        } else {
            status = SAT_CIRCLE_INTERSECTING;
            out->contained = 0;
        }
    }

    /* ---- analytic area (also for OUTSIDE/CONTAINED, so the value is
     *      consistent with the status by construction) ---- */
    if (r == 0.0) {
        area = 0.0;
    } else {
        double S = 0.0;        /* signed boundary integral, polygon frame */
        double s = (poly_area >= 0.0) ? 1.0 : -1.0;
        double r2 = r * r;

        for (i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            double ax = (double)p->p[i].x - cx, ay = (double)p->p[i].y - cy;
            double bx = (double)p->p[j].x - cx, by = (double)p->p[j].y - cy;
            double dx = bx - ax, dy = by - ay;
            double A = dx * dx + dy * dy;
            double t0 = 0.0, t1 = 1.0;
            int has_interval = 0;

            if (A == 0.0) continue;            /* zero-length edge */

            {
                double B = 2.0 * (ax * dx + ay * dy);
                double C = ax * ax + ay * ay - r2;
                double disc = B * B - 4.0 * A * C;

                if (disc >= 0.0) {
                    /* stable roots of A t^2 + B t + C = 0 */
                    double sq = sqrt(disc);
                    double q = (B >= 0.0) ? -0.5 * (B + sq)
                                          : -0.5 * (B - sq);
                    double rr0, rr1;
                    if (q == 0.0) {
                        rr0 = rr1 = 0.0;
                    } else {
                        rr0 = q / A;
                        rr1 = C / q;
                    }
                    if (rr0 > rr1) { double tmp = rr0; rr0 = rr1; rr1 = tmp; }

                    t0 = (rr0 < 0.0) ? 0.0 : rr0;
                    t1 = (rr1 > 1.0) ? 1.0 : rr1;
                    if (t0 < t1) has_interval = 1;

                    /* record boundary crossings for the arc pass */
                    if (disc == 0.0) {
                        if (rr0 >= 0.0 && rr0 <= 1.0 && m < 64) {
                            double ux = ax + rr0 * dx, uy = ay + rr0 * dy;
                            ang[m++] = sat_norm_angle(atan2(uy, ux));
                        }
                    } else {
                        if (rr0 >= 0.0 && rr0 <= 1.0 && m < 64) {
                            double ux = ax + rr0 * dx, uy = ay + rr0 * dy;
                            ang[m++] = sat_norm_angle(atan2(uy, ux));
                        }
                        if (rr1 >= 0.0 && rr1 <= 1.0 && m < 64) {
                            double ux = ax + rr1 * dx, uy = ay + rr1 * dy;
                            ang[m++] = sat_norm_angle(atan2(uy, ux));
                        }
                    }
                }
            }

            if (has_interval) {
                double ux = ax + t0 * dx, uy = ay + t0 * dy;
                double vx = ax + t1 * dx, vy = ay + t1 * dy;
                S += 0.5 * (ux * vy - uy * vx);
            }
        }

        /* Sort the crossing angles.  Duplicates are deliberately NOT
         * removed: a vertex exactly on the circle is reported by both
         * adjacent edges, and the zero-length arc between the two copies
         * contributes exactly nothing.  Merging them with a fixed angular
         * tolerance would silently drop genuinely short arcs and lose
         * up to r^2 * tolerance / 2 of area at large radii. */
        if (m > 1)
            qsort(ang, (size_t)m, sizeof(double), sat_cmp_double);

        /* arcs of the circle that are polygon boundary contribute
         * (r^2/2) * dtheta; the sign follows the polygon orientation */
        S += s * 0.5 * r2 * sat_arc_measure(p, cx, cy, r, ang, m);

        area = fabs(S);
    }

    out->status = status;
    out->area = area;
    out->poly_area = poly_area;
    return status;
}

double sat_circle_polygon_area(const SatPoly* p, double cx, double cy, double r) {
    SatCirclePoly res;
    memset(&res, 0, sizeof res);
    sat_circle_polygon(p, cx, cy, r, &res);
    return res.area;
}
