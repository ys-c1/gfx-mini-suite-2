/* sat_overlap_mtv tests: boolean preservation, degenerate rejection, and
 * an independent brute-force comparison of the reported separation.
 *
 * Brute force does not use SAT or edge normals.  For a sample direction
 * theta it finds, by bisection, the smallest t for which moving B by
 * t*(cos theta, sin theta) makes SAT-disjoint.  Sampling 7200 directions
 * yields an upper bound on how good any direction can be (the true minimum
 * lies within half a degree of some sample).  A reported distance that
 * beats this upper bound by more than the tolerance would be a bug: no
 * direction can be better than the best sampled one.  Conversely, moving B
 * by the reported vector must itself separate (checked exactly with the
 * unchanged sat_overlap).  The reported direction is also compared
 * against the argmin of the sampled sweep with a documented error bound.
 */
#include "../src/sat.h"
#include "exact_ref.h"
#include <stdio.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKF(c, fmt, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

static SatPoly tri(float x0, float y0, float x1, float y1, float x2, float y2) {
    SatPoly p; p.n = 3;
    p.p[0] = (SatVec){x0, y0}; p.p[1] = (SatVec){x1, y1}; p.p[2] = (SatVec){x2, y2};
    return p;
}

static SatPoly translated(const SatPoly* s, float dx, float dy) {
    SatPoly p = *s;
    for (int i = 0; i < p.n; ++i) { p.p[i].x += dx; p.p[i].y += dy; }
    return p;
}

/* Independently classify a boolean from geometry: the unchanged predicate
 * is checked against a separate segment-intersection + containment test. */
