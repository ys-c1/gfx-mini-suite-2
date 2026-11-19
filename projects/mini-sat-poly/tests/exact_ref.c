#include "exact_ref.h"
#include <math.h>
#include <stdlib.h>

typedef struct { double x, y; } P;

static int cmp_pt(const void* pa, const void* pb) {
    const P* a = pa; const P* b = pb;
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    if (a->y < b->y) return -1;
    if (a->y > b->y) return 1;
    return 0;
}
static double cross(P o, P a, P b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* Andrew monotone chain; returns hull vertex count.  Collinear points on
 * the hull are dropped, so adjacent hull vertices give distinct edges. */
static int hull(P* pts, int n, P* out) {
    qsort(pts, (size_t)n, sizeof(P), cmp_pt);
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(out[k - 2], out[k - 1], pts[i]) <= 0) --k;
        out[k++] = pts[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(out[k - 2], out[k - 1], pts[i]) <= 0) --k;
        out[k++] = pts[i];
    }
    return k - 1; /* last point equals first */
}

/* Distance from origin to segment pq, and the closest point. */
static double seg_dist(P p, P q, P* closest) {
    double ex = q.x - p.x, ey = q.y - p.y;
    double len2 = ex * ex + ey * ey;
    double t = len2 > 0.0 ? -(p.x * ex + p.y * ey) / len2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    closest->x = p.x + t * ex;
    closest->y = p.y + t * ey;
    return sqrt(closest->x * closest->x + closest->y * closest->y);
}

/* Point in convex polygon (CCW or CW): all cross products the same sign,
 * with zero counting as inside, so the boundary is included. */
static int origin_in_hull(const P* h, int n) {
    int pos = 0, neg = 0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        double c = (h[j].x - h[i].x) * (0.0 - h[i].y) - (h[j].y - h[i].y) * (0.0 - h[i].x);
        if (c > 0) pos = 1;
        if (c < 0) neg = 1;
    }
    return !(pos && neg);
}

ExactMtv exact_mtv(const SatPoly* a, const SatPoly* b) {
    ExactMtv r;
    r.overlaps = 0; r.dist_defined = 0; r.dist = 0.0;
    r.dirx = 0.0; r.diry = 0.0;
    r.n_opt_dirs = 0; r.n_opt_deg = 0;

    int cap = a->n * b->n;
    P* pts = malloc((size_t)cap * sizeof(P));
    P* hl = malloc((size_t)cap * sizeof(P));
    for (int i = 0; i < a->n; ++i)
        for (int j = 0; j < b->n; ++j) {
            pts[i * b->n + j].x = (double)a->p[i].x - (double)b->p[j].x;
            pts[i * b->n + j].y = (double)a->p[i].y - (double)b->p[j].y;
        }
    int hn = hull(pts, cap, hl);
    r.overlaps = origin_in_hull(hl, hn);

    /* Closest hull boundary point to the origin. */
    double best = 1e300;
    (void)0;
    for (int i = 0; i < hn; ++i) {
        int j = (i + 1) % hn;
        P c;
        double d = seg_dist(hl[i], hl[j], &c);
        if (d < best) { best = d; }
    }
    r.dist = best;
    r.dist_defined = best > 1e-12;

    /* Collect every direction achieving the optimum within 1e-9, so the
     * caller can tell a unique answer from a tie. */
    double opt = best + 1e-9;
    for (int i = 0; i < hn; ++i) {
        int j = (i + 1) % hn;
        P c;
        if (seg_dist(hl[i], hl[j], &c) > opt) continue;
        double norm = sqrt(c.x * c.x + c.y * c.y);
        if (norm < 1e-12) continue;
        double ux = c.x / norm, uy = c.y / norm;
        int dup = 0;
        for (int k = 0; k < r.n_opt_dirs; ++k) {
            double ox = cos(r.opt_deg[k] * 3.14159265358979 / 180.0);
            double oy = sin(r.opt_deg[k] * 3.14159265358979 / 180.0);
            if (ux * ox + uy * oy > 1.0 - 1e-9) { dup = 1; break; }
        }
        if (dup) continue;
        double deg = atan2(uy, ux) * 180.0 / 3.14159265358979;
        if (deg < 0.0) deg += 360.0;
        if (r.n_opt_dirs == 0) { r.dirx = ux; r.diry = uy; }
        if (r.n_opt_dirs < 16) r.opt_deg[r.n_opt_dirs++] = deg;
    }
    r.n_opt_deg = r.n_opt_dirs;

    free(pts);
    free(hl);
    return r;
}
