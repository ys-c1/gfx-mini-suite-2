import math
import sys

sys.path.insert(0, "src")
import duck

SR = 48000
THRESHOLD = 0.25
RATIO = 4.0
MAX_GR_DB = 18.0
ATTACK_MS = 5.0
RELEASE_MS = 120.0

# Theoretical steady reduction for a voice at level L with the settings
# above: (1 - 1/ratio) * 20*log10(L / threshold), subject to the clamp.
def steady_gr_db(level):
    excess = 20.0 * math.log10(level / THRESHOLD)
    return min((1.0 - 1.0 / RATIO) * excess, MAX_GR_DB)


def sine(n_samples, freq=220.0, amp=0.5):
    return [amp * math.sin(2.0 * math.pi * freq * i / SR)
            for i in range(n_samples)]


def gate_voice(length, start_s, end_s, level):
    """Hard-gated voice: positive constant level while speaking, 0 otherwise."""
    start, end = int(start_s * SR), int(end_s * SR)
    return [level if start <= i < end else 0.0 for i in range(length)]


def recovery_time_ms(reduction_db, voice_off_index, peak_gr):
    """Time from voice-off until reduction decays to 10% of its peak."""
    floor = 0.10 * peak_gr
    for i in range(voice_off_index, len(reduction_db)):
        if reduction_db[i] <= floor:
            return (i - voice_off_index) * 1000.0 / SR
    return math.inf


def report(segment, result, voice_off):
    peak = max(result["reduction_db"])
    rec = recovery_time_ms(result["reduction_db"], voice_off, peak)
    print("  depth          peak GR: %.2f dB (max allowed %.1f dB)"
          % (peak, MAX_GR_DB))
    print("  recovery       90%% released in %.1f ms (release tau %d ms)"
          % (rec, RELEASE_MS))
    print("  priority       handoffs: %d, target jump <= %.3f dB, "
          "applied gain jump <= %.6f linear"
          % (result["handoffs"], result["target_jump_max_db"],
             result["applied_jump_max"]))
    return peak, rec


def test_segment_a_single_voice():
    """One voice above threshold: depth from the gain computer, clean release."""
    n = int(2.0 * SR)
    bg = sine(n)
    voices = [gate_voice(n, 0.2, 1.2, 0.8)]
    r = duck.duck(bg, voices, SR, threshold=THRESHOLD, ratio=RATIO,
                  max_gain_reduction_db=MAX_GR_DB,
                  attack_ms=ATTACK_MS, release_ms=RELEASE_MS)

    assert all(w in (0, -1) for w in r["winner"])
    assert abs(r["max_reduction_db"] - steady_gr_db(0.8)) < 0.1
    assert r["max_reduction_db"] <= MAX_GR_DB + 1e-12
    assert r["handoffs"] == 0
    assert len(r["output"]) == n

    # Below threshold before the gate opens: bus passes through.
    first_gr = r["reduction_db"][0]
    assert first_gr < 1e-6
    assert abs(r["gain"][0] - 1.0) < 1e-12

    print("[segment A] single voiceover, level 0.8")
    report("A", r, int(1.2 * SR))


def test_segment_b_two_voices_priority_handoff():
    """Two voices talk over each other; the higher-priority one arbitrates."""
    n = int(2.0 * SR)
    bg = sine(n)
    # Chain 0 speaks in two windows (0.2-0.5 s and 1.1-1.4 s); the hotter
    # chain 1 fills the middle alone (0.5-1.1 s).  Each boundary is a real
    # winner handoff, and when both talk at 1.1 s priority takes over even
    # though chain 1 is 1.5x louder.
    v0 = [a + b for a, b in zip(
        gate_voice(n, 0.2, 0.5, 0.8),
        gate_voice(n, 1.1, 1.4, 0.8))]
    voices = [v0, gate_voice(n, 0.5, 1.1, 1.2)]
    # Explicit priorities: 10 beats 5 regardless of level.
    r = duck.duck(bg, voices, SR, threshold=THRESHOLD, ratio=RATIO,
                  max_gain_reduction_db=MAX_GR_DB,
                  attack_ms=ATTACK_MS, release_ms=RELEASE_MS,
                  priorities=[10, 5])

    assert r["winner"][int(0.3 * SR)] == 0
    assert r["winner"][int(0.6 * SR)] == 1
    # Overlap at 1.1 s: priority, not level, wins.
    assert r["winner"][int(1.2 * SR)] == 0
    assert r["winner"][-1] == -1

    # Target handoff magnitude: |GR(1.2) - GR(0.8)|, once at each boundary.
    expected_jump = abs(steady_gr_db(1.2) - steady_gr_db(0.8))
    assert abs(r["target_jump_max_db"] - expected_jump) < 1e-9
    assert r["handoffs"] == 2
    # Attack smoothing at 5 ms keeps the linear gain step tiny.
    assert r["applied_jump_max"] < 1e-3
    assert r["max_reduction_db"] <= MAX_GR_DB + 1e-12

    print("[segment B] two voiceovers alternating, two priority handoffs")
    report("B", r, int(1.4 * SR))


