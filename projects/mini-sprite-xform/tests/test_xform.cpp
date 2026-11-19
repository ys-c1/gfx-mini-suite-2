#include "../src/xform.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t bits_of(float f) {
    uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u;
}
/* 逐位比较（NaN 也按载荷位比较） */
static bool same_bits(float a, float b) { return bits_of(a) == bits_of(b); }

/* 与头文件中状态码定义无关的独立期望值，防止枚举值误改 */
static void test_status_codes_distinguishable() {
    CHECK(XFORM_OK == 0);
    CHECK(XFORM_ERR_BAD_ARG != XFORM_EMPTY);
    CHECK(XFORM_EMPTY != XFORM_ERR_BAD_SCALE);
    CHECK(XFORM_ERR_BAD_SCALE != XFORM_ERR_CAPACITY);
    CHECK(XFORM_ERR_CAPACITY != XFORM_ERR_BAD_ARG);
    CHECK(XFORM_ERR_BAD_ARG != XFORM_ERR_BAD_SCALE);
    CHECK(XFORM_EMPTY != XFORM_ERR_CAPACITY);
}

/* 确定性 LCG，测试与基准共用同一份数据生成方式 */
static uint32_t lcg_state = 0x12345678u;
static float rand_float(float lo, float hi) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    float t = (lcg_state >> 8) / float(1u << 24);
    return lo + t * (hi - lo);
}