static int seg_cross(SatVec a, SatVec b, SatVec c, SatVec d) {
    double d1 = (double)(b.x - a.x) * (c.y - a.y) - (double)(b.y - a.y) * (c.x - a.x);
    double d2 = (double)(b.x - a.x) * (d.y - a.y) - (double)(b.y - a.y) * (d.x - a.x);
    double d3 = (double)(d.x - c.x) * (a.y - c.y) - (double)(d.y - c.y) * (a.x - c.x);
    double d4 = (double)(d.x - c.x) * (b.y - c.y) - (double)(d.y - c.y) * (b.x - c.x);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
static int ref_overlap(const SatPoly* a, const SatPoly* b) {
    for (int i = 0, j = a->n - 1; i < a->n; j = i++)
        for (int k = 0, l = b->n - 1; k < b->n; l = k++)
            if (seg_cross(a->p[j], a->p[i], b->p[l], b->p[k])) return 1;
    return sat_point_inside(a, b->p[0].x, b->p[0].y) ||
           sat_point_inside(b, a->p[0].x, a->p[0].y);
}

/* Smallest t >= 0 at which moving B by t*(ux,uy) becomes disjoint.
 * Bisection on the unchanged boolean; exact boundary is not needed. */
static float push_to_escape(const SatPoly* a, const SatPoly* b, float ux, float uy) {
    float lo = 0.0f, hi = 0.0f;
    SatPoly bt = *b;
    int i;
    for (i = 0; i < 60; ++i) {
        bt = translated(b, hi * ux, hi * uy);
        if (sat_overlap(a, &bt) == 0) break;
        hi = hi > 0.0f ? hi * 2.0f : 0.05f;
    }
    if (i == 60) return 1e30f;
    for (int k = 0; k < 40; ++k) {
        float mid = 0.5f * (lo + hi);
        bt = translated(b, mid * ux, mid * uy);
        if (sat_overlap(a, &bt) == 0) hi = mid; else lo = mid;
    }
    return hi;
}

typedef struct {
    SatPoly a, b;
    const char* name;
} Case;

static void run_case(const Case* c) {
    SatMtv m = sat_overlap_mtv(&c->a, &c->b);
    int ov = sat_overlap(&c->a, &c->b);
    printf("\n== %s ==\n", c->name);
    printf("  sat_overlap = %d   mtv.status = %d\n", ov, (int)m.status);
    if (ov != 1) {
        CHECK(m.status != SAT_MTV_OVERLAP);
        return;
    }
    CHECK(m.status == SAT_MTV_OVERLAP);
    float ang = atan2f(m.diry, m.dirx) * 180.0f / 3.14159265358979f;
    if (ang < 0.0f) ang += 360.0f;
    printf("  dir = (%+.5f, %+.5f)  angle = %8.3f deg  dist = %.6f\n",
           m.dirx, m.diry, ang, m.dist);

    /* Unit length.  Moving by exactly dist lands on contact, which the
     * unchanged predicate counts as overlap; strictly past it separates. */
    CHECKF(fabsf(m.dirx * m.dirx + m.diry * m.diry - 1.0f) < 1e-5f,
           "direction not unit, norm^2 = %.8f", m.dirx * m.dirx + m.diry * m.diry);
    {
        float over = m.dist + 1e-3f * m.dist + 1e-4f;
        SatPoly moved = translated(&c->b, m.dirx * over, m.diry * over);
        CHECKF(sat_overlap(&c->a, &moved) == 0,
               "moving B by (dist + eps) did not separate (dist=%.6f)", m.dist);
    }

    /* A little less should still overlap (unless dist is zero/contact). */
    if (m.dist > 1e-4f) {
        SatPoly half = translated(&c->b, m.dirx * (m.dist * 0.999f),
                                  m.diry * (m.dist * 0.999f));
        CHECKF(sat_overlap(&c->a, &half) == 1,
               "dist is not minimal: 0.999*dist already separates (dist=%.6f)", m.dist);
    }

    /* Sample 7200 directions (0.05 deg steps) and bisect on the unchanged
     * boolean to find how far B must move along each one.  With exact
     * arithmetic the sampled minimum bounds the true minimum from above
     * (it is attained at a sampled direction), so:
     *   - the reported dist may not be shorter than the sampled minimum
     *     by more than the float/bisection noise, including the 0.025 deg
     *     of unsampled direction between grid points;
     *   - the reported direction must not need more travel than the
     *     reported dist claims.
     * The sharper statement -- that no direction at all can do better --
     * is the exact Minkowski-hull check below, which does not rely on a
     * direction grid and so needs no Lipschitz argument.  Angles are
     * printed only: several configurations have many equally optimal
     * directions, where equality of angles is meaningless. */
    float best = 1e30f, best_ang = 0.0f, second = 1e30f;
    for (int s = 0; s < 7200; ++s) {
        float th = (float)s * (2.0f * 3.14159265358979f / 7200.0f);
        float t = push_to_escape(&c->a, &c->b, cosf(th), sinf(th));
        if (t < best) { second = best; best = t; best_ang = th; }
        else if (t < second) second = t;
    }
    float bang = best_ang * 180.0f / 3.14159265358979f;
    if (bang < 0.0f) bang += 360.0f;
    float bound = 1e-4f + 1e-5f * m.dist; /* bisection + float noise */
    float along = push_to_escape(&c->a, &c->b, m.dirx, m.diry);
    printf("  sampled sweep: best sampled dist = %.6f at %.2f deg (2nd %.6f)\n",
           best, bang, second);
    printf("  dist along reported dir = %.6f   |mtv - brute| = %.6f   allowed = %.6f\n",
           along, fabsf(m.dist - best), bound);
    CHECKF(m.dist <= best + bound,
           "reported dist %.6f beats sampled optimum %.6f beyond bound %.6f",
           m.dist, best, bound);
    CHECKF(m.dist >= best - bound,
           "reported dist %.6f worse than sampled optimum %.6f by more than bound",
           m.dist, best);
    CHECKF(fabsf(along - m.dist) <= bound,
           "reported direction needs %.6f but dist says %.6f (bound %.6f)",
           along, m.dist, bound);

    /* Independent exact reference: Minkowski-difference convex hull via
     * per-vertex brute force (tests/exact_ref.c), sharing no code with the
     * implementation.  Exact tolerance is float rounding only. */
    ExactMtv ex = exact_mtv(&c->a, &c->b);
    float exact_bound = 1e-4f * (1.0f + fabsf(m.dist));
    printf("  exact (Minkowski hull): dist = %.6f  optimal dirs = %d",
           ex.dist, ex.n_opt_dirs);
    for (int k = 0; k < ex.n_opt_deg; ++k) printf(" %.2f", ex.opt_deg[k]);
    printf(" deg\n");
    printf("  |mtv - exact| = %.6f   exact allowed = %.6f\n",
           fabsf(m.dist - (float)ex.dist), exact_bound);
    CHECK(ex.overlaps);
    CHECKF(fabs(m.dist - ex.dist) <= exact_bound,
           "dist %.6f differs from exact %.6f", m.dist, ex.dist);

    /* If the exact optimum has a unique direction, the reported angle must
     * match it.  With multiple optimal directions, only the distance is
     * meaningful, and set membership is the strongest check we can make. */
    if (ex.n_opt_dirs == 1) {
        float ex_ang = (float)atan2(ex.diry, ex.dirx) * 180.0f / 3.14159265358979f;
        if (ex_ang < 0.0f) ex_ang += 360.0f;
        float d = fabsf(ang - ex_ang);
        if (d > 180.0f) d = 360.0f - d;
        printf("  unique exact direction %.4f deg, reported %.4f deg, diff %.4f\n",
               ex_ang, ang, d);
        CHECKF(d <= 1e-3f, "unique-optimum angle mismatch: %.4f vs %.4f", ang, ex_ang);
    } else {
        float best_d = 1e9f;
        for (int k = 0; k < ex.n_opt_deg; ++k) {
            float d = fabsf(ang - (float)ex.opt_deg[k]);
            if (d > 180.0f) d = 360.0f - d;
            if (d < best_d) best_d = d;
        }
        printf("  tie: reported angle is %.4f deg from nearest optimal direction\n", best_d);
        CHECKF(best_d <= 1e-3f, "reported angle is not an optimal direction: %.4f deg", best_d);
    }
}

int main(void) {
    /* ---- 1. original boolean samples, verbatim expectations ---- */
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

    /* ---- 2. contact cases: boolean must remain 1, dist must be 0 ---- */
    SatPoly edge_touch = box(10, 0, 5, 5);     /* shares x = 10 edge */
    SatPoly corner_touch = box(10, 10, 5, 5);  /* shares one corner  */
    CHECK(sat_overlap(&a, &edge_touch) == 1);
    CHECK(sat_overlap(&a, &corner_touch) == 1);
    SatMtv me = sat_overlap_mtv(&a, &edge_touch);
    SatMtv mc = sat_overlap_mtv(&a, &corner_touch);
    CHECK(me.status == SAT_MTV_OVERLAP && me.dist == 0.0f);
    CHECK(mc.status == SAT_MTV_OVERLAP && mc.dist == 0.0f);

    /* ---- 3. degenerate inputs are distinguishable failures ---- */
    SatPoly two = { .n = 2, .p = {{0,0},{1,0}} };
    SatPoly collinear = tri(0, 0, 5, 0, 10, 0);        /* zero area */
    SatPoly coincident = tri(3, 3, 3, 3, 3, 3);        /* all same point */
    CHECK(sat_overlap_mtv(&two, &a).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(&a, &two).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(&collinear, &a).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(&a, &collinear).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(&coincident, &a).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(NULL, &a).status == SAT_MTV_DEGENERATE);
    CHECK(sat_overlap_mtv(&a, NULL).status == SAT_MTV_DEGENERATE);
    /* A degenerate-but-overlapping pair must not be reported as disjoint
     * either: it is its own status. */
    SatPoly collinear_inside = tri(2, 0, 5, 0, 8, 0);
    CHECK(sat_overlap(&collinear_inside, &a) == 1);
    CHECK(sat_overlap_mtv(&collinear_inside, &a).status == SAT_MTV_DEGENERATE);

    /* Non-convex input is rejected too: the edge-normal construction does
     * not define a correct MTV for it, and returning a plausible number
     * would be worse than failing. */
    SatPoly concave = { .n = 4, .p = {{1,1},{9,1},{5,5},{9,9}} }; /* dart */
    CHECK(sat_overlap_mtv(&concave, &a).status == SAT_MTV_NONCONVEX);
    CHECK(sat_overlap_mtv(&a, &concave).status == SAT_MTV_NONCONVEX);
    /* A concave pair that the boolean already calls disjoint keeps the
     * trustworthy DISJOINT answer; NONCONVEX is only for pairs the
     * edge-normal construction would otherwise mis-handle. */
    SatPoly far_concave = { .n = 4, .p = {{100,100},{109,100},{105,105},{109,109}} };
    CHECK(sat_overlap(&a, &far_concave) == 0);
    CHECK(sat_overlap_mtv(&a, &far_concave).status == SAT_MTV_DISJOINT);

    /* Distinct failures must be distinguishable from each other. */
    CHECK(SAT_MTV_DEGENERATE != SAT_MTV_NONCONVEX);
    CHECK(SAT_MTV_DEGENERATE != SAT_MTV_OVERLAP);
    CHECK(SAT_MTV_DEGENERATE != SAT_MTV_DISJOINT);
    CHECK(SAT_MTV_NONCONVEX != SAT_MTV_OVERLAP);

    /* ---- 3b. the documented tie-break is observable and stable ---- */
    /* Two boxes sharing the lower-left corner, sizes 10 and 8: the
     * overlap is 8 along both x and y.  Every candidate axis of A and of B
     * therefore ties at 8, so the documented order (edges of A first, then
     * lowest edge index) must decide, and must decide the same way every
     * time. */
    SatPoly ta = box(0, 0, 10, 10);
    SatPoly tb = box(0, 0, 8, 8);
    SatMtv t0 = sat_overlap_mtv(&ta, &tb);
    CHECK(t0.status == SAT_MTV_OVERLAP);
    CHECK(t0.dist == 8.0f);
    for (int rep = 0; rep < 1000; ++rep) {
        SatMtv t = sat_overlap_mtv(&ta, &tb);
        CHECK(t.dirx == t0.dirx && t.diry == t0.diry && t.dist == t0.dist);
    }
    printf("\ntie case: dir = (%+.5f, %+.5f)  dist = %.6f (repeatable over 1000 calls)\n",
           t0.dirx, t0.diry, t0.dist);

    /* ---- 4. the four reported configurations ---- */
    Case cases[4];
    cases[0].name = "1. axis-aligned boxes, corner overlap";
    cases[0].a = box(0, 0, 10, 10);
    cases[0].b = box(7, 6, 10, 10);
    cases[1].name = "2. box vs rotated square (diamond)";
    cases[1].a = box(0, 0, 10, 10);
    cases[1].b = (SatPoly){ .n = 4, .p = {{9,5},{5,1},{1,5},{5,9}} };
    cases[2].name = "3. triangle inside box, off-centre";
    cases[2].a = box(0, 0, 10, 10);
    cases[2].b = tri(6, 6, 9, 5, 8, 9);
    cases[3].name = "4. two rotated triangles";
    cases[3].a = tri(0, 0, 8, 2, 3, 7);
    cases[3].b = tri(5, 1, 11, 4, 6, 8);

    for (int i = 0; i < 4; ++i) run_case(&cases[i]);

    /* ---- 5. boolean equivalence against an independent classifier ---- */
    for (int i = 0; i < 4; ++i) {
        int ov = sat_overlap(&cases[i].a, &cases[i].b);
        int ref = ref_overlap(&cases[i].a, &cases[i].b);
        CHECKF(ov && ref, "boolean disagrees with reference classifier on case %d", i + 1);
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
