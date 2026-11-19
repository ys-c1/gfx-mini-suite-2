#include "../src/cull.h"
#include "../src/xform.h"
#include "gen_data.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const Viewport VP = {0.0f, 0.0f, 100.0f, 100.0f};

static int kept(const SpriteBox& b, const Viewport& vp = VP) {
    return cull_box_intersects_viewport(&b, &vp);
}

static void test_basic_intersection() {
    // Fully inside.
    CHECK(kept(SpriteBox{10, 10, 20, 20}) == 1);
    // Straddling edges.
    CHECK(kept(SpriteBox{-10, -10, 10, 10}) == 1);
    CHECK(kept(SpriteBox{90, 90, 110, 110}) == 1);
    // Fully outside, each axis/direction.
    CHECK(kept(SpriteBox{-20, 10, -10, 20}) == 0);   // left
    CHECK(kept(SpriteBox{110, 10, 120, 20}) == 0);   // right
    CHECK(kept(SpriteBox{10, -20, 20, -10}) == 0);   // above
    CHECK(kept(SpriteBox{10, 110, 20, 120}) == 0);   // below
    CHECK(kept(SpriteBox{-20, -20, -10, -10}) == 0); // diagonal corner gap
}

static void test_edge_touching_kept() {
    // Requirement: anything touching the edge must survive.
    CHECK(kept(SpriteBox{-10, 40, 0.0f, 60}) == 1);    // left edge, vertical span
    CHECK(kept(SpriteBox{100.0f, 40, 110, 60}) == 1);  // right edge
    CHECK(kept(SpriteBox{40, -10, 60, 0.0f}) == 1);    // top edge
    CHECK(kept(SpriteBox{40, 100.0f, 60, 110}) == 1);  // bottom edge
    CHECK(kept(SpriteBox{0.0f, 0.0f, 0.0f, 0.0f}) == 1);      // corner point
    CHECK(kept(SpriteBox{100.0f, 100.0f, 100.0f, 100.0f}) == 1);
    // A strict gap (larger than one float ulp at this magnitude) still culls.
    CHECK(kept(SpriteBox{-10, 40, -0.000001f, 60}) == 0);
    CHECK(kept(SpriteBox{100.0001f, 40, 110, 60}) == 0);
}

static void test_zero_size_boxes() {
    // Zero-area point and line sprites use the same closed-interval rule.
    CHECK(kept(SpriteBox{50, 50, 50, 50}) == 1);       // point inside
    CHECK(kept(SpriteBox{0, 50, 0, 50}) == 1);         // point on left edge
    CHECK(kept(SpriteBox{50, 0, 50, 0}) == 1);         // point on top edge
    CHECK(kept(SpriteBox{-1, 50, -1, 50}) == 0);       // point outside
    CHECK(kept(SpriteBox{10, 50, 90, 50}) == 1);       // horizontal line inside
    CHECK(kept(SpriteBox{10, 50, 90, 50.0f}) == 1);
    CHECK(kept(SpriteBox{-30, 50, -10, 50}) == 0);     // horizontal line outside
    CHECK(kept(SpriteBox{50, 10, 50, 90}) == 1);       // vertical line inside
    CHECK(kept(SpriteBox{50, 110, 50, 130}) == 0);     // vertical line outside
}

static void test_empty_viewport_culls_all() {
    const float nan = std::nanf("");
    SpriteBox inside{10, 10, 20, 20};
    SpriteBox point{50, 50, 50, 50};
    int idx[4] = {-1, -1, -1, -1};

    // Zero width / zero height.
    Viewport zw{0, 0, 0, 100};
    CHECK(cull_sprites(&inside, 1, &zw, idx, nullptr) == 0);
    Viewport zh{0, 0, 100, 0};
    CHECK(cull_sprites(&inside, 1, &zh, idx, nullptr) == 0);
    // Inverted viewport.
    Viewport inv{100, 100, 0, 0};
    CHECK(cull_sprites(&inside, 1, &inv, idx, nullptr) == 0);
    // NaN anywhere in the viewport.
    Viewport nv[4] = {{nan, 0, 100, 100}, {0, nan, 100, 100},
                      {0, 0, nan, 100}, {0, 0, 100, nan}};
    for (const auto& v : nv) CHECK(cull_sprites(&point, 1, &v, idx, nullptr) == 0);
    // Empty viewport with n == 0 is still 0 kept, deterministically.
    CHECK(cull_sprites(nullptr, 0, &zw, nullptr, nullptr) == 0);
}

