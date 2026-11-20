#include "sat.h"
#include <math.h>

void sat_centroid(const SatPoly* a, float* cx, float* cy) {
    float sx = 0, sy = 0;
    for (int i = 0; i < a->n; ++i) { sx += a->p[i].x; sy += a->p[i].y; }
    *cx = sx / (float)a->n; *cy = sy / (float)a->n;
}

static void project(const SatPoly* a, float ax, float ay, float* lo, float* hi) {
    float mn = 1e30f, mx = -1e30f;
    for (int i = 0; i < a->n; ++i) {
        float d = a->p[i].x * ax + a->p[i].y * ay;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }
    *lo = mn; *hi = mx;
}

static int axes_overlap(const SatPoly* a, const SatPoly* b) {
    for (int e = 0; e < a->n; ++e) {
        int i2 = (e + 1) % a->n;
        float ex = a->p[i2].x - a->p[e].x, ey = a->p[i2].y - a->p[e].y;
        float ax = -ey, ay = ex;
        float len = sqrtf(ax * ax + ay * ay);
        if (len < 1e-9f) continue;
        ax /= len; ay /= len;
        float alo, ahi, blo, bhi;
        project(a, ax, ay, &alo, &ahi);
        project(b, ax, ay, &blo, &bhi);
        if (ahi < blo || bhi < alo) return 0;
    }
    return 1;
}

int sat_overlap(const SatPoly* a, const SatPoly* b) {
    if (!a || !b || a->n < 3 || b->n < 3) return -1;
    return axes_overlap(a, b) && axes_overlap(b, a);
}

