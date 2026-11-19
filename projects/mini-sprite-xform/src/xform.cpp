#include "xform.h"
#include <cfloat>
#include <cmath>
#include <cstddef>

int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst) {
    if (!src || !dst || n <= 0) return -1;
    for (int i = 0; i < n; ++i) {
        dst[i].x = src[i].x * sx + tx;
        dst[i].y = src[i].y * sy + ty;
    }
    return 0;
}

int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy) {
    if (!pts || !minx || !miny || !maxx || !maxy || n <= 0) return -1;
    float a = FLT_MAX, b = FLT_MAX, c = -FLT_MAX, d = -FLT_MAX;
    for (int i = 0; i < n; ++i) {
        if (pts[i].x < a) a = pts[i].x;
        if (pts[i].y < b) b = pts[i].y;
        if (pts[i].x > c) c = pts[i].x;
        if (pts[i].y > d) d = pts[i].y;
    }
    *minx = a; *miny = b; *maxx = c; *maxy = d;
    return 0;
}

/*
 * 两阶段实现：阶段 1 只做全量校验（任何错误都不得产生部分输出），
 * 阶段 2 执行。阶段 2 的循环体与上面两个单精灵函数的循环体逐字相同。
 */
XformStatus xform_batch(const XformSpriteJob* jobs, int count,
                        SpritePoint* dst, int dst_cap,
                        SpriteBounds* bounds, int bounds_cap) {
    /* ---- 阶段 1：校验 ---- */
    if (count == 0) return XFORM_EMPTY;
    if (!jobs || !dst || dst_cap < 0 || (bounds && bounds_cap < 0) || count < 0)
        return XFORM_ERR_BAD_ARG;

    std::size_t total = 0;
    for (int j = 0; j < count; ++j) {
        if (!jobs[j].src) return XFORM_ERR_BAD_ARG;
        if (jobs[j].n <= 0)   return XFORM_ERR_BAD_ARG;
        if (!std::isfinite(jobs[j].sx) || !std::isfinite(jobs[j].sy))
            return XFORM_ERR_BAD_SCALE;
        total += static_cast<std::size_t>(jobs[j].n);
    }
    if (bounds && bounds_cap < count) return XFORM_ERR_CAPACITY;
    if (total > static_cast<std::size_t>(dst_cap)) return XFORM_ERR_CAPACITY;

    /* ---- 阶段 2：执行 ----
     * 变换与包围盒融在同一次遍历：x/y 的计算式与 xform_sprite 完全相同，
     * 比较式与 xform_bounds 完全相同，且比较的操作数就是写入 dst 的同一
     * 个 float 值，因此输出与包围盒和逐次调用逐位一致，但 dst 只写不读，
     * 省掉一遍内存流量。 */
    int base = 0;
    for (int j = 0; j < count; ++j) {
        const SpritePoint* src = jobs[j].src;
        const int n = jobs[j].n;
        const float sx = jobs[j].sx, sy = jobs[j].sy;
        const float tx = jobs[j].tx, ty = jobs[j].ty;
        SpritePoint* od = dst + base;

        float a = FLT_MAX, b = FLT_MAX, c = -FLT_MAX, d = -FLT_MAX;
        for (int i = 0; i < n; ++i) {
            float x = src[i].x * sx + tx;
            float y = src[i].y * sy + ty;
            od[i].x = x;
            od[i].y = y;
            if (x < a) a = x;
            if (y < b) b = y;
            if (x > c) c = x;
            if (y > d) d = y;
        }

        if (bounds) {
            bounds[j].minx = a; bounds[j].miny = b;
            bounds[j].maxx = c; bounds[j].maxy = d;
        }
        base += n;
    }
    return XFORM_OK;
}
