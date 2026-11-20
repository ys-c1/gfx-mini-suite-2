#include "xform.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

int finite_f(float v) { return std::isfinite(v) ? 1 : 0; }
int finite_d(double v) { return std::isfinite(v) ? 1 : 0; }

void report_init(XformReport* r) {
    if (!r) return;
    std::memset(r, 0, sizeof(*r));
    r->status = XFORM_OK;
}

int fail(XformReport* r, int status, const char* field, const char* fmt, ...) {
    if (r) {
        r->status = status;
        r->degraded = (status == XFORM_DEGRADED) ? 1 : 0;
        r->error_field = field;
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(r->message, sizeof(r->message), fmt, ap);
        va_end(ap);
    }
    return status;
}

/* The 2x2 linear block in m-layout:
 *     A = [a c; b d]  with  a = m[0], b = m[1], c = m[2], d = m[3].
 * This is the standard matrix acting on column (x, y). */
struct Mat2 { double a, b, c, d; };

Mat2 mat2_of(const SpriteLinear& lin) {
    Mat2 m;
    m.a = lin.m[0];
    m.b = lin.m[1];
    m.c = lin.m[2];
    m.d = lin.m[3];
    return m;
}

double det2(const Mat2& m) { return m.a * m.d - m.b * m.c; }

/* sigma_max / sigma_min, Wilson's closed form.  +inf when singular. */
double cond2(const Mat2& m) {
    const double e = (m.a + m.d) * 0.5;
    const double f = (m.a - m.d) * 0.5;
    const double g = (m.b + m.c) * 0.5;
    const double h = (m.b - m.c) * 0.5;
    const double q = std::hypot(e, h);
    const double r = std::hypot(f, g);
    const double smin = std::fabs(q - r);
    if (smin == 0.0) return HUGE_VAL;
    return (q + r) / smin;
}

int resolve_cond_max(double cond_max, double* out, XformReport* r) {
    double m = cond_max;
    if (m <= 0.0) m = XFORM_COND_MAX_DEFAULT;
    if (!finite_d(m))
        return fail(r, XFORM_ERR_THRESHOLD, "cond_max",
                    "cond_max must be finite and positive, got %g", cond_max);
    *out = m;
    return XFORM_OK;
}

/* Bounded rank-1 recovery used when cond >= cond_max.  Keeps the dominant
 * singular triple of the linear block and returns its Moore-Penrose
 * pseudoinverse A+ = u1 v1^T / sigma1 instead of a divergent adjugate. */
int rank1_recovery(const Mat2& m, const double t[2], SpriteAffine* inv,
                   XformReport* r) {
    const double s11 = m.a * m.a + m.b * m.b;
    const double s12 = m.a * m.c + m.b * m.d;
    const double s22 = m.c * m.c + m.d * m.d;
    const double lambda1 =
        (s11 + s22) * 0.5 + std::hypot((s11 - s22) * 0.5, s12);
    if (!(lambda1 > 0.0) || !finite_d(lambda1))
        return fail(r, XFORM_ERR_DET_ZERO, "det",
                    "linear part has no dominant direction to recover");

    /* Right singular vector v1: eigenvector of A^T A for lambda1. */
    double vx, vy;
    if (std::fabs(s12) > 0.0) {
        vx = s12;
        vy = lambda1 - s11;
    } else if (s11 >= s22) {
        vx = 1.0; vy = 0.0;
    } else {
        vx = 0.0; vy = 1.0;
    }
    const double vn = std::hypot(vx, vy);
    if (!(vn > 0.0))
        return fail(r, XFORM_ERR_DET_ZERO, "det",
                    "linear part has no dominant direction to recover");
    vx /= vn;
    vy /= vn;

    const double sigma1 = std::sqrt(lambda1);
    const double ux = (m.a * vx + m.c * vy) / sigma1;
    const double uy = (m.b * vx + m.d * vy) / sigma1;

    inv->m[0] = (float)(ux * vx / sigma1);
    inv->m[1] = (float)(uy * vx / sigma1);
    inv->m[2] = (float)(ux * vy / sigma1);
    inv->m[3] = (float)(uy * vy / sigma1);
    inv->m[4] = (float)-(inv->m[0] * t[0] + inv->m[2] * t[1]);
    inv->m[5] = (float)-(inv->m[1] * t[0] + inv->m[3] * t[1]);
    for (int i = 0; i < 6; ++i)
        if (!finite_f(inv->m[i]))
            return fail(r, XFORM_ERR_NONFINITE, "inv",
                        "rank-1 recovery overflowed at entry m[%d]", i);
    return XFORM_OK;
}

