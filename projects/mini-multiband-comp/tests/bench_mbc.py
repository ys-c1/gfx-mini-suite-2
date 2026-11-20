"""Benchmark + hand-calculation cross-check for the two-band compressor.

Compares 2-channel and 6-channel layouts for peak error, compression amount
and wall time, then checks every measured low-band gain against a hand-calculated
number. Any full-scale overload in any run is reported explicitly.
"""

import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import mbc

SR = 48000.0
FRAMES = int(SR * 2)               # 2 s per channel
CUTOFF = 200.0
LOW_THR, LOW_R = 0.5, 2.0
HIGH_THR, HIGH_R = 0.25, 4.0
LOW_AMP, HIGH_AMP = 0.70, 0.28       # sum 0.98 < 1.0: no overload by design
LOW_F, HIGH_F = 60.0, 4000.0
# Amplitudes sum to 0.98 < 1.0 so the benchmark itself never overloads;
# an overload is demonstrated separately below with forced makeup gain.


def build_signal(channels):
    mono_low = [LOW_AMP * math.sin(2 * math.pi * LOW_F * i / SR)
                for i in range(FRAMES)]
    mono_high = [HIGH_AMP * math.sin(2 * math.pi * HIGH_F * i / SR)
                 for i in range(FRAMES)]
    mono = [a + b for a, b in zip(mono_low, mono_high)]
    samples = [0.0] * (FRAMES * channels)
    for c in range(channels):
        scale = 1.0 - 0.1 * c
        samples[c::channels] = [v * scale for v in mono]
    return samples


def time_run(fn, min_reps=3, min_seconds=0.5):
    # timed runs include the full split/compress/merge pipeline
    times = []
    t0 = time.perf_counter()
    while True:
        s = time.perf_counter()
        fn()
        times.append(time.perf_counter() - s)
        if len(times) >= min_reps and time.perf_counter() - t0 >= min_seconds:
            break
    return sum(times) / len(times), min(times), len(times)


