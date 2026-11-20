#include "../src/sat.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
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

    /* regression pins for the pre-existing polygon routines: the
     * circle/polygon work must not change them */
    {
        SatPoly c1 = box(10, 0, 5, 5), c2 = box(15, 0, 5, 5);
        SatPoly far = box(15.5, 0, 2, 2);
        CHECK(sat_overlap(&c1, &c2) == 1);      /* touching edges */
        CHECK(sat_overlap(&c1, &far) == 0);
        /* half-open boundary rule of sat_point_inside */
        CHECK(sat_point_inside(&a, 0.0f, 0.0f) == 1);
        CHECK(sat_point_inside(&a, 10.0f, 5.0f) == 0);
        CHECK(sat_point_inside(&a, 5.0f, 10.0f) == 0);
        CHECK(sat_overlap(NULL, &a) == -1);
        CHECK(sat_overlap(&a, &b) == 1);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
