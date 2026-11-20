#include "../src/xform.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

struct Case {
    const char* name;
    SpriteAffine fwd;
    SpriteAffine inv_hand; /* hand-derived inverse */
    double cond_hand;
    double det_hand;
};

static void make_cases(Case c[4]) {
    /* A: pure translation. Identity linear block, det 1, cond 1.
     * inverse: translate by (-10, 5). */
    c[0].name = "A translate";
    c[0].fwd = {{1.0f, 0.0f, 0.0f, 1.0f, 10.0f, -5.0f}};
    c[0].inv_hand = {{1.0f, 0.0f, 0.0f, 1.0f, -10.0f, 5.0f}};
    c[0].cond_hand = 1.0;
    c[0].det_hand = 1.0;

    /* B: anisotropic scale (2, 0.5) then translate (-3, 7).
     * A = diag(2, 0.5), det 1, sigma {2, 0.5}, cond 4.
     * A^-1 = diag(0.5, 2), translation -A^-1 t = (1.5, -14). */
    c[1].name = "B aniso scale";
    c[1].fwd = xform_affine_from_scale(2.0f, 0.5f, -3.0f, 7.0f);
    c[1].inv_hand = xform_affine_from_scale(0.5f, 2.0f, 1.5f, -14.0f);
    c[1].cond_hand = 4.0;
    c[1].det_hand = 1.0;

    /* C: rotate 30 deg, uniform scale 1.5, translate (4, -2).
     * A = [1.299038 -0.75; 0.75 1.299038], det 2.25, cond 1.
     * A^-1 = [0.5773503 0.3333333; -0.3333333 0.5773503],
     * -A^-1 t = (-1.6427344, 2.4880339). */
    c[2].name = "C rotate+scale";
    c[2].fwd = xform_affine_trs(3.14159265358979323846 / 6.0, 1.5f, 4.0f, -2.0f);
    c[2].inv_hand = {{0.5773503f, -0.3333333f, 0.3333333f, 0.5773503f,
                      -1.6427344f, 2.4880339f}};
    c[2].cond_hand = 1.0;
    c[2].det_hand = 2.25;

    /* D: near-singular shear A = [1 1; 1 1+1/1024].  Every input entry is a
     * float and so are all six inverse entries, making the hand result exact.
     * det 1/1024, sigma {2.0004884, 4.8816e-4}, cond 4098.
     * A^-1 = [1025 -1024; -1024 1024], -A^-1 t = (-3074, 3072). */
    c[3].name = "D near-singular";
    c[3].fwd = {{1.0f, 1.0f, 1.0f, 1.0009765625f, 2.0f, -1.0f}};
    c[3].inv_hand = {{1025.0f, -1024.0f, -1024.0f, 1024.0f, -3074.0f, 3072.0f}};
    c[3].cond_hand = 4098.0007;
    c[3].det_hand = 1.0 / 1024.0;
}

static const char* field_of(const XformReport& r) { return r.error_field ? r.error_field : "-"; }