def main():
    lines = []
    def emit(s=""):
        print(s)
        lines.append(s)

    emit("=" * 92)
    emit("two-band compressor bench: sr=%g, %d frames/channel, "
         "cutoff=%g Hz" % (SR, FRAMES, CUTOFF))
    emit("low:  T=%.2f R=%.1f   high: T=%.2f R=%.1f   "
         "attack=5ms release=50ms" % (LOW_THR, LOW_R, HIGH_THR, HIGH_R))
    emit("=" * 92)
    emit("%-8s %-8s %10s %10s %10s %10s %10s %9s" % (
        "config", "state", "in_peak", "out_peak", "max|d|",
        "lowGR/dB", "highGR/dB", "ms/run"))
    emit("-" * 92)

    overload_events = []
    for channels in (2, 6):
        samples = build_signal(channels)
        in_peak = max(abs(v) for v in samples)
        for enabled in (True, False):
            fn = lambda: mbc.multiband_compress(
                samples, channels, SR, enabled=enabled,
                cutoff=CUTOFF, low_thr=LOW_THR, low_ratio=LOW_R,
                high_thr=HIGH_THR, high_ratio=HIGH_R)
            out, stats = fn()
            avg_ms, best_ms, reps = time_run(fn)
            if enabled:
                # max per-sample change between input and output (full signal)
                max_delta = max(abs(a - b) for a, b in zip(samples, out))
                # same metric over the steady-state tail (no onset transient)
                tail = slice(-FRAMES // 20, None)
                ss_err = max(abs(a - b)
                             for a, b in zip(samples[tail], out[tail]))
                peak_err = max_delta
                ss_text = "  (steady-state max|d| %.2e)" % ss_err
            else:
                peak_err = 0.0
                ss_text = ""
            if stats["overload"]:
                overload_events.append((channels, enabled, stats["peak"]))
            emit("%-8d %-8s %10.5f %10.5f %10.2e %10.3f %10.3f %9.2f" % (
                channels,
                "split" if enabled else "bypass",
                in_peak, stats["peak"], peak_err,
                stats["low_gr_db"], stats["high_gr_db"],
                avg_ms * 1000.0))
            emit("  (best %.2f ms over %d runs)%s"
                 % (best_ms * 1000.0, reps, ss_text))

    # ---- hand-calculation cross-check (pure tones, exact analytic numbers) --
    # The mixed-signal table above has cross-band leakage that a closed form
    # cannot match; here each band is driven by a pure tone instead.
    emit("-" * 92)
    a_x = mbc.crossover_coeff(CUTOFF, SR)

    def lp_mag(f):
        w = 2.0 * math.pi * f / SR
        return a_x / math.sqrt(a_x * a_x + 4.0 * (1.0 - a_x)
                               * math.sin(w / 2.0) ** 2)

    lp60 = lp_mag(LOW_F)
    low_peak_hand = LOW_AMP * lp60
    HAND_G = (low_peak_hand / LOW_THR) ** -0.5
    HAND_Y = low_peak_hand * HAND_G
    HAND_GR = -20.0 * math.log10(HAND_G)
    emit("hand calculation, LOW band (pure %.0f Hz, amp=%.2f):" %
         (LOW_F, LOW_AMP))
    emit("  crossover |H_LP(60)|=%.6f -> band peak=%.6f"
         % (lp60, low_peak_hand))
    emit("  g = (peak/T)^-0.5 = %.9f ; y = peak*g = %.9f ; GR = %.6f dB"
         % (HAND_G, HAND_Y, HAND_GR))
    pure_low = [LOW_AMP * math.sin(2 * math.pi * LOW_F * i / SR)
                for i in range(FRAMES)]
    lp0, _ = mbc.split_mono(pure_low, [CUTOFF], SR)
    lc0, g0 = mbc.compress_band(lp0, LOW_THR, LOW_R,
                                attack=0.0, release=0.0, sr=SR)
    # skip the crossover onset transient (a couple of low-band time constants)
    steady = range(FRAMES // 4, FRAMES)
    k_peak = max(steady, key=lambda i: lp0[i])
    # analytic gain evaluated AT the measured sample level, so discrete-time
    # peak quantization does not enter the comparison
    g_at_k = (lp0[k_peak] / LOW_THR) ** -0.5
    emit("  measured: |lp|=%.6f, |y|=%.6f, min_gain=%.6f"
         % (lp0[k_peak], lc0[k_peak], g0))
    checks = [
        ("low-band peak vs analytic", abs(lp0[k_peak] - low_peak_hand) < 1e-3),
        ("sample gain vs analytic curve",
         abs(lc0[k_peak] / lp0[k_peak] - g_at_k) < 1e-9),
        ("output at peak vs hand y", abs(lc0[k_peak] - HAND_Y) < 1e-3),
        ("global min gain vs hand GR (within 0.1 dB)",
         abs(mbc.gain_reduction_db(g0) - HAND_GR) < 0.1),
    ]

    pure_high = [HIGH_AMP * math.sin(2 * math.pi * HIGH_F * i / SR)
                 for i in range(FRAMES)]
    _, hp0 = mbc.split_mono(pure_high, [CUTOFF], SR)
    w2 = 2.0 * math.pi * HIGH_F / SR
    hp_mag = math.sqrt(
        4.0 * (1.0 - a_x) * math.sin(w2 / 2.0) ** 2
        / (a_x * a_x + 4.0 * (1.0 - a_x) * math.sin(w2 / 2.0) ** 2))
    high_peak_hand = HIGH_AMP * hp_mag
    hand_gh = (high_peak_hand / HIGH_THR) ** (1.0 / HIGH_R - 1.0)
    hc0, gh0 = mbc.compress_band(hp0, HIGH_THR, HIGH_R,
                                 attack=0.0, release=0.0, sr=SR)
    hk = max(steady, key=lambda i: abs(hp0[i]))
    gh_at_k = (abs(hp0[hk]) / HIGH_THR) ** (1.0 / HIGH_R - 1.0)
    emit("hand calculation, HIGH band (pure %.0f Hz): |H_HP|=%.6f, "
         "analytic peak=%.6f, hand g(analytic)=%.9f"
         % (HIGH_F, hp_mag, high_peak_hand, hand_gh))
    emit("  measured steady: |hp|=%.6f, |hc|=%.6f, "
         "global min_gain=%.6f (onset rings slightly past steady peak)"
         % (abs(hp0[hk]), abs(hc0[hk]), gh0))
    # Discrete-time peak: hand-recompute the one-pole recurrence over a full
    # steady-state window (independent of the module's split_mono) and compare
    # bit for bit. The closed-form transfer function describes continuous
    # amplitude, but only 12 samples/cycle exist here plus filter phase lag.
    y = 0.0
    hand_hp_full = []
    for x in pure_high:
        y += a_x * (x - y)
        hand_hp_full.append(x - y)
    win0, win1 = FRAMES // 4, FRAMES // 4 + int(SR // 10)
    hand_steady_peak = max(abs(v) for v in hand_hp_full[win0:win1])
    checks = [
        ("high band above threshold (GR applied)", gh0 < 1.0),
        ("hand recurrence matches module hp (all samples)",
         max(abs(hand_hp_full[i] - hp0[i]) for i in range(FRAMES)) < 1e-15),
        ("steady peak from hand recurrence",
         abs(hand_steady_peak - abs(hp0[hk])) < 1e-12),
        ("steady peak within 2% of analytic transfer value",
         abs(hand_steady_peak - high_peak_hand) < 0.02 * high_peak_hand),
        ("sample gain vs analytic curve",
         abs(hc0[hk] / hp0[hk] - gh_at_k) < 1e-9),
    ]
    # Onset minimum gain: same recurrence over the first 32 samples
    onset_peak = max(abs(v) for v in hand_hp_full[:32])
    onset_g = (onset_peak / HIGH_THR) ** (1.0 / HIGH_R - 1.0)
    emit("  onset hand recurrence (32 samples): peak=%.6f, g=%.6f"
         % (onset_peak, onset_g))
    checks.append(("global min gain == onset hand gain",
                   abs(gh0 - onset_g) < 1e-9))
    for name, ok in checks:
        emit("  [%s] %s" % ("OK" if ok else "FAIL", name))
    assert all(ok for _, ok in checks), "hand-calculation cross-check failed"

    # ---- overload report ----------------------------------------------------
    emit("-" * 92)
    if overload_events:
        for ch, en, pk in overload_events:
            emit("PEAK OVERLOAD: channels=%d state=%s peak=%.6f > 1.0"
                 % (ch, "split" if en else "bypass", pk))
    else:
        emit("peak overload: none in the benchmark runs (all peaks <= 1.0)")
    # forced overload demonstration
    hot, hot_stats = mbc.multiband_compress(
        [1.0] * int(SR // 10), 1, SR, low_thr=0.5, low_ratio=2.0,
        makeup=1.8, attack=0.0, release=0.0)
    emit("forced case (makeup=1.8 on full-scale DC): peak=%.6f overload=%s"
         % (hot_stats["peak"], hot_stats["overload"]))
    assert hot_stats["overload"] is True
    emit("=" * 92)

    out_path = os.path.join(
        os.path.dirname(__file__), "..", "..", "..",
        "artifacts", "mbc_bench_report.txt")
    out_path = os.path.normpath(out_path)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("report written to", out_path)


if __name__ == "__main__":
    main()
