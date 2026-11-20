/* Circle/polygon overlap: exact status triple {outside, intersecting,
 * contained}, analytic area, and degenerate-input contracts.
 *
 * Exit status 0 when every check passes.  Also prints a timing row for the
 * four fixture configurations.
 */
#include "../src/sat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y};     p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

static SatPoly ngon(int n, float cx, float cy, float rr, float phase) {
    SatPoly p; int i;
    if (n > 16) n = 16;
    p.n = n;
    for (i = 0; i < n; ++i) {
        double a = phase + 2.0 * 3.14159265358979323846 * i / n;
        p.p[i].x = (float)(cx + rr * cos(a));
        p.p[i].y = (float)(cy + rr * sin(a));
    }
    return p;
}

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

static void report(const char* name, const SatPoly* p, double cx, double cy,
                   double r) {
    SatCirclePoly res;
    sat_circle_polygon(p, cx, cy, r, &res);
    printf("  %-28s status=%-2d contained=%+d area=%.12f\n",
           name, res.status, res.contained, res.area);
}

int main(void) {
    const double PI = 3.14159265358979323846;
    SatPoly sq = box(0, 0, 10, 10);
    SatCirclePoly res;

    /* --- three-state classification --- */
    CHECK(sat_circle_polygon(&sq, 20, 5, 2, &res) == SAT_CIRCLE_OUTSIDE);
    CHECK(res.contained == 0 && res.area == 0.0);

    CHECK(sat_circle_polygon(&sq, 5, 5, 1, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.contained == +1);
    CHECK(near(res.area, PI, 1e-15));

    CHECK(sat_circle_polygon(&sq, 5, 5, 8, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.contained == -1);
    CHECK(near(res.area, 100.0, 1e-15));

    CHECK(sat_circle_polygon(&sq, 10, 5, 4, &res) == SAT_CIRCLE_INTERSECTING);
    CHECK(res.contained == 0);
    CHECK(near(res.area, 0.5 * PI * 16, 1e-15));

    /* --- analytic areas with closed-form expected values --- */
    /* centre on a corner, radius 3 -> quarter disk */
    CHECK(sat_circle_polygon(&sq, 0, 0, 3, &res) == SAT_CIRCLE_INTERSECTING);
    CHECK(near(res.area, 0.25 * PI * 9, 1e-15));
    /* centre on an edge -> half disk */
    CHECK(sat_circle_polygon(&sq, 0, 5, 2, &res) == SAT_CIRCLE_INTERSECTING);
    CHECK(near(res.area, 0.5 * PI * 4, 1e-15));
    /* centre inside, circle crosses all four edges: disk minus four caps */
    {
        double d = 5.0, r = 7.0;           /* caps beyond the sides */
        double half = r * r * acos(d / r) - d * sqrt(r * r - d * d);
        double expect = PI * r * r - 4.0 * half;
        CHECK(sat_circle_polygon(&sq, 5, 5, r, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(near(res.area, expect, 1e-13));
    }
    /* internal tangency: disk inscribed in the square, all four sides touch */
    CHECK(sat_circle_polygon(&sq, 5, 5, 5, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.contained == +1);
    CHECK(near(res.area, PI * 25, 1e-13));
    /* external tangency: closed sets touch, overlap area 0, reported OUTSIDE */
    CHECK(sat_circle_polygon(&sq, 12, 5, 2, &res) == SAT_CIRCLE_OUTSIDE);
    CHECK(res.area == 0.0);
    /* centre exactly on the boundary: INTERSECTING, exactly half the disk */
    CHECK(sat_circle_polygon(&sq, 10, 5, 3, &res) == SAT_CIRCLE_INTERSECTING);
    CHECK(near(res.area, 0.5 * PI * 9, 1e-15));

    /* --- clockwise winding: same area (magnitude is orientation-free) --- */
    {
        SatPoly cw; cw.n = 4;
        cw.p[0] = (SatVec){0, 0}; cw.p[1] = (SatVec){0, 10};
        cw.p[2] = (SatVec){10, 10}; cw.p[3] = (SatVec){10, 0};
        CHECK(sat_circle_polygon(&cw, 10, 5, 4, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(near(res.area, 0.5 * PI * 16, 1e-15));
        CHECK(res.poly_area < 0.0);
    }

    /* --- non-convex polygon (L shape) --- */
    {
        SatPoly L; L.n = 6;
        L.p[0] = (SatVec){0, 0};   L.p[1] = (SatVec){10, 0};
        L.p[2] = (SatVec){10, 4};  L.p[3] = (SatVec){4, 4};
        L.p[4] = (SatVec){4, 10};  L.p[5] = (SatVec){0, 10};
        /* disk centred on the left edge at the junction, r = 4: for x in
         * [0,4] the disk is inside the full-height arm, for x < 0 it is
         * outside, so the overlap is exactly the half disk 8*pi */
        CHECK(sat_circle_polygon(&L, 0, 4, 4, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(near(res.area, 8.0 * PI, 1e-13));
        /* placed in the reflex notch region (outside the polygon) */
        CHECK(sat_circle_polygon(&L, 7, 7, 1, &res) == SAT_CIRCLE_OUTSIDE);
        CHECK(res.area == 0.0);
        /* disk centred at the reflex corner: the half with x <= 4 lies in
         * the full-height arm (half disk); the half with x > 4 is clipped to
         * y <= 4 (quarter disk); total 3*pi */
        CHECK(sat_circle_polygon(&L, 4, 4, 2, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(near(res.area, 3.0 * PI, 1e-13));
    }

    /* --- 16-gon: small disk well inside (CONTAINED), and a generic
     *     intersecting placement (checked against the quad reference in
     *     tests/verify_circle.py) --- */
    {
        SatPoly g = ngon(16, 0, 0, 5.0, 0.0);
        /* inradius 4.9039, centre 1.4765 from the origin, r = 1.4:
         * the closed disk cannot reach any edge */
        CHECK(sat_circle_polygon(&g, 1.3, -0.7, 1.4, &res) == SAT_CIRCLE_CONTAINED);
        CHECK(res.contained == +1);
        CHECK(near(res.area, PI * 1.4 * 1.4, 1e-13));
        /* crossing the polygon boundary (16-gon is not convex relative to
         * this placement?  it is convex, so crossing is well defined) */
        CHECK(sat_circle_polygon(&g, 4.0, 0.0, 1.4, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(res.area > 0.0 && res.area < PI * 1.4 * 1.4);
        /* disk around the whole 16-gon */
        CHECK(sat_circle_polygon(&g, 0, 0, 9.0, &res) == SAT_CIRCLE_CONTAINED);
        CHECK(res.contained == -1);
    }

    /* --- radius zero --- */
    CHECK(sat_circle_polygon(&sq, 5, 5, 0, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.contained == +1 && res.area == 0.0);
    CHECK(sat_circle_polygon(&sq, 0, 0, 0, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.area == 0.0);
    CHECK(sat_circle_polygon(&sq, 10, 5, 0, &res) == SAT_CIRCLE_CONTAINED);
    CHECK(res.area == 0.0);
    CHECK(sat_circle_polygon(&sq, 15, 5, 0, &res) == SAT_CIRCLE_OUTSIDE);
    CHECK(res.area == 0.0 && res.contained == 0);

    /* --- degenerate polygons --- */
    {
        SatPoly d; d.n = 4;
        d.p[0] = (SatVec){0, 0}; d.p[1] = (SatVec){1, 0};
        d.p[2] = (SatVec){2, 0}; d.p[3] = (SatVec){1, 0};
        CHECK(sat_circle_polygon(&d, 1, 0, 1, &res) == SAT_CIRCLE_POLY_DEGENERATE);
        CHECK(res.area == 0.0);
        /* clearly non-degenerate thin triangle: must NOT be called degenerate */
        d.n = 3;
        d.p[0] = (SatVec){0, 0}; d.p[1] = (SatVec){2, 0};
        d.p[2] = (SatVec){1, 0.001f};
        CHECK(sat_circle_polygon(&d, 1, 0, 0.5, &res) == SAT_CIRCLE_INTERSECTING);
        CHECK(res.area > 0.0);
        /* all vertices coincident */
        d.n = 3; d.p[0] = d.p[1] = d.p[2] = (SatVec){3, 3};
        CHECK(sat_circle_polygon(&d, 3, 3, 1, &res) == SAT_CIRCLE_POLY_DEGENERATE);
    }
    {
        SatPoly d; d.n = 2;
        d.p[0] = (SatVec){0, 0}; d.p[1] = (SatVec){1, 1};
        CHECK(sat_circle_polygon(&d, 0, 0, 1, &res) == SAT_CIRCLE_POLY_EMPTY);
        CHECK(res.area == 0.0);
    }

    /* --- invalid input --- */
    CHECK(sat_circle_polygon(&sq, 0, 0, -1, &res) == SAT_CIRCLE_EINVAL);
    CHECK(res.area == 0.0);
    CHECK(sat_circle_polygon(NULL, 0, 0, 1, &res) == SAT_CIRCLE_EINVAL);
    CHECK(sat_circle_polygon(&sq, NAN, 0, 1, &res) == SAT_CIRCLE_EINVAL);
    CHECK(sat_circle_polygon(&sq, 0, INFINITY, 1, &res) == SAT_CIRCLE_EINVAL);
    CHECK(sat_circle_polygon(&sq, 0, 0, 1, NULL) == SAT_CIRCLE_EINVAL);
    CHECK(sat_circle_polygon_area(&sq, 0, 0, -1) == 0.0);

    /* --- original polygon API must be untouched --- */
    {
        SatPoly b = box(5, 5, 10, 10), c = box(20, 20, 5, 5);
        SatPoly two = sq; two.n = 2;
        CHECK(sat_overlap(&sq, &b) == 1);
        CHECK(sat_overlap(&sq, &c) == 0);
        CHECK(sat_overlap(&sq, &sq) == 1);
        CHECK(sat_overlap(&two, &sq) == -1);
        CHECK(sat_point_inside(&sq, 5, 5) == 1);
        CHECK(sat_point_inside(&sq, 50, 5) == 0);
        /* half-open boundary rule: pinned so it cannot drift */
        CHECK(sat_point_inside(&sq, 0, 0) == 1);
        CHECK(sat_point_inside(&sq, 10, 5) == 0);
        CHECK(sat_point_inside(&sq, 5, 10) == 0);
        {
            float cx, cy;
            sat_centroid(&sq, &cx, &cy);
            CHECK(near(cx, 5.0, 1e-6) && near(cy, 5.0, 1e-6));
        }
    }

    /* --- four configurations: status, analytic area ---- */
    printf("\nfour configurations (10x10 square):\n");
    report("outside (20,5) r=2", &sq, 20, 5, 2);
    report("contained, disk (5,5) r=1", &sq, 5, 5, 1);
    report("intersecting (10,5) r=4", &sq, 10, 5, 4);
    report("intersecting corner (0,0) r=3", &sq, 0, 0, 3);

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
