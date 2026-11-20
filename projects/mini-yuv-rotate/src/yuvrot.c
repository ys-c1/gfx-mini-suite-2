#include "yuvrot.h"
#include <stddef.h>

/* ---- 方向 ---- */

int yr_op_valid(yr_op op) {
    return op >= YR_IDENTITY && op <= YR_MIRROR_V;
}

int yr_swaps_axes(yr_op op) {
    return op == YR_R90 || op == YR_R270;
}

int yr_dst_size(yr_op op, int src_w, int src_h, int* dst_w, int* dst_h) {
    if (!yr_op_valid(op) || src_w <= 0 || src_h <= 0 || !dst_w || !dst_h)
        return -1;
    if (yr_swaps_axes(op)) {
        *dst_w = src_h;
        *dst_h = src_w;
    } else {
        *dst_w = src_w;
        *dst_h = src_h;
    }
    return 0;
}

/*
 * 逆映射: 给定目标像素 (dx, dy), 返回它取自源像素 (*sx, *sy)。
 * 源尺寸 w x h, 目标尺寸 h x w (90/270)。
 *   R90  正向 (x,y)->(h-1-y, x);     逆向 (dx,dy)->(dy, h-1-dx)
 *   R180 正向 (x,y)->(w-1-x, h-1-y); 逆向同式
 *   R270 正向 (x,y)->(y, w-1-x);     逆向 (dx,dy)->(w-1-dy, dx)
 */
static void inverse_map(yr_op op, int dx, int dy, int w, int h,
                        int* sx, int* sy) {
    switch (op) {
        case YR_R90:      *sx = dy;             *sy = h - 1 - dx; break;
        case YR_R180:     *sx = w - 1 - dx;     *sy = h - 1 - dy; break;
        case YR_R270:     *sx = w - 1 - dy;     *sy = dx;         break;
        case YR_MIRROR_H: *sx = w - 1 - dx;     *sy = dy;         break;
        case YR_MIRROR_V: *sx = dx;             *sy = h - 1 - dy; break;
        case YR_IDENTITY:
        default:          *sx = dx;             *sy = dy;         break;
    }
}

/* ---- 等变色度约定: 单轴划分 ----
 * n 个像素, c=(n+1)/2 个色度格。
 * axis_cell_pixel(k, t): 格 k 覆盖的第 t 个像素 (t=0..n_px-1)。
 * axis_owner(x): 像素 x 所属色度格。
 * axis_rep(k): 格 k 的代表像素 (逆变换上采样时复用其色度)。
 */

static int axis_cell_count(int n) { return (n + 1) / 2; }

/* 格 k 包含的像素个数: 偶数 n 恒为 2; 奇数 n 仅中心格为 1。 */
static int axis_cell_npx(int n, int k) {
    if ((n & 1) && k == n / 2) return 1;
    return 2;
}

static int axis_cell_pixel(int n, int k, int t) {
    if (n & 1) {
        int center = n / 2;
        if (k < center) {
            /* 反射配对: 格 k = {k, n-1-k} */
            return t == 0 ? k : (n - 1 - k);
        }
        return center;              /* 中心单像素格 */
    }
    return 2 * k + t;              /* 偶数 n: 标准相邻对 */
}

static int axis_owner(int n, int x) {
    if (n & 1) {
        int center = n / 2;
        if (x == center) return center;
        return x < center ? x : (n - 1 - x);
    }
    return x / 2;
}

static int axis_rep(int n, int k) {
    return axis_cell_pixel(n, k, 0);
}

/* ---- RGB 几何变换 ---- */

int yr_transform_rgb(yr_op op,
                     const unsigned char* src, int src_w, int src_h, int src_stride,
                     unsigned char* dst, int dst_w, int dst_h, int dst_stride) {
    if (!yr_op_valid(op) || !src || !dst || src_w <= 0 || src_h <= 0 ||
        src_stride < src_w * 3 || dst_stride < dst_w * 3)
        return -1;
    int ew, eh;
    if (yr_dst_size(op, src_w, src_h, &ew, &eh) != 0 ||
        ew != dst_w || eh != dst_h)
        return -1;
    for (int dy = 0; dy < dst_h; ++dy)
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx, sy;
            inverse_map(op, dx, dy, src_w, src_h, &sx, &sy);
            const unsigned char* p =
                src + (size_t)sy * src_stride + (size_t)sx * 3;
            unsigned char* q =
                dst + (size_t)dy * dst_stride + (size_t)dx * 3;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
        }
    return 0;
}

/* ---- BT.601 full-range, 与 mini-chroma-planar 同系数 ---- */

static void rgb_to_yuv_px(const unsigned char* p, int* yv, int* uv, int* vv) {
    int r = p[0], g = p[1], b = p[2];
    *yv = (77 * r + 150 * g + 29 * b + 128) >> 8;
    *uv = ((-43 * r - 85 * g + 128 * b + 128) >> 8) + 128;
    *vv = ((128 * r - 107 * g - 21 * b + 128) >> 8) + 128;
}

