#ifndef XFORM_H
#define XFORM_H
struct SpritePoint { float x, y; };

int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst);
int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy);

/* ---------------- 批量变换 ---------------- */

/* 五种可区分的返回状态 */
enum XformStatus {
    XFORM_OK            =  0,  /* 整批成功 */
    XFORM_ERR_BAD_ARG   = -1,  /* 空指针、负/零顶点数等参数错误 */
    XFORM_EMPTY         = -2,  /* 空批次：count == 0 */
    XFORM_ERR_BAD_SCALE = -3,  /* 批内至少一个作业的 sx/sy 为 NaN/±Inf */
    XFORM_ERR_CAPACITY  = -4,  /* dst 或 bounds 容量装不下本批结果 */
};

/* 一个精灵的变换作业：各自独立的缩放与偏移 */
struct XformSpriteJob {
    const SpritePoint* src;
    int   n;
    float sx, sy;
    float tx, ty;
};

struct SpriteBounds { float minx, miny, maxx, maxy; };

/*
 * 一次处理 count 个精灵。
 *  - 变换后的顶点按作业顺序紧凑写入 dst：第 j 个精灵的输出起点
 *    等于前 j 个作业 n 之和，与逐个调用 xform_sprite 的拼接顺序完全一致。
 *  - dst_cap 是 dst 可容纳的 SpritePoint 总数；bounds 传 nullptr 表示
 *    不计算包围盒，否则 bounds_cap 必须 >= count。
 *  - 先全量校验后执行：任一错误返回时不写任何输出。
 *  - 成功路径的每条算术/比较语句与 xform_sprite、xform_bounds 相同，
 *    输出（含包围盒）与逐次调用逐位一致。
 */
XformStatus xform_batch(const XformSpriteJob* jobs, int count,
                        SpritePoint* dst, int dst_cap,
                        SpriteBounds* bounds, int bounds_cap);
#endif
