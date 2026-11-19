#define _POSIX_C_SOURCE 199309L
/* malloc 后已有非空检查并 exit，GCC 仍对 CHECK 宏路径给出误报，定点抑制 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../src/chroma.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned char clamp8(int x) { return (unsigned char)(x < 0 ? 0 : x > 255 ? 255 : x); }

/* 旧最近点公式的独立参考实现，用于逐像素回归 */
static void ref_nearest(const unsigned char* y, int ys, const unsigned char* u, int us,
                        const unsigned char* v, int vs, int w, int h,
                        unsigned char* rgb, int rs) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int yy = y[(size_t)j * ys + i];
            int uu = u[(size_t)(j / 2) * us + i / 2] - 128;
            int vv = v[(size_t)(j / 2) * vs + i / 2] - 128;
            int r = yy + ((1436 * vv) >> 10);
            int g = yy - ((352 * uu + 731 * vv) >> 10);
            int b = yy + ((1815 * uu) >> 10);
            unsigned char* p = rgb + (size_t)j * rs + (size_t)i * 3;
            p[0] = clamp8(r); p[1] = clamp8(g); p[2] = clamp8(b);
        }
}

static void make_gradient(unsigned char* rgb, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + ((size_t)j * w + i) * 3;
            p[0] = (unsigned char)(i * 17 + j * 9);
            p[1] = (unsigned char)(j * 31);
            p[2] = (unsigned char)(128 + 60 * ((i + j) & 1));
        }
}

/* 类文字图案：明暗相间的横竖条带，带高频跳变的色度边缘 */
static void make_textlike(unsigned char* rgb, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + ((size_t)j * w + i) * 3;
            int on = (((i * 7) / 4 + (j * 5) / 3) & 3) < 2;
            if (on) { p[0] = 240; p[1] = 235; p[2] = 210; }
            else    { p[0] = 20;  p[1] = 25;  p[2] = 60;  }
        }
}

/* 与某像素相邻（8 邻域，含自身）是否存在大色差，用于圈出文字边缘带 */
static int is_edge_band(const unsigned char* rgb, int w, int h, int i, int j) {
    const unsigned char* c = rgb + ((size_t)j * w + i) * 3;
    for (int dj = -1; dj <= 1; ++dj)
        for (int di = -1; di <= 1; ++di) {
            int ni = i + di, nj = j + dj;
            if (ni < 0 || ni >= w || nj < 0 || nj >= h) continue;
            const unsigned char* q = rgb + ((size_t)nj * w + ni) * 3;
            if (abs((int)c[0] - q[0]) + abs((int)c[1] - q[1]) +
                abs((int)c[2] - q[2]) > 300)
                return 1;
        }
    return 0;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static const char* mode_name(enum chroma_up_mode m) {
    return m == CHROMA_UP_NEAREST ? "nearest " : m == CHROMA_UP_BILINEAR ? "bilinear" : "bicubic ";
}

static void run_case(int w, int h, int textlike, int iters) {
    size_t npix = (size_t)w * h;
    unsigned char* rgb = NULL;
    unsigned char* out = NULL;
    unsigned char* ref = NULL;
    unsigned char* y = NULL;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    size_t ccap = (size_t)cw * ch + 16;
    unsigned char* u = NULL;
    unsigned char* v = NULL;
    rgb = malloc(npix * 3); out = malloc(npix * 3); ref = malloc(npix * 3);
    y = malloc(npix); u = malloc(ccap); v = malloc(ccap);
    if (!rgb || !out || !ref || !y || !u || !v) {
        printf("malloc failed for %dx%d\n", w, h);
        exit(2);
    }
    if (textlike) make_textlike(rgb, w, h); else make_gradient(rgb, w, h);

    CHECK(rgb_to_yuv420(rgb, w, h, w * 3, y, w, u, cw, v, cw) == 0);
    ref_nearest(y, w, u, cw, v, cw, w, h, ref, w * 3);

    for (int m = 0; m < 3; ++m) {
        enum chroma_up_mode mode = (enum chroma_up_mode)m;
        double sse = 0.0, edge_sse = 0.0;
        long edge_n = 0, worst = 0, overshoot = 0;
        double t0 = now_sec();
        int st = -99;
        for (int it = 0; it < iters; ++it) {
            st = yuv420_to_rgb_ex(y, w, u, cw, v, cw, w, h, out, w * 3,
                                  mode, ccap, ccap);
        }
        double dt = (now_sec() - t0) / iters;
        CHECK(st == CHROMA_OK || st == CHROMA_ODD || st == CHROMA_DEGENERATE);

        if (mode == CHROMA_UP_NEAREST) {
            /* 旧最近点还原逐像素不变：ex 路径 == 旧接口 == 独立参考 */
            unsigned char* legacy = malloc(npix * 3);
            CHECK(yuv420_to_rgb(y, w, u, cw, v, cw, w, h, legacy, w * 3) == 0);
            CHECK(memcmp(legacy, ref, npix * 3) == 0);
            CHECK(memcmp(out, ref, npix * 3) == 0);
            free(legacy);
        }

        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const unsigned char* a = rgb + ((size_t)j * w + i) * 3;
                const unsigned char* b = out + ((size_t)j * w + i) * 3;
                long d0 = (long)a[0] - b[0], d1 = (long)a[1] - b[1], d2 = (long)a[2] - b[2];
                double d2s = (double)(d0 * d0 + d1 * d1 + d2 * d2);
                sse += d2s;
                long pixmax = labs(d0);
                if (labs(d1) > pixmax) pixmax = labs(d1);
                if (labs(d2) > pixmax) pixmax = labs(d2);
                if (pixmax > worst) worst = pixmax;
                if (is_edge_band(rgb, w, h, i, j)) {
                    edge_sse += d2s;
                    ++edge_n;
                    if (pixmax > 64) ++overshoot;
                }
            }
        double rms = sqrt(sse / (3.0 * (double)npix));
        double erms = edge_n ? sqrt(edge_sse / (3.0 * (double)edge_n)) : 0.0;
        printf("| %2dx%-2d | %s | %8.3f | %8.3f | %4ld | %6ld | %10.3f | %s |\n",
               w, h, mode_name(mode), rms, erms, worst, overshoot,
               dt * 1e6, textlike ? "text" : "grad");
    }
    free(rgb); free(out); free(ref); free(y); free(u); free(v);
}