int yr_rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                     unsigned char* y, int y_stride,
                     unsigned char* u, int u_stride,
                     unsigned char* v, int v_stride) {
    if (!rgb || !y || !u || !v || w <= 0 || h <= 0 ||
        rgb_stride < w * 3 || y_stride < w ||
        u_stride < axis_cell_count(w) || v_stride < axis_cell_count(w))
        return -1;
    int cw = axis_cell_count(w), ch = axis_cell_count(h);

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const unsigned char* p =
                rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            int yv, uv, vv;
            rgb_to_yuv_px(p, &yv, &uv, &vv);
            y[(size_t)j * y_stride + i] = (unsigned char)yv;
        }

    for (int cj = 0; cj < ch; ++cj) {
        int npy = axis_cell_npx(h, cj);
        for (int ci = 0; ci < cw; ++ci) {
            int npx = axis_cell_npx(w, ci);
            int su = 0, sv = 0;
            for (int ty = 0; ty < npy; ++ty) {
                int py = axis_cell_pixel(h, cj, ty);
                for (int tx = 0; tx < npx; ++tx) {
                    int px = axis_cell_pixel(w, ci, tx);
                    const unsigned char* p =
                        rgb + (size_t)py * rgb_stride + (size_t)px * 3;
                    int yv, uv, vv;
                    rgb_to_yuv_px(p, &yv, &uv, &vv);
                    su += uv;
                    sv += vv;
                }
            }
            int cnt = npx * npy;
            u[(size_t)cj * u_stride + ci] = (unsigned char)(su / cnt);
            v[(size_t)cj * v_stride + ci] = (unsigned char)(sv / cnt);
        }
    }
    return 0;
}

int yr_yuv420_to_rgb(const unsigned char* y, int y_stride,
                     const unsigned char* u, int u_stride,
                     const unsigned char* v, int v_stride,
                     int w, int h, unsigned char* rgb, int rgb_stride) {
    if (!y || !u || !v || !rgb || w <= 0 || h <= 0 ||
        rgb_stride < w * 3 || y_stride < w ||
        u_stride < axis_cell_count(w) || v_stride < axis_cell_count(w))
        return -1;
    int cw = axis_cell_count(w);           /* 参数校验用 */
    (void)cw;
    for (int j = 0; j < h; ++j) {
        int cj = axis_owner(h, j);
        for (int i = 0; i < w; ++i) {
            int ci = axis_owner(w, i);
            int yv = y[(size_t)j * y_stride + i];
            int uu = u[(size_t)cj * u_stride + ci] - 128;
            int vv = v[(size_t)cj * v_stride + ci] - 128;
            int r = yv + ((1436 * vv) >> 10);
            int g = yv - ((352 * uu + 731 * vv) >> 10);
            int b = yv + ((1815 * uu) >> 10);
            unsigned char* p =
                rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    }
    return 0;
}

/* ---- 平面 YUV420 几何变换 ---- */

int yr_transform_yuv420(yr_op op,
                        const unsigned char* sy, int sy_stride,
                        const unsigned char* su, int su_stride,
                        const unsigned char* sv, int sv_stride,
                        int src_w, int src_h,
                        unsigned char* dy, int dy_stride,
                        unsigned char* du, int du_stride,
                        unsigned char* dv, int dv_stride) {
    if (!yr_op_valid(op) || !sy || !su || !sv || !dy || !du || !dv ||
        src_w <= 0 || src_h <= 0 ||
        sy_stride < src_w || dy_stride < (yr_swaps_axes(op) ? src_h : src_w))
        return -1;
    int scw = axis_cell_count(src_w);
    if (su_stride < scw || sv_stride < scw) return -1;

    int dst_w, dst_h;
    if (yr_dst_size(op, src_w, src_h, &dst_w, &dst_h) != 0) return -1;
    int dcw = axis_cell_count(dst_w), dch = axis_cell_count(dst_h);
    if (du_stride < dcw || dv_stride < dcw) return -1;

    /* Y: 逐像素逆映射搬移 */
    for (int dpy = 0; dpy < dst_h; ++dpy)
        for (int dpx = 0; dpx < dst_w; ++dpx) {
            int spx, spy;
            inverse_map(op, dpx, dpy, src_w, src_h, &spx, &spy);
            dy[(size_t)dpy * dy_stride + dpx] =
                sy[(size_t)spy * sy_stride + spx];
        }

    /* U/V: 色度格整体搬移。目标格 (dci,dcj) 的代表像素做逆映射,
     * 落点所属的源色度格即源格 —— 格划分对二面体群等变, 与格内取点无关。 */
    for (int dcj = 0; dcj < dch; ++dcj)
        for (int dci = 0; dci < dcw; ++dci) {
            int rpx = axis_rep(dst_w, dci);
            int rpy = axis_rep(dst_h, dcj);
            int spx, spy;
            inverse_map(op, rpx, rpy, src_w, src_h, &spx, &spy);
            int sci = axis_owner(src_w, spx);
            int scj = axis_owner(src_h, spy);
            du[(size_t)dcj * du_stride + dci] =
                su[(size_t)scj * su_stride + sci];
            dv[(size_t)dcj * dv_stride + dci] =
                sv[(size_t)scj * sv_stride + sci];
        }
    return 0;
}
