"""Report: loudness deviation, peak, delay compensation and runtime.

Runs :func:`gain.loudness_gain` on the three synthetic materials and also
sweeps the look-ahead length on the transient material, which is the one that
actually needs it.

    python3 tests/report_normalize.py
"""

import math
import sys
import time

sys.path.insert(0, "src")
sys.path.insert(0, "tests")

import gain
import materials

FS = 48000
TARGET_DB = -23.0


def main():
    target = TARGET_DB
    print("target loudness = %.1f dBFS, fs = %d Hz" % (target, FS))
    print("window = %.0f ms, trim = %.0f dB, slew = %.0f dB/s, "
          "peak attack/release = %.0f/%.0f dB/s"
          % (gain.DEFAULT_WINDOW_MS, gain.DEFAULT_TRIM_DB,
             gain.DEFAULT_SLEW_DB_PER_S, gain.DEFAULT_PEAK_ATTACK_DB_PER_S,
             gain.DEFAULT_PEAK_RELEASE_DB_PER_S))
    print("look-ahead = %.0f ms (%d samples)"
          % (gain.DEFAULT_LOOKAHEAD_MS,
             int(round(gain.DEFAULT_LOOKAHEAD_MS * FS / 1000.0))))
    print()

    header = ("%-14s %8s %8s %10s %8s %7s %7s %9s %9s %8s"
              % ("material", "dur s", "L in", "L out-tgt", "peak out",
                 "clips", "oversh", "rate dB/s", "latency", "time ms"))
    print(header)
    print("-" * len(header))

    for name, x in materials.build().items():
        t0 = time.perf_counter()
        r = gain.loudness_gain(x, target_db=target)
        dt = time.perf_counter() - t0
        print("%-14s %8.2f %8.2f %10.3f %8.3f %7d %7.3f %9.1f %9d %8.1f"
              % (name, len(x) / FS, r.integrated_loudness_in_db,
                 r.loudness_deviation_db, gain.peak_db(r.y),
                 r.ceiling_clips, r.max_ceiling_overshoot_db,
                 r.max_gain_rate_db_per_s, r.latency, dt * 1e3))

    print()
    print("look-ahead sweep on the transient material (dynamic_mix):")
    print("%10s %8s %8s %9s %8s %10s %9s"
          % ("lookahead", "clips", "oversh", "peak extra", "rate", "delay", "dev dB"))
    x = materials.dynamic_mix()
    for la_ms in (0.0, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0):
        r = gain.loudness_gain(x, target_db=target, lookahead_ms=la_ms)
        print("%8.1f ms %8d %8.3f %9.2f %8.0f %8d smp %9.3f"
              % (la_ms, r.ceiling_clips, r.max_ceiling_overshoot_db,
                 r.peak_attenuation_db, r.max_gain_rate_db_per_s,
                 r.latency, r.loudness_deviation_db))

    print()
    print("delay compensation (output[latency + k] == x[k] * 10**(g[k]/20)):")
    for name, x in materials.build().items():
        for la_ms in (0.0, 20.0):
            r = gain.loudness_gain(x, target_db=target, lookahead_ms=la_ms)
            n = len(x)
            assert len(r.y) == n + r.latency
            assert r.y[:r.latency] == [0.0] * r.latency
            err = max(abs(r.y[r.latency + k] - x[k] * 10.0 ** (r.gain_db[k] / 20.0))
                      for k in range(n))
            aligned = gain.delay_compensate(r.y, r.latency)
            assert len(aligned) == n
            print("  %-14s lookahead %5.1f ms -> latency %4d smp "
                  "max sample error %.2e, aligned len %d"
                  % (name, la_ms, r.latency, err, len(aligned)))

    print()
    print("determinism / edge cases:")
    print("  empty input        -> y=%r latency=%d"
          % (gain.loudness_gain([]).y, gain.loudness_gain([]).latency))
    z = gain.loudness_gain([0.0] * 4800)
    print("  all silence 100 ms -> unique(y)=%r gain=%r latency=%d L=%r"
          % (sorted(set(z.y)), sorted(set(z.gain_db)), z.latency,
             z.loudness_deviation_db))
    w = gain.loudness_gain([0.5, -0.25, 0.125, 0.0], target_db=-6.0,
                           window_ms=0.0, lookahead_ms=0.0)
    print("  window_ms=0        -> gain=%r peak=%.3f"
          % (["%.4f" % g for g in w.gain_db], gain.peak(w.y)))
    t = gain.loudness_gain([0.5] * 4800, target_db=0.0)
    print("  target_db=0        -> L_out-tgt=%.3f dB gain=%.2f dB"
          % (t.loudness_deviation_db, t.global_gain_db))
    print("  negative lookahead -> ValueError")
    try:
        gain.loudness_gain([0.1], lookahead_ms=-1.0)
    except ValueError as e:
        print("                        (%s)" % e)


if __name__ == "__main__":
    main()
