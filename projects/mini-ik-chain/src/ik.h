#ifndef IK_H
#define IK_H

/*
 * 三层平面骨骼（3 关节串联链）逆向运动学。
 *
 * 角度均为相对父骨骼的局部转角（弧度），从基座开始正向累积：
 *   关节 i 的世界朝向 = theta[0] + ... + theta[i]
 * 末端位置由 ik_forward() 给出。
 *
 * 求解器为 CCD（Cyclic Coordinate Descent）：
 *   - 关节限位 [lo, hi] 硬约束，每次更新后夹紧；
 *   - 相邻帧角度变化上限 max_step：每次迭代单关节转角不超过该值
 *     （信赖域式步长），帧内可多步推进，因此解不会跳变、
 *     也不会因步长预算耗尽而无法收敛；
 *   - 目标不可达时返回整个迭代过程中残差最小的可达构型，
 *     绝不只报失败；
 *   - 迭代次数有硬上界 max_iter；
 *   - 无随机数、固定更新顺序，结果可复现。
 */

#define IK_N 3

typedef struct { float x, y; } IkVec;

typedef struct {
    IkVec  base;                 /* 基座世界坐标 */
    float  len[IK_N];            /* 每节骨骼长度 */
    float  lo[IK_N];             /* 关节局部角下限（弧度） */
    float  hi[IK_N];             /* 关节局部角上限（弧度） */
    float  max_step;             /* 相邻帧单关节角变化上限（弧度，>0） */
    int    max_iter;             /* 迭代硬上界（>0） */
    float  tol;                  /* 末端残差收敛阈值（>0） */
} IkChain;

typedef struct {
    float  theta[IK_N];          /* 求解得到的局部角 */
    IkVec  ee;                   /* 实际末端位置 */
    float  residual;             /* 末端到目标的距离（残差） */
    int    iters;                /* 实际执行的关节扫掠轮数 */
    int    reached;              /* 残差 <= tol 为 1，否则 0（不可达/未收敛） */
} IkSolution;

/* 正向累积：由局部角计算每个关节的世界坐标，joint[0..IK_N]，
 * joint[IK_N] 即末端。返回末端。 */
IkVec ik_forward(const IkChain* c, const float theta[IK_N], IkVec joint[IK_N + 1]);

/* 单帧求解。
 *   target  : 本帧末端目标
 *   prev    : 上一帧角度（首帧传初始姿态），用于角度变化上限
 *   sol     : 输出，见 IkSolution
 * 返回 0 正常结束（含不可达时的最近可达解），-1 表示参数非法。 */
int ik_solve(const IkChain* c, IkVec target,
             const float prev[IK_N], IkSolution* sol);

#endif
