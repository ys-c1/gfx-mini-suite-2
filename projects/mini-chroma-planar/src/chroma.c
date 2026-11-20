#include "chroma.h"
#include <stddef.h>

/* 统一的参数检查：平面非空、尺寸为正、stride 可容纳一行、行带落在帧内 */
static int valid_planes(const unsigned char* a, const unsigned char* b,
                        const unsigned char* c, const unsigned char* d,
                        int w, int h, int s0, int s1, int s2, int s3,
                        int min_pitch0, int row0, int band_h) {
    if (!a || !b || !c || !d || w <= 0 || h <= 0) return 0;
    if (s0 < min_pitch0 || s1 < (w + 1) / 2 || s2 < (w + 1) / 2) return 0;
    if (s3 < w) return 0;
    if (row0 < 0 || band_h <= 0 || row0 > h - band_h) return 0;
    return 1;
}

/* ============================ YUV420 -> RGB ============================ */

/* 转换绝对行 [j0, j0+rows) 到 rgb（rgb 基指针对应行带的第 0 行）。
   色度采样全部按绝对坐标 j/2、i/2 索引，整幅与行带走同一条计算路径。 */
static int yuv420_to_rgb_core(const unsigned char* y, int y_stride,
                              const unsigned char* u, int u_stride,
                              const unsigned char* v, int v_stride,
                              int w, int h, int j0, int rows,
                              unsigned char* rgb, int rgb_stride) {
    (void)h;
    for (int r = 0; r < rows; ++r) {
        int j = j0 + r;
        int cj = j / 2;
        for (int i = 0; i < w; ++i) {
            int yy = y[(size_t)j * y_stride + i];
            int uu = u[(size_t)cj * u_stride + i / 2] - 128;
            int vv = v[(size_t)cj * v_stride + i / 2] - 128;
            int rr = yy + ((1436 * vv) >> 10);
            int gg = yy - ((352 * uu + 731 * vv) >> 10);
            int bb = yy + ((1815 * uu) >> 10);
            unsigned char* p = rgb + (size_t)r * rgb_stride + (size_t)i * 3;
            p[0] = (unsigned char)(rr < 0 ? 0 : (rr > 255 ? 255 : rr));
            p[1] = (unsigned char)(gg < 0 ? 0 : (gg > 255 ? 255 : gg));
            p[2] = (unsigned char)(bb < 0 ? 0 : (bb > 255 ? 255 : bb));
        }
    }
    return 0;
}

int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride) {
    if (!valid_planes(y, u, v, rgb, w, h, rgb_stride, u_stride, v_stride,
                      y_stride, w * 3, 0, h))
        return -1;
    return yuv420_to_rgb_core(y, y_stride, u, u_stride, v, v_stride,
                              w, h, 0, h, rgb, rgb_stride);
}

int yuv420_to_rgb_band(const unsigned char* y, int y_stride,
                       const unsigned char* u, int u_stride,
                       const unsigned char* v, int v_stride,
                       int w, int h, int row0, int band_h,
                       unsigned char* rgb, int rgb_stride) {
    if (!valid_planes(y, u, v, rgb, w, h, rgb_stride, u_stride, v_stride,
                      y_stride, w * 3, row0, band_h))
        return -1;
    return yuv420_to_rgb_core(y, y_stride, u, u_stride, v, v_stride,
                              w, h, row0, band_h, rgb, rgb_stride);
}

/* ============================ RGB -> YUV420 ============================ */

static void encode_luma_row(const unsigned char* rgb, int rgb_stride,
                            int w, int j, unsigned char* y, int y_stride) {
    for (int i = 0; i < w; ++i) {
        const unsigned char* p = rgb + (size_t)j * rgb_stride + (size_t)i * 3;
        y[(size_t)j * y_stride + i] =
            (unsigned char)((77 * p[0] + 150 * p[1] + 29 * p[2] + 128) >> 8);
    }
}

/* 编码色度块行 j（覆盖亮度行 2j、2j+1），边缘行/列按整幅路径复制 */
static void encode_chroma_row(const unsigned char* rgb, int rgb_stride,
                              int w, int h, int j,
                              unsigned char* u, int u_stride,
                              unsigned char* v, int v_stride) {
    int cw = (w + 1) / 2;
    int y0 = j * 2;
    int y1 = (y0 + 1 < h) ? y0 + 1 : y0;
    for (int i = 0; i < cw; ++i) {
        int x0 = i * 2;
        int x1 = (x0 + 1 < w) ? x0 + 1 : x0;
        int su = 0, sv = 0;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                int xx = dx ? x1 : x0, yy = dy ? y1 : y0;
                const unsigned char* p =
                    rgb + (size_t)yy * rgb_stride + (size_t)xx * 3;
                su += ((-43 * p[0] - 85 * p[1] + 128 * p[2] + 128) >> 8) + 128;
                sv += ((128 * p[0] - 107 * p[1] - 21 * p[2] + 128) >> 8) + 128;
            }
        u[(size_t)j * u_stride + i] = (unsigned char)(su / 4);
        v[(size_t)j * v_stride + i] = (unsigned char)(sv / 4);
    }
}

int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride) {
    if (!valid_planes(rgb, y, u, v, w, h, rgb_stride, u_stride, v_stride,
                      y_stride, w * 3, 0, h))
        return -1;
    for (int j = 0; j < h; ++j)
        encode_luma_row(rgb, rgb_stride, w, j, y, y_stride);
    for (int j = 0; j < (h + 1) / 2; ++j)
        encode_chroma_row(rgb, rgb_stride, w, h, j, u, u_stride, v, v_stride);
    return 0;
}

int rgb_to_yuv420_band(const unsigned char* rgb, int rgb_stride,
                       int w, int h, int row0, int band_h,
                       unsigned char* y, int y_stride,
                       unsigned char* u, int u_stride,
                       unsigned char* v, int v_stride) {
    if (!valid_planes(rgb, y, u, v, w, h, rgb_stride, u_stride, v_stride,
                      y_stride, w * 3, row0, band_h))
        return -1;

    /* 亮度：行带内逐行写入 */
    for (int j = row0; j < row0 + band_h; ++j)
        encode_luma_row(rgb, rgb_stride, w, j, y, y_stride);

    /* 色度：块行 j 覆盖亮度行 {2j, 2j+1}，归属于包含其顶行 2j 的行带，
       即满足 row0 <= 2j < row0+band_h：
         j_first = ceil(row0/2),  j_last_excl = ceil(end/2)
       行带序列划分了全部亮度行，因此每个块行恰好被写一次，无遗漏也无
       重复；块内取的始终是绝对位置像素（配对行可能在下一带或被帧底
       边缘复制），与整幅编码逐字节一致。上界为 ceil(h/2)=ch，不会越界。 */
    int j_first = (row0 + 1) / 2;
    int j_last_excl = (row0 + band_h + 1) / 2;
    for (int j = j_first; j < j_last_excl; ++j)
        encode_chroma_row(rgb, rgb_stride, w, h, j, u, u_stride, v, v_stride);
    return 0;
}
