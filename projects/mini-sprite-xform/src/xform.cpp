#include "xform.h"
#include <cfloat>
#include <cmath>

Affine2D affine_identity() { return {1, 0, 0, 0, 1, 0}; }

Affine2D affine_translate(double tx, double ty) { return {1, 0, tx, 0, 1, ty}; }

Affine2D affine_scale(double sx, double sy) { return {sx, 0, 0, 0, sy, 0}; }

Affine2D affine_rotate(double radians) {
    double c = std::cos(radians), s = std::sin(radians);
    return {c, -s, 0, s, c, 0};
}

Affine2D affine_shear(double kx, double ky) { return {1, kx, 0, ky, 1, 0}; }

Affine2D affine_multiply(Affine2D o, Affine2D i) {
    return {
        o.m00 * i.m00 + o.m01 * i.m10,
        o.m00 * i.m01 + o.m01 * i.m11,
        o.m00 * i.m02 + o.m01 * i.m12 + o.m02,
        o.m10 * i.m00 + o.m11 * i.m10,
        o.m10 * i.m01 + o.m11 * i.m11,
        o.m10 * i.m02 + o.m11 * i.m12 + o.m12
    };
}

double affine_det(Affine2D m) { return m.m00 * m.m11 - m.m01 * m.m10; }

int affine_invert(Affine2D m, Affine2D* inv) {
    if (!inv) return -1;
    double det = affine_det(m);
    if (det == 0.0) return -1;  /* degenerate: no inverse, deterministic failure */
    double r = 1.0 / det;
    inv->m00 =  m.m11 * r;
    inv->m01 = -m.m01 * r;
    inv->m02 = (m.m01 * m.m12 - m.m11 * m.m02) * r;
    inv->m10 = -m.m10 * r;
    inv->m11 =  m.m00 * r;
    inv->m12 = (m.m10 * m.m02 - m.m00 * m.m12) * r;
    return 0;
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

int xform_sprite_affine(const SpritePoint* src, int n, Affine2D m, SpritePoint* dst) {
    if (!src || !dst || n <= 0) return -1;
    for (int i = 0; i < n; ++i) {
        double x = src[i].x, y = src[i].y;
        dst[i].x = (float)(m.m00 * x + m.m01 * y + m.m02);
        dst[i].y = (float)(m.m10 * x + m.m11 * y + m.m12);
    }
    return 0;
}

int xform_sprite_affine_d(const SpritePointD* src, int n, Affine2D m, SpritePointD* dst) {
    if (!src || !dst || n <= 0) return -1;
    for (int i = 0; i < n; ++i) {
        double x = src[i].x, y = src[i].y;
        dst[i].x = m.m00 * x + m.m01 * y + m.m02;
        dst[i].y = m.m10 * x + m.m11 * y + m.m12;
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

int xform_bounds_affine(const SpritePoint* pts, int n, Affine2D m,
                        float* minx, float* miny, float* maxx, float* maxy) {
    if (!pts || !minx || !miny || !maxx || !maxy || n <= 0) return -1;
    double a = DBL_MAX, b = DBL_MAX, c = -DBL_MAX, d = -DBL_MAX;
    for (int i = 0; i < n; ++i) {
        double x = pts[i].x, y = pts[i].y;
        double ux = m.m00 * x + m.m01 * y + m.m02;
        double uy = m.m10 * x + m.m11 * y + m.m12;
        if (ux < a) a = ux;
        if (uy < b) b = uy;
        if (ux > c) c = ux;
        if (uy > d) d = uy;
    }
    *minx = (float)a; *miny = (float)b; *maxx = (float)c; *maxy = (float)d;
    return 0;
}

double xform_signed_area(const SpritePoint* pts, int n) {
    if (!pts || n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        s += (double)pts[i].x * pts[j].y - (double)pts[j].x * pts[i].y;
    }
    return 0.5 * s;
}
