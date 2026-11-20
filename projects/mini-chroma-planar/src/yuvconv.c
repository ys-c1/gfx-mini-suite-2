#include "yuvconv.h"
#include <string.h>

int yc_chroma_dims(int fmt, int w, int h, int* cw, int* ch) {
    if (!cw || !ch) return YC_ERR_ARG;
    if (fmt != YC_FMT_420 && fmt != YC_FMT_422) return YC_ERR_FORMAT;
    if (w <= 0 || h <= 0) return YC_ERR_SIZE;
    *cw = (w + 1) / 2;
    *ch = (fmt == YC_FMT_420) ? (h + 1) / 2 : h;
    return YC_OK;
}

static int clamp_row(int r, int rows) {
    return r < 0 ? 0 : (r >= rows ? rows - 1 : r);
}

/* 422 -> 420：仅垂直方向 2:1 抽取，[1 3 3 1]/8 滤波，中心在 2j+0.5 */
static void decimate_v(const unsigned char* src, int ss, unsigned char* dst, int ds,
                       int cw, int src_rows, int dst_rows) {
    for (int j = 0; j < dst_rows; ++j) {
        const unsigned char* r0 = src + (size_t)clamp_row(2 * j - 1, src_rows) * ss;
        const unsigned char* r1 = src + (size_t)clamp_row(2 * j,     src_rows) * ss;
        const unsigned char* r2 = src + (size_t)clamp_row(2 * j + 1, src_rows) * ss;
        const unsigned char* r3 = src + (size_t)clamp_row(2 * j + 2, src_rows) * ss;
        unsigned char* d = dst + (size_t)j * ds;
        for (int i = 0; i < cw; ++i)
            d[i] = (unsigned char)((r0[i] + 3 * r1[i] + 3 * r2[i] + r3[i] + 4) >> 3);
    }
}

/* 420 -> 422：仅垂直方向 1:2 相位对齐线性插值。
 * 420 第 j 行色度位于亮度行 2j+0.5 处；422 第 r 行与亮度行 r 对齐：
 *   r 偶 (r=2j)：0.25*c[j-1] + 0.75*c[j]
 *   r 奇 (r=2j+1)：0.75*c[j] + 0.25*c[j+1] */
static void upsample_v(const unsigned char* src, int ss, unsigned char* dst, int ds,
                       int cw, int src_rows, int dst_rows) {
    for (int r = 0; r < dst_rows; ++r) {
        int j = r >> 1;
        const unsigned char* a;
        const unsigned char* b;
        int wa, wb;
        if ((r & 1) == 0) {
            a = src + (size_t)clamp_row(j - 1, src_rows) * ss;
            b = src + (size_t)clamp_row(j,     src_rows) * ss;
            wa = 1; wb = 3;
        } else {
            a = src + (size_t)clamp_row(j,     src_rows) * ss;
            b = src + (size_t)clamp_row(j + 1, src_rows) * ss;
            wa = 3; wb = 1;
        }
        unsigned char* d = dst + (size_t)r * ds;
        for (int i = 0; i < cw; ++i)
            d[i] = (unsigned char)((wa * a[i] + wb * b[i] + 2) >> 2);
    }
}

static void copy_plane(const unsigned char* src, int ss, unsigned char* dst, int ds,
                       int cols, int rows) {
    if (src == dst && ss == ds) return;
    for (int r = 0; r < rows; ++r)
        memcpy(dst + (size_t)r * ds, src + (size_t)r * ss, (size_t)cols);
}

int yuv_convert(int src_fmt, int dst_fmt,
                const unsigned char* y_src, int y_stride_src,
                const unsigned char* u_src, int u_stride_src,
                const unsigned char* v_src, int v_stride_src,
                int w, int h,
                unsigned char* y_dst, int y_stride_dst, size_t y_cap,
                unsigned char* u_dst, int u_stride_dst, size_t u_cap,
                unsigned char* v_dst, int v_stride_dst, size_t v_cap) {
    if (!y_src || !u_src || !v_src || !y_dst || !u_dst || !v_dst) return YC_ERR_ARG;
    int scw, sch, dcw, dch, rc;
    rc = yc_chroma_dims(src_fmt, w, h, &scw, &sch);
    if (rc != YC_OK) return rc;               /* 未知源格式或尺寸非法 */
    rc = yc_chroma_dims(dst_fmt, w, h, &dcw, &dch);
    if (rc != YC_OK) return rc;               /* 未知目标格式 */
    if (y_stride_src < w || y_stride_dst < w ||
        u_stride_src < scw || v_stride_src < scw ||
        u_stride_dst < dcw || v_stride_dst < dcw) return YC_ERR_SIZE;
    /* 容量按 (行数-1)*stride + 列数 计算，不足则整体拒绝 */
    if (y_cap < (size_t)(h - 1) * (size_t)y_stride_dst + (size_t)w ||
        u_cap < (size_t)(dch - 1) * (size_t)u_stride_dst + (size_t)dcw ||
        v_cap < (size_t)(dch - 1) * (size_t)v_stride_dst + (size_t)dcw)
        return YC_ERR_BUFFER;

    copy_plane(y_src, y_stride_src, y_dst, y_stride_dst, w, h);  /* 亮度逐像素不动 */
    if (src_fmt == dst_fmt) {
        copy_plane(u_src, u_stride_src, u_dst, u_stride_dst, scw, sch);
        copy_plane(v_src, v_stride_src, v_dst, v_stride_dst, scw, sch);
        return YC_OK;
    }
    if (src_fmt == YC_FMT_422) {              /* 422 -> 420：垂直抽取 */
        decimate_v(u_src, u_stride_src, u_dst, u_stride_dst, scw, sch, dch);
        decimate_v(v_src, v_stride_src, v_dst, v_stride_dst, scw, sch, dch);
    } else {                                  /* 420 -> 422：垂直插值 */
        upsample_v(u_src, u_stride_src, u_dst, u_stride_dst, scw, sch, dch);
        upsample_v(v_src, v_stride_src, v_dst, v_stride_dst, scw, sch, dch);
    }
    return YC_OK;
}

