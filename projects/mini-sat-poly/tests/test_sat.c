#include "../src/sat.h"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

/* Concave L-shape. */
static SatPoly lshape(void) {
    SatPoly p; p.n = 6;
    p.p[0] = (SatVec){0, 0};  p.p[1] = (SatVec){10, 0};
    p.p[2] = (SatVec){10, 4}; p.p[3] = (SatVec){4, 4};
    p.p[4] = (SatVec){4, 10}; p.p[5] = (SatVec){0, 10};
    return p;
}

static SatPoly rotated(const SatPoly* in, int k) {
    SatPoly out; out.n = in->n;
    for (int i = 0; i < in->n; ++i) out.p[i] = in->p[(i + k) % in->n];
    return out;
}

static SatPoly reversed(const SatPoly* in) {
    SatPoly out; out.n = in->n;
    for (int i = 0; i < in->n; ++i) out.p[i] = in->p[in->n - 1 - i];
    return out;
}

static void project(const SatPoly* a, SatVec ax, float* lo, float* hi) {
    float mn = 1e30f, mx = -1e30f;
    for (int i = 0; i < a->n; ++i) {
        float d = a->p[i].x * ax.x + a->p[i].y * ax.y;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }
    *lo = mn; *hi = mx;
}

int main(void) {
    SatPoly a = box(0, 0, 10, 10), b = box(5, 5, 10, 10), c = box(20, 20, 5, 5);
    CHECK(sat_overlap(&a, &b) == 1);
    CHECK(sat_overlap(&a, &c) == 0);
    CHECK(sat_overlap(&a, &a) == 1);
    CHECK(sat_point_inside(&a, 5, 5) == 1);
    CHECK(sat_point_inside(&a, 50, 5) == 0);
    SatPoly degenerate; degenerate.n = 2;
    CHECK(sat_overlap(&degenerate, &a) == -1);
    float cx, cy;
    sat_centroid(&a, &cx, &cy);
    CHECK(cx > 4.9f && cx < 5.1f && cy > 4.9f && cy < 5.1f);

    /* --- extended query: axis + depth --- */
    SatVec axis; float depth;

    /* Separated: verdict 0, reported axis really separates with gap == depth. */
    CHECK(sat_overlap_axis(&a, &c, &axis, &depth) == 0);
    CHECK(fabsf(axis.x * axis.x + axis.y * axis.y - 1.0f) < 1e-4f);
    CHECK(depth > 0.0f);
    {
        float alo, ahi, clo, chi;
        project(&a, axis, &alo, &ahi);
        project(&c, axis, &clo, &chi);
        /* axis is oriented from a toward c, so c must lie above a. */
        CHECK(clo > ahi);
        CHECK(fabsf((clo - ahi) - depth) < 1e-3f);
    }

    /* Overlapping: verdict 1, depth is the MTV magnitude, axis aims a -> b. */
    CHECK(sat_overlap_axis(&a, &b, &axis, &depth) == 1);
    CHECK(fabsf(depth - 5.0f) < 1e-3f);
    CHECK(fabsf(axis.x * axis.x + axis.y * axis.y - 1.0f) < 1e-4f);
    CHECK(axis.x * 5.0f + axis.y * 5.0f > 0.0f); /* b's centroid is at +5,+5 from a's */

    /* NULL axis/depth out-params are allowed. */
    CHECK(sat_overlap_axis(&a, &b, NULL, NULL) == 1);
    CHECK(sat_overlap_axis(&a, &c, NULL, NULL) == 0);

    /* Degenerate input: verdict -1, out-params untouched. */
    axis = (SatVec){7, 8}; depth = 9;
    CHECK(sat_overlap_axis(&degenerate, &a, &axis, &depth) == -1);
    CHECK(axis.x == 7 && axis.y == 8 && depth == 9);
    CHECK(sat_overlap_axis(NULL, &a, &axis, &depth) == -1);

    /* Touching edges still count as overlap (verdict unchanged). */
    SatPoly t = box(10, 0, 10, 10);
    CHECK(sat_overlap(&a, &t) == 1);
    CHECK(sat_overlap_axis(&a, &t, &axis, &depth) == 1);

    /* --- concave polygons --- */
    SatPoly l = lshape();
    SatPoly far = box(30, 30, 4, 4);
    SatPoly notch = box(6, 6, 4, 4);   /* sits in the L's concave notch: no real contact */
    SatPoly hit = box(2, 2, 2, 2);     /* inside the L's solid corner */

    /* Separation verdict is exact for concave input: reported axis must
     * genuinely separate the projections. */
    CHECK(sat_overlap_axis(&l, &far, &axis, &depth) == 0);
    {
        float llo, lhi, flo, fhi;
        project(&l, axis, &llo, &lhi);
        project(&far, axis, &flo, &fhi);
        CHECK(flo > lhi);
        CHECK(fabsf((flo - lhi) - depth) < 1e-3f);
    }
    /* Verdicts for concave pairs are unchanged from sat_overlap. */
    CHECK(sat_overlap_axis(&l, &notch, NULL, NULL) == sat_overlap(&l, &notch));
    CHECK(sat_overlap_axis(&l, &hit, NULL, NULL) == sat_overlap(&l, &hit));
    CHECK(sat_overlap(&l, &hit) == 1);

    /* --- axis/vertex order must not change verdicts --- */
    {
        SatPoly polys[4] = {a, b, l, c};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                int expect = sat_overlap(&polys[i], &polys[j]);
                for (int k = 0; k < polys[i].n; ++k) {
                    SatPoly r = rotated(&polys[i], k);
                    SatPoly v = reversed(&r);
                    CHECK(sat_overlap(&r, &polys[j]) == expect);
                    CHECK(sat_overlap(&v, &polys[j]) == expect);
                    CHECK(sat_overlap_axis(&v, &polys[j], NULL, NULL) == expect);
                }
            }
        }
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
