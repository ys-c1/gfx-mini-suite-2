#define _POSIX_C_SOURCE 199309L
#include "../src/sat.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

/* ---- reference: direct concave-polygon overlap (edge crossing + containment) ---- */
static float orient(SatVec a, SatVec b, SatVec c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
static int seg_cross(SatVec a, SatVec b, SatVec c, SatVec d) {
    float o1 = orient(a, b, c), o2 = orient(a, b, d);
    float o3 = orient(c, d, a), o4 = orient(c, d, b);
    return (o1 * o2 < 0) && (o3 * o4 < 0);
}
static int ref_overlap(const SatPoly* a, const SatPoly* b) {
    for (int i = 0; i < a->n; ++i)
        for (int j = 0; j < b->n; ++j)
            if (seg_cross(a->p[i], a->p[(i + 1) % a->n], b->p[j], b->p[(j + 1) % b->n])) return 1;
    if (sat_point_inside(a, b->p[0].x, b->p[0].y)) return 1;
    if (sat_point_inside(b, a->p[0].x, a->p[0].y)) return 1;
    return 0;
}

static int in_piece_strict(const SatPoly* p, float x, float y) {
    for (int i = 0; i < p->n; ++i) {
        SatVec a = p->p[i], b = p->p[(i + 1) % p->n];
        if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < 1e-4f) return 0;
    }
    return 1;
}

static SatPoly moved(const SatPoly* a, float dx, float dy) {
    SatPoly r = *a;
    for (int i = 0; i < r.n; ++i) { r.p[i].x += dx; r.p[i].y += dy; }
    return r;
}