int compose_inverse_impl(const SpriteAffine* a, double cond_max,
                         SpriteAffine* inv, XformReport* r) {
    SpriteLinear lin;
    double t[2];
    int st = xform_split(a, &lin, t, r);
    if (st != XFORM_OK) return st;

    double cmax = 0.0;
    st = resolve_cond_max(cond_max, &cmax, r);
    if (st != XFORM_OK) return st;

    const Mat2 A = mat2_of(lin);
    const double det = det2(A);
    const double cond = cond2(A);
    if (r) {
        r->det = det;
        r->cond = cond;
    }

    if (det == 0.0)
        return fail(r, XFORM_ERR_DET_ZERO, "det",
                    "linear determinant is 0; the transform cannot be inverted");

    if (cond >= cmax) {
        st = rank1_recovery(A, t, inv, r);
        if (st != XFORM_OK) return st;
        if (r) {
            r->degraded = 1;
            r->status = XFORM_DEGRADED;
            std::snprintf(r->message, sizeof(r->message),
                          "condition number %.6g exceeds cond_max %.6g; degraded "
                          "to a bounded rank-1 recovery, the inverse is lossy",
                          cond, cmax);
        }
        return XFORM_DEGRADED;
    }

    const double inv_det = 1.0 / det;
    const double i0 = A.d * inv_det;  /* inv m[0] =  d / det */
    const double i1 = -A.b * inv_det; /* inv m[1] = -b / det */
    const double i2 = -A.c * inv_det; /* inv m[2] = -c / det */
    const double i3 = A.a * inv_det;  /* inv m[3] =  a / det */
    inv->m[0] = (float)i0;
    inv->m[1] = (float)i1;
    inv->m[2] = (float)i2;
    inv->m[3] = (float)i3;
    inv->m[4] = (float)-(i0 * t[0] + i2 * t[1]);
    inv->m[5] = (float)-(i1 * t[0] + i3 * t[1]);
    for (int i = 0; i < 6; ++i)
        if (!finite_f(inv->m[i]))
            return fail(r, XFORM_ERR_NONFINITE, "inv",
                        "composed inverse overflowed with cond=%.6g", cond);

    if (r) {
        r->status = XFORM_OK;
        r->error_field = nullptr;
        r->message[0] = '\0';
    }
    return XFORM_OK;
}

} /* namespace */

SpriteAffine xform_affine_from_scale(float sx, float sy, float tx, float ty) {
    SpriteAffine a;
    a.m[0] = sx; a.m[1] = 0.0f; a.m[2] = 0.0f;
    a.m[3] = sy; a.m[4] = tx; a.m[5] = ty;
    return a;
}

SpriteAffine xform_affine_trs(float angle_rad, float s, float tx, float ty) {
    const double co = std::cos(angle_rad) * s;
    const double si = std::sin(angle_rad) * s;
    SpriteAffine a;
    a.m[0] = (float)co;  /* x' =  co*x - si*y + tx */
    a.m[1] = (float)si;  /* y' =  si*x + co*y + ty */
    a.m[2] = (float)-si;
    a.m[3] = (float)co;
    a.m[4] = tx;
    a.m[5] = ty;
    return a;
}

double xform_condition(const SpriteAffine* a) {
    if (!a) return -1.0;
    for (int i = 0; i < 6; ++i)
        if (!finite_f(a->m[i])) return HUGE_VAL;
    const SpriteLinear lin{{a->m[0], a->m[1], a->m[2], a->m[3]}};
    return cond2(mat2_of(lin));
}

