import sys
sys.path.insert(0, "src")
import gain


def test_apply_gain():
    assert gain.apply_gain([0.5, -0.5], 2.0) == [1.0, -1.0]


def test_peak_rms():
    s = [0.5, -0.5, 0.0, 0.0]
    assert abs(gain.peak(s) - 0.5) < 1e-9
    assert abs(gain.rms(s) - 0.35355339) < 1e-6


def test_negative_gain():
    try:
        gain.apply_gain([0.1], -1.0)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


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
