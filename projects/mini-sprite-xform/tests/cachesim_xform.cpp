/* 缓存行为模拟：量化批内两种源数据布局对缓存的影响。
 * 缓存模型：组相联、LRU、行 64B；容量/相联度按 i7-13620H 量级设置。
 * 只模拟源顶点读取（dst/bounds/jobs 在两种布局下都是顺序访问，不产生差异）。
 * 访问序列与 bench_xform.cpp 的实际处理顺序一致：作业 j 的 4 个点依次读。
 */
#include "../src/xform.h"
#include <cstdint>
#include <cstdio>
#include <vector>

struct Cache {
    int sets, ways, line;
    std::vector<uint64_t> tags;   /* 每组 ways 个，tags[set*ways+k]，LRU 队首 */
    long hits = 0, misses = 0;
    Cache(int kb, int ways_, int line_ = 64)
        : sets(kb * 1024 / (ways_ * line_)), ways(ways_), line(line_),
          tags((size_t)sets * ways_, UINT64_MAX) {}
    /* 返回命中的层级编号：0=本层命中，1=缺失（需下一层/内存） */
    int access(uint64_t addr) {
        uint64_t tag = addr / line;
        int set = (int)((addr / line) % (uint64_t)sets);
        auto base = tags.begin() + (size_t)set * ways;
        for (int k = 0; k < ways; ++k) {
            if (base[k] == tag) {                 /* 命中：移到 LRU 队尾 */
                uint64_t h = base[k];
                for (int t = k; t < ways - 1; ++t) base[t] = base[t + 1];
                base[ways - 1] = h;
                ++hits;
                return 0;
            }
        }
        for (int t = 0; t < ways - 1; ++t) base[t] = base[t + 1];  /* 缺失：换出队首 */
        base[ways - 1] = tag;
        ++misses;
        return 1;
    }
};

static void simulate(int count, bool scattered) {
    const int n = 4, stride_pts = scattered ? 64 : n;
    /* 与 bench 相同的地址关系（绝对地址不影响模型，只看跨度） */
    std::vector<SpritePoint> mem((size_t)count * stride_pts);
    std::vector<XformSpriteJob> jobs(count);
    for (int j = 0; j < count; ++j)
        jobs[j] = {mem.data() + (size_t)j * stride_pts, n, 1, 1, 0, 0};

    Cache l1(48, 12), l2(1280, 16), l3(24 * 1024, 16);
    long l1m = 0, l2m = 0, l3m = 0, accesses = 0;
    for (int j = 0; j < count; ++j) {
        const char* base = reinterpret_cast<const char*>(jobs[j].src);
        for (int i = 0; i < n; ++i) {
            /* 每个点 8 字节，x、y 同在一个 64B 行内 -> 一次行访问 */
            uint64_t addr = (uint64_t)(base + (size_t)i * sizeof(SpritePoint));
            ++accesses;
            if (l1.access(addr)) {
                ++l1m;
                if (l2.access(addr)) {
                    ++l2m;
                    if (l3.access(addr)) ++l3m;
                }
            }
        }
    }
    auto pct = [](long part, long whole) { return 100.0 * part / whole; };
    std::printf("%-6d %-10s  L1 miss %6.2f%%  L2 miss(占L1缺失) %6.2f%%  "
                "L3 miss(占L2缺失) %6.2f%%  估算内存行请求 %ld\n",
                count, scattered ? "scattered" : "contig",
                pct(l1m, accesses), pct(l2m, l1m), pct(l3m, l2m), l3m);
}

int main() {
    std::printf("缓存模型: L1 48KB/12路, L2 1.25MB/16路, L3 24MB/16路, 行64B\n");
    simulate(10000, false);
    simulate(10000, true);
    simulate(100000, false);
    simulate(100000, true);
    return 0;
}