static void test_all_outside() {
    std::vector<SpriteBox> boxes;
    for (int i = 0; i < 100; ++i) {
        float o = 200.0f + static_cast<float>(i);
        boxes.push_back(SpriteBox{o, o, o + 5, o + 5});
    }
    std::vector<int> kept_idx(boxes.size()), culled_idx(boxes.size());
    int k = cull_sprites(boxes.data(), static_cast<int>(boxes.size()), &VP,
                         kept_idx.data(), culled_idx.data());
    CHECK(k == 0);
    // Every original index must be reported as culled, in order.
    for (int i = 0; i < 100; ++i) CHECK(culled_idx[i] == i);
}

static void test_conservative_malformed_inputs() {
    const float nan = std::nanf("");
    // Inverted boxes and NaN-bearing boxes are kept (never silently dropped).
    CHECK(kept(SpriteBox{20, 20, 10, 10}) == 1);
    CHECK(kept(SpriteBox{nan, 10, 20, 20}) == 1);
    CHECK(kept(SpriteBox{10, nan, 20, 20}) == 1);
    CHECK(kept(SpriteBox{10, 10, nan, 20}) == 1);
    CHECK(kept(SpriteBox{10, 10, 20, nan}) == 1);
    // Null safety.
    CHECK(cull_box_intersects_viewport(nullptr, &VP) == 0);
    Viewport vp = VP;
    CHECK(cull_box_intersects_viewport(nullptr, nullptr) == 0);
    CHECK(cull_sprites(nullptr, 4, &vp, nullptr, nullptr) == -1);
    CHECK(cull_sprites(reinterpret_cast<const SpriteBox*>(&vp), 4, nullptr, nullptr, nullptr) == -1);
    CHECK(cull_sprites(reinterpret_cast<const SpriteBox*>(&vp), -1, &vp, nullptr, nullptr) == -1);
}

static void test_batch_indices_and_partition() {
    // Mixed scene; verify counts, both index lists, and that they partition
    // the full original index range exactly once.
    std::vector<SpriteBox> boxes = {
        SpriteBox{10, 10, 20, 20},       // 0 kept
        SpriteBox{-30, -30, -20, -20},   // 1 culled
        SpriteBox{0, 40, 0, 60},         // 2 kept (edge point line)
        SpriteBox{110, 10, 120, 20},     // 3 culled
        SpriteBox{90, 90, 110, 110},     // 4 kept (straddle)
        SpriteBox{50, 50, 50, 50},       // 5 kept (point inside)
        SpriteBox{50, 200, 50, 210},     // 6 culled
    };
    const int n = static_cast<int>(boxes.size());
    std::vector<int> ki(n, -1), ci(n, -1);
    int k = cull_sprites(boxes.data(), n, &VP, ki.data(), ci.data());
    CHECK(k == 4);
    int expect_kept[4] = {0, 2, 4, 5};
    int expect_culled[3] = {1, 3, 6};
    for (int i = 0; i < k; ++i) CHECK(ki[i] == expect_kept[i]);
    for (int i = 0; i < n - k; ++i) CHECK(ci[i] == expect_culled[i]);
    // Ascending order and a strict partition of [0, n).
    std::vector<int> seen(n, 0);
    for (int i = 0; i < k; ++i) {
        CHECK(ki[i] >= 0 && ki[i] < n && seen[ki[i]] == 0);
        seen[ki[i]] = 1;
        if (i) CHECK(ki[i] > ki[i - 1]);
    }
    for (int i = 0; i < n - k; ++i) {
        CHECK(ci[i] >= 0 && ci[i] < n && seen[ci[i]] == 0);
        seen[ci[i]] = 1;
        if (i) CHECK(ci[i] > ci[i - 1]);
    }
    for (int i = 0; i < n; ++i) CHECK(seen[i] == 1);
    // Null output pointers must not crash and must still return the count.
    CHECK(cull_sprites(boxes.data(), n, &VP, nullptr, nullptr) == 4);
    // n == 0: deterministic zero.
    CHECK(cull_sprites(boxes.data(), 0, &VP, ki.data(), ci.data()) == 0);
}

