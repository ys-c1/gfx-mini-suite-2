#include "sat.h"
#include <math.h>
#include <float.h>
#include <stddef.h>

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

/*
 * Scan the edge normals of `a` (both polygons projected on each).
 * Returns 1 as soon as one axis separates the projections, reporting that
 * axis and the positive gap along it. Otherwise tracks the axis with the
 * smallest overlap. Axis orientation is normalized by the caller.
 */
static int scan_axes(const SatPoly* a, const SatPoly* b,
                     SatVec* sep_axis, float* sep_gap,
                     SatVec* mtv_axis, float* mtv_depth) {
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
        if (ahi < blo || bhi < alo) {
            float gap = (ahi < blo) ? (blo - ahi) : (alo - bhi);
            float sign = (ahi < blo) ? 1.0f : -1.0f; /* points from a toward b */
            sep_axis->x = sign * ax; sep_axis->y = sign * ay;
            *sep_gap = gap;
            return 1;
        }
        float ov = fminf(ahi, bhi) - fmaxf(alo, blo);
        if (ov < *mtv_depth) {
            *mtv_depth = ov;
            mtv_axis->x = ax; mtv_axis->y = ay;
        }
    }
    return 0;
}

int sat_overlap_axis(const SatPoly* a, const SatPoly* b, SatVec* axis, float* depth) {
    if (!a || !b || a->n < 3 || b->n < 3) return -1;
    SatVec sep_axis = {0, 0}, mtv_axis = {0, 0};
    float sep_gap = 0, mtv_depth = FLT_MAX;
    int separated = scan_axes(a, b, &sep_axis, &sep_gap, &mtv_axis, &mtv_depth);
    if (!separated) {
        separated = scan_axes(b, a, &sep_axis, &sep_gap, &mtv_axis, &mtv_depth);
        if (separated) {
            /* scan_axes orients from its first argument toward its second;
             * normalize to point from a toward b. */
            sep_axis.x = -sep_axis.x; sep_axis.y = -sep_axis.y;
        }
    }
    if (separated) {
        if (axis) *axis = sep_axis;
        if (depth) *depth = sep_gap;
        return 0;
    }
    if (mtv_depth == FLT_MAX) {
        /* No usable edge normals at all (all vertices coincident): verdict
         * stays "overlap" as before, but there is no axis to report. */
        if (axis) { axis->x = 0; axis->y = 0; }
        if (depth) *depth = 0;
        return 1;
    }
    /* Orient the MTV axis from a's centroid toward b's so that moving b by
     * axis*depth separates the pair. */
    float cax, cay, cbx, cby;
    sat_centroid(a, &cax, &cay);
    sat_centroid(b, &cbx, &cby);
    if ((cbx - cax) * mtv_axis.x + (cby - cay) * mtv_axis.y < 0) {
        mtv_axis.x = -mtv_axis.x; mtv_axis.y = -mtv_axis.y;
    }
    if (axis) *axis = mtv_axis;
    if (depth) *depth = mtv_depth;
    return 1;
}

int sat_overlap(const SatPoly* a, const SatPoly* b) {
    return sat_overlap_axis(a, b, NULL, NULL);
}

int sat_point_inside(const SatPoly* a, float x, float y) {
    int inside = 0;
    for (int i = 0, j = a->n - 1; i < a->n; j = i++) {
        float xi = a->p[i].x, yi = a->p[i].y, xj = a->p[j].x, yj = a->p[j].y;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return inside;
}