int sat_point_inside(const SatPoly* a, float x, float y) {
    int inside = 0;
    for (int i = 0, j = a->n - 1; i < a->n; j = i++) {
        float xi = a->p[i].x, yi = a->p[i].y, xj = a->p[j].x, yj = a->p[j].y;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return inside;
}

/* 最小平移向量：沿所有边轴求重叠，取深度最小的轴。
   角点处两轴深度几乎相等时，轴的遍历顺序会让结果在帧间来回翻转 ——
   这是连续帧推力方向"一抖一抖"的根源，因此近等深轴用 ref 消歧：
   选与参考方向（质心分离方向，或上一帧推力方向）点积最大者。 */
static int mtv_on_axes(const SatPoly* a, const SatPoly* b, int use_b_axes,
                       float refx, float refy, float tol,
                       float* best_depth, float* best_x, float* best_y) {
    const SatPoly* src = use_b_axes ? b : a;
    for (int e = 0; e < src->n; ++e) {
        int i2 = (e + 1) % src->n;
        float ex = src->p[i2].x - src->p[e].x;
        float ey = src->p[i2].y - src->p[e].y;
        float ax = -ey, ay = ex;
        float len = sqrtf(ax * ax + ay * ay);
        if (len < 1e-9f) continue;
        ax /= len; ay /= len;
        float alo, ahi, blo, bhi;
        project(a, ax, ay, &alo, &ahi);
        project(b, ax, ay, &blo, &bhi);
        if (ahi < blo || bhi < alo) return 0;
        float depth = fminf(ahi - blo, bhi - alo);
        if (depth < *best_depth) {
            *best_depth = depth;
            *best_x = ax;
            *best_y = ay;
        } else if (depth <= *best_depth + tol) {
            /* 近等深：与参考方向更一致的轴胜出，避免顺序相关的翻转 */
            float dot_new = ax * refx + ay * refy;
            float dot_old = *best_x * refx + *best_y * refy;
            if (dot_new > dot_old) {
                *best_depth = depth;
                *best_x = ax;
                *best_y = ay;
            }
        }
    }
    return 1;
}

/* ref_override 非空时优先作为近等深消歧方向（如用上一帧推力方向），
   否则回退到两多边形质心的分离方向。 */
static int mtv_impl(const SatPoly* a, const SatPoly* b, SatVec* out,
                    const SatVec* ref_override, float tol) {
    float acx, acy, bcx, bcy;
    sat_centroid(a, &acx, &acy);
    sat_centroid(b, &bcx, &bcy);
    float refx = acx - bcx, refy = acy - bcy;
    if (ref_override) { refx = ref_override->x; refy = ref_override->y; }
    if (refx * refx + refy * refy < 1e-12f) { refx = 1.0f; refy = 0.0f; }

    float best_depth = 1e30f, best_x = 1.0f, best_y = 0.0f;
    if (!mtv_on_axes(a, b, 0, refx, refy, tol, &best_depth, &best_x, &best_y)) return 0;
    if (!mtv_on_axes(a, b, 1, refx, refy, tol, &best_depth, &best_x, &best_y)) return 0;

    /* 轴法线朝向不确定，统一为从 b 指向 a 的分离方向 */
    if (best_x * refx + best_y * refy < 0.0f) {
        best_x = -best_x;
        best_y = -best_y;
    }
    out->x = best_x * best_depth;
    out->y = best_y * best_depth;
    return 1;
}

int sat_mtv(const SatPoly* a, const SatPoly* b, SatVec* out) {
    if (!a || !b || a->n < 3 || b->n < 3) return -1;
    if (!out) return -1;
    return mtv_impl(a, b, out, 0, 1e-3f);
}

void sat_push_default_config(SatPushConfig* cfg) {
    cfg->max_mag_delta = 0.6f;  /* 每帧分离量最多变化 0.6 单位 */
    cfg->max_ang_delta = 0.18f; /* 每帧方向最多旋转约 10 度 */
    cfg->hold_frames   = 3;     /* 接触丢失后状态保留 3 帧再复位 */
    cfg->tie_tol       = 1e-3f;
}

void sat_push_init(SatPushState* st) {
    st->dir.x = 1.0f; st->dir.y = 0.0f;
    st->mag = 0.0f;
    st->have = 0;
    st->gap = 0;
}

/* 把方向朝目标方向旋转至多 max_ang 弧度（二维平面内取最短弧） */
static SatVec rotate_toward(SatVec cur, SatVec tgt, float max_ang) {
    float cross = cur.x * tgt.y - cur.y * tgt.x;
    float dot   = cur.x * tgt.x + cur.y * tgt.y;
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    float ang = atan2f(cross, dot);
    if (ang >  max_ang) ang =  max_ang;
    if (ang < -max_ang) ang = -max_ang;
    float cs = cosf(ang), sn = sinf(ang);
    SatVec r = { cur.x * cs - cur.y * sn, cur.x * sn + cur.y * cs };
    return r;
}

SatVec sat_push_update(SatPushState* st, const SatPoly* a, const SatPoly* b,
                       const SatPushConfig* cfg) {
    SatVec zero = { 0.0f, 0.0f };

    /* 已有历史时，用上一帧推力方向做近等深消歧，强化时间相干性 */
    SatVec raw;
    int hit;
    if (st->have && st->gap <= cfg->hold_frames) {
        hit = mtv_impl(a, b, &raw, &st->dir, cfg->tie_tol);
    } else {
        hit = mtv_impl(a, b, &raw, 0, cfg->tie_tol);
    }
    if (hit <= 0) {
        /* 非接触帧：不输出推力；状态先保留 hold_frames 帧，超期才复位，
           这样短暂分离不会让下次接触重新初始化而产生跳变 */
        if (st->have) {
            ++st->gap;
            if (st->gap > cfg->hold_frames) {
                st->have = 0;
                st->mag = 0.0f;
            }
        }
        return zero;
    }

    float raw_mag = sqrtf(raw.x * raw.x + raw.y * raw.y);
    if (raw_mag < 1e-9f) return zero;
    SatVec raw_dir = { raw.x / raw_mag, raw.y / raw_mag };

    if (!st->have || st->gap > cfg->hold_frames) {
        /* 首次接触 / 长期分离后重新接触：直接采用真实值，不做平滑 */
        st->dir = raw_dir;
        st->mag = raw_mag;
        st->have = 1;
        st->gap = 0;
        return raw;
    }
    st->gap = 0;

    /* 分离量限幅：只限制变化率，不引入稳态残差 —— 深度持续变化时
       每帧都能跟上，最终一定收敛到真实深度，该有的推力不会漏 */
    float dmag = raw_mag - st->mag;
    if (dmag >  cfg->max_mag_delta) dmag =  cfg->max_mag_delta;
    if (dmag < -cfg->max_mag_delta) dmag = -cfg->max_mag_delta;
    st->mag += dmag;
    if (st->mag < 0.0f) st->mag = 0.0f;

    /* 方向变化率上限：每帧最多转 max_ang_delta */
    st->dir = rotate_toward(st->dir, raw_dir, cfg->max_ang_delta);

    SatVec push = { st->dir.x * st->mag, st->dir.y * st->mag };
    return push;
}
