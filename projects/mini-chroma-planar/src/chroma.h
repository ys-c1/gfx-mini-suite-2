#ifndef CHROMA_H
#define CHROMA_H
#include <stddef.h>

/* 色度上采样方式（YUV420 -> RGB 还原阶段） */
enum chroma_up_mode {
    CHROMA_UP_NEAREST  = 0, /* 最近点复制：cx = floor(i/2)，与旧版实现逐像素一致 */
    CHROMA_UP_BILINEAR = 1, /* 双线性，可分离两级 1/2 混合，每级四舍五入 */
    CHROMA_UP_BICUBIC  = 2  /* 双三次 Catmull-Rom(a=-0.5)，定点 1/16 权重 */
};

/* 可区分的返回码：>0 为带形状标记的成功，<0 为错误 */
enum chroma_status {
    CHROMA_OK         = 0,  /* 成功，宽高均为偶数 */
    CHROMA_DEGENERATE = 1,  /* 成功，但 w==1 或 h==1，单方向退化为复制 */
    CHROMA_ODD        = 2,  /* 成功，宽或高为奇数，末列/末行按边界规则处理 */
    CHROMA_ERR_ARG    = -1, /* 空指针、尺寸<=0、非法 mode、stride 过小 */
    CHROMA_ERR_BUFFER = -2, /* u_cap/v_cap 小于色度平面实际所需字节数 */
    CHROMA_ERR_NOMEM  = -3  /* 内部中间缓冲分配失败 */
};

/*
 * 坐标落位与取值规则（三种上采样共同遵守）：
 *
 * 色度平面尺寸 cw = ceil(w/2), ch = ceil(h/2)。
 * 采用 co-sited（同位）落位：色度采样点 (cx,cy) 与亮度像素 (2cx,2cy) 同址，
 * 即亮度像素 (i,j) 映射到连续色度坐标 (i/2, j/2)。
 *
 * 以水平方向为例（垂直方向规则完全相同）：
 *  规则1 i 为偶数——含奇数宽度的最后一列 i=w-1：坐标恰好落在采样点
 *        cx=i/2 上，直接取该采样点，不读取 cx+1；三种方式在此像素结果
 *        完全相同，因此与亮度通道严格对齐。
 *  规则2 i 为奇数且 cx+1<cw：相位 1/2，按各自滤波器混合 cx 与 cx+1
 *        （最近点取 cx；双线性 (a+b+1)/2；双三次 (-p0+9a+9b-p3)/16）。
 *  规则3 i 为奇数且 cx+1==cw（只可能出现在偶数宽度的最后一列）：
 *        钳制下标、复制最后一个采样点，不做任何外推。
 *  规则4 w==1 或 h==1：仅尺寸为 1 的那个轴退化为复制——该轴只有一个
 *        采样点，所有像素坐标均为偶数，规则2/3 永不触发；另一个轴仍按
 *        所选方式正常滤波（例如 1x12 水平复制、垂直照常混合）。
 *
 * 双三次边界：四次取数下标统一钳制到 [0,cw-1]（边界复制）；偶数位置
 * 权重为 (0,16,0,0)/16，同样精确命中采样点。所有定点除法向远离 0 方向
 * 四舍五入，最终色度值钳制到 [0,255]。
 */

int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride);

/* 旧接口：签名与行为保持不变，内部等价于以 CHROMA_UP_NEAREST 调用 ex 版 */
int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride);

/* 扩展接口：mode 选择上采样方式；u_cap/v_cap 为 u/v 缓冲实际容量（字节），
 * 不足以容纳 (ch-1)*stride+cw 时返回 CHROMA_ERR_BUFFER。*/
int yuv420_to_rgb_ex(const unsigned char* y, int y_stride,
                     const unsigned char* u, int u_stride,
                     const unsigned char* v, int v_stride,
                     int w, int h, unsigned char* rgb, int rgb_stride,
                     enum chroma_up_mode mode,
                     size_t u_cap, size_t v_cap);

/* 色度平面尺寸查询与给定 stride 下的最小字节容量 */
int chroma420_plane_dims(int w, int h, int* cw, int* ch);
size_t chroma420_plane_bytes(int c_stride, int cw, int ch);
#endif