/* [5] 32x24 锐利彩边专项：测量边缘带 RMSE、大误差像素占比与最差像素。
 * 图案为宽度 2~3 像素交替的高饱和彩条，模拟文字/细笔画的色度跳变。 */
static void edge_study(void) {
    enum { W = 32, H = 24 };
    size_t npix = (size_t)W * H;
    unsigned char* rgb = malloc(npix * 3);
    unsigned char* out = malloc(npix * 3);
    unsigned char* y = malloc(npix);
    int cw = (W + 1) / 2, ch = (H + 1) / 2;
    size_t ccap = (size_t)cw * ch;
    unsigned char* u = malloc(ccap);
    unsigned char* v = malloc(ccap);
    if (!rgb || !out || !y || !u || !v) { printf("malloc failed\n"); exit(2); }

    static const unsigned char pal[4][3] = {
        {230, 40, 40}, {40, 60, 230}, {240, 230, 60}, {40, 200, 120}
    };
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            int stripe = ((i / 2) + (j / 3)) & 3;
            unsigned char* p = rgb + ((size_t)j * W + i) * 3;
            p[0] = pal[stripe][0]; p[1] = pal[stripe][1]; p[2] = pal[stripe][2];
        }
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, cw, v, cw) == 0);

    printf("[5] sharp-edge study 32x24 (time per call, us)\n");
    printf("| mode     | edge RMSE | edge max | big-err pixels(>48) | big-err %% | time(us) |\n");
    printf("|----------|----------:|---------:|--------------------:|----------:|---------:|\n");
    for (int m = 0; m < 3; ++m) {
        enum chroma_up_mode mode = (enum chroma_up_mode)m;
        int iters = 3000;
        double t0 = now_sec();
        int st = 0;
        for (int it = 0; it < iters; ++it)
            st = yuv420_to_rgb_ex(y, W, u, cw, v, cw, W, H, out, W * 3,
                                  mode, ccap, ccap);
        double dt = (now_sec() - t0) / iters;
        CHECK(st == CHROMA_OK);
        double edge_sse = 0.0;
        long edge_n = 0, edge_max = 0, big = 0;
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                if (!is_edge_band(rgb, W, H, i, j)) continue;
                const unsigned char* a = rgb + ((size_t)j * W + i) * 3;
                const unsigned char* b = out + ((size_t)j * W + i) * 3;
                long d0 = (long)a[0] - b[0], d1 = (long)a[1] - b[1], d2 = (long)a[2] - b[2];
                edge_sse += (double)(d0 * d0 + d1 * d1 + d2 * d2);
                ++edge_n;
                long pixmax = labs(d0);
                if (labs(d1) > pixmax) pixmax = labs(d1);
                if (labs(d2) > pixmax) pixmax = labs(d2);
                if (pixmax > edge_max) edge_max = pixmax;
                if (pixmax > 48) ++big;
            }
        double erms = sqrt(edge_sse / (3.0 * (double)edge_n));
        printf("| %s | %9.3f | %8ld | %19ld | %8.2f%% | %9.3f |\n",
               mode_name(mode), erms, edge_max, big,
               100.0 * (double)big / (double)edge_n, dt * 1e6);
    }
    free(rgb); free(out); free(y); free(u); free(v);
    printf("\n");
}

