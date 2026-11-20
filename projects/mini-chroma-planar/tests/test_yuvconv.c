#include "../src/yuvconv.h"
#include "../src/chroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    /* ---- 色度尺寸：奇数宽高 ---- */
    int cw = 0, ch = 0;
    CHECK(yc_chroma_dims(YC_FMT_420, 9, 7, &cw, &ch) == YC_OK && cw == 5 && ch == 4);
    CHECK(yc_chroma_dims(YC_FMT_422, 9, 7, &cw, &ch) == YC_OK && cw == 5 && ch == 7);
    CHECK(yc_chroma_dims(YC_FMT_420, 8, 8, &cw, &ch) == YC_OK && cw == 4 && ch == 4);
    CHECK(yc_chroma_dims(YC_FMT_422, 8, 8, &cw, &ch) == YC_OK && cw == 4 && ch == 8);
    CHECK(yc_chroma_dims(99, 8, 8, &cw, &ch) == YC_ERR_FORMAT);
    CHECK(yc_chroma_dims(YC_FMT_420, 0, 8, &cw, &ch) == YC_ERR_SIZE);

    /* ---- 失败码区分 ---- */
    enum { W = 9, H = 7, CW = 5, CH4 = 4 };
    unsigned char y422[W * H], u422[CW * H], v422[CW * H];
    unsigned char y420[W * H], u420[CW * CH4], v420[CW * CH4];
    for (int i = 0; i < W * H; ++i) y422[i] = (unsigned char)(i * 7 + 13);
    for (int i = 0; i < CW * H; ++i) { u422[i] = (unsigned char)(i * 5 + 40); v422[i] = (unsigned char)(200 - i * 3); }

    /* 空指针 */
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, NULL, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_ARG);
    /* 未知格式（源 / 目标） */
    CHECK(yuv_convert(7, YC_FMT_420, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_FORMAT);
    CHECK(yuv_convert(YC_FMT_422, -1, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_FORMAT);
    /* 尺寸不匹配：非正尺寸、stride 小于平面宽度 */
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W, u422, CW, v422, CW, 0, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_SIZE);
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W - 1, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_SIZE);
    /* 缓冲不足：y / u / v 各自差一字节 */
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420) - 1, u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_ERR_BUFFER);
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420) - 1, v420, CW, sizeof(v420)) == YC_ERR_BUFFER);
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420) - 1) == YC_ERR_BUFFER);

    /* ---- 422 -> 420：亮度逐像素不动 ---- */
    memset(y420, 0, sizeof(y420));
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422, W, u422, CW, v422, CW, W, H,
                      y420, W, sizeof(y420), u420, CW, sizeof(u420), v420, CW, sizeof(v420)) == YC_OK);
    CHECK(memcmp(y422, y420, sizeof(y422)) == 0);

    /* ---- 往返一致性：420 -> 422 -> 420 应接近原 420 ---- */
    unsigned char u422b[CW * H], v422b[CW * H], y422b[W * H];
    unsigned char u420b[CW * CH4], v420b[CW * CH4], y420b[W * H];
    CHECK(yuv_convert(YC_FMT_420, YC_FMT_422, y420, W, u420, CW, v420, CW, W, H,
                      y422b, W, sizeof(y422b), u422b, CW, sizeof(u422b), v422b, CW, sizeof(v422b)) == YC_OK);
    CHECK(yuv_convert(YC_FMT_422, YC_FMT_420, y422b, W, u422b, CW, v422b, CW, W, H,
                      y420b, W, sizeof(y420b), u420b, CW, sizeof(u420b), v420b, CW, sizeof(v420b)) == YC_OK);
    int worst = 0;
    for (int i = 0; i < CW * CH4; ++i) {
        int d1 = (int)u420b[i] - (int)u420[i]; if (d1 < 0) d1 = -d1;
        int d2 = (int)v420b[i] - (int)v420[i]; if (d2 < 0) d2 = -d2;
        if (d1 > worst) worst = d1;
        if (d2 > worst) worst = d2;
    }
    printf("420->422->420 worst chroma delta %d\n", worst);
    /* 复合核为 [3 10 3]/16 的轻度平滑，对逐像素噪声输入的有界损失 */
    CHECK(worst <= 16);

    /* ---- 同格式拷贝 ---- */
    unsigned char ucopy[CW * CH4], vcopy[CW * CH4], ycopy[W * H];
    CHECK(yuv_convert(YC_FMT_420, YC_FMT_420, y420, W, u420, CW, v420, CW, W, H,
                      ycopy, W, sizeof(ycopy), ucopy, CW, sizeof(ucopy), vcopy, CW, sizeof(vcopy)) == YC_OK);
    CHECK(memcmp(ucopy, u420, sizeof(ucopy)) == 0 && memcmp(vcopy, v420, sizeof(vcopy)) == 0);

    /* ---- 矩阵切换 ---- */
    unsigned char rgb[W * H * 3];
    for (int i = 0; i < W * H * 3; ++i) rgb[i] = (unsigned char)(i * 11 + 5);
    unsigned char y1[W * H], u1[CW * CH4], v1[CW * CH4];
    unsigned char y2[W * H], u2[CW * CH4], v2[CW * CH4];
    unsigned char yl[W * H], ul[CW * CH4], vl[CW * CH4];
    CHECK(rgb_to_yuv420_m(rgb, W, H, W * 3, y1, W, u1, CW, v1, CW, YC_MATRIX_BT601) == YC_OK);
    CHECK(rgb_to_yuv420_m(rgb, W, H, W * 3, y2, W, u2, CW, v2, CW, YC_MATRIX_BT709) == YC_OK);
    CHECK(rgb_to_yuv420_m(rgb, W, H, W * 3, y1, W, u1, CW, v1, CW, 42) == YC_ERR_FORMAT);
    /* BT.601 路径必须与原四二〇路径逐字节一致 */
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, yl, W, ul, CW, vl, CW) == 0);
    CHECK(memcmp(y1, yl, sizeof(yl)) == 0 && memcmp(u1, ul, sizeof(ul)) == 0 && memcmp(v1, vl, sizeof(vl)) == 0);
    /* 换矩阵后色度应有可见差异 */
    CHECK(memcmp(u1, u2, sizeof(u1)) != 0 || memcmp(v1, v2, sizeof(v1)) != 0);
    CHECK(yuv420_to_rgb_m(y1, W, u1, CW, v1, CW, W, H, rgb, W * 3, 42) == YC_ERR_FORMAT);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
