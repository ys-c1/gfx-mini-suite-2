#include "chroma.h"
#include <stddef.h>
#include <stdlib.h>

int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride) {
    if (!rgb || !y || !u || !v || w <= 0 || h <= 0) return -1;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            y[(size_t)j * y_stride + i] = (unsigned char)((77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8);
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
                    su += ((-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8) + 128;
                    sv += ((128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8) + 128;
                }
            u[(size_t)j * u_stride + i] = (unsigned char)(su / 4);
            v[(size_t)j * v_stride + i] = (unsigned char)(sv / 4);
        }
    return 0;
}

int chroma420_plane_dims(int w, int h, int* cw, int* ch) {
    if (w <= 0 || h <= 0 || !cw || !ch) return CHROMA_ERR_ARG;
    *cw = (w + 1) / 2;
    *ch = (h + 1) / 2;
    return CHROMA_OK;
}

size_t chroma420_plane_bytes(int c_stride, int cw, int ch) {
    if (c_stride <= 0 || cw <= 0 || ch <= 0 || c_stride < cw) return 0;
    return (size_t)(ch - 1) * (size_t)c_stride + (size_t)cw;
}

/* 向远离 0 方向四舍五入除以 d（d>0） */
static int round_div(int num, int d) {
    return num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d);
}

static int clamp_u8(int x) {
    return x < 0 ? 0 : (x > 255 ? 255 : x);
}

static int clampi(int x, int hi) { /* 钳制到 [0, hi-1] */
    if (x < 0) return 0;
    if (x >= hi) return hi - 1;
    return x;
}

/* 水平方向：把一行 c 个色度样本放大到 w 个，落位规则见 chroma.h */
static void upsample_h(const unsigned char* src, int c,
                       int w, enum chroma_up_mode mode, int* dst) {
    for (int i = 0; i < w; ++i) {
        int cx = i / 2;
        if ((i & 1) == 0) {                 /* 规则1：命中采样点 */
            dst[i] = src[cx];
            continue;
        }
        if (cx + 1 >= c) {                  /* 规则3：边界复制 */
            dst[i] = src[c - 1];
            continue;
        }
        int a = src[cx], b = src[cx + 1];
        if (mode == CHROMA_UP_NEAREST) {   /* 规则2-N：取左样本 */
            dst[i] = a;
        } else if (mode == CHROMA_UP_BILINEAR) {
            dst[i] = (a + b + 1) >> 1;     /* 规则2-L：1/2 混合 */
        } else {                            /* 规则2-C：Catmull-Rom 1/16 */
            int p0 = src[clampi(cx - 1, c)], p3 = src[clampi(cx + 2, c)];
            dst[i] = round_div(-p0 + 9 * a + 9 * b - p3, 16);
        }
    }
}

/* 垂直方向：src 为按行排列的 int 中间值，c 行、w 列 */
static int upsample_v(const int* src, int c, int stride_row,
                      int j, int i, enum chroma_up_mode mode) {
    int cy = j / 2;
    if ((j & 1) == 0)                       /* 规则1：命中采样点 */
        return src[(size_t)cy * stride_row + i];
    if (cy + 1 >= c)                        /* 规则3：边界复制 */
        return src[(size_t)(c - 1) * stride_row + i];
    int a = src[(size_t)cy * stride_row + i];
    int b = src[(size_t)(cy + 1) * stride_row + i];
    if (mode == CHROMA_UP_NEAREST) return a;
    if (mode == CHROMA_UP_BILINEAR) return (a + b + 1) >> 1;
    const int* r0 = src + (size_t)clampi(cy - 1, c) * stride_row + i;
    const int* r3 = src + (size_t)clampi(cy + 2, c) * stride_row + i;
    return round_div(-(*r0) + 9 * a + 9 * b - (*r3), 16);
}

/* 旧接口：原始逐行循环原样保留，保证最近点还原逐像素不变 */
int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride) {
    if (!y || !u || !v || !rgb || w <= 0 || h <= 0) return -1;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int yy = y[(size_t)j * y_stride + i];
            int uu = u[(size_t)(j / 2) * u_stride + i / 2] - 128;
            int vv = v[(size_t)(j / 2) * v_stride + i / 2] - 128;
            int r = yy + ((1436 * vv) >> 10);
            int g = yy - ((352 * uu + 731 * vv) >> 10);
            int b = yy + ((1815 * uu) >> 10);
            unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    return 0;
}

int yuv420_to_rgb_ex(const unsigned char* y, int y_stride,
                     const unsigned char* u, int u_stride,
                     const unsigned char* v, int v_stride,
                     int w, int h, unsigned char* rgb, int rgb_stride,
                     enum chroma_up_mode mode,
                     size_t u_cap, size_t v_cap) {
    if (!y || !u || !v || !rgb || w <= 0 || h <= 0 ||
        (mode != CHROMA_UP_NEAREST && mode != CHROMA_UP_BILINEAR &&
         mode != CHROMA_UP_BICUBIC) ||
        y_stride < w || u_stride <= 0 || v_stride <= 0 || rgb_stride < w * 3)
        return CHROMA_ERR_ARG;

    int cw, ch;
    if (chroma420_plane_dims(w, h, &cw, &ch) != CHROMA_OK) return CHROMA_ERR_ARG;
    if (u_stride < cw || v_stride < cw) return CHROMA_ERR_ARG;

    size_t need = chroma420_plane_bytes(u_stride, cw, ch);
    if (need == 0 || u_cap < need || v_cap < chroma420_plane_bytes(v_stride, cw, ch))
        return CHROMA_ERR_BUFFER;

    int status = CHROMA_OK;
    if (w == 1 || h == 1)
        status = CHROMA_DEGENERATE;
    else if ((w & 1) || (h & 1))
        status = CHROMA_ODD;

    /* 最近点走与旧接口完全相同的直接索引路径，避免任何中间精度差异。
     * 注意 w==1/h==1 不短路：尺寸为 1 的只是其中一个轴（该轴全部命中
     * 规则1自然复制），另一个轴仍须正常滤波。 */
    if (mode == CHROMA_UP_NEAREST) {
        yuv420_to_rgb(y, y_stride, u, u_stride, v, v_stride, w, h, rgb, rgb_stride);
        return status;
    }

    /* 双线性/双三次：可分离，先水平后垂直，u/v 各一份中间缓冲 */
    int* tmp = (int*)malloc((size_t)cw * (size_t)ch * (size_t)w * 2 * sizeof(int));
    if (!tmp) return CHROMA_ERR_NOMEM;
    int* hu = tmp;
    int* hv = tmp + (size_t)cw * (size_t)ch * (size_t)w;

    for (int cy = 0; cy < ch; ++cy) {
        upsample_h(u + (size_t)cy * u_stride, cw, w, mode,
                   hu + (size_t)cy * w);
        upsample_h(v + (size_t)cy * v_stride, cw, w, mode,
                   hv + (size_t)cy * w);
    }

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int uu = clamp_u8(upsample_v(hu, ch, w, j, i, mode)) - 128;
            int vv = clamp_u8(upsample_v(hv, ch, w, j, i, mode)) - 128;
            int yy = y[(size_t)j * y_stride + i];
            int r = yy + ((1436 * vv) >> 10);
            int g = yy - ((352 * uu + 731 * vv) >> 10);
            int b = yy + ((1815 * uu) >> 10);
            unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }

    free(tmp);
    return status;
}