static void test_reference_cross_check() {
    // Property test against the structurally independent reference,
    // including degenerate boxes, malformed boxes and NaN coordinates.
    cull_gen::Rng rng(0xC0FFEEULL);
    const float nan = std::nanf("");
    for (int iter = 0; iter < 20000; ++iter) {
        Viewport vp{rng.uniform(-50, 50), rng.uniform(-50, 50), 0, 0};
        vp.maxx = vp.minx + rng.uniform(-5, 120);
        vp.maxy = vp.miny + rng.uniform(-5, 120);
        if ((iter % 97) == 0) {  // sprinkle NaN viewports
            float* p = &vp.minx;
            p[iter % 4] = nan;
        }
        SpriteBox b{rng.uniform(-120, 120), rng.uniform(-120, 120), 0, 0};
        b.maxx = b.minx + rng.uniform(-2, 20);
        b.maxy = b.miny + rng.uniform(-2, 20);
        if ((iter % 13) == 0) b.maxx = b.minx;               // zero width
        if ((iter % 17) == 0) b.maxy = b.miny;               // zero height
        if ((iter % 53) == 0) b.maxx = b.minx - 3.0f;        // inverted
        if ((iter % 71) == 0) (&b.minx)[iter % 4] = nan;     // NaN coordinate
        bool ref = cull_gen::reference_kept(b, vp);
        bool got = cull_box_intersects_viewport(&b, &vp) == 1;
        CHECK(ref == got);
    }
}

static void test_xform_integration_unchanged() {
    // End-to-end: transform -> bounds -> cull. Also proves the existing
    // transform/bounds outputs are untouched.
    SpritePoint src[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    SpritePoint dst[4];
    CHECK(xform_sprite(src, 4, 2.0f, 3.0f, 1.0f, -1.0f, dst) == 0);
    float minx, miny, maxx, maxy;
    CHECK(xform_bounds(dst, 4, &minx, &miny, &maxx, &maxy) == 0);
    CHECK(minx == 1.0f && miny == -1.0f && maxx == 5.0f && maxy == 2.0f);
    SpriteBox box{minx, miny, maxx, maxy};
    Viewport vp{0, 0, 4, 4};
    CHECK(cull_box_intersects_viewport(&box, &vp) == 1);   // overlaps
    Viewport far_vp{100, 100, 200, 200};
    CHECK(cull_box_intersects_viewport(&box, &far_vp) == 0);
}

static void test_scenarios_from_csv() {
    const int n = 20000;
    const std::uint64_t seed = 20261120ULL;
    int total_false_cull = 0, total_false_keep = 0, total = 0;
    std::printf("\nscenario report (n=%d per scenario, seed=%llu)\n", n,
                static_cast<unsigned long long>(seed));
    std::printf("%-15s %10s %10s %12s %12s %12s\n", "scenario", "kept",
                "culled", "keep_rate", "false_cull", "false_keep");

    for (int id = 0; id < cull_gen::SCENARIO_COUNT; ++id) {
        cull_gen::Scenario generated = cull_gen::generate_scenario(id, n, seed);
        cull_gen::Scenario from_file;
        std::string path = std::string("data/") + generated.name + ".csv";
        bool have_file = cull_gen::read_scenario_csv(path, from_file);
        const cull_gen::Scenario& s = have_file ? from_file : generated;
        if (!have_file)
            std::printf("note: %s not on disk, validated against in-memory generator\n",
                        path.c_str());
        CHECK(have_file);
        CHECK(static_cast<int>(s.boxes.size()) == n);
        if (have_file) CHECK(cull_gen::checksum(s) == cull_gen::checksum(generated));

        std::vector<int> ki(n), ci(n);
        int k = cull_sprites(s.boxes.data(), n, &s.vp, ki.data(), ci.data());
        CHECK(k >= 0 && k <= n);

        // Every verdict must match the independent reference.
        int false_cull = 0, false_keep = 0;
        std::vector<char> ref_kept(n);
        for (int i = 0; i < n; ++i) {
            ref_kept[i] = cull_gen::reference_kept(s.boxes[i], s.vp) ? 1 : 0;
            bool got = false;
            // Locate i in the returned partitions.
            for (int j = 0; j < k; ++j) if (ki[j] == i) got = true;
            if (ref_kept[i] && !got) ++false_cull;
            if (!ref_kept[i] && got) ++false_keep;
        }
        CHECK(false_cull == 0);
        CHECK(false_keep == 0);

        double rate = 100.0 * static_cast<double>(k) / n;
        std::printf("%-15s %10d %10d %11.2f%% %12d %12d\n", s.name.c_str(), k,
                    n - k, rate, false_cull, false_keep);
        total_false_cull += false_cull;
        total_false_keep += false_keep;
        total += n;
    }
    std::printf("totals: %d sprites, culling error = %d (false_cull=%d false_keep=%d)\n",
                total, total_false_cull + total_false_keep, total_false_cull,
                total_false_keep);
    CHECK(total_false_cull + total_false_keep == 0);
}

int main() {
    test_basic_intersection();
    test_edge_touching_kept();
    test_zero_size_boxes();
    test_empty_viewport_culls_all();
    test_all_outside();
    test_conservative_malformed_inputs();
    test_batch_indices_and_partition();
    test_reference_cross_check();
    test_xform_integration_unchanged();
    test_scenarios_from_csv();
    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
