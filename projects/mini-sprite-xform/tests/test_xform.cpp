#include "../src/xform.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Deterministic point cloud in [-2, 2]^2 (LCG, no rand() dependence). */
static std::vector<SpritePoint> make_points(int n) {
    std::vector<SpritePoint> v(n);
    unsigned long long s = 12345;
    for (int i = 0; i < n; ++i) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        float x = (float)((s >> 33) & 0xFFFFFF) / (float)0xFFFFFF * 4.0f - 2.0f;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        float y = (float)((s >> 33) & 0xFFFFFF) / (float)0xFFFFFF * 4.0f - 2.0f;
        v[i] = {x, y};
    }
    return v;
}

int main() {
    /* ---- original checks: translate/scale path must be unchanged ---- */
    SpritePoint src[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    SpritePoint dst[4];
    CHECK(xform_sprite(src, 4, 2.0f, 3.0f, 1.0f, -1.0f, dst) == 0);
    CHECK(dst[1].x == 5.0f && dst[1].y == -1.0f);
    float a, b, c, d;
    CHECK(xform_bounds(dst, 4, &a, &b, &c, &d) == 0);
    CHECK(a == 1.0f && b == -1.0f && c == 5.0f && d == 2.0f);
    CHECK(xform_sprite(src, 0, 1, 1, 0, 0, dst) == -1);

    /* ---- bit-identical: xform_sprite body untouched, matches the
     * original float formula bitwise ---- */
    {
        SpritePoint pts[3] = {{0.5f, -1.25f}, {3.75f, 2.5f}, {-2.0f, 0.25f}};
        SpritePoint o1[3], o2[3];
        CHECK(xform_sprite(pts, 3, 1.5f, 0.75f, -3.0f, 2.0f, o1) == 0);
        for (int i = 0; i < 3; ++i) {
            o2[i].x = pts[i].x * 1.5f + -3.0f;
            o2[i].y = pts[i].y * 0.75f + 2.0f;
        }
        CHECK(std::memcmp(o1, o2, sizeof o1) == 0);
    }

    /* ---- rotate(0) is exactly the identity ---- */
    {
        Affine2D r0 = affine_rotate(0.0), id = affine_identity();
        CHECK(r0.m00 == id.m00 && r0.m01 == id.m01 && r0.m02 == id.m02 &&
              r0.m10 == id.m10 && r0.m11 == id.m11 && r0.m12 == id.m12);
        SpritePoint in[3] = {{1.5f, -2.5f}, {0.25f, 3.75f}, {-4.0f, 0.5f}};
        SpritePoint out[3];
        CHECK(xform_sprite_affine(in, 3, r0, out) == 0);
        CHECK(std::memcmp(in, out, sizeof in) == 0);
    }

    /* ---- degenerate shear (kx*ky == 1 => det == 0): deterministic ---- */
    {
        Affine2D h = affine_shear(1.0, 1.0);
        CHECK(affine_det(h) == 0.0);
        Affine2D inv;
        CHECK(affine_invert(h, &inv) == -1);       /* no inverse, no NaN */
        SpritePoint in[2] = {{1, 2}, {3, 4}};
        SpritePoint out[2];
        CHECK(xform_sprite_affine(in, 2, h, out) == 0); /* forward still defined */
    }

    /* ---- zero points: deterministic results ---- */
    {
        SpritePoint one[1] = {{1, 1}};
        SpritePoint out[1];
        float w, x, y, z;
        Affine2D id = affine_identity();
        CHECK(xform_sprite_affine(one, 0, id, out) == -1);
        CHECK(xform_bounds_affine(one, 0, id, &w, &x, &y, &z) == -1);
        CHECK(xform_signed_area(one, 0) == 0.0);
    }

    /* ---- signed area: sign follows sign(det), magnitude scales by |det| ---- */
    {
        SpritePoint ccw[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
        double a0 = xform_signed_area(ccw, 4);
        CHECK(a0 == 2.0);
        Affine2D refl = affine_scale(-1.0, 1.0);   /* det < 0 */
        SpritePoint t[4];
        CHECK(xform_sprite_affine(ccw, 4, refl, t) == 0);
        double a1 = xform_signed_area(t, 4);
        CHECK(a1 < 0.0);                             /* sign flips */
        CHECK(a1 == affine_det(refl) * a0);          /* exact: integer coords */
        Affine2D m = affine_multiply(affine_translate(5, -3),
                     affine_multiply(affine_rotate(0.6), affine_scale(2.0, 0.5)));
        CHECK(xform_sprite_affine(ccw, 4, m, t) == 0);
        double a2 = xform_signed_area(t, 4);
        CHECK(a2 > 0.0);                             /* det > 0: sign kept */
        CHECK(std::fabs(a2 - affine_det(m) * a0) < 1e-5);
    }

    /* ---- composition order: multiply(outer, inner) applies inner first ---- */
    {
        const double PI = 3.14159265358979323846;
        Affine2D m = affine_multiply(affine_translate(10, 0), affine_rotate(PI / 2));
        SpritePoint in[1] = {{1, 0}}, out[1];
        CHECK(xform_sprite_affine(in, 1, m, out) == 0);
        CHECK(std::fabs(out[0].x - 10.0f) < 1e-6f && std::fabs(out[0].y - 1.0f) < 1e-6f);
    }

    /* ---- four transform groups: round-trip error, bbox error, 1M-point time ---- */
    const double PI = 3.14159265358979323846;
    struct Group { const char* name; Affine2D m; };
    Group groups[4] = {
        {"translate+scale", affine_multiply(affine_translate(3, -2), affine_scale(1.5, 0.75))},
        {"rotate+translate", affine_multiply(affine_translate(2, 1), affine_rotate(PI / 6))},
        {"shear+scale", affine_multiply(affine_scale(2.0, 1.0), affine_shear(0.3, -0.2))},
        {"composite T*R*H*S", affine_multiply(affine_translate(2, -1),
                              affine_multiply(affine_rotate(0.7),
                              affine_multiply(affine_shear(0.25, 0.1), affine_scale(1.2, 0.8))))},
    };

    const int N = 1000000;
    std::vector<SpritePoint> pts = make_points(N);
    std::vector<SpritePoint> fwd(N), rec(N);
    std::vector<SpritePointD> ptsd(N), fwdd(N), recd(N);
    for (int i = 0; i < N; ++i) ptsd[i] = {pts[i].x, pts[i].y};

    std::printf("\n%-20s %12s %12s %12s %12s\n",
                "group", "rt err(dbl)", "rt err(flt)", "bbox err", "1M pts ms");
    for (int g = 0; g < 4; ++g) {
        Affine2D m = groups[g].m, inv;
        CHECK(affine_invert(m, &inv) == 0);

        /* timed: forward affine transform of 1M vertices (float path) */
        auto t0 = std::chrono::steady_clock::now();
        CHECK(xform_sprite_affine(pts.data(), N, m, fwd.data()) == 0);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        /* float-storage round trip (informational: includes 2 float roundings) */
        CHECK(xform_sprite_affine(fwd.data(), N, inv, rec.data()) == 0);
        double rt_f = 0;
        for (int i = 0; i < N; ++i) {
            double ex = std::fabs((double)rec[i].x - pts[i].x);
            double ey = std::fabs((double)rec[i].y - pts[i].y);
            if (ex > rt_f) rt_f = ex;
            if (ey > rt_f) rt_f = ey;
        }

        /* double-precision round trip: the guaranteed < 1e-6 path */
        CHECK(xform_sprite_affine_d(ptsd.data(), N, m, fwdd.data()) == 0);
        CHECK(xform_sprite_affine_d(fwdd.data(), N, inv, recd.data()) == 0);
        double rt_d = 0;
        for (int i = 0; i < N; ++i) {
            double ex = std::fabs(recd[i].x - ptsd[i].x);
            double ey = std::fabs(recd[i].y - ptsd[i].y);
            if (ex > rt_d) rt_d = ex;
            if (ey > rt_d) rt_d = ey;
        }
        CHECK(rt_d < 1e-6);

        /* bbox error vs long double reference over the same vertices */
        float bx0, by0, bx1, by1;
        CHECK(xform_bounds_affine(pts.data(), N, m, &bx0, &by0, &bx1, &by1) == 0);
        long double rx0 = 1e300L, ry0 = 1e300L, rx1 = -1e300L, ry1 = -1e300L;
        for (int i = 0; i < N; ++i) {
            long double x = pts[i].x, y = pts[i].y;
            long double ux = (long double)m.m00 * x + (long double)m.m01 * y + m.m02;
            long double uy = (long double)m.m10 * x + (long double)m.m11 * y + m.m12;
            if (ux < rx0) rx0 = ux;
            if (uy < ry0) ry0 = uy;
            if (ux > rx1) rx1 = ux;
            if (uy > ry1) ry1 = uy;
        }
        double be = std::fabs((double)(bx0 - rx0));
        if (std::fabs((double)(by0 - ry0)) > be) be = std::fabs((double)(by0 - ry0));
        if (std::fabs((double)(bx1 - rx1)) > be) be = std::fabs((double)(bx1 - rx1));
        if (std::fabs((double)(by1 - ry1)) > be) be = std::fabs((double)(by1 - ry1));
        CHECK(be < 1e-5);

        std::printf("%-20s %12.3e %12.3e %12.3e %12.3f\n",
                    groups[g].name, rt_d, rt_f, be, ms);
    }

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