int main(void) {
    /* ---------- 1. 旧测试保留：9x7 渐变往返误差 ---------- */
    enum { W = 9, H = 7 };
    unsigned char rgb[W * H * 3], back[W * H * 3];
    unsigned char y[W * H], u[8 * 4], v[8 * 4];
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            unsigned char* p = rgb + (j * W + i) * 3;
            p[0] = (unsigned char)(i * 16);
            p[1] = (unsigned char)(j * 16);
            p[2] = 100;
        }
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 8, v, 8) == 0);
    memset(back, 0, sizeof(back));
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, back, W * 3) == 0);
    int worst0 = 0;
    for (int i = 0; i < W * H * 3; ++i) {
        int d = (int)back[i] - (int)rgb[i];
        if (d < 0) d = -d;
        if (d > worst0) worst0 = d;
    }
    CHECK(worst0 <= 60);
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, NULL, W * 3) == -1);
    printf("[1] legacy 9x7 worst delta = %d\n\n", worst0);

    /* ---------- 2. 三类输入可区分返回码 ---------- */
    printf("[2] status codes (must be pairwise distinguishable)\n");
    int st;
    unsigned char big[64 * 64 * 3], yb[64 * 64], ub[64 * 64], vb[64 * 64];

    st = yuv420_to_rgb_ex(yb, 12, ub, 1, vb, 1, 1, 12, big, 36,
                          CHROMA_UP_BILINEAR, 12, 12);
    printf("  w=1 (1x12) bilinear -> %d (expect %d DEGENERATE)\n", st, CHROMA_DEGENERATE);
    CHECK(st == CHROMA_DEGENERATE);
    st = yuv420_to_rgb_ex(yb, 12, ub, 12, vb, 12, 12, 1, big, 36,
                          CHROMA_UP_BICUBIC, 12, 12);
    printf("  h=1 (12x1) bicubic  -> %d (expect %d DEGENERATE)\n", st, CHROMA_DEGENERATE);
    CHECK(st == CHROMA_DEGENERATE);
    st = yuv420_to_rgb_ex(yb, 9, ub, 5, vb, 5, 9, 7, big, 27,
                          CHROMA_UP_BILINEAR, sizeof ub, sizeof vb);
    printf("  odd (9x7)           -> %d (expect %d ODD)\n", st, CHROMA_ODD);
    CHECK(st == CHROMA_ODD);
    st = yuv420_to_rgb_ex(yb, 8, ub, 4, vb, 4, 8, 8, big, 24,
                          CHROMA_UP_NEAREST, sizeof ub, sizeof vb);
    printf("  even (8x8)          -> %d (expect %d OK)\n", st, CHROMA_OK);
    CHECK(st == CHROMA_OK);

    /* 缓冲不足：9x7 需要 5*4=20 字节，只给 16 */
    st = yuv420_to_rgb_ex(yb, 9, ub, 5, vb, 5, 9, 7, big, 27,
                          CHROMA_UP_BILINEAR, 16, sizeof vb);
    printf("  u short (need 20, cap 16) -> %d (expect %d ERR_BUFFER)\n", st, CHROMA_ERR_BUFFER);
    CHECK(st == CHROMA_ERR_BUFFER);
    st = yuv420_to_rgb_ex(yb, 9, ub, 5, vb, 5, 9, 7, big, 27,
                          CHROMA_UP_BILINEAR, sizeof ub, 19);
    printf("  v short (need 20, cap 19) -> %d (expect %d ERR_BUFFER)\n", st, CHROMA_ERR_BUFFER);
    CHECK(st == CHROMA_ERR_BUFFER);

    /* 非法参数与缓冲不足、成功三者互不混淆 */
    st = yuv420_to_rgb_ex(NULL, 9, ub, 5, vb, 5, 9, 7, big, 27,
                          CHROMA_UP_NEAREST, 64, 64);
    printf("  null y              -> %d (expect %d ERR_ARG)\n", st, CHROMA_ERR_ARG);
    CHECK(st == CHROMA_ERR_ARG);
    st = yuv420_to_rgb_ex(yb, 9, ub, 5, vb, 5, 9, 0, big, 27,
                          CHROMA_UP_NEAREST, 64, 64);
    printf("  h=0                 -> %d (expect %d ERR_ARG)\n", st, CHROMA_ERR_ARG);
    CHECK(st == CHROMA_ERR_ARG);
    st = yuv420_to_rgb_ex(yb, 9, ub, 5, vb, 5, 9, 7, big, 27,
                          (enum chroma_up_mode)9, 64, 64);
    printf("  bad mode            -> %d (expect %d ERR_ARG)\n", st, CHROMA_ERR_ARG);
    CHECK(st == CHROMA_ERR_ARG);
    st = yuv420_to_rgb_ex(yb, 9, ub, 4, vb, 4, 9, 7, big, 27,
                          CHROMA_UP_NEAREST, 64, 64);
    printf("  stride < cw         -> %d (expect %d ERR_ARG)\n", st, CHROMA_ERR_ARG);
    CHECK(st == CHROMA_ERR_ARG);
    printf("  bytes(9x7, stride5) = %zu (expect 20)\n\n",
           chroma420_plane_bytes(5, 5, 4));
    CHECK(chroma420_plane_bytes(5, 5, 4) == 20);

    /* ---------- 3. 对齐与边界取值规则 ---------- */
    printf("[3] alignment and boundary rules\n");
    /* 3a. 三种方式在所有偶数 i/j（含奇数宽最后一列）取值完全一致，且等于采样点 */
    for (int sz = 0; sz < 4; ++sz) {
        int dims[4][2] = {{8, 8}, {9, 7}, {1, 12}, {12, 1}};
        int ww = dims[sz][0], hh = dims[sz][1];
        size_t n = (size_t)ww * hh;
        unsigned char* r1 = malloc(n * 3); unsigned char* r2 = malloc(n * 3);
        unsigned char* r3 = malloc(n * 3);
        unsigned char* yy2 = malloc(n);
        int ccw = (ww + 1) / 2, cch = (hh + 1) / 2;
        size_t cc = (size_t)ccw * cch;
        unsigned char* uu2 = malloc(cc); unsigned char* vv2 = malloc(cc);
        make_gradient(r1, ww, hh);
        rgb_to_yuv420(r1, ww, hh, ww * 3, yy2, ww, uu2, ccw, vv2, ccw);
        yuv420_to_rgb_ex(yy2, ww, uu2, ccw, vv2, ccw, ww, hh, r1, ww * 3,
                         CHROMA_UP_NEAREST, cc, cc);
        yuv420_to_rgb_ex(yy2, ww, uu2, ccw, vv2, ccw, ww, hh, r2, ww * 3,
                         CHROMA_UP_BILINEAR, cc, cc);
        yuv420_to_rgb_ex(yy2, ww, uu2, ccw, vv2, ccw, ww, hh, r3, ww * 3,
                         CHROMA_UP_BICUBIC, cc, cc);
        int aligned = 1;
        for (int jj = 0; jj < hh; ++jj)
            for (int ii = 0; ii < ww; ++ii) {
                if ((ii & 1) || (jj & 1)) continue;
                size_t k = ((size_t)jj * ww + ii) * 3;
                if (r1[k] != r2[k] || r2[k] != r3[k] ||
                    r1[k+1] != r2[k+1] || r2[k+1] != r3[k+1] ||
                    r1[k+2] != r2[k+2] || r2[k+2] != r3[k+2])
                    aligned = 0;
            }
        printf("  %2dx%-2d even-site three-mode identical: %s\n",
               ww, hh, aligned ? "yes" : "NO");
        CHECK(aligned);
        /* 退化尺寸：尺寸为 1 的轴全部命中规则1（已被上面的偶数位点检查
         * 覆盖）；另一个轴必须仍在滤波——双线性输出不得与最近点全同 */
        if (ww == 1 || hh == 1) {
            int differs = memcmp(r1, r2, n * 3) != 0;
            printf("  %2dx%-2d degenerate: surviving axis still filters (bilinear != nearest): %s\n",
                   ww, hh, differs ? "yes" : "NO");
            CHECK(differs);
        }
        free(r1); free(r2); free(r3); free(yy2); free(uu2); free(vv2);
    }

    /* 3b. 手工构造色度平面，核对奇数相位与边界列的具体数值规则。
     * 取 Y=128、V=128(VV=0)，则 B=clamp(128+1815*(U-128)/1024)，
     * 由真实 API 的 B 通道反推 U，验证的是源码路径而非重放公式。 */
    {
        /* cw=4 -> w=8，水平单行；垂直方向 ch=1 时规则退化为复制。
         * U 取温和区间使 B 通道不钳位，相邻 U 的 B 差异约 2 可分辨。 */
        unsigned char row[4] = {100, 110, 120, 170};
        unsigned char uu3[4], vv3[4], out_b[8 * 3], out_c[8 * 3], yy3[8];
        memcpy(uu3, row, 4); memset(vv3, 128, 4); memset(yy3, 128, 8);
        /* 期望 U：双线性 1/2 混合，末列边界复制 */
        int exp_u_b[8] = {100, 105, 110, 115, 120, 145, 170, 170};
        /* 双三次 (-p0+9a+9b-p3)/16：i1=104 i3=113 i5=146 i7=边界复制170 */
        int exp_u_c[8] = {100, 104, 110, 113, 120, 146, 170, 170};
        yuv420_to_rgb_ex(yy3, 8, uu3, 4, vv3, 4, 8, 1, out_b, 24,
                         CHROMA_UP_BILINEAR, 4, 4);
        yuv420_to_rgb_ex(yy3, 8, uu3, 4, vv3, 4, 8, 1, out_c, 24,
                         CHROMA_UP_BICUBIC, 4, 4);
        int ok_b = 1, ok_c = 1;
        for (int i = 0; i < 8; ++i) {
            /* 由期望 U 正算 B（Y=128,VV=0），再与真实 API 输出比，避免反推量化 */
            int eb = clamp8(128 + (1815 * (exp_u_b[i] - 128)) / 1024);
            int ec = clamp8(128 + (1815 * (exp_u_c[i] - 128)) / 1024);
            if (abs((int)out_b[i * 3 + 2] - eb) > 1) ok_b = 0;
            if (abs((int)out_c[i * 3 + 2] - ec) > 1) ok_c = 0;
        }
        printf("  bilinear hand row {100,110,120,170}->w=8: %s (expect U 100 105 110 115 120 145 170 170)\n",
               ok_b ? "match" : "NO");
        printf("  bicubic  hand row                       : %s (expect U 100 104 110 113 120 146 170 170)\n",
               ok_c ? "match" : "NO");
        CHECK(ok_b); CHECK(ok_c);

        /* 奇数宽度最后一列 i=w-1 为偶数：必须直接命中采样点，三方式一致 */
        unsigned char ro[5] = {0, 50, 100, 150, 250}; /* cw=5 -> w=9 */
        unsigned char uo[5], vo[5], oo[3][9 * 3], yo[9];
        memcpy(uo, ro, 5); memset(vo, 128, 5); memset(yo, 80, 9);
        for (int m = 0; m < 3; ++m)
            yuv420_to_rgb_ex(yo, 9, uo, 5, vo, 5, 9, 1, oo[m], 27,
                             (enum chroma_up_mode)m, 5, 5);
        int last_same = !memcmp(oo[0] + 8 * 3, oo[1] + 8 * 3, 3) &&
                        !memcmp(oo[1] + 8 * 3, oo[2] + 8 * 3, 3);
        printf("  odd-width last column (i=8, cx=4) identical across modes: %s\n",
               last_same ? "yes" : "NO");
        CHECK(last_same);
    }
    printf("\n");

    /* ---------- 4. 四组尺寸 x 三种方式：RMSE / 边缘 RMSE / 耗时 ---------- */
    printf("[4] benchmark: RGB->YUV420->RGB roundtrip (time per call, us)\n");
    printf("| size  | mode     |  full RMSE | edge RMSE | max d | overshoot | time(us) | pattern |\n");
    printf("|-------|----------|-----------:|----------:|------:|----------:|---------:|---------|\n");
    struct { int w, h, iters, text; } cases[4] = {
        {8,  8,  3000, 1},   /* 偶数 x 偶数，类文字 */
        {9,  7,  3000, 1},   /* 奇数 x 奇数，类文字 */
        {1,  12, 5000, 1},   /* 宽为一（退化） */
        {16, 12, 2000, 1},   /* 稍大的文字图案 */
    };
    for (int k = 0; k < 4; ++k)
        run_case(cases[k].w, cases[k].h, cases[k].text, cases[k].iters);
    printf("\n");
    printf("[4b] smooth gradient reference (same four sizes)\n");
    printf("| size  | mode     |  full RMSE | edge RMSE | max d | overshoot | time(us) | pattern |\n");
    printf("|-------|----------|-----------:|----------:|------:|----------:|---------:|---------|\n");
    for (int k = 0; k < 4; ++k)
        run_case(cases[k].w, cases[k].h, 0, cases[k].iters);
    printf("\n");

    edge_study();

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