int main() {
    /* ---- 原有测试（原样保留） ---- */
    SpritePoint src[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    SpritePoint dst[4];
    CHECK(xform_sprite(src, 4, 2.0f, 3.0f, 1.0f, -1.0f, dst) == 0);
    CHECK(dst[1].x == 5.0f && dst[1].y == -1.0f);
    float a, b, c, d;
    CHECK(xform_bounds(dst, 4, &a, &b, &c, &d) == 0);
    CHECK(a == 1.0f && b == -1.0f && c == 5.0f && d == 2.0f);
    CHECK(xform_sprite(src, 0, 1, 1, 0, 0, dst) == -1);

    test_status_codes_distinguishable();

    /* ---- 1. 空批次 ---- */
    SpritePoint tmp_pt[1]; SpriteBounds tmp_bd[1];
    CHECK(xform_batch(nullptr, 0, tmp_pt, 1, tmp_bd, 1) == XFORM_EMPTY);
    /* 即使同时存在其他毛病，空批次优先报 EMPTY */
    CHECK(xform_batch(nullptr, 0, nullptr, 0, nullptr, 0) == XFORM_EMPTY);

    /* ---- 2. 普通参数错误（与非法缩放、容量不足可区分） ---- */
    XformSpriteJob good[1] = {{src, 4, 1, 1, 0, 0}};
    CHECK(xform_batch(nullptr, 1, tmp_pt, 4, tmp_bd, 1) == XFORM_ERR_BAD_ARG);
    CHECK(xform_batch(good, 1, nullptr, 4, tmp_bd, 1) == XFORM_ERR_BAD_ARG);
    XformSpriteJob bad_n[1] = {{src, 0, 1, 1, 0, 0}};
    CHECK(xform_batch(bad_n, 1, tmp_pt, 4, tmp_bd, 1) == XFORM_ERR_BAD_ARG);
    XformSpriteJob bad_src[1] = {{nullptr, 4, 1, 1, 0, 0}};
    CHECK(xform_batch(bad_src, 1, tmp_pt, 4, tmp_bd, 1) == XFORM_ERR_BAD_ARG);

    /* ---- 3. 批内混入非法缩放（NaN / +Inf / -Inf），另含合法负缩放 ---- */
    XformSpriteJob scale_jobs[4] = {
        {src, 4, -1.0f,  1.0f, 0.0f, 0.0f},          /* 负缩放合法 */
        {src, 4,  NAN,   1.0f, 0.0f, 0.0f},
        {src, 4,  1.0f, INFINITY, 0.0f, 0.0f},
        {src, 4,  1.0f, -INFINITY, 0.0f, 0.0f},
    };
    CHECK(xform_batch(scale_jobs, 4, tmp_pt, 16, tmp_bd, 4) == XFORM_ERR_BAD_SCALE);
    XformSpriteJob only_bad[1] = {{src, 4, 0.0f, NAN, 0.0f, 0.0f}};
    CHECK(xform_batch(only_bad, 1, tmp_pt, 4, tmp_bd, 1) == XFORM_ERR_BAD_SCALE);
    /* 非法缩放与其他错误同时存在时，参数错误先报（空指针优先），
       但容量不足要等校验全过才报——见下条 */

    /* ---- 4. 容量不足（顶点容量与包围盒容量分别触发） ---- */
    SpritePoint cap_pt[15]; SpriteBounds cap_bd[4];
    CHECK(xform_batch(scale_jobs, 4, cap_pt, 16, cap_bd, 4) == XFORM_ERR_BAD_SCALE);
    XformSpriteJob cap_jobs[2] = {
        {src, 4, 1, 1, 0, 0}, {src, 4, 2, 2, 1, 1},
    };
    CHECK(xform_batch(cap_jobs, 2, cap_pt, 7, cap_bd, 2) == XFORM_ERR_CAPACITY);
    CHECK(xform_batch(cap_jobs, 2, cap_pt, 8, cap_bd, 1) == XFORM_ERR_CAPACITY);
    /* 刚好够：边界值应成功 */
    CHECK(xform_batch(cap_jobs, 2, cap_pt, 8, cap_bd, 2) == XFORM_OK);
    /* 错误时不写任何输出：目标缓冲填入哨兵后调用失败，哨兵必须原样保留 */
    for (int i = 0; i < 15; ++i) cap_pt[i] = {12345.0f, 12345.0f};
    for (int i = 0; i < 4; ++i) cap_bd[i] = {1, 2, 3, 4};
    CHECK(xform_batch(cap_jobs, 2, cap_pt, 7, cap_bd, 2) == XFORM_ERR_CAPACITY);
    bool untouched = true;
    for (int i = 0; i < 15; ++i)
        if (!same_bits(cap_pt[i].x, 12345.0f) || !same_bits(cap_pt[i].y, 12345.0f))
            untouched = false;
    for (int i = 0; i < 4; ++i)
        if (!same_bits(cap_bd[i].minx, 1.0f) || !same_bits(cap_bd[i].maxy, 4.0f))
            untouched = false;
    CHECK(untouched);

    /* ---- 5. 不同缩放/偏移组合 + 与逐次调用逐位一致（顶点与包围盒） ---- */
    const int K = 37;                 /* 精灵数，每个顶点数 1..5 不等 */
    std::vector<std::vector<SpritePoint>> pools(K);
    std::vector<XformSpriteJob> jobs(K);
    int total_n = 0;
    for (int j = 0; j < K; ++j) {
        int n = 1 + (j % 5);
        pools[j].resize(n);
        for (int i = 0; i < n; ++i) {
            pools[j][i].x = rand_float(-100.0f, 100.0f);
            pools[j][i].y = rand_float(-100.0f, 100.0f);
        }
        jobs[j] = {pools[j].data(), n,
                   rand_float(-4.0f, 4.0f), rand_float(-4.0f, 4.0f),
                   rand_float(-50.0f, 50.0f), rand_float(-50.0f, 50.0f)};
        total_n += n;
    }
    /* 注入取整敏感值：若两边 FMA 收缩策略不同，这类值会暴露差异 */
    pools[0][0].x = 0.1f; jobs[0].sx = 0.1f; jobs[0].tx = 0.2f;   /* 0.1*0.1+0.2 */
    pools[1][0].y = 1.0f; jobs[1].sy = 3.0f; jobs[1].ty = 1.0f / 3.0f;

    std::vector<SpritePoint> batch_out(total_n);
    std::vector<SpriteBounds> batch_bd(K);
    CHECK(xform_batch(jobs.data(), K, batch_out.data(), total_n,
                      batch_bd.data(), K) == XFORM_OK);

    std::vector<SpritePoint> ref_out(total_n);
    int off = 0;
    for (int j = 0; j < K; ++j) {
        CHECK(xform_sprite(jobs[j].src, jobs[j].n, jobs[j].sx, jobs[j].sy,
                           jobs[j].tx, jobs[j].ty, ref_out.data() + off) == 0);
        float rminx, rminy, rmaxx, rmaxy;
        CHECK(xform_bounds(ref_out.data() + off, jobs[j].n,
                           &rminx, &rminy, &rmaxx, &rmaxy) == 0);
        CHECK(same_bits(batch_bd[j].minx, rminx));
        CHECK(same_bits(batch_bd[j].miny, rminy));
        CHECK(same_bits(batch_bd[j].maxx, rmaxx));
        CHECK(same_bits(batch_bd[j].maxy, rmaxy));
        off += jobs[j].n;
    }
    /* 顶点逐位一致，且输出顺序等于逐次调用的拼接顺序 */
    bool bits_equal = true;
    for (int i = 0; i < total_n; ++i)
        if (!same_bits(batch_out[i].x, ref_out[i].x) ||
            !same_bits(batch_out[i].y, ref_out[i].y))
            bits_equal = false;
    CHECK(bits_equal);

    /* 偏移 tx/ty 含 Inf 合法（规格只禁止非法缩放），结果位模式仍须一致 */
    XformSpriteJob inf_jobs[2] = {
        {src, 4, 1.0f, 1.0f, INFINITY, 0.0f},
        {src, 4, 1.0f, 1.0f, -INFINITY, 0.0f},
    };
    SpritePoint inf_out[8], ref_inf[8];
    SpriteBounds inf_bd[2];
    CHECK(xform_batch(inf_jobs, 2, inf_out, 8, inf_bd, 2) == XFORM_OK);
    for (int j = 0; j < 2; ++j) {
        xform_sprite(inf_jobs[j].src, 4, inf_jobs[j].sx, inf_jobs[j].sy,
                     inf_jobs[j].tx, inf_jobs[j].ty, ref_inf + 4 * j);
        for (int i = 0; i < 4; ++i) {
            CHECK(same_bits(inf_out[4 * j + i].x, ref_inf[4 * j + i].x));
            CHECK(same_bits(inf_out[4 * j + i].y, ref_inf[4 * j + i].y));
        }
    }

    /* bounds == nullptr：跳过包围盒，顶点仍然正确 */
    CHECK(xform_batch(cap_jobs, 2, cap_pt, 8, nullptr, 0) == XFORM_OK);
    CHECK(same_bits(cap_pt[4].x, src[0].x * 2.0f + 1.0f));

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
