/* Verifies the requirement "the boolean must stay bit-identical on every
 * sample, including exact contact".
 *
 * It compiles the untouched original sat.c (tests/orig_sat_snapshot.c)
 * under renamed symbols together with the new sat.c, then compares them on:
 *   - every deterministic case below,
 *   - 2,000,000 random pairs, deliberately biased towards contact
 *     (coordinates drawn from a small integer lattice so shared edges and
 *     vertices occur constantly),
 *   - axis-aligned boxes and triangles at random integer offsets.
 * Any disagreement is printed and fails the run.
 */
#include "../src/sat.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>

/* Renamed copies of the original functions. */
#define sat_overlap orig_sat_overlap
#define sat_point_inside orig_sat_point_inside
#define sat_centroid orig_sat_centroid
#include "orig_sat_snapshot.c"
#undef sat_overlap
#undef sat_point_inside
#undef sat_centroid

int   orig_sat_overlap(const SatPoly* a, const SatPoly* b);
int   orig_sat_point_inside(const SatPoly* a, float x, float y);
void  orig_sat_centroid(const SatPoly* a, float* cx, float* cy);

static int fails = 0, checks = 0, contact_n = 0, overlap_n = 0, disjoint_n = 0, invalid_n = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

static void tally(int v) {
    if (v == 1) ++overlap_n;
    else if (v == 0) ++disjoint_n;
    else ++invalid_n;
}

static void compare(const char* tag, const SatPoly* a, const SatPoly* b) {
    int nw = sat_overlap(a, b), od = orig_sat_overlap(a, b);
    tally(nw);
    if (nw != od) {
        ++fails;
        printf("MISMATCH %s: new=%d orig=%d  A(n=%d) B(n=%d)\n", tag, nw, od, a->n, b->n);
        for (int i = 0; i < a->n; ++i) printf("  a[%d]=(%g,%g)\n", i, a->p[i].x, a->p[i].y);
        for (int i = 0; i < b->n; ++i) printf("  b[%d]=(%g,%g)\n", i, b->p[i].x, b->p[i].y);
    }
    ++checks;
}

static unsigned long long rng_state = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 32);
}
static int rnd_int(int lo, int hi) { return lo + (int)(rnd() % (unsigned)(hi - lo + 1)); }

static float key_angle(SatVec v) {
    float a = atan2f(v.y, v.x);
    return a < 0.0f ? a + 6.2831853f : a;
}

static SatPoly random_poly(int n, int lat) {
    /* Vertices from a small integer lattice, sorted by angle around the
     * origin, giving simple (usually convex) polygons that share
     * coordinates often, so exact contact is common. */
    SatPoly p; p.n = n;
    for (int i = 0; i < n; ++i) {
        p.p[i].x = (float)rnd_int(-lat, lat);
        p.p[i].y = (float)rnd_int(-lat, lat);
    }
    for (int i = 1; i < n; ++i) {
        SatVec key = p.p[i];
        float ka = key_angle(key);
        int j = i - 1;
        while (j >= 0 && key_angle(p.p[j]) > ka) { p.p[j + 1] = p.p[j]; --j; }
        p.p[j + 1] = key;
    }
    return p;
}

/* Conservative exact-contact detector used only for reporting coverage.
 * An overlapping pair whose 1-D projected intervals *touch* (no positive
 * overlap) on some axis is in contact there.  This misses a contact that
 * is covered by overlap on every axis, so the reported count is a lower
 * bound, not an over-count. */
static int is_contact(const SatPoly* a, const SatPoly* b) {
    for (int which = 0; which < 2; ++which) {
        const SatPoly* src = which ? b : a;
        for (int e = 0; e < src->n; ++e) {
            int e2 = (e + 1) % src->n;
            double ex = (double)src->p[e2].x - src->p[e].x;
            double ey = (double)src->p[e2].y - src->p[e].y;
            double len = sqrt(ex * ex + ey * ey);
            if (len < 1e-9) continue;
            double ax = -ey / len, ay = ex / len;
            double alo = 1e300, ahi = -1e300, blo = 1e300, bhi = -1e300;
            for (int i = 0; i < a->n; ++i) {
                double d = a->p[i].x * ax + a->p[i].y * ay;
                if (d < alo) alo = d;
                if (d > ahi) ahi = d;
            }
            for (int i = 0; i < b->n; ++i) {
                double d = b->p[i].x * ax + b->p[i].y * ay;
                if (d < blo) blo = d;
                if (d > bhi) bhi = d;
            }
            const double eps = 1e-9 * (1.0 + fabs(ahi) + fabs(bhi));
            if (fabs(ahi - blo) <= eps || fabs(bhi - alo) <= eps) return 1;
        }
    }
    return 0;
}

static void count_contact(const SatPoly* a, const SatPoly* b) {
    if (sat_overlap(a, b) == 1 && is_contact(a, b)) ++contact_n;
}

