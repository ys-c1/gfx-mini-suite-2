#ifndef XFORM_H
#define XFORM_H

struct SpritePoint { float x, y; };

/* Affine transform, row-major 2x3:
 *     x' = m[0] * x + m[2] * y + m[4]
 *     y' = m[1] * x + m[3] * y + m[5]
 * m[0..3] is the linear block, (m[4], m[5]) the translation. */
struct SpriteAffine {
    float m[6];
};

/* Stage 1 output: the linear block alone, in double so the inverse is not
 * rounded before composition.  Block = [m[0] m[2]; m[1] m[3]]. */
struct SpriteLinear {
    double m[4];
};

enum {
    XFORM_OK = 0,
    XFORM_DEGRADED = 1,      /* cond >= cond_max: bounded rank-1 recovery */
    XFORM_ERR_NULL = -1,     /* null pointer / forward-mode n <= 0 */
    XFORM_ERR_DET_ZERO = -2, /* linear part has determinant exactly 0 */
    XFORM_ERR_NONFINITE = -3,/* a named parameter is NaN or infinite */
    XFORM_ERR_POINTS = -4,   /* fewer than XFORM_MIN_POINTS points */
    XFORM_ERR_THRESHOLD = -5 /* cond_max is not finite and positive */
};

/* An affine needs three points to pin down its six parameters. */
constexpr int XFORM_MIN_POINTS = 3;
constexpr double XFORM_COND_MAX_DEFAULT = 1.0e5;

struct XformReport {
    int status;              /* XFORM_* status of the last operation */
    int degraded;            /* 1 when a rank-1 recovery replaced the inverse */
    const char* error_field; /* which item failed: "sx", "n", "det", ... */
    char message[192];       /* human-readable reason, empty on success */
    double det;              /* determinant of the linear part */
    double cond;             /* condition number of the linear part */
    double max_residual;     /* round-trip residual, max abs */
    double rms_residual;     /* round-trip residual, quadratic mean */
};

/* Build an affine from the legacy scale+translate parameters. */
SpriteAffine xform_affine_from_scale(float sx, float sy, float tx, float ty);

/* Build an affine as rotate(angle_rad) * scale(s) then translate. */
SpriteAffine xform_affine_trs(float angle_rad, float s, float tx, float ty);

/* Condition number (sigma_max / sigma_min) of the linear block, Wilson's
 * closed form.  +inf for a singular block, -1 for a null pointer. */
double xform_condition(const SpriteAffine* a);

/* Stage 1: split an affine into its linear block and translation.  Validates
 * every affine entry; error_field names the offending "m[i]". */
int xform_split(const SpriteAffine* a, SpriteLinear* lin, double trans[2],
                XformReport* report);

/* Stage 2: compose the inverse affine from the split linear block.
 *   det == 0            -> XFORM_ERR_DET_ZERO, error_field "det"
 *   cond >= cond_max    -> XFORM_DEGRADED and a bounded rank-1 recovery of the
 *                          linear part (never a divergent adjugate inverse)
 *   otherwise           -> XFORM_OK and the adjugate inverse
 * cond_max <= 0 means XFORM_COND_MAX_DEFAULT; a NaN/inf cond_max is
 * XFORM_ERR_THRESHOLD. */
int xform_compose_inverse(const SpriteAffine* a, double cond_max,
                          SpriteAffine* inv, XformReport* report);

/* Stage 3: round-trip n points and report max/rms of |a(inv(a(p))) - a(p)|,
 * i.e. how far the returned inverse lands from the point it was given. */
int xform_check_roundtrip(const SpriteAffine* a, const SpriteAffine* inv,
                          const SpritePoint* pts, int n,
                          double* max_err, double* rms_err, XformReport* report);

/* Apply an arbitrary affine to n points; inverse == 0 is the forward
 * transform (n >= 1).  Same status/report contract as xform_sprite_ex(). */
int xform_affine_apply(const SpriteAffine* a, const SpritePoint* src, int n,
                       int inverse, double cond_max,
                       SpritePoint* dst, XformReport* report);

/* Forward or inverse point transform with guard-rails.
 * inverse == 0: exactly xform_sprite(); dst is bit-identical, no extra
 *               validation, no diagnostics, cond_max ignored.
 * inverse != 0: dst = a(src) with a the inverse affine.  src holds
 *               forward-transformed points, so the reported residual is
 *               |fwd(dst) - src|.  A near-singular affine (cond >= cond_max)
 *               reports XFORM_DEGRADED and returns the bounded recovery.
 * On any negative status dst is left untouched and report->error_field names
 * the failing item ("n", "src", "sx", "sy", "tx", "ty", "det", "m[i]", ...). */
int xform_sprite_ex(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                    int inverse, double cond_max,
                    SpritePoint* dst, XformReport* report);

int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst);
int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy);
#endif
