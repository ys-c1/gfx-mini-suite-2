/* 穷举验证: 所有 1..NMAX 的宽高组合 x 6 种方向, 比较两条流水线的
 * Y/U/V 逐字节输出。覆盖全部奇偶组合与最小跨度 (stride == width)。 */
#define _POSIX_C_SOURCE 200809L
#include "../src/yuvrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NMAX 13

static unsigned char* alloc(size_t n) {
    unsigned char* p = malloc(n);
    if (!p) { fprintf(stderr, "oom\n"); exit(2); }
    return p;
}

int main(void) {
    srand(12345);
    long cases = 0, bytes = 0;
    static const yr_op ops[] = {
        YR_IDENTITY, YR_R90, YR_R180, YR_R270, YR_MIRROR_H, YR_MIRROR_V
    };

    for (int w = 1; w <= NMAX; ++w)
      for (int h = 1; h <= NMAX; ++h) {
        size_t rgb_n = (size_t)w * h * 3;
        unsigned char* rgb = alloc(rgb_n);
        for (size_t i = 0; i < rgb_n; ++i) rgb[i] = (unsigned char)(rand() >> 4);

        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        unsigned char* sy = alloc((size_t)w * h);
        unsigned char* su = alloc((size_t)cw * ch);
        unsigned char* sv = alloc((size_t)cw * ch);
        if (yr_rgb_to_yuv420(rgb, w, h, w * 3, sy, w, su, cw, sv, cw) != 0)
            { printf("bad convert %dx%d\n", w, h); return 2; }

        for (int oi = 0; oi < 6; ++oi) {
            yr_op op = ops[oi];
            int dw, dh;
            yr_dst_size(op, w, h, &dw, &dh);
            int dcw = (dw + 1) / 2, dch = (dh + 1) / 2;

            /* A: 先转色后转平面 */
            unsigned char* aY = alloc((size_t)dw * dh);
            unsigned char* aU = alloc((size_t)dcw * dch);
            unsigned char* aV = alloc((size_t)dcw * dch);
            if (yr_transform_yuv420(op, sy, w, su, cw, sv, cw, w, h,
                                    aY, dw, aU, dcw, aV, dcw) != 0)
                { printf("bad transform A\n"); return 2; }

            /* B: 先转 RGB 后转色 */
            unsigned char* drgb = alloc((size_t)dw * dh * 3);
            unsigned char* bY = alloc((size_t)dw * dh);
            unsigned char* bU = alloc((size_t)dcw * dch);
            unsigned char* bV = alloc((size_t)dcw * dch);
            if (yr_transform_rgb(op, rgb, w, h, w * 3,
                                 drgb, dw, dh, dw * 3) != 0 ||
                yr_rgb_to_yuv420(drgb, dw, dh, dw * 3,
                                 bY, dw, bU, dcw, bV, dcw) != 0)
                { printf("bad pipeline B\n"); return 2; }

            if (memcmp(aY, bY, (size_t)dw * dh) ||
                memcmp(aU, bU, (size_t)dcw * dch) ||
                memcmp(aV, bV, (size_t)dcw * dch)) {
                printf("MISMATCH w=%d h=%d op=%d\n", w, h, op);
                return 1;
            }
            ++cases;
            bytes += (long)dw * dh + 2L * dcw * dch;
            free(aY); free(aU); free(aV);
            free(drgb); free(bY); free(bU); free(bV);
        }
        free(rgb); free(sy); free(su); free(sv);
      }
    printf("exhaustive OK: %ld cases, %ld compared bytes, 0 mismatches\n",
           cases, bytes);
    return 0;
}
