#include "chroma.h"
#include <stddef.h>
#include <stdint.h>

/* Bytes needed by one row of each plane (widened before the +1). */
static size_t rgb_row_bytes(int w) { return (size_t)w * 3u; }
static size_t luma_row_bytes(int w) { return (size_t)w; }
static size_t chroma_row_bytes(int w) { return ((size_t)w + 1u) / 2u; }

/*
 * A stride must be positive and at least as large as the row it carries.
 * Comparing in int64 avoids overflow in the caller-supplied int values.
 */
static int stride_ok(int stride, size_t needed) {
    return stride > 0 && (uint64_t)stride >= (uint64_t)needed;
}

/*
 * Validate that walking `rows` rows of `stride` bytes starting at a base
 * pointer stays inside addressable memory.  This catches absurd w/h/stride
 * combinations that would otherwise wrap the size_t offset.
 */
static int extent_ok(int rows, int stride, size_t used_per_row) {
    if (rows <= 0) return 1;
    uint64_t span = (uint64_t)(rows - 1) * (uint64_t)stride + (uint64_t)used_per_row;
    return span <= (uint64_t)SIZE_MAX;
}

int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride) {
    if (!rgb || !y || !u || !v || w <= 0 || h <= 0) return -1;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    size_t rgb_used = rgb_row_bytes(w);
    size_t y_used = luma_row_bytes(w);
    size_t c_used = chroma_row_bytes(w);
    if (!stride_ok(rgb_stride, rgb_used) || !stride_ok(y_stride, y_used) ||
        !stride_ok(u_stride, c_used) || !stride_ok(v_stride, c_used))
        return -1;
    if (!extent_ok(h, rgb_stride, rgb_used) || !extent_ok(h, y_stride, y_used) ||
        !extent_ok(ch, u_stride, c_used) || !extent_ok(ch, v_stride, c_used))
        return -1;

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const unsigned char* p = rgb + (size_t)j * (size_t)rgb_stride + (size_t)i * 3;
            y[(size_t)j * (size_t)y_stride + (size_t)i] = (unsigned char)((77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8);
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
                    const unsigned char* p = rgb + (size_t)yy * (size_t)rgb_stride + (size_t)xx * 3;
                    su += ((-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8) + 128;
                    sv += ((128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8) + 128;
                }
            u[(size_t)j * (size_t)u_stride + (size_t)i] = (unsigned char)(su / 4);
            v[(size_t)j * (size_t)v_stride + (size_t)i] = (unsigned char)(sv / 4);
        }
    return 0;
}

int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride) {
    if (!y || !u || !v || !rgb || w <= 0 || h <= 0) return -1;
    int ch = (h + 1) / 2;
    size_t rgb_used = rgb_row_bytes(w);
    size_t y_used = luma_row_bytes(w);
    size_t c_used = chroma_row_bytes(w);
    if (!stride_ok(rgb_stride, rgb_used) || !stride_ok(y_stride, y_used) ||
        !stride_ok(u_stride, c_used) || !stride_ok(v_stride, c_used))
        return -1;
    if (!extent_ok(h, y_stride, y_used) || !extent_ok(h, rgb_stride, rgb_used) ||
        !extent_ok(ch, u_stride, c_used) || !extent_ok(ch, v_stride, c_used))
        return -1;

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int yy = y[(size_t)j * (size_t)y_stride + (size_t)i];
            int uu = u[(size_t)(j / 2) * (size_t)u_stride + (size_t)i / 2] - 128;
            int vv = v[(size_t)(j / 2) * (size_t)v_stride + (size_t)i / 2] - 128;
            int r = yy + ((1436 * vv) >> 10);
            int g = yy - ((352 * uu + 731 * vv) >> 10);
            int b = yy + ((1815 * uu) >> 10);
            unsigned char* p = rgb + (size_t)j * (size_t)rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    return 0;
}