int main() {
    SpritePoint grid[4] = {{0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 1.0f}, {0.0f, 1.0f}};
    Case cases[4];
    make_cases(cases);

    /* ---------- inverse off: results identical to the old behaviour ---------- */
    {
        SpritePoint a[4], b[4];
        CHECK(xform_sprite(grid, 4, 2.0f, 3.0f, 1.0f, -1.0f, a) == 0);
        XformReport r;
        CHECK(xform_sprite_ex(grid, 4, 2.0f, 3.0f, 1.0f, -1.0f, 0, 0.0, b, &r) == 0);
        CHECK(std::memcmp(a, b, sizeof(a)) == 0);
        CHECK(r.status == XFORM_OK && r.error_field == NULL);

        /* Legacy bounds check from the original suite. */
        float mnx, mny, mxx, mxy;
        CHECK(xform_bounds(a, 4, &mnx, &mny, &mxx, &mxy) == 0);
        CHECK(mnx == 1.0f && mny == -1.0f && mxx == 5.0f && mxy == 2.0f);

        /* Legacy forward error contract. */
        CHECK(xform_sprite_ex(grid, 0, 1, 1, 0, 0, 0, 0.0, b, &r) == -1);
        CHECK(std::strcmp(field_of(r), "n") == 0);
        CHECK(xform_sprite_ex(NULL, 4, 1, 1, 0, 0, 0, 0.0, b, &r) == -1);
        CHECK(std::strcmp(field_of(r), "src") == 0);
        CHECK(xform_sprite_ex(grid, 4, 1, 1, 0, 0, 0, 0.0, NULL, &r) == -1);
        CHECK(std::strcmp(field_of(r), "dst") == 0);

        /* NaN passes straight through forward mode, same as xform_sprite. */
        SpritePoint nanp[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
        SpritePoint nan_out[4];
        xform_sprite(nanp, 4, std::numeric_limits<float>::quiet_NaN(), 1, 0, 0, a);
        CHECK(xform_sprite_ex(nanp, 4, std::numeric_limits<float>::quiet_NaN(), 1, 0, 0,
                              0, 0.0, nan_out, &r) == 0);
        CHECK(std::memcmp(a, nan_out, sizeof(a)) == 0);
        std::printf("inverse off: bit-identical to xform_sprite (including NaN in)\n");
    }

    /* ---------- stages 1..3 across the four affines ---------- */
    std::printf("\n== four affines ==\n");
    std::printf("%-16s %10s %10s %12s %12s %12s\n",
                "case", "cond", "det", "max res", "rms res", "max rel err");
    double fwd_ns[4] = {0}, inv_ns[4] = {0};
    const int kIters = 100000;

    for (int k = 0; k < 4; ++k) {
        const Case& c = cases[k];
        XformReport r;

        /* Stage 1: split linear block and translation. */
        SpriteLinear lin;
        double t[2];
        CHECK(xform_split(&c.fwd, &lin, t, &r) == XFORM_OK);
        CHECK(lin.m[0] == c.fwd.m[0] && lin.m[1] == c.fwd.m[1] &&
              lin.m[2] == c.fwd.m[2] && lin.m[3] == c.fwd.m[3]);
        CHECK(t[0] == c.fwd.m[4] && t[1] == c.fwd.m[5]);

        /* Stage 2: compose inverse, compare with hand derivation. */
        SpriteAffine inv;
        const double cond_max = XFORM_COND_MAX_DEFAULT;
        int st = xform_compose_inverse(&c.fwd, cond_max, &inv, &r);
        CHECK(st == XFORM_OK); /* all four are below the default cap */
        CHECK(r.degraded == 0);
        CHECK(near(r.det, c.det_hand, 1e-6 * std::fabs(c.det_hand) + 1e-12));
        CHECK(near(r.cond, c.cond_hand, 1e-3 * c.cond_hand));
        const double det_case = r.det, cond_case = r.cond;

        double scale = 0.0;
        for (int i = 0; i < 6; ++i)
            scale = std::fmax(scale, std::fabs((double)c.inv_hand.m[i]));
        const double tol = 1e-5 * scale + 1e-7;
        double worst = 0.0;
        for (int i = 0; i < 6; ++i) {
            const double d = std::fabs((double)inv.m[i] - c.inv_hand.m[i]);
            if (d > worst) worst = d;
            CHECK(d <= tol);
        }

        /* Stage 3: round trip over the grid. */
        double mx = 0.0, rms = 0.0;
        CHECK(xform_check_roundtrip(&c.fwd, &inv, grid, 4, &mx, &rms, &r) == XFORM_OK);
        if (c.cond_hand < 10.0) {
            CHECK(mx < 1e-5);
        } else {
            /* Near-singular: float inverse entries ~1e4 amplify rounding. */
            CHECK(mx < 2e-2);
        }
        std::printf("%-16s %10.6g %10.6g %12.3g %12.3g %12.2e\n",
                    c.name, cond_case, det_case, mx, rms, worst);

        /* Timing, forward vs inverse, per point. */
        SpritePoint fwdp[4], back[4];
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i)
            xform_affine_apply(&c.fwd, grid, 4, 0, 0.0, fwdp, NULL);
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i)
            xform_affine_apply(&c.fwd, fwdp, 4, 1, cond_max, back, NULL);
        auto t2 = std::chrono::steady_clock::now();
        fwd_ns[k] = std::chrono::duration<double, std::nano>(t1 - t0).count() / (kIters * 4.0);
        inv_ns[k] = std::chrono::duration<double, std::nano>(t2 - t1).count() / (kIters * 4.0);

        /* The point transform really does recover the input. */
        for (int i = 0; i < 4; ++i) {
            const double e = std::hypot((double)back[i].x - grid[i].x,
                                        (double)back[i].y - grid[i].y);
            if (c.cond_hand < 10.0) CHECK(e < 1e-5);
        }
    }

    std::printf("\n%-16s %12s %12s\n", "case", "fwd ns/pt", "inv ns/pt");
    for (int k = 0; k < 4; ++k)
        std::printf("%-16s %12.2f %12.2f\n", cases[k].name, fwd_ns[k], inv_ns[k]);

    /* ---------- degradation above the condition cap ---------- */
    {
        std::printf("\n== degradation ==\n");
        SpriteAffine inv;
        XformReport r;

        /* D at its measured cond (~4.1e3) degrades when the cap is 1e3. */
        int st = xform_compose_inverse(&cases[3].fwd, 1.0e3, &inv, &r);
        CHECK(st == XFORM_DEGRADED);
        CHECK(r.degraded == 1);
        CHECK(r.status == XFORM_DEGRADED);
        CHECK(r.cond >= 1.0e3);
        double big = 0.0;
        for (int i = 0; i < 6; ++i) {
            CHECK(std::isfinite(inv.m[i]));
            big = std::fmax(big, std::fabs((double)inv.m[i]));
        }
        CHECK(big < 1.0); /* bounded, not the ~3e3 divergent adjugate */
        std::printf("  D cond=%.6g cap=1e3 -> status=%d degraded=%d max|inv|=%.6g "
                    "(adjugate ~3e3)\n", r.cond, st, r.degraded, big);
        CHECK(std::strstr(r.message, "exceeds cond_max") != NULL);

        /* Same through the point transform: finite output, reported loss. */
        SpritePoint src[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
        SpritePoint out[4];
        st = xform_affine_apply(&cases[3].fwd, src, 4, 1, 1.0e3, out, &r);
        CHECK(st == XFORM_DEGRADED);
        CHECK(r.degraded == 1);
        for (int i = 0; i < 4; ++i)
            CHECK(std::isfinite(out[i].x) && std::isfinite(out[i].y));
        CHECK(r.max_residual > 1e-3); /* loss is real and reported */
        std::printf("  degraded round trip max residual = %.6g (lossy by design)\n",
                    r.max_residual);

        /* A true rank-one matrix has det exactly 0: that is the det-zero
         * error, not a degradation. */
        SpriteAffine rank1 = {{1, 1, 1, 1, 0, 0}};
        st = xform_compose_inverse(&rank1, 1.0e3, &inv, &r);
        CHECK(st == XFORM_ERR_DET_ZERO);
        CHECK(std::strcmp(field_of(r), "det") == 0);
        std::printf("  rank-one matrix cond=%.6g -> status=%d field=%s\n",
                    r.cond, st, field_of(r));

        /* Thresholds validate themselves. */
        SpriteAffine ok = xform_affine_from_scale(2, 2, 0, 0);
        CHECK(xform_compose_inverse(&ok, std::numeric_limits<double>::quiet_NaN(), &inv, &r)
              == XFORM_ERR_THRESHOLD);
        CHECK(std::strcmp(field_of(r), "cond_max") == 0);
        CHECK(xform_compose_inverse(&ok, -1.0, &inv, &r) == XFORM_OK); /* 0/-1 -> default */
    }

    /* ---------- error reporting: which item failed ---------- */
    {
        std::printf("\n== error reporting ==\n");
        XformReport r;
        SpriteAffine inv;

        SpriteAffine singular = xform_affine_from_scale(1.0f, 0.0f, 3.0f, 4.0f);
        CHECK(xform_compose_inverse(&singular, 0.0, &inv, &r) == XFORM_ERR_DET_ZERO);
        CHECK(std::strcmp(field_of(r), "det") == 0);
        std::printf("  det=0        status=%d field=%s message=\"%s\"\n",
                    r.status, field_of(r), r.message);

        SpriteAffine nan_t = xform_affine_from_scale(1.0f, 1.0f, 0.0f,
                                                     std::numeric_limits<float>::quiet_NaN());
        CHECK(xform_compose_inverse(&nan_t, 0.0, &inv, &r) == XFORM_ERR_NONFINITE);
        CHECK(std::strcmp(field_of(r), "m[5]") == 0);
        std::printf("  NaN ty       status=%d field=%s message=\"%s\"\n",
                    r.status, field_of(r), r.message);

        SpriteAffine inf_a = {{1.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f}};
        CHECK(xform_compose_inverse(&inf_a, 0.0, &inv, &r) == XFORM_ERR_NONFINITE);
        CHECK(std::strcmp(field_of(r), "m[3]") == 0);

        CHECK(xform_compose_inverse(NULL, 0.0, &inv, &r) == XFORM_ERR_NULL);
        CHECK(std::strcmp(field_of(r), "a") == 0);

        SpritePoint out[4] = {{-7, -7}, {-7, -7}, {-7, -7}, {-7, -7}};
        CHECK(xform_sprite_ex(grid, 2, 1, 1, 0, 0, 1, 0.0, out, &r) == XFORM_ERR_POINTS);
        CHECK(std::strcmp(field_of(r), "n") == 0);
        CHECK(out[0].x == -7.0f); /* dst untouched on error */
        std::printf("  n=2 inverse  status=%d field=%s message=\"%s\"\n",
                    r.status, field_of(r), r.message);

        CHECK(xform_sprite_ex(grid, 4, std::numeric_limits<float>::infinity(), 1, 0, 0,
                              1, 0.0, out, &r) == XFORM_ERR_NONFINITE);
        CHECK(std::strcmp(field_of(r), "sx") == 0);
        CHECK(xform_sprite_ex(grid, 4, 1, 1, 0,
                              std::numeric_limits<float>::quiet_NaN(), 1, 0.0, out, &r)
              == XFORM_ERR_NONFINITE);
        CHECK(std::strcmp(field_of(r), "ty") == 0);
        CHECK(xform_sprite_ex(grid, 4, 1, 0, 0, 0, 1, 0.0, out, &r) == XFORM_ERR_DET_ZERO);
        CHECK(std::strcmp(field_of(r), "det") == 0);

        double mx = 0.0, rms = 0.0;
        SpriteAffine id = xform_affine_from_scale(1, 1, 0, 0);
        CHECK(xform_check_roundtrip(&id, &id, grid, 2, &mx, &rms, &r) ==
              XFORM_ERR_POINTS);
        CHECK(std::strcmp(field_of(r), "n") == 0);
        SpritePoint badpt[3] = {{0, 0}, {1, std::numeric_limits<float>::infinity()}, {2, 0}};
        CHECK(xform_check_roundtrip(&id, &id, badpt, 3, &mx, &rms, &r) ==
              XFORM_ERR_NONFINITE);
        CHECK(std::strcmp(field_of(r), "pts") == 0);
        CHECK(xform_check_roundtrip(&id, NULL, grid, 4, &mx, &rms, &r) ==
              XFORM_ERR_NULL);
        CHECK(std::strcmp(field_of(r), "inv") == 0);
    }

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
