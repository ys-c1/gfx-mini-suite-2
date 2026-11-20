#define _POSIX_C_SOURCE 199309L
#include "../src/ik.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const float PI = 3.14159265358979323846f;

/* 网格暴力搜索：在关节限位内穷举，找目标的最近可达构型，
 * 作为不可达用例的独立参照（帧间步长只限制单步速度，不限制可达域）。 */
static float brute_force_residual(const IkChain* c, IkVec target, int n_per_axis) {
    float best = 1e30f;
    float t[IK_N];
    float denom = (float)(n_per_axis - 1);
    for (int a = 0; a < n_per_axis; ++a) {
        t[0] = c->lo[0] + (c->hi[0] - c->lo[0]) * (float)a / denom;
        for (int b = 0; b < n_per_axis; ++b) {
            t[1] = c->lo[1] + (c->hi[1] - c->lo[1]) * (float)b / denom;
            for (int d = 0; d < n_per_axis; ++d) {
                t[2] = c->lo[2] + (c->hi[2] - c->lo[2]) * (float)d / denom;
                IkVec j[IK_N + 1];
                IkVec ee = ik_forward(c, t, j);
                float dx = ee.x - target.x, dy = ee.y - target.y;
                float r = sqrtf(dx * dx + dy * dy);
                if (r < best) best = r;
            }
        }
    }
    return best;
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

int main(void) {
    /* ---- 1. 正向累积：完全伸直指向 x 轴，末端 = 基座 + 总长 ---- */
    IkChain chain = {
        .base = {0.0f, 0.0f},
        .len = {1.0f, 0.8f, 0.6f},
        .lo = {-PI / 2, -2.0f, -2.0f},
        .hi = { PI / 2,  2.0f,  2.0f},
        .max_step = 0.35f,
        .max_iter = 64,
        .tol = 1e-4f,
    };
    {
        float th[IK_N] = {0, 0, 0};
        IkVec j[IK_N + 1];
        IkVec ee = ik_forward(&chain, th, j);
        CHECK(fabsf(j[1].x - 1.0f) < 1e-6f && fabsf(j[1].y) < 1e-6f);
        CHECK(fabsf(j[2].x - 1.8f) < 1e-6f);
        CHECK(fabsf(ee.x - 2.4f) < 1e-6f && fabsf(ee.y) < 1e-6f);

        float up[IK_N] = {PI / 2, 0, 0};   /* 整体指向上方 */
        ee = ik_forward(&chain, up, j);
        CHECK(fabsf(ee.x) < 1e-6f && fabsf(ee.y - 2.4f) < 1e-6f);
    }

    /* ---- 2. 可达目标：残差收敛到容差以内，末端贴近目标 ---- */
    float prev[IK_N] = {0, 0, 0};
    IkSolution sol;
    IkVec t1 = {1.9f, 0.8f};
    CHECK(ik_solve(&chain, t1, prev, &sol) == 0);
    CHECK(sol.reached == 1);
    CHECK(sol.residual <= chain.tol);
    CHECK(sol.iters <= chain.max_iter && sol.iters > 0);
    CHECK(fabsf(sol.ee.x - t1.x) < 1e-3f && fabsf(sol.ee.y - t1.y) < 1e-3f);

    /* ---- 3. 关节限位：所有角度都在 [lo, hi] 内 ---- */
    for (int i = 0; i < IK_N; ++i)
        CHECK(sol.theta[i] >= chain.lo[i] - 1e-6f &&
              sol.theta[i] <= chain.hi[i] + 1e-6f);

    /* 故意把基座关节限制在很窄的区间，解必须遵守 */
    IkChain narrow = chain;
    narrow.lo[0] = -0.05f; narrow.hi[0] = 0.05f;
    IkSolution sol_n;
    CHECK(ik_solve(&narrow, (IkVec){1.0f, 1.8f}, prev, &sol_n) == 0);
    CHECK(sol_n.theta[0] >= -0.05f - 1e-6f && sol_n.theta[0] <= 0.05f + 1e-6f);

    /* ---- 4. 帧间步长：max_iter=1 时单帧每关节变化 <= max_step ----
     * （步长是逐迭代信赖域，一轮扫掠内每关节只更新一次）。 */
    IkChain one = chain; one.max_iter = 1;
    IkSolution sol1;
    CHECK(ik_solve(&one, (IkVec){0.4f, 1.9f}, prev, &sol1) == 0);
    for (int i = 0; i < IK_N; ++i)
        CHECK(fabsf(sol1.theta[i] - prev[i]) <= chain.max_step + 1e-6f);
    /* 收紧步长预算后，单帧位移也随之变小（解不跳变）。 */
    IkChain tight = chain; tight.max_step = 0.05f; tight.max_iter = 1;
    IkSolution solt;
    CHECK(ik_solve(&tight, (IkVec){0.4f, 1.9f}, prev, &solt) == 0);
    for (int i = 0; i < IK_N; ++i)
        CHECK(fabsf(solt.theta[i] - prev[i]) <= 0.05f + 1e-6f);

    /* ---- 5. 不可达目标：必须返回最近可达解 + 残差，不能失败 ---- */
    IkVec far = {-2.2f, 1.5f};    /* 基座关节限 ±90°，够不到左后方 */
    IkSolution sol_far;
    CHECK(ik_solve(&chain, far, prev, &sol_far) == 0);
    CHECK(sol_far.reached == 0);
    CHECK(sol_far.residual > chain.tol);
    float grid_best = brute_force_residual(&chain, far, 41);
    /* CCD 解应接近网格最优（网格本身有离散误差，给 5% 余量） */
    CHECK(sol_far.residual <= grid_best * 1.05f + 1e-3f);
    for (int i = 0; i < IK_N; ++i)
        CHECK(sol_far.theta[i] >= chain.lo[i] - 1e-6f &&
              sol_far.theta[i] <= chain.hi[i] + 1e-6f);

    /* 超出最大臂展的目标：最近点在完全伸直方向上 */
    IkVec beyond = {5.0f, 0.0f};
    IkSolution sol_b;
    CHECK(ik_solve(&chain, beyond, prev, &sol_b) == 0);
    CHECK(sol_b.reached == 0);
    CHECK(fabsf(sol_b.residual - (5.0f - 2.4f)) < 1e-2f);

    /* ---- 6. 可复现：同样输入两次求解逐位一致 ---- */
    IkSolution sol_again;
    CHECK(ik_solve(&chain, t1, prev, &sol_again) == 0);
    for (int i = 0; i < IK_N; ++i) CHECK(sol.theta[i] == sol_again.theta[i]);
    CHECK(sol.iters == sol_again.iters);

    /* ---- 7. 迭代硬上界：max_iter=2 时 iters 不得超过 2 ---- */
    IkChain capped = chain; capped.max_iter = 2;
    IkSolution sol_c;
    CHECK(ik_solve(&capped, (IkVec){0.5f, 1.7f}, prev, &sol_c) == 0);
    CHECK(sol_c.iters <= 2);

    /* ---- 8. 非法参数返回 -1 ---- */
    IkChain bad = chain; bad.max_step = 0.0f;
    CHECK(ik_solve(&bad, t1, prev, &sol) == -1);
    CHECK(ik_solve(&chain, t1, NULL, &sol) == -1);

    /* ================= 四组目标的连续帧演示 ================= */
    printf("\n=== 四帧连续求解（len = 1.0 / 0.8 / 0.6，tol = 1e-4，max_iter = 64，单步上限 = 0.35 rad/迭代）===\n");
    IkVec targets[4] = {
        { 1.9f,  0.8f},   /* 帧1：可达 */
        { 0.6f,  1.7f},   /* 帧2：可达，需多步推进（每步 <= 0.35 rad） */
        {-2.2f,  1.5f},   /* 帧3：不可达，给最近可达解 */
        { 1.8f, -1.2f},   /* 帧4：重新可达 */
    };
    float frame_prev[IK_N] = {0, 0, 0};
    double total_us = 0.0;
    for (int f = 0; f < 4; ++f) {
        IkSolution s;
        double t0 = now_us();
        int rc = ik_solve(&chain, targets[f], frame_prev, &s);
        double t1_us = now_us();
        double dt = t1_us - t0;
        total_us += dt;
        CHECK(rc == 0);

        float dmax = 0.0f, dsum = 0.0f;
        for (int i = 0; i < IK_N; ++i) {
            float d = fabsf(s.theta[i] - frame_prev[i]);
            if (d > dmax) dmax = d;
            dsum += d;
            CHECK(s.theta[i] >= chain.lo[i] - 1e-6f &&
                  s.theta[i] <= chain.hi[i] + 1e-6f);
        }
        /* 信赖域验证：逐迭代求解（max_iter=1..iters），相邻迭代间
         * 每个关节的变化都不得超过 max_step。 */
        IkChain step = chain;
        float watch[IK_N];
        for (int i = 0; i < IK_N; ++i) watch[i] = frame_prev[i];
        for (int it = 1; it <= s.iters; ++it) {
            step.max_iter = it;
            IkSolution sk;
            CHECK(ik_solve(&step, targets[f], frame_prev, &sk) == 0);
            for (int i = 0; i < IK_N; ++i)
                CHECK(fabsf(sk.theta[i] - watch[i]) <= chain.max_step + 1e-6f);
            for (int i = 0; i < IK_N; ++i) watch[i] = sk.theta[i];
        }

        printf("帧%d 目标(% .2f,% .2f) | %s | 残差 %.6f | 迭代 %2d | "
               "角度(度) [%7.2f %7.2f %7.2f] | 帧内角变化 max %.3f / sum %.3f rad | 耗时 %.1f us\n",
               f + 1, targets[f].x, targets[f].y,
               s.reached ? "可达  " : "不可达",
               s.residual, s.iters,
               s.theta[0] * 180.0f / PI, s.theta[1] * 180.0f / PI, s.theta[2] * 180.0f / PI,
               dmax, dsum, dt);

        for (int i = 0; i < IK_N; ++i) frame_prev[i] = s.theta[i];
    }
    printf("四帧总耗时 %.1f us，平均 %.1f us/帧\n", total_us, total_us / 4.0);
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