int main(void) {
    /* original convex-only tests */
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

    /* four concave shapes (CCW) */
    SatPoly shapes[4];
    const char* names[4] = { "L", "U", "plus", "comb" };
    shapes[0].n = 6;  /* L, area 12 */
    shapes[0].p[0]=(SatVec){0,0}; shapes[0].p[1]=(SatVec){4,0}; shapes[0].p[2]=(SatVec){4,2};
    shapes[0].p[3]=(SatVec){2,2}; shapes[0].p[4]=(SatVec){2,4}; shapes[0].p[5]=(SatVec){0,4};
    shapes[1].n = 8;  /* U, area 19 */
    shapes[1].p[0]=(SatVec){0,0}; shapes[1].p[1]=(SatVec){6,0}; shapes[1].p[2]=(SatVec){6,4};
    shapes[1].p[3]=(SatVec){4,4}; shapes[1].p[4]=(SatVec){4,1.5f}; shapes[1].p[5]=(SatVec){2,1.5f};
    shapes[1].p[6]=(SatVec){2,4}; shapes[1].p[7]=(SatVec){0,4};
    shapes[2].n = 12; /* plus, area 32 */
    shapes[2].p[0]=(SatVec){1,0}; shapes[2].p[1]=(SatVec){5,0}; shapes[2].p[2]=(SatVec){5,1};
    shapes[2].p[3]=(SatVec){6,1}; shapes[2].p[4]=(SatVec){6,5}; shapes[2].p[5]=(SatVec){5,5};
    shapes[2].p[6]=(SatVec){5,6}; shapes[2].p[7]=(SatVec){1,6}; shapes[2].p[8]=(SatVec){1,5};
    shapes[2].p[9]=(SatVec){0,5}; shapes[2].p[10]=(SatVec){0,1}; shapes[2].p[11]=(SatVec){1,1};
    shapes[3].n = 16; /* comb, area 13 */
    shapes[3].p[0]=(SatVec){0,0}; shapes[3].p[1]=(SatVec){8,0}; shapes[3].p[2]=(SatVec){8,2};
    shapes[3].p[3]=(SatVec){7,2}; shapes[3].p[4]=(SatVec){7,1}; shapes[3].p[5]=(SatVec){6,1};
    shapes[3].p[6]=(SatVec){6,2}; shapes[3].p[7]=(SatVec){5,2}; shapes[3].p[8]=(SatVec){5,1};
    shapes[3].p[9]=(SatVec){4,1}; shapes[3].p[10]=(SatVec){4,2}; shapes[3].p[11]=(SatVec){3,2};
    shapes[3].p[12]=(SatVec){3,1}; shapes[3].p[13]=(SatVec){2,1}; shapes[3].p[14]=(SatVec){2,2};
    shapes[3].p[15]=(SatVec){0,2};

    printf("%-5s %5s %8s %6s %6s %10s %10s\n", "shape", "verts", "baseline", "pieces", "ratio", "area_err", "us/decomp");
    for (int s = 0; s < 4; ++s) {
        SatPoly pieces[SAT_MAX_PIECES];
        int np = sat_decompose(&shapes[s], pieces, SAT_MAX_PIECES);
        CHECK(np >= 1);
        int baseline = shapes[s].n - 2;                 /* greedy ear-clip triangulation */
        CHECK(2 * np <= 3 * baseline);                  /* pieces <= 1.5x baseline */
        double asum = 0;
        for (int i = 0; i < np; ++i) asum += fabs(sat_area(&pieces[i]));
        double aerr = fabs(asum - fabs(sat_area(&shapes[s])));
        CHECK(aerr < 1e-6);
        /* pieces non-overlapping & covering: dense sampling, strictly-interior
         * samples must lie in exactly one piece iff inside the shape */
        float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
        for (int i = 0; i < shapes[s].n; ++i) {
            if (shapes[s].p[i].x < mnx) mnx = shapes[s].p[i].x;
            if (shapes[s].p[i].y < mny) mny = shapes[s].p[i].y;
            if (shapes[s].p[i].x > mxx) mxx = shapes[s].p[i].x;
            if (shapes[s].p[i].y > mxy) mxy = shapes[s].p[i].y;
        }
        for (float x = mnx + 0.013f; x < mxx; x += 0.05f)
            for (float y = mny + 0.017f; y < mxy; y += 0.05f) {
                int cnt = 0;
                for (int i = 0; i < np; ++i) cnt += in_piece_strict(&pieces[i], x, y);
                int inshape = sat_point_inside(&shapes[s], x, y);
                CHECK(cnt <= 1);                        /* no overlap between pieces */
                CHECK(!(cnt == 1 && !inshape));         /* pieces stay inside shape */
                CHECK(!(cnt == 0 && inshape));          /* pieces cover the shape */
            }
        /* timing */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int iters = 20000, acc = 0;
        for (int r = 0; r < iters; ++r) acc += sat_decompose(&shapes[s], pieces, SAT_MAX_PIECES);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3) / iters;
        CHECK(acc == iters * np);
        printf("%-5s %5d %8d %6d %6.2f %10.2e %10.3f\n",
               names[s], shapes[s].n, baseline, np, (double)np / baseline, aerr, us);
    }

    /* consistency: decomposed SAT must agree with direct concave reference.
     * Grid offsets keep probe edges clear of exact vertex/edge coincidence,
     * where "touching counts as overlap" is convention-dependent. */
    int mism = 0, naive_mism = 0, total = 0;
    for (int s = 0; s < 4; ++s) {
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) {
                SatPoly probe = box(-1.31f + 0.63f * i, -1.137f + 0.573f * j, 1.3f, 1.1f);
                int ref = ref_overlap(&shapes[s], &probe);
                int got = sat_overlap_any(&shapes[s], &probe);
                int naive = sat_overlap(&shapes[s], &probe);   /* convex-only SAT on concave shape */
                ++total;
                if (got != ref) ++mism;
                if (naive != ref) ++naive_mism;
            }
    }
    /* concave-vs-concave pairs */
    for (int s = 0; s < 4; ++s) {
        SatPoly other = shapes[(s + 1) % 4];
        for (int i = 0; i < 11; ++i)
            for (int j = 0; j < 11; ++j) {
                SatPoly o = moved(&other, -2.73f + 0.9f * i, -2.31f + 0.9f * j);
                int ref = ref_overlap(&shapes[s], &o);
                int got = sat_overlap_any(&shapes[s], &o);
                ++total;
                if (got != ref) ++mism;
            }
    }
    printf("consistency: %d/%d mismatches (naive convex-SAT on concave: %d)\n", mism, total, naive_mism);
    CHECK(mism == 0);
    CHECK(naive_mism > 0);   /* confirms the concave misjudgment exists without the fix */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
