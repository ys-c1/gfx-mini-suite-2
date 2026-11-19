import math
import random
import sys
sys.path.insert(0, "src")
import gain


def longest_plateau(samples):
    """Length of the longest run of consecutive identical samples."""
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


def make_signals(n=24000):
    sine = [1.8 * math.sin(2.0 * math.pi * 440.0 * i / 48000.0) for i in range(n)]
    square = [1.3 if (i // 109) % 2 == 0 else -1.3 for i in range(n)]
    rng = random.Random(42)
    noise = [2.5 * (rng.random() * 2.0 - 1.0) for _ in range(n)]
    return {"hot_sine": sine, "square": square, "noise_burst": noise}


# --- determinism at the three required edge inputs ---

def test_threshold_zero_deterministic():
    sig = [0.0, 0.5, -0.5, 3.0, -3.0]
    out = gain.protect_peaks(sig, 0.0)
    assert out == [s / (1.0 + abs(s)) for s in sig]
    assert gain.protect_peaks(sig, 0.0) == out  # repeat -> identical


def test_threshold_negative_deterministic():
    for sig in ([], [0.0], [1.5, -2.0]):
        try:
            gain.protect_peaks(sig, -0.1)
        except ValueError:
            continue
        raise AssertionError("expected ValueError for negative threshold")


def test_silence_deterministic():
    silence = [0.0] * 4096
    for t in (0.0, 0.5, 0.9):
        assert gain.protect_peaks(silence, t) == silence
    assert gain.protect_peaks([], 0.9) == []


# --- protection invariants on real signals ---

def test_output_never_out_of_bounds():
    for name, sig in make_signals().items():
        out = gain.protect_peaks(sig, 0.9)
        assert all(abs(s) < 1.0 for s in out), name


def test_no_new_plateaus():
    for name, sig in make_signals().items():
        before = longest_plateau(sig)
        after = longest_plateau(gain.protect_peaks(sig, 0.9))
        assert after <= before, (name, before, after)
    # plateau-free input stays plateau-free (no run longer than 1)
    sine = make_signals()["hot_sine"]
    assert longest_plateau(gain.protect_peaks(sine, 0.9)) <= 1


def test_loudness_change_bounded():
    for name, sig in make_signals().items():
        delta = loudness_db(gain.protect_peaks(sig, 0.9)) - loudness_db(sig)
        assert delta <= 0.0, (name, delta)  # hard upper bound: never louder
        assert delta > -20.0, (name, delta)  # and gentle in practice


def test_below_threshold_untouched():
    sig = [0.1, -0.3, 0.89, -0.9, 0.0]
    assert gain.protect_peaks(sig, 0.9) == sig


def test_input_not_mutated():
    sig = [1.5, -2.0, 0.1]
    gain.protect_peaks(sig, 0.9)
    assert sig == [1.5, -2.0, 0.1]


# --- regression: pre-existing paths bit-identical to golden values ---

def test_existing_paths_sample_identical():
    sig = [0.25, -0.5, 0.75, -1.0]
    assert gain.apply_gain(sig, 1.5) == [0.375, -0.75, 1.125, -1.5]
    assert gain.peak([0.25, -0.5, 0.75, -1.0]) == 1.0
    assert gain.peak(gain.apply_gain(sig, 1.5)) == 1.5
    assert gain.rms([0.375, -0.75, 1.125, -1.5]) == 1.0269797953221864
    assert gain.rms([]) == 0.0 and gain.peak([]) == 0.0


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn(); print("PASS", name)
            except AssertionError as e:
                failed += 1; print("FAIL", name, e)
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
