/* 四组尺寸的 422<->420 基准：色差均方根、往返一致性、耗时，
 * 以及矩阵切换（BT.601 vs BT.709）对色度的影响。 */
#define _POSIX_C_SOURCE 199309L
#include "../src/yuvconv.h"
#include "../src/chroma.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static double rms_diff(const unsigned char* a, const unsigned char* b, size_t n,
                       int* max_abs, int* identical) {
    double s = 0.0;
    int mx = 0, same = 0;
    for (size_t i = 0; i < n; ++i) {
        int d = (int)a[i] - (int)b[i];
        if (d == 0) ++same;
        if (d < 0) d = -d;
        if (d > mx) mx = d;
        s += (double)d * d;
    }
    if (max_abs) *max_abs = mx;
    if (identical) *identical = same;
    return sqrt(s / (double)n);
}

static void fill_rgb(unsigned char* rgb, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + ((size_t)j * w + i) * 3;
            p[0] = (unsigned char)(i * 255 / (w > 1 ? w - 1 : 1));
            p[1] = (unsigned char)(j * 255 / (h > 1 ? h - 1 : 1));
            p[2] = (unsigned char)(((i / 8) + (j / 8)) & 1 ? 230 : 30); /* 8px 棋盘，制造色度细节 */
        }
}

int main(void) {
    static const int sizes[][2] = { {320, 240}, {640, 480}, {1280, 720}, {641, 479} };
    const int iters = 100;
    printf("%-10s | %-22s | %-22s | %-14s | %-12s | %-12s\n",
           "size", "rt420 RMS U/V (max)", "rt422 RMS U/V (max)", "identical U/V", "422>420 ms", "420>422 ms");
    for (int s = 0; s < 4; ++s) {
        int w = sizes[s][0], h = sizes[s][1];
        int cw = (w + 1) / 2, ch420 = (h + 1) / 2;
        size_t yn = (size_t)w * h, c4n = (size_t)cw * ch420, c2n = (size_t)cw * h;
        unsigned char* rgb = malloc(yn * 3);
        unsigned char *y0 = malloc(yn), *u0 = malloc(c4n), *v0 = malloc(c4n);   /* 原 420 */
        unsigned char *y9 = malloc(yn), *u9 = malloc(c4n), *v9 = malloc(c4n);   /* BT.709 420 */
        unsigned char *y2 = malloc(yn), *u2 = malloc(c2n), *v2 = malloc(c2n);   /* 上采样 422 */
        unsigned char *yr = malloc(yn), *ur = malloc(c4n), *vr = malloc(c4n);   /* 往返 420 */
        unsigned char *y2r = malloc(yn), *u2r = malloc(c2n), *v2r = malloc(c2n);/* 往返 422 */
        if (!rgb || !y0 || !u0 || !v0 || !y9 || !u9 || !v9 || !y2 || !u2 || !v2 || !yr || !ur || !vr || !y2r || !u2r || !v2r) {
            fprintf(stderr, "alloc failed\n");
            return 1;
        }
        fill_rgb(rgb, w, h);
        rgb_to_yuv420(rgb, w, h, w * 3, y0, w, u0, cw, v0, cw);                       /* 原四二〇路径 */
        rgb_to_yuv420_m(rgb, w, h, w * 3, y9, w, u9, cw, v9, cw, YC_MATRIX_BT709);    /* 换矩阵 */

        /* 往返 1：420 -> 422 -> 420 */
        yuv_convert(YC_FMT_420, YC_FMT_422, y0, w, u0, cw, v0, cw, w, h,
                    y2, w, yn, u2, cw, c2n, v2, cw, c2n);
        yuv_convert(YC_FMT_422, YC_FMT_420, y2, w, u2, cw, v2, cw, w, h,
                    yr, w, yn, ur, cw, c4n, vr, cw, c4n);
        /* 往返 2：422 -> 420 -> 422 */
        yuv_convert(YC_FMT_422, YC_FMT_420, y2, w, u2, cw, v2, cw, w, h,
                    yr, w, yn, ur, cw, c4n, vr, cw, c4n);
        yuv_convert(YC_FMT_420, YC_FMT_422, yr, w, ur, cw, vr, cw, w, h,
                    y2r, w, yn, u2r, cw, c2n, v2r, cw, c2n);

        int mxu, mxv, idu, idv;
        double ru = rms_diff(u0, ur, c4n, &mxu, &idu);
        double rv = rms_diff(v0, vr, c4n, &mxv, &idv);
        int mx2u, mx2v;
        double r2u = rms_diff(u2, u2r, c2n, &mx2u, NULL);
        double r2v = rms_diff(v2, v2r, c2n, &mx2v, NULL);

        /* 矩阵切换对色度的影响（同一 RGB，601 vs 709） */
        double mu = rms_diff(u0, u9, c4n, NULL, NULL);
        double mv = rms_diff(v0, v9, c4n, NULL, NULL);

        /* 耗时 */
        double t0 = now_ms();
        for (int it = 0; it < iters; ++it)
            yuv_convert(YC_FMT_422, YC_FMT_420, y2, w, u2, cw, v2, cw, w, h,
                        yr, w, yn, ur, cw, c4n, vr, cw, c4n);
        double t_down = (now_ms() - t0) / iters;
        t0 = now_ms();
        for (int it = 0; it < iters; ++it)
            yuv_convert(YC_FMT_420, YC_FMT_422, y0, w, u0, cw, v0, cw, w, h,
                        y2, w, yn, u2, cw, c2n, v2, cw, c2n);
        double t_up = (now_ms() - t0) / iters;

        printf("%4dx%-5d | %6.3f / %6.3f (%d/%d) | %6.3f / %6.3f (%d/%d) | %5.1f%% / %5.1f%% | %10.3f | %10.3f\n",
               w, h, ru, rv, mxu, mxv, r2u, r2v, mx2u, mx2v,
               100.0 * idu / (double)c4n, 100.0 * idv / (double)c4n, t_down, t_up);
        printf("           matrix BT.601 vs BT.709 chroma RMS: U %.3f, V %.3f\n", mu, mv);
        free(rgb); free(y0); free(u0); free(v0); free(y9); free(u9); free(v9);
        free(y2); free(u2); free(v2); free(yr); free(ur); free(vr);
        free(y2r); free(u2r); free(v2r);
    }
    return 0;
}
