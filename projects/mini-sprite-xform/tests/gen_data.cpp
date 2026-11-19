#include "gen_data.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

// Generates the committed CSV fixtures under data/: one file per scenario.
// Deterministic: same (n, seed) always reproduces byte-identical rows, and
// each file embeds an FNV-1a checksum that the test verifies after parsing.
int main(int argc, char** argv) {
    int n = (argc > 1) ? std::atoi(argv[1]) : 20000;
    std::uint64_t seed = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 20261120ULL;
    if (n <= 0) {
        std::printf("invalid n: %d\n", n);
        return 2;
    }
    mkdir("data", 0755);  // ignore EEXIST; writes below would fail anyway

    int rc = 0;
    for (int id = 0; id < cull_gen::SCENARIO_COUNT; ++id) {
        cull_gen::Scenario s = cull_gen::generate_scenario(id, n, seed);
        std::string path = std::string("data/") + s.name + ".csv";
        if (!cull_gen::write_scenario_csv(path, s, seed)) {
            std::printf("failed to write %s\n", path.c_str());
            rc = 1;
            continue;
        }
        std::printf("wrote %s: n=%d checksum=%08x\n", path.c_str(),
                    static_cast<int>(s.boxes.size()), cull_gen::checksum(s));
    }
    return rc;
}
