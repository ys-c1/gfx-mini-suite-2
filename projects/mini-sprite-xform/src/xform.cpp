#include "xform.h"
#include <cfloat>

int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst) {
    if (!src || !dst || n <= 0) return -1;
    for (int i = 0; i < n; ++i) {
        dst[i].x = src[i].x * sx + tx;
        dst[i].y = src[i].y * sy + ty;
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
