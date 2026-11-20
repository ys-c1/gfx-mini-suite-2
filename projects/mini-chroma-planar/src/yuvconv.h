#ifndef YUVCONV_H
#define YUVCONV_H
#include <stddef.h>

/*
 * 平面 YUV 4:2:2 <-> 4:2:0 互转 + 可切换矩阵的 RGB<->YUV420。
 *
 * 色度尺寸（宽 w、高 h 的亮度面）：
 *   4:2:0:  cw = (w+1)/2, ch = (h+1)/2   （水平、垂直均 2:1）
 *   4:2:2:  cw = (w+1)/2, ch = h         （仅水平 2:1）
 * 奇数宽：最后一列色度只覆盖最后一列亮度；奇数高（仅 420）：
 * 最后一行色度只覆盖最后一行亮度。重采样滤波在边界处复制边缘行。
 *
 * 两种格式水平方向采样相同，差异只在垂直方向，因此互转只做
 * 垂直方向滤波（插值方向与采样方向一致），水平逐列独立。
 * 亮度通道逐像素原样拷贝，不做任何改动。
 */

/* 像素格式 */
#define YC_FMT_420 0
#define YC_FMT_422 1

/* 转换矩阵（均为 full-range） */
#define YC_MATRIX_BT601 0
#define YC_MATRIX_BT709 1

/* 状态码：不同失败原因给不同码 */
#define YC_OK          0
#define YC_ERR_ARG    (-1)  /* 空指针参数 */
#define YC_ERR_FORMAT (-2)  /* 未知像素格式或未知矩阵 */
#define YC_ERR_SIZE   (-3)  /* 尺寸非法或 stride 小于平面宽度（尺寸不匹配） */
#define YC_ERR_BUFFER (-4)  /* 输出缓冲容量不足 */

/* 查询某格式下色度面的列数/行数；返回 YC_OK 或错误码 */
int yc_chroma_dims(int fmt, int w, int h, int* cw, int* ch);

/*
 * 420 <-> 422 互转。
 * y_dst/u_dst/v_dst 为输出平面，*_cap 为对应缓冲的字节容量，
 * 容量不足返回 YC_ERR_BUFFER 且不写任何输出。
 * 允许 y_dst == y_src（亮度原地）；色度不支持原地。
 * src_fmt == dst_fmt 时只做逐面拷贝。
 */
int yuv_convert(int src_fmt, int dst_fmt,
                const unsigned char* y_src, int y_stride_src,
                const unsigned char* u_src, int u_stride_src,
                const unsigned char* v_src, int v_stride_src,
                int w, int h,
                unsigned char* y_dst, int y_stride_dst, size_t y_cap,
                unsigned char* u_dst, int u_stride_dst, size_t u_cap,
                unsigned char* v_dst, int v_stride_dst, size_t v_cap);

/* 带矩阵选择的 RGB<->YUV420；matrix 取 YC_MATRIX_*，未知矩阵返回 YC_ERR_FORMAT。
 * matrix == YC_MATRIX_BT601 时结果与 chroma.h 中的原函数一致。 */
int rgb_to_yuv420_m(const unsigned char* rgb, int w, int h, int rgb_stride,
                    unsigned char* y, int y_stride,
                    unsigned char* u, int u_stride,
                    unsigned char* v, int v_stride, int matrix);
int yuv420_to_rgb_m(const unsigned char* y, int y_stride,
                    const unsigned char* u, int u_stride,
                    const unsigned char* v, int v_stride,
                    int w, int h, unsigned char* rgb, int rgb_stride, int matrix);
#endif
