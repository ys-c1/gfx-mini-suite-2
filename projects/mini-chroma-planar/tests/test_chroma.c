#include "../src/chroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

enum { MAXW = 16, MAXH = 16, CW = (MAXW + 1) / 2, CH = (MAXH + 1) / 2 };

static void fill_gradient(unsigned char* rgb, int w, int h, int stride) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + (size_t)j * stride + (size_t)i * 3;
            p[0] = (unsigned char)(i * 16);
            p[1] = (unsigned char)(j * 16);
            p[2] = 100;
        }
}

static void fill_lcg(unsigned char* rgb, int w, int h, int stride) {
    unsigned int s = 0x12345678u;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + (size_t)j * stride + (size_t)i * 3;
            for (int k = 0; k < 3; ++k) {
                s = s * 1103515245u + 12345u;
                p[k] = (unsigned char)(s >> 23);
            }
        }
}

static void plane_copy_rows(unsigned char* dst, int ds, const unsigned char* src, int ss,
                            int row_bytes, int rows) {
    for (int j = 0; j < rows; ++j)
        memcpy(dst + (size_t)j * ds, src + (size_t)j * ss, (size_t)row_bytes);
}

/* 用给定带高把整幅解码/编码各跑一遍，结果必须与整幅路径逐字节相同 */
static void run_band_height(int w, int h, int band_h, int padded) {
    int rgb_stride = w * 3 + (padded ? 4 : 0);
    int y_stride = w + (padded ? 2 : 0);
    int c_stride = (w + 1) / 2 + (padded ? 1 : 0);
    int cw = (w + 1) / 2, ch = (h + 1) / 2;

    unsigned char* rgb = calloc((size_t)h * rgb_stride, 1);
    unsigned char* ref_rgb = calloc((size_t)h * rgb_stride, 1);
    unsigned char* full_rgb = calloc((size_t)h * rgb_stride, 1);
    unsigned char* band_rgb = calloc((size_t)band_h * rgb_stride, 1);
    unsigned char* y = calloc((size_t)h * y_stride, 1);
    unsigned char* u = calloc((size_t)ch * c_stride, 1);
    unsigned char* v = calloc((size_t)ch * c_stride, 1);
    unsigned char* y2 = calloc((size_t)h * y_stride, 1);
    unsigned char* u2 = calloc((size_t)ch * c_stride, 1);
    unsigned char* v2 = calloc((size_t)ch * c_stride, 1);
    CHECK(rgb && ref_rgb && full_rgb && band_rgb && y && u && v && y2 && u2 && v2);

    fill_lcg(rgb, w, h, rgb_stride);

    /* 整幅编码 + 整幅解码，作为基准 */
    CHECK(rgb_to_yuv420(rgb, w, h, rgb_stride, y, y_stride, u, c_stride, v, c_stride) == 0);
    CHECK(yuv420_to_rgb(y, y_stride, u, c_stride, v, c_stride, w, h, ref_rgb, rgb_stride) == 0);

    /* ---- 解码按带：拼接后必须与整幅解码逐字节一致 ---- */
    memset(full_rgb, 0xAB, (size_t)h * rgb_stride);
    for (int row0 = 0; row0 < h; row0 += band_h) {
        int hb = (row0 + band_h <= h) ? band_h : h - row0; /* 末尾截短 */
        CHECK(yuv420_to_rgb_band(y, y_stride, u, c_stride, v, c_stride,
                                 w, h, row0, hb, band_rgb, rgb_stride) == 0);
        plane_copy_rows(full_rgb + (size_t)row0 * rgb_stride, rgb_stride,
                        band_rgb, rgb_stride, w * 3, hb);
    }
    for (int j = 0; j < h; ++j)
        CHECK(memcmp(full_rgb + (size_t)j * rgb_stride,
                     ref_rgb + (size_t)j * rgb_stride, (size_t)w * 3) == 0);

    /* 接缝两侧绝对差：必须为 0，即小于一个量化级（1 LSB） */
    for (int b = band_h; b < h; b += band_h) {
        int dmax = 0;
        for (int i = 0; i < w * 3; ++i) {
            int d = (int)full_rgb[(size_t)(b - 1) * rgb_stride + i]
                  - (int)full_rgb[(size_t)b * rgb_stride + i];
            if (d < 0) d = -d;
            if (d > dmax) dmax = d;
        }
        /* 与基准在接缝两行的输出完全一致即证明无相位错位 */
        CHECK(memcmp(full_rgb + (size_t)(b - 1) * rgb_stride,
                     ref_rgb + (size_t)(b - 1) * rgb_stride, (size_t)w * 3) == 0);
        CHECK(memcmp(full_rgb + (size_t)b * rgb_stride,
                     ref_rgb + (size_t)b * rgb_stride, (size_t)w * 3) == 0);
        CHECK(dmax >= 0); /* 记录变量用途；真正的强度断言是上面两行 memcmp */
    }

    /* ---- 编码按带：三个平面必须与整幅编码逐字节一致 ---- */
    memset(y2, 0xAB, (size_t)h * y_stride);
    memset(u2, 0xAB, (size_t)ch * c_stride);
    memset(v2, 0xAB, (size_t)ch * c_stride);
    for (int row0 = 0; row0 < h; row0 += band_h) {
        int hb = (row0 + band_h <= h) ? band_h : h - row0;
        CHECK(rgb_to_yuv420_band(rgb, rgb_stride, w, h, row0, hb,
                                 y2, y_stride, u2, c_stride, v2, c_stride) == 0);
    }
    for (int j = 0; j < h; ++j)
        CHECK(memcmp(y2 + (size_t)j * y_stride, y + (size_t)j * y_stride, (size_t)w) == 0);
    for (int j = 0; j < ch; ++j) {
        CHECK(memcmp(u2 + (size_t)j * c_stride, u + (size_t)j * c_stride, (size_t)cw) == 0);
        CHECK(memcmp(v2 + (size_t)j * c_stride, v + (size_t)j * c_stride, (size_t)cw) == 0);
    }

    free(rgb); free(ref_rgb); free(full_rgb); free(band_rgb);
    free(y); free(u); free(v); free(y2); free(u2); free(v2);
}

