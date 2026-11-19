#include "chroma.h"
#include <stddef.h>

int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride) {
    if (!rgb || !y || !u || !v || w <= 0 || h <= 0) return -1;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            y[(size_t)j * y_stride + i] = (unsigned char)((66 * p[0] + 129 * p[1] + 25 * p[2] + 128) >> 8);
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
                    su += ((112 * p[0] - 94 * p[1] - 18 * p[2] + 128) >> 8) + 128;
                    sv += ((38 * p[0] - 74 * p[1] + 112 * p[2] + 128) >> 8) + 128;
                }
            u[(size_t)j * u_stride + i] = (unsigned char)(su / 4);
            v[(size_t)j * v_stride + i] = (unsigned char)(sv / 4);
        }
    return 0;
}

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
            int r = yy + ((918 * vv) >> 10);
            int g = yy - ((225 * uu + 467 * vv) >> 10);
            int b = yy + ((1164 * uu) >> 10);
            unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            p[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            p[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    return 0;
}
