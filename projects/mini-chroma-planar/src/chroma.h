#ifndef CHROMA_H
#define CHROMA_H

/* ---- 整幅路径：一次转换整个画面（保留） ---- */
int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride);
int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride);

/*
 * ---- 按行带（band）路径：每次只转换 band_h 行，输出缓冲只需容纳该行带 ----
 *
 * 调用约定：
 *  - 所有平面参数一律传【整幅平面的基指针和整幅 stride】，row0 是该行带在
 *    整幅中的绝对起始行号；严禁把平面切片后传子指针，否则色度相位会错。
 *  - 色度 2x2 降采样/上采样始终用绝对坐标 (j/2, i/2) 定位，带与带之间不做
 *    任何独立滤波或切片，因此任意行带边界（含奇数带高、奇数起始行）都与
 *    整幅路径逐字节一致：接缝误差为 0，即小于一个量化级。
 *  - 末尾不完整的行带由调用方把 band_h 截到 h-row0 后传入；帧底/帧右的
 *    边缘像素按整幅路径同样的规则复制处理。
 *
 * rgb_to_yuv420_band 的色度写盘归属：只写顶行（2j）落在本行带内的色度块
 * 行 j。行带序列覆盖整幅时，每个色度块行恰好被写一次，拼接结果与整幅编码
 * 逐字节一致；它通过整幅基指针最多多读带外 1 行亮度对应的 RGB，不会读到
 * 帧外。
 *
 * 返回值：成功返回 0；参数非法返回 -1，包括：空指针、w/h<=0、row0<0、
 * row0>=h、band_h<=0、row0+band_h 超出 h。
 */
int yuv420_to_rgb_band(const unsigned char* y, int y_stride,
                       const unsigned char* u, int u_stride,
                       const unsigned char* v, int v_stride,
                       int w, int h, int row0, int band_h,
                       unsigned char* rgb, int rgb_stride);
int rgb_to_yuv420_band(const unsigned char* rgb, int rgb_stride,
                       int w, int h, int row0, int band_h,
                       unsigned char* y, int y_stride,
                       unsigned char* u, int u_stride,
                       unsigned char* v, int v_stride);
#endif