int main(void) {
    /* ---- 原有用例：梯度图往返误差 ---- */
    enum { W = 9, H = 7 };
    unsigned char rgb[W * H * 3], back[W * H * 3];
    unsigned char y[W * H], u[8 * 4], v[8 * 4];
    fill_gradient(rgb, W, H, W * 3);
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 8, v, 8) == 0);
    memset(back, 0, sizeof(back));
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, back, W * 3) == 0);
    int worst = 0;
    for (int i = 0; i < W * H * 3; ++i) {
        int d = (int)back[i] - (int)rgb[i];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    CHECK(worst <= 60);
    printf("worst roundtrip delta %d\n", worst);

    /* 整幅路径的空指针检查仍然保留 */
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, NULL, W * 3) == -1);
    CHECK(rgb_to_yuv420(NULL, W, H, W * 3, y, W, u, 8, v, 8) == -1);

    /* ---- 按带一致性：奇/偶尺寸 x 奇/偶/1 带高 x 紧凑/填充 stride ---- */
    int dims[][2] = {{9, 7}, {8, 6}, {1, 1}, {11, 3}, {6, 11}};
    int heights[] = {1, 2, 3, 5, 7};
    for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); ++d)
        for (size_t k = 0; k < sizeof(heights) / sizeof(heights[0]); ++k) {
            run_band_height(dims[d][0], dims[d][1], heights[k], 0);
            run_band_height(dims[d][0], dims[d][1], heights[k], 1);
        }

    /* ---- 非法行带参数必须返回 -1，不得写出或越界 ---- */
    unsigned char band[W * 3 * 4];
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H, 0, 0, band, W * 3) == -1);
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, 0, 0, y, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H, 0, H + 1, band, W * 3) == -1);
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, 0, H + 1, y, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H, H, 1, band, W * 3) == -1);
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, H, 1, y, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H, -1, 1, band, W * 3) == -1);
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, -1, 1, y, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H, 5, 3, band, W * 3) == -1); /* 5+3>7 */
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, 5, 3, y, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(NULL, W, u, 8, v, 8, W, H, 0, 1, band, W * 3) == -1);
    CHECK(rgb_to_yuv420_band(rgb, W * 3, W, H, 0, 1, NULL, W, u, 8, v, 8) == -1);
    CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, 0, H, 0, 1, band, W * 3) == -1);

    /* ---- 模拟设备端流式循环：带高 3，帧高 7，末带截短为 1 ---- */
    unsigned char streamed[W * H * 3];
    memset(streamed, 0, sizeof(streamed));
    int row0 = 0, band_h = 3;
    while (row0 < H) {
        int hb = (row0 + band_h <= H) ? band_h : H - row0;
        CHECK(yuv420_to_rgb_band(y, W, u, 8, v, 8, W, H,
                                 row0, hb,
                                 streamed + (size_t)row0 * W * 3, W * 3) == 0);
        row0 += hb;
    }
    CHECK(memcmp(streamed, back, sizeof(streamed)) == 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