int main(void) {
    /* Deterministic original samples, verbatim. */
    SatPoly a = box(0, 0, 10, 10), b = box(5, 5, 10, 10), c = box(20, 20, 5, 5);
    compare("orig A/B", &a, &b);
    compare("orig A/C", &a, &c);
    compare("orig A/A", &a, &a);
    SatPoly degenerate; degenerate.n = 2;
    compare("degenerate", &degenerate, &a);

    /* Exact-contact cases, each with the boolean expected to be 1. */
    struct { float x, y, w, h; const char* tag; } contact[] = {
        {10, 0, 5, 5, "edge touch, right"},
        {10, 10, 5, 5, "corner touch, top-right"},
        {5, 10, 5, 5, "edge touch, top aligned width"},
        {0, 0, 5, 5, "contained corner share"},
        {-5, -5, 5, 5, "corner touch, bottom-left"},
    };
    for (int i = 0; i < 5; ++i) {
        SatPoly t = box(contact[i].x, contact[i].y, contact[i].w, contact[i].h);
        int v = sat_overlap(&a, &t);
        CHECK(v == 1);
        compare(contact[i].tag, &a, &t);
        ++contact_n;
    }
    /* Triangles sharing an edge and sharing a single vertex. */
    SatPoly tri_shared_edge = { .n = 3, .p = {{10, 0}, {15, 5}, {10, 10}} };
    SatPoly tri_shared_vertex = { .n = 3, .p = {{10, 10}, {15, 14}, {14, 15}} };
    CHECK(sat_overlap(&a, &tri_shared_edge) == 1);
    CHECK(sat_overlap(&a, &tri_shared_vertex) == 1);
    compare("tri shared edge", &a, &tri_shared_edge);
    compare("tri shared vertex", &a, &tri_shared_vertex);
    contact_n += 2;

    /* Random differential run. */
    const int N = 2000000;
    for (int it = 0; it < N; ++it) {
        int na = rnd_int(3, 8), nb = rnd_int(3, 8);
        SatPoly pa = random_poly(na, 6);
        SatPoly pb = random_poly(nb, 6);
        /* Half the time, shift B onto the same lattice window so contact
         * and near-contact are well represented. */
        if (it & 1) {
            float dx = (float)rnd_int(-6, 6), dy = (float)rnd_int(-6, 6);
            for (int i = 0; i < pb.n; ++i) { pb.p[i].x += dx; pb.p[i].y += dy; }
            count_contact(&pa, &pb);
        }
        compare("random", &pa, &pb);
    }

    /* Random boxes and triangles on integer lattices: highest contact rate. */
    for (int it = 0; it < 500000; ++it) {
        SatPoly ba = box((float)rnd_int(-5, 5), (float)rnd_int(-5, 5),
                         (float)rnd_int(1, 6), (float)rnd_int(1, 6));
        SatPoly bb = box((float)rnd_int(-5, 8), (float)rnd_int(-5, 8),
                         (float)rnd_int(1, 6), (float)rnd_int(1, 6));
        count_contact(&ba, &bb);
        compare("box", &ba, &bb);

        SatPoly ta = { .n = 3, .p = {
            {(float)rnd_int(-4, 4), (float)rnd_int(-4, 4)},
            {(float)rnd_int(-4, 4), (float)rnd_int(-4, 4)},
            {(float)rnd_int(-4, 4), (float)rnd_int(-4, 4)} } };
        SatPoly tb = { .n = 3, .p = {
            {(float)rnd_int(-2, 6), (float)rnd_int(-2, 6)},
            {(float)rnd_int(-2, 6), (float)rnd_int(-2, 6)},
            {(float)rnd_int(-2, 6), (float)rnd_int(-2, 6)} } };
        count_contact(&ta, &tb);
        compare("tri", &ta, &tb);
    }

    /* point_inside and centroid must also be unchanged. */
    for (int it = 0; it < 200000; ++it) {
        SatPoly p = random_poly(rnd_int(3, 8), 5);
        float x = (float)rnd_int(-7, 7), y = (float)rnd_int(-7, 7);
        if (sat_point_inside(&p, x, y) != orig_sat_point_inside(&p, x, y)) {
            ++fails; printf("MISMATCH point_inside\n");
        }
        float cx1, cy1, cx2, cy2;
        sat_centroid(&p, &cx1, &cy1);
        orig_sat_centroid(&p, &cx2, &cy2);
        if (cx1 != cx2 || cy1 != cy2) { ++fails; printf("MISMATCH centroid\n"); }
        checks += 2;
    }

    printf("boolean differential: %d checks, %d failures\n", checks, fails);
    printf("  overlap=%d disjoint=%d invalid=%d (exact-contact pairs in overlap set: %d)\n",
           overlap_n, disjoint_n, invalid_n, contact_n);
    return fails ? 1 : 0;
}
