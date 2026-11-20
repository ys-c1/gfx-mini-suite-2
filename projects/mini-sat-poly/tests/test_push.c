#include "../src/sat.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static SatPoly box(float x, float y, float w, float h) {
    SatPoly p; p.n = 4;
    p.p[0] = (SatVec){x, y}; p.p[1] = (SatVec){x + w, y};
    p.p[2] = (SatVec){x + w, y + h}; p.p[3] = (SatVec){x, y + h};
    return p;
}

/* 中心 (cx,cy)、外接半径 r、旋转角 ang 的正 n 边形 */
static SatPoly ngon(float cx, float cy, float r, int n, float ang) {
    SatPoly p; p.n = n;
    for (int i = 0; i < n; ++i) {
        float a = ang + (float)i * 6.283185307f / n;
        p.p[i] = (SatVec){cx + r * cosf(a), cy + r * sinf(a)};
    }
    return p;
}

static unsigned long rng;
static float jitter(float amp) {
    rng = rng * 1103515245UL + 12345UL;
    float u = (float)((rng >> 16) & 0x7fff) / 16384.0f; /* [0,1) */
    return (u - 0.5f) * 2.0f * amp;
}

static float vmag(SatVec v) { return sqrtf(v.x * v.x + v.y * v.y); }

/* 两个单位方向间的夹角 [0, pi] */
static float ang_between(SatVec u, SatVec v) {
    float d = u.x * v.x + u.y * v.y;
    float c = u.x * v.y - u.y * v.x;
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    return atan2f(fabsf(c), d);
}

typedef struct {
    const char* name;
    int frames, contacts, missed, wrong;
    float raw_max_dmag, sm_max_dmag;   /* 帧间分离量跳变 */
    float raw_max_dang, sm_max_dang;   /* 帧间方向跳变 */
    float sm_min_mag;
    float sum_ang_diff;                /* 平滑方向与真实 MTV 方向的累计偏差 */
    float last_raw_mag, last_sm_mag, last_ang_diff;
    int prev_contact;
    SatVec prev_raw_dir, prev_sm_dir;
    float prev_raw_mag, prev_sm_mag;
} SeqStat;

/* wrong: 推力与质心分离方向反向（真正的 180 度错推）。
   平滑方向因转角上限暂时滞后于当帧 MTV 不算错推。 */
static int points_wrong_way(const SatPoly* a, const SatPoly* b, SatVec push) {
    float acx, acy, bcx, bcy;
    sat_centroid(a, &acx, &acy);
    sat_centroid(b, &bcx, &bcy);
    float rx = acx - bcx, ry = acy - bcy;
    if (rx * rx + ry * ry < 1e-12f) return 0;
    return push.x * rx + push.y * ry < 0.0f;
}

static void stat_record(SeqStat* s, const SatPoly* a, const SatPoly* b,
                        int hit, SatVec raw, SatVec push) {
    s->frames++;
    if (hit <= 0) { s->prev_contact = 0; return; }
    s->contacts++;

    float rm = vmag(raw), sm = vmag(push);
    SatVec rd = {raw.x / rm, raw.y / rm};
    if (sm == 0.0f) {
        s->missed++; /* 接触帧推力为零：漏推 */
    } else {
        if (points_wrong_way(a, b, push)) s->wrong++;
        SatVec sd = {push.x / sm, push.y / sm};
        s->sum_ang_diff += ang_between(sd, rd);
        if (sm < s->sm_min_mag || s->contacts == 1) s->sm_min_mag = sm;
        /* 只在连续接触帧之间统计抖动：跨分离间隙的重新初始化不算抖动 */
        if (s->prev_contact) {
            float dm = fabsf(sm - s->prev_sm_mag);
            float da = ang_between(sd, s->prev_sm_dir);
            if (dm > s->sm_max_dmag) s->sm_max_dmag = dm;
            if (da > s->sm_max_dang) s->sm_max_dang = da;
            float rdm = fabsf(rm - s->prev_raw_mag);
            float rda = ang_between(rd, s->prev_raw_dir);
            if (rdm > s->raw_max_dmag) s->raw_max_dmag = rdm;
            if (rda > s->raw_max_dang) s->raw_max_dang = rda;
        }
        s->prev_sm_dir = sd; s->prev_raw_dir = rd;
        s->prev_sm_mag = sm; s->prev_raw_mag = rm;
        s->last_sm_mag = sm; s->last_raw_mag = rm;
        s->last_ang_diff = ang_between(sd, rd);
    }
    s->prev_contact = 1;
}

