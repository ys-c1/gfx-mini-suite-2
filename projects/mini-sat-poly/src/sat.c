#include "sat.h"
#include <math.h>
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

double sat_area(const SatPoly* a) {
    double s = 0;
    for (int i = 0, j = a->n - 1; i < a->n; j = i++)
        s += (double)a->p[j].x * a->p[i].y - (double)a->p[i].x * a->p[j].y;
    return 0.5 * s;
}

static float cross3(SatVec a, SatVec b, SatVec c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static int veq(SatVec a, SatVec b) { return a.x == b.x && a.y == b.y; }

static int convex_all(const SatVec* v, int n) {
    for (int i = 0; i < n; ++i)
        if (cross3(v[i], v[(i + 1) % n], v[(i + 2) % n]) < -1e-6f) return 0;
    return 1;
}

/* If p1 and p2 share an edge and their union is convex, write the merged
 * CCW boundary into out/outn and return 1. */
static int try_merge(const SatPoly* p1, const SatPoly* p2, SatVec* out, int* outn) {
    for (int i = 0; i < p1->n; ++i) {
        int i2 = (i + 1) % p1->n;
        for (int j = 0; j < p2->n; ++j) {
            int j2 = (j + 1) % p2->n;
            if (veq(p1->p[i], p2->p[j2]) && veq(p1->p[i2], p2->p[j])) {
                SatVec tmp[2 * SAT_MAX_VERT];
                int t = 0;
                /* p1 forward from B all the way around to A, then p2's chain A..B */
                for (int k = 0; k < p1->n; ++k) tmp[t++] = p1->p[(i2 + k) % p1->n];
                for (int k = 1; k <= p2->n - 2; ++k) tmp[t++] = p2->p[(j2 + k) % p2->n];
                if (t > SAT_MAX_VERT) continue;
                if (convex_all(tmp, t)) {
                    for (int k = 0; k < t; ++k) out[k] = tmp[k];
                    *outn = t;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int sat_decompose(const SatPoly* in, SatPoly* out, int max_out) {
    if (!in || !out || in->n < 3 || in->n > SAT_MAX_VERT) return -1;
    const float EPS = 1e-7f;
    SatVec v[SAT_MAX_VERT];
    int n = in->n;
    if (sat_area(in) < 0) { for (int i = 0; i < n; ++i) v[i] = in->p[n - 1 - i]; }
    else                  { for (int i = 0; i < n; ++i) v[i] = in->p[i]; }
    int idx[SAT_MAX_VERT];
    for (int i = 0; i < n; ++i) idx[i] = i;
    int m = n, np = 0;
    /* ear clipping -> np triangles */
    while (m > 3) {
        int clipped = 0;
        for (int i = 0; i < m; ++i) {
            int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
            SatVec a = v[ia], b = v[ib], c = v[ic];
            if (cross3(a, b, c) <= EPS) continue;   /* reflex or degenerate */
            int ear = 1;
            for (int k = 0; k < m; ++k) {
                if (idx[k] == ia || idx[k] == ib || idx[k] == ic) continue;
                SatVec p = v[idx[k]];
                /* reject vertices inside OR on the ear boundary: a vertex on the
                 * diagonal lets a polygon edge cut through the "ear" */
                if (cross3(a, b, p) > -EPS && cross3(b, c, p) > -EPS && cross3(c, a, p) > -EPS) { ear = 0; break; }
            }
            if (!ear) continue;
            if (np >= max_out) return -1;
            out[np].n = 3; out[np].p[0] = a; out[np].p[1] = b; out[np].p[2] = c; ++np;
            memmove(&idx[i], &idx[i + 1], (size_t)(m - i - 1) * sizeof(int));
            --m; clipped = 1; break;
        }
        if (!clipped) return -1;
    }
    if (np >= max_out) return -1;
    out[np].n = 3; out[np].p[0] = v[idx[0]]; out[np].p[1] = v[idx[1]]; out[np].p[2] = v[idx[2]]; ++np;
    /* Hertel-Mehlhorn: greedily merge adjacent pieces while the union stays convex */
    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < np && !merged; ++i)
            for (int j = i + 1; j < np && !merged; ++j) {
                SatVec mv[SAT_MAX_VERT]; int mn = 0;
                if (try_merge(&out[i], &out[j], mv, &mn)) {
                    out[i].n = mn;
                    for (int k = 0; k < mn; ++k) out[i].p[k] = mv[k];
                    out[j] = out[np - 1]; --np;
                    merged = 1;
                }
            }
    }
    return np;
}

int sat_overlap_any(const SatPoly* a, const SatPoly* b) {
    if (!a || !b || a->n < 3 || b->n < 3) return -1;
    SatPoly pa[SAT_MAX_PIECES], pb[SAT_MAX_PIECES];
    int na = sat_decompose(a, pa, SAT_MAX_PIECES);
    int nb = sat_decompose(b, pb, SAT_MAX_PIECES);
    if (na < 1 || nb < 1) return -1;
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < nb; ++j)
            if (sat_overlap(&pa[i], &pb[j])) return 1;
    return 0;
}
