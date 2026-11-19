"""Benchmark report: peak / plateau / loudness before vs after protection."""
import math
import random
import sys
import time
sys.path.insert(0, "src")
import gain


def longest_plateau(samples):
    best = run = 0
    prev = None
    for s in samples:
        run = run + 1 if s == prev else 1
        prev = s
        best = max(best, run)
    return best


def loudness_db(samples):
    r = gain.rms(samples)
    return 20.0 * math.log10(r) if r > 0 else float("-inf")


def main():
    n = 24000  # 0.5 s @ 48 kHz
    rng = random.Random(42)
    signals = {
        "hot_sine   (peak 1.80)": [1.8 * math.sin(2.0 * math.pi * 440.0 * i / 48000.0)
                                   for i in range(n)],
        "square     (peak 1.30)": [1.3 if (i // 109) % 2 == 0 else -1.3
                                   for i in range(n)],
        "noise_burst(peak ~2.5)": [2.5 * (rng.random() * 2.0 - 1.0)
                                   for _ in range(n)],
    }
    threshold = 0.9
    print("threshold = %.2f, %d samples per signal" % (threshold, n))
    print("%-24s %18s %14s %12s %10s" % ("signal", "peak pre->post",
                                         "plateau pre->post", "dLoudness dB", "time ms"))
    for name, sig in signals.items():
        t0 = time.perf_counter()
        out = gain.protect_peaks(sig, threshold)
        dt = (time.perf_counter() - t0) * 1e3
        dl = loudness_db(out) - loudness_db(sig)
        print("%-24s %7.4f -> %6.4f %7d -> %-6d %10.4f %10.3f"
              % (name, gain.peak(sig), gain.peak(out),
                 longest_plateau(sig), longest_plateau(out), dl, dt))

    # determinism spot checks
    print("\ndeterminism:")
    print("  threshold=0   ->", gain.protect_peaks([0.0, 0.5, -3.0], 0.0))
    try:
        gain.protect_peaks([1.0], -0.5)
    except ValueError as e:
        print("  threshold=-0.5-> ValueError:", e)
    print("  silence x4    ->", gain.protect_peaks([0.0] * 4, 0.9))

    # proof that untouched paths are sample-identical to golden values
    sig = [0.25, -0.5, 0.75, -1.0]
    ok = (gain.apply_gain(sig, 1.5) == [0.375, -0.75, 1.125, -1.5]
          and gain.peak(sig) == 1.0
          and gain.rms(gain.apply_gain(sig, 1.5)) == 1.0269797953221864)
    print("\nexisting gain/peak/rms paths bit-identical to golden:", ok)


if __name__ == "__main__":
    main()
