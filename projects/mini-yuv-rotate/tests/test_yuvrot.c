#define _POSIX_C_SOURCE 200809L
#include "../src/yuvrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define GUARD 8
#define SENTINEL 0xA5

/* ---- 平面缓冲: 尺寸 = 行数 * 跨度 + 尾部守卫, 全部带哨兵 ---- */
typedef struct {
    unsigned char* p;
    int stride;
    int cols;          /* 有效列宽 */
    int rows;          /* 有效行数 */
    size_t bytes;
} Plane;

static Plane plane_make(int cols, int rows, int stride) {
    Plane pl;
    pl.cols = cols; pl.rows = rows; pl.stride = stride;
    pl.bytes = (size_t)rows * stride + GUARD;
    pl.p = malloc(pl.bytes);
    memset(pl.p, SENTINEL, pl.bytes);
    return pl;
}

/* 校验所有行外填充与尾部守卫未被改动, 返回违例字节数 */
static int plane_guard_ok(const Plane* pl) {
    int bad = 0;
    for (int r = 0; r < pl->rows; ++r)
        for (int x = pl->cols; x < pl->stride; ++x)
            if (pl->p[(size_t)r * pl->stride + x] != SENTINEL) ++bad;
    for (int g = 0; g < GUARD; ++g)
        if (pl->p[(size_t)pl->rows * pl->stride + g] != SENTINEL) ++bad;
    return bad;
}

static void plane_free(Plane* pl) { free(pl->p); pl->p = NULL; }

/* RGB 缓冲同样按 stride 排布并带哨兵 */
static unsigned char* rgb_make(int h, int stride, size_t* bytes) {
    *bytes = (size_t)h * stride + GUARD;
    unsigned char* p = malloc(*bytes);
    memset(p, SENTINEL, *bytes);
    return p;
}

static int rgb_guard_ok(const unsigned char* p, int w, int h, int stride,
                        size_t bytes) {
    int bad = 0;
    for (int r = 0; r < h; ++r)
        for (int x = w * 3; x < stride; ++x)
            if (p[(size_t)r * stride + x] != SENTINEL) ++bad;
    for (size_t g = (size_t)h * stride; g < bytes; ++g)
        if (p[g] != SENTINEL) ++bad;
    return bad;
}

/* ---- 测试图案 ---- */
/* pattern 0: 平滑渐变 (天然图像, 色度空间相关)
 * pattern 1: 高频棋盘条纹 (压力测试, 色差上界) */
static void fill_pattern(unsigned char* rgb, int w, int h, int stride, int pat) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* q = rgb + (size_t)j * stride + (size_t)i * 3;
            if (pat == 0) {
                q[0] = (unsigned char)(i * 7);
                q[1] = (unsigned char)(j * 11);
                q[2] = (unsigned char)((i * 5 + j * 3) & 0xFF);
            } else {
                int c = ((i ^ j) & 1) ? 255 : 0;
                q[0] = (unsigned char)((i & 1) ? c : 40);
                q[1] = (unsigned char)((j & 1) ? c : 90);
                q[2] = (unsigned char)c;
            }
        }
}

static int max_abs_delta(const unsigned char* a, const unsigned char* b,
                         int w, int h, int stride, long* sum) {
    int worst = 0;
    *sum = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w * 3; ++i) {
            int d = (int)a[(size_t)j * stride + i] -
                    (int)b[(size_t)j * stride + i];
            if (d < 0) d = -d;
            if (d > worst) worst = d;
            *sum += d;
        }
    return worst;
}

