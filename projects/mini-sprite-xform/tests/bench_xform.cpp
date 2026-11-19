/* 批量 vs 逐次变换基准。
 * 两种批内源数据布局：
 *   contiguous —— 各精灵顶点在一个连续大缓冲中（每 4 个点一段）
 *   scattered  —— 各精灵顶点以 256 字节步进错开，模拟描述符分散的真实场景
 * 输出缓冲两种路径完全相同（同一块扁平 dst / bounds），保证对比公平。
 */
#include "../src/xform.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static uint32_t lcg_state = 0x9e3779b9u;
static float rand_float(float lo, float hi) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    float t = (lcg_state >> 8) / float(1u << 24);
    return lo + t * (hi - lo);
}
static uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

static int NVERT = 4;   /* 每精灵顶点数，可由 argv[1] 覆盖 */

struct Layout {
    std::vector<SpritePoint> mem;
    std::vector<XformSpriteJob> jobs;
    int stride;
};

static Layout make_layout(int count, bool scattered) {
    const int n = NVERT;
    const int stride = scattered ? 64 : n;   /* 64 个 SpritePoint = 256 字节 */
    Layout L;
    L.stride = stride;
    L.mem.resize((size_t)count * stride);
    L.jobs.resize(count);
    for (int j = 0; j < count; ++j) {
        SpritePoint* s = L.mem.data() + (size_t)j * stride;
        for (int i = 0; i < n; ++i) {
            s[i].x = rand_float(-100.0f, 100.0f);
            s[i].y = rand_float(-100.0f, 100.0f);
        }
        L.jobs[j] = {s, n, rand_float(-4.0f, 4.0f), rand_float(-4.0f, 4.0f),
                        rand_float(-50.0f, 50.0f), rand_float(-50.0f, 50.0f)};
    }
    return L;
}

static uint64_t checksum(const SpritePoint* p, int n, const SpriteBounds* b, int m) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t v) { h ^= v; h *= 1099511628211ull; };
    for (int i = 0; i < n; ++i) { mix(bits_of(p[i].x)); mix(bits_of(p[i].y)); }
    for (int i = 0; i < m; ++i) {
        mix(bits_of(b[i].minx)); mix(bits_of(b[i].miny));
        mix(bits_of(b[i].maxx)); mix(bits_of(b[i].maxy));
    }
    return h;
}

using clock_ = std::chrono::steady_clock;
static double median_ms(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main(int argc, char** argv) {
    if (argc > 1) NVERT = std::atoi(argv[1]);
    const int TRIALS = 7, REPS = 200;
    const int counts[2] = {10000, 100000};

    std::printf("# verts/sprite = %d\n", NVERT);
    std::printf("%-10s %-10s %22s %12s %12s %10s\n",
                "sprites", "layout", "mode", "ms/pass", "ns/sprite", "speedup");
    for (int count : counts) {
        for (int sc = 0; sc < 2; ++sc) {
            bool scattered = sc == 1;
            Layout L = make_layout(count, scattered);
            const int total = count * NVERT;
            std::vector<SpritePoint> dst(total);
            std::vector<SpriteBounds> bd(count);

            /* 正确性：批量结果必须与逐次路径逐位一致 */
            std::vector<SpritePoint> ref(total);
            std::vector<SpriteBounds> refbd(count);
            xform_batch(L.jobs.data(), count, dst.data(), total, bd.data(), count);
            int off = 0;
            for (int j = 0; j < count; ++j) {
                xform_sprite(L.jobs[j].src, NVERT, L.jobs[j].sx, L.jobs[j].sy,
                             L.jobs[j].tx, L.jobs[j].ty, ref.data() + off);
                xform_bounds(ref.data() + off, NVERT, &refbd[j].minx, &refbd[j].miny,
                             &refbd[j].maxx, &refbd[j].maxy);
                off += NVERT;
            }
            if (std::memcmp(dst.data(), ref.data(), total * sizeof(SpritePoint)) != 0 ||
                std::memcmp(bd.data(), refbd.data(), count * sizeof(SpriteBounds)) != 0) {
                std::printf("BIT MISMATCH at count=%d scattered=%d\n", count, scattered);
                return 1;
            }

            auto run = [&](int mode, uint64_t& cs) {
                std::vector<double> ms;
                for (int t = 0; t < TRIALS; ++t) {
                    auto t0 = clock_::now();
                    for (int r = 0; r < REPS; ++r) {
                        if (mode == 0) {                 /* 逐次：仅变换 */
                            int o = 0;
                            for (int j = 0; j < count; ++j) {
                                xform_sprite(L.jobs[j].src, NVERT, L.jobs[j].sx, L.jobs[j].sy,
                                             L.jobs[j].tx, L.jobs[j].ty, dst.data() + o);
                                o += NVERT;
                            }
                        } else if (mode == 1) {          /* 逐次：变换+包围盒 */
                            int o = 0;
                            for (int j = 0; j < count; ++j) {
                                xform_sprite(L.jobs[j].src, NVERT, L.jobs[j].sx, L.jobs[j].sy,
                                             L.jobs[j].tx, L.jobs[j].ty, dst.data() + o);
                                xform_bounds(dst.data() + o, NVERT, &bd[j].minx, &bd[j].miny,
                                             &bd[j].maxx, &bd[j].maxy);
                                o += NVERT;
                            }
                        } else {                         /* 批量：变换+包围盒 */
                            xform_batch(L.jobs.data(), count, dst.data(), total, bd.data(), count);
                        }
                    }
                    auto t1 = clock_::now();
                    ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / REPS);
                }
                cs = checksum(dst.data(), total, bd.data(), count);
                return median_ms(ms);
            };

            uint64_t c0, c1, c2;
            double m0 = run(0, c0);
            double m1 = run(1, c1);
            double m2 = run(2, c2);
            (void)c0; (void)c1; (void)c2;
            const char* layout = scattered ? "scattered" : "contig";
            std::printf("%-10d %-10s %22s %12.4f %12.3f %10s\n",
                        count, layout, "sequential xform", m0, m0 * 1e6 / count, "-");
            std::printf("%-10d %-10s %22s %12.4f %12.3f %10s\n",
                        count, layout, "sequential xform+bounds", m1, m1 * 1e6 / count, "1.00x");
            std::printf("%-10d %-10s %22s %12.4f %12.3f %9.2fx\n",
                        count, layout, "batch xform+bounds", m2, m2 * 1e6 / count, m1 / m2);
        }
    }
    return 0;
}
