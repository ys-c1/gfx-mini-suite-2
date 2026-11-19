#include "sat.h"
#include <math.h>

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