/* 比较两个平面有效区域的逐字节一致性, 返回不同字节数 */
static int plane_diff(const Plane* a, const Plane* b) {
    if (a->rows != b->rows || a->cols != b->cols) return -1;
    int d = 0;
    for (int r = 0; r < a->rows; ++r)
        for (int c = 0; c < a->cols; ++c)
            if (a->p[(size_t)r * a->stride + c] !=
                b->p[(size_t)r * b->stride + c]) ++d;
    return d;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static const struct { yr_op op; const char* name; } OPS[] = {
    { YR_IDENTITY,  "identity"  },
    { YR_R90,       "rot90"     },
    { YR_R180,      "rot180"    },
    { YR_R270,      "rot270"    },
    { YR_MIRROR_H,  "mirror-h"  },
    { YR_MIRROR_V,  "mirror-v"  },
};
#define N_OPS (int)(sizeof(OPS) / sizeof(OPS[0]))

/* 三组尺寸: (宽, 高)。跨度取最小跨度再 +3, 保证最小跨度为偶数时
 * 实际跨度为奇数, 最小跨度本身为奇数时保持奇数 —— 全部用奇数跨度。 */
static const int SIZES[][2] = {
    { 9, 7 },    /* 奇 x 奇 (竖屏素材代表) */
    { 16, 12 },  /* 偶 x 偶 */
    { 13, 8 },   /* 奇 x 偶 (90 度后变 偶 x 奇, 覆盖奇偶互换) */
};
#define N_SIZES (int)(sizeof(SIZES) / sizeof(SIZES[0]))

/* 每个 (尺寸, 操作) 的核对结果 */
typedef struct {
    int dy, du, dv;        /* 两条顺序的平面字节差异 */
    int guard_bad;         /* 哨兵违例 */
    double t_a, t_b;       /* 每迭代耗时 (纳秒) */
} Result;

int main(void) {
    const int ITERS = 2000;
    Result res[N_SIZES][N_OPS];
    memset(res, 0, sizeof(res));

    /* ---- 色差表 (RGB -> YUV420 -> RGB 往返, 两种图案) ---- */
    printf("== roundtrip color delta (rgb->yuv420->rgb) ==\n");
    printf("%-9s %-7s | smooth: max/mean      | hf-stripe: max/mean\n",
           "size", "stride");
    int rt_worst = 0;
    for (int s = 0; s < N_SIZES; ++s) {
        int w = SIZES[s][0], h = SIZES[s][1];
        int rgb_stride = w * 3 + 3;           /* 奇数跨度 */
        int y_stride = w + 1;                 /* 奇数 */
        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        int c_stride = cw + 1;                /* 奇数 */
        for (int pat = 0; pat < 2; ++pat) {
            size_t rb;
            unsigned char* rgb = rgb_make(h, rgb_stride, &rb);
            unsigned char* back = rgb_make(h, rgb_stride, &rb);
            Plane y = plane_make(w, h, y_stride);
            Plane u = plane_make(cw, ch, c_stride);
            Plane v = plane_make(cw, ch, c_stride);
            fill_pattern(rgb, w, h, rgb_stride, pat);
            CHECK(yr_rgb_to_yuv420(rgb, w, h, rgb_stride,
                                   y.p, y.stride, u.p, u.stride,
                                   v.p, v.stride) == 0);
            CHECK(yr_yuv420_to_rgb(y.p, y.stride, u.p, u.stride,
                                   v.p, v.stride, w, h,
                                   back, rgb_stride) == 0);
            long sum;
            int worst = max_abs_delta(rgb, back, w, h, rgb_stride, &sum);
            double mean = (double)sum / (w * h * 3);
            if (worst > rt_worst) rt_worst = worst;
            if (pat == 0)
                printf("%dx%d     (odd)  | %3d / %6.2f        | ",
                       w, h, worst, mean);
            else
                printf("%3d / %6.2f\n", worst, mean);
            CHECK(plane_guard_ok(&y) == 0);
            CHECK(plane_guard_ok(&u) == 0);
            CHECK(plane_guard_ok(&v) == 0);
            CHECK(rgb_guard_ok(back, w, h, rgb_stride, rb) == 0);
            plane_free(&y); plane_free(&u); plane_free(&v);
            free(rgb); free(back);
        }
    }
    printf("(roundtrip worst across all sizes/patterns: %d)\n\n", rt_worst);

    /* ---- 两种顺序的字节一致性 + 计时 ---- */
    printf("== byte identity: A=rotate(rgb->yuv) vs B=rgb->yuv(rotate(rgb)) ==\n");
    for (int s = 0; s < N_SIZES; ++s) {
        int w = SIZES[s][0], h = SIZES[s][1];
        int rgb_stride = w * 3 + 3;
        int y_stride = w + 1;
        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        int c_stride = cw + 1;

        size_t rb;
        unsigned char* rgb = rgb_make(h, rgb_stride, &rb);
        fill_pattern(rgb, w, h, rgb_stride, 0);

        printf("-- %dx%d  (rgb_stride=%d y_stride=%d c_stride=%d, all odd) --\n",
               w, h, rgb_stride, y_stride, c_stride);
        printf("%-9s | mismatches Y/U/V | guards | ns/iter A (rot-plane)  "
               "B (rot-rgb)\n", "op");

        for (int oi = 0; oi < N_OPS; ++oi) {
            yr_op op = OPS[oi].op;
            int dw, dh;
            CHECK(yr_dst_size(op, w, h, &dw, &dh) == 0);
            int dcw = (dw + 1) / 2, dch = (dh + 1) / 2;
            int drgb_stride = dw * 3 + 3;
            int dy_stride = dw + 1;
            int dc_stride = dcw + 1;

            /* 源 YUV */
            Plane sy = plane_make(w, h, y_stride);
            Plane su = plane_make(cw, ch, c_stride);
            Plane sv = plane_make(cw, ch, c_stride);
            CHECK(yr_rgb_to_yuv420(rgb, w, h, rgb_stride,
                                   sy.p, sy.stride, su.p, su.stride,
                                   sv.p, sv.stride) == 0);

            /* 顺序 A: 先转色, 再转平面 */
            Plane aY = plane_make(dw, dh, dy_stride);
            Plane aU = plane_make(dcw, dch, dc_stride);
            Plane aV = plane_make(dcw, dch, dc_stride);

            double t0 = now_sec();
            for (int it = 0; it < ITERS; ++it)
                CHECK(yr_transform_yuv420(op,
                        sy.p, sy.stride, su.p, su.stride, sv.p, sv.stride,
                        w, h,
                        aY.p, aY.stride, aU.p, aU.stride, aV.p, aV.stride) == 0);
            double t1 = now_sec();

            /* 顺序 B: 先转 RGB, 再转色 */
            size_t drb;
            unsigned char* drgb = rgb_make(dh, drgb_stride, &drb);
            Plane bY = plane_make(dw, dh, dy_stride);
            Plane bU = plane_make(dcw, dch, dc_stride);
            Plane bV = plane_make(dcw, dch, dc_stride);

            double t2 = now_sec();
            for (int it = 0; it < ITERS; ++it) {
                CHECK(yr_transform_rgb(op, rgb, w, h, rgb_stride,
                                       drgb, dw, dh, drgb_stride) == 0);
                CHECK(yr_rgb_to_yuv420(drgb, dw, dh, drgb_stride,
                                       bY.p, bY.stride, bU.p, bU.stride,
                                       bV.p, bV.stride) == 0);
            }
            double t3 = now_sec();

            res[s][oi].dy = plane_diff(&aY, &bY);
            res[s][oi].du = plane_diff(&aU, &bU);
            res[s][oi].dv = plane_diff(&aV, &bV);
            res[s][oi].guard_bad = plane_guard_ok(&aY) + plane_guard_ok(&aU) +
                                   plane_guard_ok(&aV) + plane_guard_ok(&bY) +
                                   plane_guard_ok(&bU) + plane_guard_ok(&bV) +
                                   rgb_guard_ok(drgb, dw, dh, drgb_stride, drb);
            res[s][oi].t_a = (t1 - t0) / ITERS * 1e9;
            res[s][oi].t_b = (t3 - t2) / ITERS * 1e9;

            CHECK(res[s][oi].dy == 0);
            CHECK(res[s][oi].du == 0);
            CHECK(res[s][oi].dv == 0);
            CHECK(res[s][oi].guard_bad == 0);

            printf("%-9s | %7d/%d/%-5d | %6d | %10.0f        %10.0f\n",
                   OPS[oi].name, res[s][oi].dy, res[s][oi].du,
                   res[s][oi].dv, res[s][oi].guard_bad,
                   res[s][oi].t_a, res[s][oi].t_b);

            plane_free(&sy); plane_free(&su); plane_free(&sv);
            plane_free(&aY); plane_free(&aU); plane_free(&aV);
            plane_free(&bY); plane_free(&bU); plane_free(&bV);
            free(drgb);
        }
        free(rgb);
        printf("\n");
    }

    /* ---- 几何正确性: 色度采样点跟着转 ---- */
    printf("== geometry spot checks ==\n");
    {
        /* 9x7 -> 90 度 -> 7x9; 色度 5x4 -> 4x5, 角格来源核对 */
        int w = 9, h = 7;
        Plane y = plane_make(w, h, w), u = plane_make(5, 4, 5),
              v = plane_make(5, 4, 5);
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c)
                y.p[r * w + c] = (unsigned char)(r * w + c);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 5; ++c) {
                u.p[r * 5 + c] = (unsigned char)(10 + r * 5 + c);
                v.p[r * 5 + c] = (unsigned char)(100 + r * 5 + c);
            }
        Plane dy = plane_make(7, 9, 7), du = plane_make(4, 5, 4),
              dv = plane_make(4, 5, 4);
        CHECK(yr_transform_yuv420(YR_R90,
                y.p, y.stride, u.p, u.stride, v.p, v.stride, w, h,
                dy.p, dy.stride, du.p, du.stride, dv.p, dv.stride) == 0);
        /* Y(0,0): R90 逆映射 (0,0)->源 (0, h-1=6) */
        CHECK(dy.p[0] == y.p[6 * w + 0]);
        /* 色度格核对。源 U/V 为 5 列 (stride 5), 目标 U/V 为 4 列
         * (stride 4)。R90 逆映射 (rpx,rpy)->(rpy, h-1-rpx), h=7。 */
        /* 目标格 (0,0) 代表 (0,0) -> 源 (0,6),
         * owner=(owner_x(0)=0, owner_y(6)=0) -> 源格 (0,0) */
        CHECK(du.p[0] == u.p[0 * 5 + 0]);
        /* 目标格 (dci=3,dcj=4): dst_w=7 中心格 ci=3 为单像素格,
         * 代表 (axis_rep(7,3)=3, axis_rep(9,4)=4) -> 源 (4,3),
         * owner=(4, owner_y(3)=3) -> 源格 (4,3) */
        CHECK(du.p[4 * 4 + 3] == u.p[3 * 5 + 4]);
        CHECK(dv.p[4 * 4 + 3] == v.p[3 * 5 + 4]);
        /* 目标格 (dci=0,dcj=4) 代表 (0,4) -> 源 (4,6),
         * owner_y(6): n=7, center=3, 6>3 反射为 7-1-6=0 -> 源格 (4,0) */
        CHECK(du.p[4 * 4 + 0] == u.p[0 * 5 + 4]);
        plane_free(&y); plane_free(&u); plane_free(&v);
        plane_free(&dy); plane_free(&du); plane_free(&dv);
    }

    /* ---- 参数校验 ---- */
    CHECK(yr_op_valid((yr_op)99) == 0);
    CHECK(yr_dst_size(YR_R90, 0, 4, NULL, NULL) == -1);
    {
        unsigned char dummy[64] = {0};
        CHECK(yr_transform_rgb((yr_op)7, dummy, 4, 4, 12,
                               dummy, 4, 4, 12) == -1);
        CHECK(yr_rgb_to_yuv420(NULL, 4, 4, 12, dummy, 4,
                               dummy, 2, dummy, 2) == -1);
        /* Y 跨度小于目标宽度 (R180 目标宽仍为 4, 给 3) -> 非法 */
        CHECK(yr_transform_yuv420(YR_R180,
                dummy, 4, dummy, 2, dummy, 2, 4, 4,
                dummy, 3, dummy, 2, dummy, 2) == -1);
        /* 色度跨度小于色度宽 (cw=2, 给 1) -> 非法 */
        CHECK(yr_transform_yuv420(YR_R180,
                dummy, 4, dummy, 1, dummy, 2, 4, 4,
                dummy, 4, dummy, 2, dummy, 2) == -1);
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