/* ---- 矩阵系数（full-range，定点） ---- */
/* BT.601: Y=( 77R+150G+ 29B)>>8                BT.709: Y=(218R+732G+ 74B)>>10
 *         U=(-43R- 85G+128B)>>8 + 128                  U=(-117R-395G+512B)>>10 + 128
 *         V=(128R-107G- 21B)>>8 + 128                  V=(512R-465G- 47B)>>10 + 128 */
int rgb_to_yuv420_m(const unsigned char* rgb, int w, int h, int rgb_stride,
                    unsigned char* y, int y_stride,
                    unsigned char* u, int u_stride,
                    unsigned char* v, int v_stride, int matrix) {
    if (!rgb || !y || !u || !v) return YC_ERR_ARG;
    if (matrix != YC_MATRIX_BT601 && matrix != YC_MATRIX_BT709) return YC_ERR_FORMAT;
    if (w <= 0 || h <= 0 || rgb_stride < w * 3 || y_stride < w) return YC_ERR_SIZE;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    if (u_stride < cw || v_stride < cw) return YC_ERR_SIZE;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            int yy = (matrix == YC_MATRIX_BT601)
                ? (77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8
                : (218 * p[0] + 732 * p[1] + 74 * p[2] + 512) >> 10;
            y[(size_t)j * y_stride + i] = (unsigned char)yy;
        }
    for (int j = 0; j < ch; ++j)
        for (int i = 0; i < cw; ++i) {
            int x0 = i * 2, y0 = j * 2;
            int x1 = (x0 + 1 < w) ? x0 + 1 : x0;
            int y1 = (y0 + 1 < h) ? y0 + 1 : y0;
            int su = 0, sv = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    int xx = dx ? x1 : x0, yy = dy ? y1 : y0;
                    const unsigned char* p = rgb + (size_t)yy * rgb_stride + (size_t)xx * 3;
                    if (matrix == YC_MATRIX_BT601) {
                        su += ((-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8) + 128;
                        sv += ((128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8) + 128;
                    } else {
                        su += ((-117 * p[0] - 395 * p[1] + 512 * p[2] + 512) >> 10) + 128;
                        sv += ((512 * p[0] - 465 * p[1] - 47 * p[2] + 512) >> 10) + 128;
                    }
                }
            u[(size_t)j * u_stride + i] = (unsigned char)(su / 4);
            v[(size_t)j * v_stride + i] = (unsigned char)(sv / 4);
        }
    return YC_OK;
}

/* BT.601 逆: R=Y+1.402V  G=Y-0.344U-0.714V  B=Y+1.772U
 * BT.709 逆: R=Y+1.575V  G=Y-0.187U-0.468V  B=Y+1.856U */
int yuv420_to_rgb_m(const unsigned char* y, int y_stride,
                    const unsigned char* u, int u_stride,
                    const unsigned char* v, int v_stride,
                    int w, int h, unsigned char* rgb, int rgb_stride, int matrix) {
    if (!y || !u || !v || !rgb) return YC_ERR_ARG;
    if (matrix != YC_MATRIX_BT601 && matrix != YC_MATRIX_BT709) return YC_ERR_FORMAT;
    if (w <= 0 || h <= 0 || rgb_stride < w * 3 || y_stride < w) return YC_ERR_SIZE;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int yy = y[(size_t)j * y_stride + i];
            int uu = u[(size_t)(j / 2) * u_stride + i / 2] - 128;
            int vv = v[(size_t)(j / 2) * v_stride + i / 2] - 128;
            int r, g, b;
            if (matrix == YC_MATRIX_BT601) {
                r = yy + ((1436 * vv) >> 10);
                g = yy - ((352 * uu + 731 * vv) >> 10);
                b = yy + ((1815 * uu) >> 10);
            } else {
                r = yy + ((1613 * vv) >> 10);
                g = yy - ((192 * uu + 479 * vv) >> 10);
                b = yy + ((1900 * uu) >> 10);
            }
            unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    return YC_OK;
}
