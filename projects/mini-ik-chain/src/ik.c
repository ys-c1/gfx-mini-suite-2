#include "ik.h"
#include <math.h>
#include <stddef.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

IkVec ik_forward(const IkChain* c, const float theta[IK_N], IkVec joint[IK_N + 1]) {
    joint[0] = c->base;
    float ang = 0.0f, cx = c->base.x, cy = c->base.y;
    for (int i = 0; i < IK_N; ++i) {
        ang += theta[i];
        cx += c->len[i] * cosf(ang);
        cy += c->len[i] * sinf(ang);
        joint[i + 1].x = cx;
        joint[i + 1].y = cy;
    }
    return joint[IK_N];
}

static float residual_of(const IkChain* c, const float theta[IK_N], IkVec target) {
    IkVec j[IK_N + 1];
    IkVec ee = ik_forward(c, theta, j);
    float dx = ee.x - target.x, dy = ee.y - target.y;
    return sqrtf(dx * dx + dy * dy);
}

int ik_solve(const IkChain* c, IkVec target,
             const float prev[IK_N], IkSolution* sol) {
    if (!c || !prev || !sol) return -1;
    if (c->max_step <= 0.0f || c->max_iter <= 0 || c->tol <= 0.0f) return -1;
    for (int i = 0; i < IK_N; ++i) {
        if (c->len[i] < 0.0f || c->lo[i] > c->hi[i]) return -1;
    }

    float theta[IK_N], best[IK_N];
    /* 起点为上一帧构型，先夹紧到关节限位内（prev 本身来自合法解，
     * 正常情况下无需移动）。 */
    for (int i = 0; i < IK_N; ++i) {
        theta[i] = clampf(prev[i], c->lo[i], c->hi[i]);
        best[i] = theta[i];
    }

    float best_res = residual_of(c, theta, target);
    int used = 0, reached = best_res <= c->tol;

    for (int it = 0; it < c->max_iter && !reached; ++it) {
        used = it + 1;
        IkVec joint[IK_N + 1];
        ik_forward(c, theta, joint);

        /* 固定从末端到基座的更新顺序，保证可复现。 */
        for (int i = IK_N - 1; i >= 0; --i) {
            float px = joint[i].x, py = joint[i].y;
            float ex = joint[IK_N].x - px, ey = joint[IK_N].y - py;
            float tx = target.x - px, ty = target.y - py;
            float el = sqrtf(ex * ex + ey * ey);
            float tl = sqrtf(tx * tx + ty * ty);
            if (el < 1e-9f || tl < 1e-9f) continue;   /* 退化构型，跳过该关节 */

            /* 把“关节->末端”方向转到“关节->目标”方向所需的转角。 */
            float dot = ex * tx + ey * ty;
            float cross = ex * ty - ey * tx;
            float d = atan2f(cross / (el * tl), dot / (el * tl));

            /* 信赖域：单次迭代转角不超过 max_step，解不会跳变；
             * 关节限位为硬约束。 */
            d = clampf(d, -c->max_step, c->max_step);
            theta[i] = clampf(theta[i] + d, c->lo[i], c->hi[i]);

            /* 增量更新后续关节世界坐标，避免每动一个关节就重算整条链。 */
            float ang = 0.0f;
            for (int k = 0; k <= i; ++k) ang += theta[k];
            float ax = joint[i].x, ay = joint[i].y;
            for (int k = i; k < IK_N; ++k) {
                ax += c->len[k] * cosf(ang);
                ay += c->len[k] * sinf(ang);
                joint[k + 1].x = ax;
                joint[k + 1].y = ay;
                if (k + 1 < IK_N) ang += theta[k + 1];
            }
        }

        float r = residual_of(c, theta, target);
        if (r < best_res) {                 /* 只记住全程最优，防止末期震荡 */
            best_res = r;
            for (int i = 0; i < IK_N; ++i) best[i] = theta[i];
        }
        if (r <= c->tol) reached = 1;
    }

    for (int i = 0; i < IK_N; ++i) sol->theta[i] = best[i];
    sol->ee = ik_forward(c, best, (IkVec[IK_N + 1]){0});
    sol->residual = best_res;
    sol->iters = used;
    sol->reached = reached;
    return 0;
}
