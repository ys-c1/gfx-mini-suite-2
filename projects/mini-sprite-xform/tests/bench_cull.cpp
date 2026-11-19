#include "../src/cull.h"
#include "gen_data.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using clock_ty = std::chrono::steady_clock;

static double ms_since(clock_ty::time_point t0) {
    return std::chrono::duration<double, std::milli>(clock_ty::now() - t0).count();
}

int main(int argc, char** argv) {
    int n = 1000000;
    int repeats = 20;
    if (argc > 1) n = std::atoi(argv[1]);
    if (argc > 2) repeats = std::atoi(argv[2]);
    if (n <= 0 || repeats <= 0) {
        std::printf("usage: %s [n] [repeats]\n", argv[0]);
        return 2;
    }
    const std::uint64_t seed = 20261120ULL;

    std::vector<int> kept_idx(n), culled_idx(n);
    std::printf("benchmark: %d sprites, %d repeats, seed=%llu\n", n, repeats,
                static_cast<unsigned long long>(seed));
    std::printf("%-15s %8s %10s %10s %10s %10s %12s\n", "scenario", "kept%",
                "min(ms)", "med(ms)", "max(ms)", "mean(ms)", "ns/sprite");

    int worst_max_ms = 0;
    double worst_ns = 0.0;

    for (int id = 0; id < cull_gen::SCENARIO_COUNT; ++id) {
        cull_gen::Scenario s = cull_gen::generate_scenario(id, n, seed);
        std::vector<double> times;
        times.reserve(repeats);
        int k = 0;

        for (int r = 0; r < repeats; ++r) {
            // Reset outputs so a broken implementation cannot leave stale data.
            std::fill(kept_idx.begin(), kept_idx.end(), -1);
            std::fill(culled_idx.begin(), culled_idx.end(), -1);
            auto t0 = clock_ty::now();
            k = cull_sprites(s.boxes.data(), n, &s.vp, kept_idx.data(),
                             culled_idx.data());
            double ms = ms_since(t0);
            times.push_back(ms);
            // Keep the optimizer honest: touch a result from every run.
            if (kept_idx[0] == 0x7fffffff) std::printf("impossible\n");
        }
        std::sort(times.begin(), times.end());
        double sum = 0;
        for (double t : times) sum += t;
        double mean = sum / repeats;
        double mn = times.front(), med = times[repeats / 2], mx = times.back();
        double ns_per_sprite = mean * 1e6 / static_cast<double>(n);
        std::printf("%-15s %7.2f%% %10.4f %10.4f %10.4f %10.4f %12.3f\n",
                    s.name.c_str(), 100.0 * k / n, mn, med, mx, mean,
                    ns_per_sprite);
        if (mx > worst_max_ms) {
            worst_max_ms = static_cast<int>(mx + 0.9999);
            worst_ns = ns_per_sprite;
        }
    }

    // Worst observed single-run time across all scenarios, rounded up: this
    // is the empirically supported upper bound on this machine.
    std::printf("\nupper bound (worst observed run): %d ms for %d sprites "
                "(%.3f ns/sprite); algorithmic bound: O(n) time, O(1) auxiliary space\n",
                worst_max_ms, n, worst_ns);
    return 0;
}
