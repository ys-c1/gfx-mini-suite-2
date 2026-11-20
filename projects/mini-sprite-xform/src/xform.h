#ifndef XFORM_H
#define XFORM_H

struct SpritePoint { float x, y; };
struct SpritePointD { double x, y; };

/* 2D affine matrix, column-vector convention:
 *
 *   | x' |   | m00 m01 m02 |   | x |
 *   | y' | = | m10 m11 m12 | * | y |
 *   | 1  |   |  0   0   1  |   | 1 |
 *
 * Coefficients are double so a forward/inverse round trip on float
 * points keeps error well below 1e-6 (float storage rounding dominates,
 * not the matrix arithmetic). */
struct Affine2D { double m00, m01, m02, m10, m11, m12; };

Affine2D affine_identity();
Affine2D affine_translate(double tx, double ty);
Affine2D affine_scale(double sx, double sy);
Affine2D affine_rotate(double radians);       /* CCW; affine_rotate(0) == affine_identity() exactly */
Affine2D affine_shear(double kx, double ky);  /* x' = x + kx*y, y' = y + ky*x; det = 1 - kx*ky */

/* Composition: affine_multiply(outer, inner) = outer * inner, i.e. the
 * result applies `inner` to the point first, then `outer`
 * (column-vector convention, same as function composition f(g(p))). */
Affine2D affine_multiply(Affine2D outer, Affine2D inner);

double affine_det(Affine2D m);

/* Returns 0 and writes *inv on success; returns -1 and leaves *inv
 * untouched when det == 0 (degenerate, e.g. shear with kx*ky == 1). */
int affine_invert(Affine2D m, Affine2D* inv);

/* Original translate/scale path. Float arithmetic, body unchanged —
 * output is bit-identical to the original implementation. */
int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst);

/* General affine path: computed in double, rounded to float on store. */
int xform_sprite_affine(const SpritePoint* src, int n, Affine2D m, SpritePoint* dst);

/* Double-precision path: no float rounding between forward and inverse,
 * this is the path that guarantees round-trip error < 1e-6. */
int xform_sprite_affine_d(const SpritePointD* src, int n, Affine2D m, SpritePointD* dst);

int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy);

/* Tight bounds of the affine-transformed vertices: every vertex is
 * transformed (in double) and min/max taken, so the box hugs the real
 * vertices — min/max are non-decreasing under rounding, so the float
 * result also exactly bounds xform_sprite_affine output. */
int xform_bounds_affine(const SpritePoint* pts, int n, Affine2D m,
                        float* minx, float* miny, float* maxx, float* maxy);

/* Signed polygon area (shoelace, double accumulation), CCW > 0.
 * Under affine m: area' = det(m) * area, so the sign is preserved when
 * det > 0 and flips when det < 0 (reflection). n < 3 returns 0.0. */
double xform_signed_area(const SpritePoint* pts, int n);

#endif
