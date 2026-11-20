#ifndef YUVROT_H
#define YUVROT_H

/*
 * 平面 YUV420 (I420 风格: Y/U/V 三个独立平面, U/V 半宽半高) 的
 * 旋转 / 镜像, 色度采样点随几何一起变换。
 *
 * 支持的方向 (yr_op):
 *   YR_IDENTITY  恒等
 *   YR_R90       顺时针 90 度
 *   YR_R180      顺时针 180 度
 *   YR_R270      顺时针 270 度
 *   YR_MIRROR_H  水平镜像 (左右翻转)
 *   YR_MIRROR_V  垂直镜像 (上下翻转, 镜像与 90 度复合即得)
 *
 * 关键性质: 色度平面的格划分在整个二面体群 (旋转 90/180/270 + 镜像)
 * 下保持不变, 因此
 *     rotate( rgb_to_yuv(A) )  ==  rgb_to_yuv( rotate(A) )
 * 对三个平面逐字节相等, 任意宽高 (含奇数)、任意行跨度 (含奇数) 都成立。
 *
 * 等变色度约定 (逐轴):
 *   偶数长度 n=2k: 格 i 覆盖像素 {2i, 2i+1} (标准相邻对);
 *   奇数长度 n=2k+1: 中心像素 k 自成一格, 其余以 k 为中心反射配对,
 *       格 0={0, n-1}, 格 1={1, n-2}, ..., 格 k-1={k-1, k+1}, 格 k={k}。
 * 二维格为 x/y 两个轴划分的笛卡尔积, 色度值是格内全部像素色度的平均。
 */

typedef enum {
    YR_IDENTITY = 0,
    YR_R90,
    YR_R180,
    YR_R270,
    YR_MIRROR_H,
    YR_MIRROR_V
} yr_op;

/* 方向合法性 (非法值返回 0) */
int yr_op_valid(yr_op op);

/* 该方向是否交换宽高 (90/270 度) */
int yr_swaps_axes(yr_op op);

/* 输出尺寸。返回 0 成功, -1 参数非法 */
int yr_dst_size(yr_op op, int src_w, int src_h, int* dst_w, int* dst_h);

/*
 * 打包 RGB (每像素 3 字节, RGB 顺序) 的几何变换, 逆映射写目标,
 * 支持任意 rgb_stride (>= 3*w)。
 */
int yr_transform_rgb(yr_op op,
                     const unsigned char* src, int src_w, int src_h, int src_stride,
                     unsigned char* dst, int dst_w, int dst_h, int dst_stride);

/*
 * 等变约定下的 RGB -> YUV420。
 * cw = (w+1)/2, ch = (h+1)/2。行跨度可大于宽度, 允许奇数。
 */
int yr_rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                     unsigned char* y, int y_stride,
                     unsigned char* u, int u_stride,
                     unsigned char* v, int v_stride);

/* 对应逆变换: YUV420 -> RGB (每像素复用所属色度格的 U/V) */
int yr_yuv420_to_rgb(const unsigned char* y, int y_stride,
                     const unsigned char* u, int u_stride,
                     const unsigned char* v, int v_stride,
                     int w, int h, unsigned char* rgb, int rgb_stride);

/*
 * 平面 YUV420 的几何变换: Y 平面逐像素搬移, U/V 平面按色度格整体搬移
 * (采样点跟着转)。目标尺寸由 yr_dst_size 决定, 目标色度平面尺寸为
 * ((dst_w+1)/2) x ((dst_h+1)/2)。
 */
int yr_transform_yuv420(yr_op op,
                        const unsigned char* sy, int sy_stride,
                        const unsigned char* su, int su_stride,
                        const unsigned char* sv, int sv_stride,
                        int src_w, int src_h,
                        unsigned char* dy, int dy_stride,
                        unsigned char* du, int du_stride,
                        unsigned char* dv, int dv_stride);
#endif
