#include "grad.h"
#include <math.h>

static int at(const unsigned char* s, int w, int h, int x, int y) {
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x >= w) x = w - 1; if (y >= h) y = h - 1;
    return s[y * w + x];
}

int grad_magnitude(const unsigned char* src, int w, int h, float* out) {
    if (!src || !out || w <= 0 || h <= 0) return -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float gx = (float)(at(src, w, h, x + 1, y - 1) + 2 * at(src, w, h, x + 1, y) + at(src, w, h, x + 1, y + 1))
                     - (float)(at(src, w, h, x - 1, y - 1) + 2 * at(src, w, h, x - 1, y) + at(src, w, h, x - 1, y + 1));
            float gy = (float)(at(src, w, h, x - 1, y + 1) + 2 * at(src, w, h, x, y + 1) + at(src, w, h, x + 1, y + 1))
                     - (float)(at(src, w, h, x - 1, y - 1) + 2 * at(src, w, h, x, y - 1) + at(src, w, h, x + 1, y - 1));
            out[y * w + x] = sqrtf(gx * gx + gy * gy);
        }
    return 0;
}

int grad_edge_count(const float* mag, int w, int h, float thresh) {
    if (!mag || w <= 0 || h <= 0 || thresh < 0.0f) return -1;
    int n = 0;
    for (int i = 0; i < w * h; ++i) if (mag[i] >= thresh) ++n;
    return n;
}