int xform_split(const SpriteAffine* a, SpriteLinear* lin, double trans[2],
                XformReport* report) {
    if (report) report_init(report);
    if (!a || !lin || !trans)
        return fail(report, XFORM_ERR_NULL, !a ? "a" : (!lin ? "lin" : "trans"),
                    "null pointer passed to xform_split");
    static const char* kNames[6] = {"m[0]", "m[1]", "m[2]", "m[3]", "m[4]", "m[5]"};
    for (int i = 0; i < 6; ++i)
        if (!finite_f(a->m[i]))
            return fail(report, XFORM_ERR_NONFINITE, kNames[i],
                        "parameter %s is not finite", kNames[i]);
    lin->m[0] = a->m[0];
    lin->m[1] = a->m[1];
    lin->m[2] = a->m[2];
    lin->m[3] = a->m[3];
    trans[0] = a->m[4];
    trans[1] = a->m[5];
    return XFORM_OK;
}

int xform_compose_inverse(const SpriteAffine* a, double cond_max,
                          SpriteAffine* inv, XformReport* report) {
    if (report) report_init(report);
    if (!a || !inv)
        return fail(report, XFORM_ERR_NULL, !a ? "a" : "inv",
                    "null pointer passed to xform_compose_inverse");
    return compose_inverse_impl(a, cond_max, inv, report);
}

int xform_check_roundtrip(const SpriteAffine* a, const SpriteAffine* inv,
                          const SpritePoint* pts, int n,
                          double* max_err, double* rms_err, XformReport* report) {
    if (report) report_init(report);
    if (!a || !inv || !pts || !max_err || !rms_err)
        return fail(report, XFORM_ERR_NULL,
                    !a ? "a" : (!inv ? "inv" : (!pts ? "pts" : "max_err")),
                    "null pointer passed to xform_check_roundtrip");
    const char* fields[6] = {"m[0]", "m[1]", "m[2]", "m[3]", "m[4]", "m[5]"};
    for (int i = 0; i < 6; ++i) {
        if (!finite_f(a->m[i]))
            return fail(report, XFORM_ERR_NONFINITE, fields[i],
                        "forward parameter %s is not finite", fields[i]);
        if (!finite_f(inv->m[i]))
            return fail(report, XFORM_ERR_NONFINITE, "inv",
                        "inverse parameter %s is not finite", fields[i]);
    }
    if (n < XFORM_MIN_POINTS)
        return fail(report, XFORM_ERR_POINTS, "n",
                    "need at least %d points, got %d", XFORM_MIN_POINTS, n);

    double maxv = 0.0, sumsq = 0.0;
    for (int i = 0; i < n; ++i) {
        if (!finite_f(pts[i].x) || !finite_f(pts[i].y))
            return fail(report, XFORM_ERR_NONFINITE, "pts",
                        "point %d is not finite", i);
        const double px = a->m[0] * (double)pts[i].x + a->m[2] * (double)pts[i].y + a->m[4];
        const double py = a->m[1] * (double)pts[i].x + a->m[3] * (double)pts[i].y + a->m[5];
        const double bx = inv->m[0] * px + inv->m[2] * py + inv->m[4];
        const double by = inv->m[1] * px + inv->m[3] * py + inv->m[5];
        const double e = std::hypot(bx - pts[i].x, by - pts[i].y);
        if (e > maxv) maxv = e;
        sumsq += e * e;
    }
    *max_err = maxv;
    *rms_err = std::sqrt(sumsq / (double)n);
    if (report) {
        report->status = XFORM_OK;
        report->max_residual = maxv;
        report->rms_residual = *rms_err;
    }
    return XFORM_OK;
}

