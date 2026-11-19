#ifndef GEN_DATA_H
#define GEN_DATA_H

// Shared deterministic scenario data for culling tests, benchmarks and the
// CSV data generator. Header-only so every target draws from exactly one
// implementation; outputs are reproducible from (scenario id, n, seed).

#include "../src/cull.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace cull_gen {

static constexpr int SCENARIO_COUNT = 4;

struct Scenario {
    int id;
    std::string name;
    Viewport vp;
    std::vector<SpriteBox> boxes;
};

// Deterministic LCG (Numerical Recipes constants); deliberately not rand(),
// whose implementation differs between libc builds.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint32_t next_u32() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 32);
    }
    // Uniform float in [lo, hi].
    float uniform(float lo, float hi) {
        float u = static_cast<float>(next_u32()) / 4294967295.0f;
        return lo + u * (hi - lo);
    }
};

inline const char* scenario_name(int id) {
    static const char* names[SCENARIO_COUNT] = {
        "dense_inside", "scattered", "edge_boundary", "all_outside"};
    return names[id & 3];
}

inline Scenario generate_scenario(int id, int n, std::uint64_t seed) {
    Scenario s;
    s.id = id & 3;
    s.name = scenario_name(s.id);
    // Same viewport for every scenario so the data sets differ only in the
    // spatial distribution of the boxes.
    s.vp = Viewport{0.0f, 0.0f, 100.0f, 100.0f};
    s.boxes.resize(n);
    Rng rng(seed ^ (0x1234567ULL * static_cast<std::uint64_t>(s.id + 1)));

    for (int i = 0; i < n; ++i) {
        float w = rng.uniform(0.5f, 8.0f);
        float h = rng.uniform(0.5f, 8.0f);
        switch (s.id) {
        case 0: {  // dense_inside: ~98% fully inside, 2% fully outside
            if ((i % 50) == 0) {
                float x = rng.uniform(150.0f, 400.0f);
                float y = rng.uniform(150.0f, 400.0f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            } else {
                float x = rng.uniform(0.0f, 100.0f - w);
                float y = rng.uniform(0.0f, 100.0f - h);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            }
            break;
        }
        case 1: {  // scattered: uniform over a much larger field
            float x = rng.uniform(-150.0f, 250.0f);
            float y = rng.uniform(-150.0f, 250.0f);
            s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            break;
        }
        case 2: {  // edge_boundary: touching, straddling, zero-size, outside
            int kind = i % 8;
            float t = rng.uniform(0.0f, 40.0f);
            switch (kind) {
            case 0: s.boxes[i] = SpriteBox{-w, t, 0.0f, t + h}; break;          // touch left edge
            case 1: s.boxes[i] = SpriteBox{100.0f, t, 100.0f + w, t + h}; break; // touch right edge
            case 2: s.boxes[i] = SpriteBox{t, -h, t + w, 0.0f}; break;           // touch top edge
            case 3: s.boxes[i] = SpriteBox{t, 100.0f, t + w, 100.0f + h}; break; // touch bottom edge
            case 4: s.boxes[i] = SpriteBox{0.0f, 0.0f, 0.0f, 0.0f}; break;       // point on corner
            case 5: s.boxes[i] = SpriteBox{50.0f - w, 50.0f, 50.0f + w, 50.0f}; break; // zero-height line inside
            case 6: s.boxes[i] = SpriteBox{-20.0f - w, t, -20.0f, t + h}; break; // fully outside
            default: {                                                           // straddling an edge
                float x = rng.uniform(-w * 0.5f, 100.0f - w * 0.5f);
                float y = rng.uniform(-h * 0.5f, 100.0f - h * 0.5f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
                break;
            }
            }
            break;
        }
        default: {  // all_outside: every box separated on at least one axis
            int side = i % 4;
            if (side == 0) {
                float x = rng.uniform(-300.0f, -120.0f);
                float y = rng.uniform(-50.0f, 150.0f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            } else if (side == 1) {
                float x = rng.uniform(120.0f, 300.0f);
                float y = rng.uniform(-50.0f, 150.0f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            } else if (side == 2) {
                float x = rng.uniform(-50.0f, 150.0f);
                float y = rng.uniform(-300.0f, -120.0f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            } else {
                float x = rng.uniform(-50.0f, 150.0f);
                float y = rng.uniform(120.0f, 300.0f);
                s.boxes[i] = SpriteBox{x, y, x + w, y + h};
            }
            break;
        }
        }
    }
    return s;
}

// Independent reference predicate: written structurally differently from
// cull_box_intersects_viewport (explicit NaN/empty handling, half-open
// interval reasoning) so agreement is a genuine cross-check, not an echo.
inline bool reference_kept(const SpriteBox& b, const Viewport& vp) {
    auto nan = [](float f) { return std::isnan(f); };
    if (nan(vp.minx) || nan(vp.miny) || nan(vp.maxx) || nan(vp.maxy)) return false;
    if (vp.minx >= vp.maxx || vp.miny >= vp.maxy) return false;          // empty viewport
    if (nan(b.minx) || nan(b.miny) || nan(b.maxx) || nan(b.maxy)) return true;
    if (b.minx > b.maxx || b.miny > b.maxy) return true;                 // malformed box: keep
    // Inside-or-touching iff no axis shows a strict gap.
    bool x_overlap = !(b.maxx < vp.minx) && !(b.minx > vp.maxx);
    bool y_overlap = !(b.maxy < vp.miny) && !(b.miny > vp.maxy);
    return x_overlap && y_overlap;
}

// 32-bit FNV-1a over the raw bytes of every box and the viewport.
inline std::uint32_t checksum(const Scenario& s) {
    std::uint32_t h = 2166136261u;
    auto eat = [&](const void* p, std::size_t len) {
        const auto* bytes = static_cast<const unsigned char*>(p);
        for (std::size_t k = 0; k < len; ++k) {
            h ^= bytes[k];
            h *= 16777619u;
        }
    };
    eat(&s.vp, sizeof(s.vp));
    eat(s.boxes.data(), s.boxes.size() * sizeof(SpriteBox));
    return h;
}

// CSV layout (one row per box, header lines start with '#'):
// # cull scenario data: <name> n=<n> seed=<seed> checksum=<hex>
// # viewport,minx,miny,maxx,maxy
// # viewport,...
// # index,minx,miny,maxx,maxy
inline bool write_scenario_csv(const std::string& path, const Scenario& s,
                               std::uint64_t seed) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# cull scenario data: " << s.name << " n=" << s.boxes.size()
      << " seed=" << seed << " checksum=";
    f.setf(std::ios::hex, std::ios::basefield);
    f.setf(std::ios::uppercase);
    f << checksum(s);
    f.unsetf(std::ios::uppercase);
    f.unsetf(std::ios::hex);
    f << "\n";
    f << "# viewport," << s.vp.minx << ',' << s.vp.miny << ','
      << s.vp.maxx << ',' << s.vp.maxy << '\n';
    f << "index,minx,miny,maxx,maxy\n";
    f.setf(std::ios::scientific);
    f.precision(9);
    for (std::size_t i = 0; i < s.boxes.size(); ++i) {
        const SpriteBox& b = s.boxes[i];
        f << i << ',' << b.minx << ',' << b.miny << ',' << b.maxx << ','
          << b.maxy << '\n';
    }
    return static_cast<bool>(f);
}

inline bool read_scenario_csv(const std::string& path, Scenario& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.boxes.clear();
    std::string line;
    int expected_n = -1;
    std::uint32_t expected_checksum = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("# viewport,", 0) == 0) {
                std::sscanf(line.c_str(), "# viewport,%f,%f,%f,%f",
                            &out.vp.minx, &out.vp.miny, &out.vp.maxx, &out.vp.maxy);
            } else {
                std::size_t p = line.find("n=");
                if (p != std::string::npos) expected_n = std::atoi(line.c_str() + p + 2);
                p = line.find("checksum=");
                if (p != std::string::npos)
                    expected_checksum =
                        static_cast<std::uint32_t>(std::strtoul(line.c_str() + p + 9, nullptr, 16));
                p = line.find("data: ");
                if (p != std::string::npos) {
                    std::size_t begin = p + 6, end = line.find(' ', begin);
                    out.name = line.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                }
            }
            continue;
        }
        if (line.rfind("index,", 0) == 0) continue;
        int idx;
        SpriteBox b;
        if (std::sscanf(line.c_str(), "%d,%f,%f,%f,%f", &idx, &b.minx, &b.miny,
                        &b.maxx, &b.maxy) != 5)
            return false;
        if (idx != static_cast<int>(out.boxes.size())) return false; // rows must be in order
        out.boxes.push_back(b);
    }
    if (expected_n >= 0 && expected_n != static_cast<int>(out.boxes.size())) return false;
    if (expected_checksum != 0 && checksum(out) != expected_checksum) return false;
    return true;
}

}  // namespace cull_gen

#endif