def test_segment_c_deep_drive_hits_depth_clamp():
    """A very hot voice would exceed the clamp; reduction must stay bounded."""
    n = int(2.0 * SR)
    bg = sine(n)
    # 5.0 -> unclamped GR = 7.78 dB over threshold ratio at ratio 4... use a
    # level that would demand ~26 dB to force the 18 dB clamp.
    hot = 20.0
    voices = [gate_voice(n, 0.3, 1.1, hot)]
    r = duck.duck(bg, voices, SR, threshold=THRESHOLD, ratio=RATIO,
                  max_gain_reduction_db=MAX_GR_DB,
                  attack_ms=ATTACK_MS, release_ms=RELEASE_MS)

    assert r["max_reduction_db"] <= MAX_GR_DB + 1e-12
    assert abs(r["max_reduction_db"] - MAX_GR_DB) < 0.1
    # Output never exceeds the input magnitude: the bus only ever ducks.
    assert all(abs(o) <= abs(s) + 1e-12
               for o, s in zip(r["output"], bg))

    print("[segment C] single voiceover, level %.1f, clamp at %.0f dB"
          % (hot, MAX_GR_DB))
    report("C", r, int(1.1 * SR))


def test_priority_beats_level():
    n = int(0.1 * SR)
    bg = [1.0] * n
    voices = [[0.9] * n, [2.0] * n]
    r = duck.duck(bg, voices, SR, threshold=THRESHOLD,
                  priorities=[100, 1])
    assert all(w == 0 for w in r["winner"])
    # Reversed priorities: the louder, now higher-priority chain takes over.
    r2 = duck.duck(bg, voices, SR, threshold=THRESHOLD,
                   priorities=[1, 100])
    assert all(w == 1 for w in r2["winner"])


def test_below_threshold_passes_through():
    r = duck.duck([0.7, -0.7], [[0.1, 0.1]], SR, threshold=THRESHOLD)
    assert r["winner"] == [-1, -1]
    assert abs(r["output"][0] - 0.7) < 1e-12
    assert max(r["reduction_db"]) < 1e-12


def test_streaming_matches_single_block():
    n = int(0.5 * SR)
    bg = sine(n)
    voices = [gate_voice(n, 0.05, 0.4, 0.9)]
    one = duck.duck(bg, voices, SR, threshold=THRESHOLD,
                    attack_ms=ATTACK_MS, release_ms=RELEASE_MS)

    d = duck.SidechainDucker(SR, threshold=THRESHOLD,
                             attack_ms=ATTACK_MS,
                             release_ms=RELEASE_MS)
    cut = n // 3
    parts = [
        d.process(bg[:cut], [voices[0][:cut]]),
        d.process(bg[cut:], [voices[0][cut:]]),
    ]
    streamed = parts[0]["output"] + parts[1]["output"]
    assert all(abs(a - b) < 1e-15 for a, b in
               zip(one["output"], streamed))


def test_gain_computer_clamp():
    d = duck.SidechainDucker(SR, threshold=THRESHOLD, ratio=RATIO,
                             max_gain_reduction_db=MAX_GR_DB)
    assert d.target_gain_reduction_db(0.1) == 0.0
    assert abs(d.target_gain_reduction_db(0.5)
               - 0.75 * 20.0 * math.log10(2.0)) < 1e-12
    assert d.target_gain_reduction_db(1e6) == MAX_GR_DB


def test_validation():
    for kwargs in (
        dict(sample_rate=0),
        dict(sample_rate=SR, threshold=0.0),
        dict(sample_rate=SR, ratio=0.5),
        dict(sample_rate=SR, max_gain_reduction_db=-1.0),
        dict(sample_rate=SR, attack_ms=0.0),
        dict(sample_rate=SR, release_ms=-1.0),
    ):
        try:
            duck.SidechainDucker(**kwargs)
        except ValueError:
            pass
        else:
            raise AssertionError("expected ValueError for %r" % kwargs)

    d = duck.SidechainDucker(SR)
    try:
        d.process([1.0, 1.0], [[1.0]])  # length mismatch
    except ValueError:
        pass
    else:
        raise AssertionError("expected ValueError on length mismatch")


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn()
                print("PASS", name)
            except AssertionError as e:
                failed += 1
                print("FAIL", name, e)
            print("")
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