int xform_affine_apply(const SpriteAffine* a, const SpritePoint* src, int n,
                       int inverse, double cond_max,
                       SpritePoint* dst, XformReport* report) {
    if (report) report_init(report);
    if (!a || !src || !dst)
        return fail(report, XFORM_ERR_NULL, !a ? "a" : (!src ? "src" : "dst"),
                    "null pointer passed to xform_affine_apply");
    const char* fields[6] = {"m[0]", "m[1]", "m[2]", "m[3]", "m[4]", "m[5]"};
    for (int i = 0; i < 6; ++i)
        if (!finite_f(a->m[i]))
            return fail(report, XFORM_ERR_NONFINITE, fields[i],
                        "parameter %s is not finite", fields[i]);
    if (n < 1)
        return fail(report, XFORM_ERR_POINTS, "n",
                    "need at least 1 point, got %d", n);

    if (!inverse) {
        for (int i = 0; i < n; ++i) {
            dst[i].x = a->m[0] * src[i].x + a->m[2] * src[i].y + a->m[4];
            dst[i].y = a->m[1] * src[i].x + a->m[3] * src[i].y + a->m[5];
        }
        return XFORM_OK;
    }

    if (n < XFORM_MIN_POINTS)
        return fail(report, XFORM_ERR_POINTS, "n",
                    "inverse needs at least %d points to verify the round trip, "
                    "got %d", XFORM_MIN_POINTS, n);

    SpriteAffine inv;
    int st = compose_inverse_impl(a, cond_max, &inv, report);
    if (st < 0) return st;

    for (int i = 0; i < n; ++i) {
        dst[i].x = (float)(inv.m[0] * (double)src[i].x + inv.m[2] * (double)src[i].y + inv.m[4]);
        dst[i].y = (float)(inv.m[1] * (double)src[i].x + inv.m[3] * (double)src[i].y + inv.m[5]);
        if (!finite_f(dst[i].x) || !finite_f(dst[i].y))
            return fail(report, XFORM_ERR_NONFINITE, "dst",
                        "inverse produced a non-finite point at index %d", i);
    }

    /* Round trip in forward space: push dst through a and compare to the
     * input.  For a well-conditioned a this is ~0; a degraded recovery
     * leaves the singular direction unrecoverable, which shows up here. */
    double maxv = 0.0, sumsq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double fx = a->m[0] * (double)dst[i].x + a->m[2] * (double)dst[i].y + a->m[4];
        const double fy = a->m[1] * (double)dst[i].x + a->m[3] * (double)dst[i].y + a->m[5];
        const double e = std::hypot(fx - src[i].x, fy - src[i].y);
        if (e > maxv) maxv = e;
        sumsq += e * e;
    }
    if (report) {
        report->max_residual = maxv;
        report->rms_residual = std::sqrt(sumsq / (double)n);
        report->status = st;
    }
    return st;
}

int xform_sprite_ex(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                    int inverse, double cond_max,
                    SpritePoint* dst, XformReport* report) {
    if (!inverse) {
        /* Legacy path: no extra validation, no diagnostics, bit-identical. */
        if (report) report_init(report);
        int rc = xform_sprite(src, n, sx, sy, tx, ty, dst);
        if (report && rc != 0) {
            report->status = XFORM_ERR_NULL;
            report->error_field = (!src ? "src" : (!dst ? "dst" : "n"));
            std::snprintf(report->message, sizeof(report->message),
                          "forward transform rejected the input (%s)",
                          report->error_field);
        }
        return rc;
    }

    if (report) report_init(report);
    if (!src || !dst)
        return fail(report, XFORM_ERR_NULL, !src ? "src" : "dst",
                    "null pointer passed to xform_sprite_ex");
    if (n < XFORM_MIN_POINTS)
        return fail(report, XFORM_ERR_POINTS, "n",
                    "inverse needs at least %d points, got %d", XFORM_MIN_POINTS, n);
    if (!finite_f(sx)) return fail(report, XFORM_ERR_NONFINITE, "sx", "sx is not finite");
    if (!finite_f(sy)) return fail(report, XFORM_ERR_NONFINITE, "sy", "sy is not finite");
    if (!finite_f(tx)) return fail(report, XFORM_ERR_NONFINITE, "tx", "tx is not finite");
    if (!finite_f(ty)) return fail(report, XFORM_ERR_NONFINITE, "ty", "ty is not finite");

    const SpriteAffine a = xform_affine_from_scale(sx, sy, tx, ty);
    return xform_affine_apply(&a, src, n, 1, cond_max, dst, report);
}

int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst) {
    if (!src || !dst || n <= 0) return -1;
    for (int i = 0; i < n; ++i) {
        dst[i].x = src[i].x * sx + tx;
        dst[i].y = src[i].y * sy + ty;
    }
    return 0;
}

int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy) {
    if (!pts || !minx || !miny || !maxx || !maxy || n <= 0) return -1;
    float a = FLT_MAX, b = FLT_MAX, c = -FLT_MAX, d = -FLT_MAX;
    for (int i = 0; i < n; ++i) {
        if (pts[i].x < a) a = pts[i].x;
        if (pts[i].y < b) b = pts[i].y;
        if (pts[i].x > c) c = pts[i].x;
        if (pts[i].y > d) d = pts[i].y;
    }
    *minx = a; *miny = b; *maxx = c; *maxy = d;
    return 0;
}