static void stat_print(const SeqStat* s) {
    printf("  %-30s frames=%2d contact=%2d missed=%d wrong=%d\n"
           "    raw   : max|dmag|=%.3f  max|dang|=%.3f rad\n"
           "    smooth: max|dmag|=%.3f  max|dang|=%.3f rad  min_mag=%.3f  mean_track_err=%.3f rad\n",
           s->name, s->frames, s->contacts, s->missed, s->wrong,
           s->raw_max_dmag, s->raw_max_dang,
           s->sm_max_dmag, s->sm_max_dang, s->sm_min_mag,
           s->contacts ? s->sum_ang_diff / s->contacts : 0.0f);
}

int main(void) {
    SatPushConfig cfg;
    sat_push_default_config(&cfg);

    /* ---- MTV 基础正确性 ---- */
    SatPoly a0 = box(0, 0, 10, 10);
    SatPoly bx = box(8, 0, 10, 10), by = box(0, 8, 10, 10), far = box(30, 30, 4, 4);
    SatVec m;
    CHECK(sat_mtv(&a0, &bx, &m) == 1);
    CHECK(fabsf(m.x - (-2.0f)) < 1e-4f && fabsf(m.y) < 1e-4f);
    CHECK(sat_mtv(&a0, &by, &m) == 1);
    CHECK(fabsf(m.y - (-2.0f)) < 1e-4f && fabsf(m.x) < 1e-4f);
    CHECK(sat_mtv(&a0, &far, &m) == 0);
    SatPoly corner = box(5, 5, 10, 10); /* 角点对角侵入，两轴等深 */
    CHECK(sat_mtv(&a0, &corner, &m) == 1);
    CHECK(fabsf(vmag(m) - 5.0f) < 1e-4f);
    CHECK(m.x <= 0.0f && m.y <= 0.0f && (m.x < 0.0f || m.y < 0.0f));
    CHECK(!points_wrong_way(&a0, &corner, m));

    SeqStat seqs[4];

    /* 序列 1：对角直推，侵入从 y 轴主导过渡到 x 轴主导，
       叠加位置噪声使原始 MTV 在交叉点附近来回翻轴（90 度方向跳变） */
    {
        SatPushState st; sat_push_init(&st);
        SeqStat* s = &seqs[0];
        *s = (SeqStat){.name = "S1 diagonal axis-flip + noise"};
        rng = 42;
        SatPoly a = box(-5, -5, 10, 10);
        for (int t = 0; t <= 45; ++t) {
            SatPoly b = box(10.0f - 0.25f * t + jitter(0.08f),
                            20.0f - 0.55f * t + jitter(0.08f), 10, 10);
            SatVec raw, push;
            int hit = sat_mtv(&a, &b, &raw);
            push = sat_push_update(&st, &a, &b, &cfg);
            stat_record(s, &a, &b, hit, raw, push);
        }
        stat_print(s);
        CHECK(s->contacts >= 15);
        CHECK(s->missed == 0);
        CHECK(s->wrong == 0);
        CHECK(s->raw_max_dang > 0.5f);             /* 原始方向确实发生了近 90 度翻轴 */
        CHECK(s->sm_max_dang <= cfg.max_ang_delta + 1e-5f);
        CHECK(s->sm_max_dmag <= cfg.max_mag_delta + 1e-5f);
        CHECK(fabsf(s->last_sm_mag - s->last_raw_mag) < 0.05f); /* 深度无稳态残差 */
        CHECK(s->last_ang_diff < 0.1f);            /* 方向最终收敛回真实方向 */
    }

    /* 序列 2：两个五边形差速自旋、中心浅侵入且基本不分离，
       原始 MTV 在相邻边法线间离散跳变（约 36 度一档），
       检验方向变化率上限下的平稳跟踪与最终收敛 */
    {
        SatPushState st; sat_push_init(&st);
        SeqStat* s = &seqs[1];
        *s = (SeqStat){.name = "S2 spinning pentagons, axis switching"};
        for (int t = 0; t < 64; ++t) {
            float u = t / 63.0f;
            float dx = 4.2f - 0.7f * u;
            SatPoly a = ngon(-dx / 2, 0.0f, 2.5f, 5, 0.11f * t);
            SatPoly b = ngon( dx / 2, 0.0f, 2.5f, 5, 3.14159265f - 0.07f * t);
            SatVec raw, push;
            int hit = sat_mtv(&a, &b, &raw);
            push = sat_push_update(&st, &a, &b, &cfg);
            stat_record(s, &a, &b, hit, raw, push);
        }
        stat_print(s);
        CHECK(s->contacts >= 55);
        CHECK(s->missed == 0);
        CHECK(s->wrong == 0);
        CHECK(s->raw_max_dang > 0.35f);            /* 原始方向存在离散轴跳 */
        CHECK(s->sm_max_dang <= cfg.max_ang_delta + 1e-5f);
        CHECK(s->sm_max_dmag <= cfg.max_mag_delta + 1e-5f);
        CHECK(s->sum_ang_diff / s->contacts < 0.12f); /* 平均跟踪误差小，不是被甩开 */
        CHECK(s->last_ang_diff < 0.15f);
    }

    /* 序列 3：持续侵入 + 大幅独立噪声，原始分离量逐帧乱跳，
       检查分离量平稳且每一帧接触都有推力 */
    {
        SatPushState st; sat_push_init(&st);
        SeqStat* s = &seqs[2];
        *s = (SeqStat){.name = "S3 noisy penetration depth"};
        rng = 99;
        SatPoly a = box(0, 0, 8, 8);
        for (int t = 0; t < 40; ++t) {
            /* 沿 x 轴侵入，噪声只加在侵入方向：轴稳定（始终 ±x），深度乱跳 */
            SatPoly b = box(6.0f - 0.08f * t + jitter(0.5f), 2.0f, 8, 8);
            SatVec raw, push;
            int hit = sat_mtv(&a, &b, &raw);
            push = sat_push_update(&st, &a, &b, &cfg);
            stat_record(s, &a, &b, hit, raw, push);
        }
        stat_print(s);
        CHECK(s->contacts >= 30);
        CHECK(s->missed == 0);
        CHECK(s->wrong == 0);
        CHECK(s->sm_min_mag > 0.0f);
        CHECK(s->sm_max_dmag <= cfg.max_mag_delta + 1e-5f);
        CHECK(s->sm_max_dang <= cfg.max_ang_delta + 1e-5f);
        CHECK(s->sm_max_dmag < 0.65f * s->raw_max_dmag); /* 抖动显著收敛 */
        CHECK(fabsf(s->last_sm_mag - s->last_raw_mag) < cfg.max_mag_delta + 0.05f);
    }

    /* 序列 4：断续接触。短间隙(<=hold)保留状态不产生再初始化跳变；
       长间隙(>hold)后复位，重新接触首帧仍必须立刻给出完整推力 */
    {
        SatPushState st; sat_push_init(&st);
        SeqStat* s = &seqs[3];
        *s = (SeqStat){.name = "S4 intermittent contact"};
        SatPoly a = box(0, 0, 10, 10);
        for (int t = 0; t < 32; ++t) {
            float bxp;
            if (t <= 9)       bxp = 9.2f - 0.2f * t;  /* 靠近并侵入 */
            else if (t <= 13) bxp = 12.0f;            /* 4 帧长间隙 -> 复位 */
            else if (t <= 19) bxp = 7.5f;             /* 重新接触，首帧即满推 */
            else if (t <= 21) bxp = 11.0f;            /* 2 帧短间隙 -> 保持 */
            else if (t <= 27) bxp = 8.5f;             /* 再接触，状态连续 */
            else              bxp = 12.0f;            /* 间隙收尾 */
            SatPoly b = box(bxp, 4.0f, 10, 10);
            SatVec raw, push;
            int hit = sat_mtv(&a, &b, &raw);
            push = sat_push_update(&st, &a, &b, &cfg);
            stat_record(s, &a, &b, hit, raw, push);
            if (t >= 10 && t <= 13) CHECK(vmag(push) == 0.0f); /* 间隙帧不推 */
            if (t == 14 || t == 22) CHECK(vmag(push) > 0.0f);  /* 再接触不漏推 */
        }
        stat_print(s);
        CHECK(s->contacts >= 18);
        CHECK(s->missed == 0);
        CHECK(s->wrong == 0);
        CHECK(s->sm_max_dmag <= cfg.max_mag_delta + 1e-5f);
        CHECK(s->sm_max_dang <= cfg.max_ang_delta + 1e-5f);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
