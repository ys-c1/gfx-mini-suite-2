"""Tests for the time-varying loudness gain with look-ahead.

Run with ``make test`` from the project directory.  The test harness is the
same tiny loop as the other projects here; no pytest is required.
"""

import sys

sys.path.insert(0, "src")
sys.path.insert(0, "tests")

import gain
import materials

FS = 48000


# --- the original constant-gain path must stay bit-identical ----------------

def test_original_paths_unchanged():
    assert gain.apply_gain([0.5, -0.5], 2.0) == [1.0, -1.0]
    assert gain.apply_gain([0.25, -0.5, 0.75, -1.0], 1.5) == [0.375, -0.75, 1.125, -1.5]
    assert gain.peak([0.5, -0.5, 0.0, 0.0]) == 0.5
    assert gain.rms([0.5, -0.5, 0.0, 0.0]) == (0.5 ** 2 * 2 / 4) ** 0.5
    assert gain.rms([]) == 0.0 and gain.peak([]) == 0.0
    try:
        gain.apply_gain([0.1], -1.0)
    except ValueError:
        pass
    else:
        raise AssertionError("expected ValueError for negative gain")


# --- determinism at the required edge inputs --------------------------------

def test_empty_input():
    r = gain.loudness_gain([])
    assert r.y == [] and r.gain_db == [] and r.latency == 0
    assert r.ceiling_clips == 0


def test_all_silence():
    r = gain.loudness_gain([0.0] * 4096)
    assert r.latency == 960
    assert r.y == [0.0] * (4096 + r.latency)
    assert r.gain_db == [0.0] * 4096
    assert r.loudness_deviation_db == float("-inf")
    assert r.ceiling_clips == 0
    # and the same with look-ahead disabled
    r0 = gain.loudness_gain([0.0] * 16, lookahead_ms=0.0)
    assert r0.y == [0.0] * 16 and r0.latency == 0


def test_window_zero():
    x = [0.5, -0.25, 0.125, 0.0]
    r = gain.loudness_gain(x, target_db=-6.0, window_ms=0.0, lookahead_ms=0.0)
    assert r.latency == 0
    assert len(r.gain_db) == len(x)
    # Instantaneous loudness: each non-zero sample is exactly at the target
    # after the global gain, so the dynamic trim stays at 0 dB for them.
    assert r.gain_db == [r.global_gain_db] * 4
    # The zero sample is silent in the instantaneous sense; it must be left
    # alone (gain held), not lifted to -inf dB.
    assert r.y[3] == 0.0


def test_target_zero():
    x = [0.5] * 4800
    r = gain.loudness_gain(x, target_db=0.0, lookahead_ms=0.0)
    assert abs(r.loudness_deviation_db) < 1e-9
    assert gain.peak(r.y) <= 1.0
    # with a target of 0 dBFS and a 0.5 amplitude, the gain must be +6.02 dB
    assert abs(r.global_gain_db - 6.020599913279624) < 1e-12


def test_invalid_parameters():
    cases = [dict(lookahead_ms=-1.0), dict(window_ms=-1.0),
             dict(max_slew_db_per_s=-1.0), dict(trim_db=-1.0),
             dict(ceiling_db=0.5), dict(peak_attack_db_per_s=-1.0),
             dict(peak_release_db_per_s=-1.0), dict(fs=0),
             dict(target_db=float("nan")),
             dict(min_gain_db=10.0, max_gain_db=0.0),
             # rate hierarchy: loudness/release may not outrun the duck
             dict(max_slew_db_per_s=2000.0),
             dict(peak_release_db_per_s=5000.0)]
    for kw in cases:
        try:
            gain.loudness_gain([0.1], **kw)
        except ValueError:
            continue
        raise AssertionError("expected ValueError for %r" % (kw,))


def test_repeatable():
    x = [0.1, -0.2, 0.3, -0.4] * 1000
    a = gain.loudness_gain(x)
    b = gain.loudness_gain(x)
    assert a.y == b.y and a.gain_db == b.gain_db and a.latency == b.latency


def test_input_not_mutated():
    x = [0.1, -0.9, 0.2, -0.2]
    before = list(x)
    gain.loudness_gain(x)
    assert x == before


# --- delay compensation -----------------------------------------------------

def _check_delay(x, r, tol=0.0):
    n = len(x)
    assert len(r.y) == n + r.latency
    assert r.y[:r.latency] == [0.0] * r.latency
    for k in range(n):
        want = x[k] * 10.0 ** (r.gain_db[k] / 20.0)
        got = r.y[r.latency + k]
        if tol == 0.0:
            # exact: the body is computed with the same expression
            assert got == want or abs(got - want) < 1e-15, (k, got, want)
        else:
            assert abs(got - want) <= tol, (k, got, want)
    aligned = gain.delay_compensate(r.y, r.latency)
    assert aligned == r.aligned
    assert len(aligned) == n


def test_delay_compensation_exact():
    x = materials.quiet_dialog()[:48000 * 2]
    for la_ms in (0.0, 1.0, 5.0, 20.0):
        r = gain.loudness_gain(x, lookahead_ms=la_ms)
        assert r.latency == max(0, int(round(la_ms * FS / 1000.0)))
        _check_delay(x, r)


def test_delay_matches_requested_lookahead():
    x = [0.3, -0.3] * 24000
    for la_ms in (0.0, 0.5, 2.0, 10.0, 20.0, 40.0):
        r = gain.loudness_gain(x, lookahead_ms=la_ms)
        assert r.latency == int(round(la_ms * FS / 1000.0))


# --- gain-curve bounds ------------------------------------------------------

def test_gain_rate_bounded():
    x = materials.dynamic_mix()
    r = gain.loudness_gain(x)
    bound = max(gain.DEFAULT_SLEW_DB_PER_S,
                gain.DEFAULT_PEAK_ATTACK_DB_PER_S,
                gain.DEFAULT_PEAK_RELEASE_DB_PER_S)
    assert r.max_gain_rate_db_per_s <= bound + 1e-6, r.max_gain_rate_db_per_s
    # with tightened parameters the bound follows them: the attack rate is
    # the fastest any gain may move, and release/slew may not exceed it
    r2 = gain.loudness_gain(x, peak_attack_db_per_s=600.0,
                            peak_release_db_per_s=40.0,
                            max_slew_db_per_s=6.0)
    assert r2.max_gain_rate_db_per_s <= 600.0 + 1e-6, r2.max_gain_rate_db_per_s


def test_peak_never_above_ceiling():
    for name, x in materials.build().items():
        r = gain.loudness_gain(x)
        assert gain.peak(r.y) <= 1.0, name
        assert r.max_ceiling_overshoot_db <= 0.0 + 1e-9, name


def test_ceiling_below_zero():
    x = materials.loud_master()
    r = gain.loudness_gain(x, ceiling_db=-3.0)
    assert gain.peak(r.y) <= 10.0 ** (-3.0 / 20.0) + 1e-12
    assert r.ceiling_clips == 0


def test_silence_stays_silent_inside_signal():
    x = materials.dynamic_mix()
    r = gain.loudness_gain(x)
    # first 0.5 s of the material is true digital silence
    assert r.y[r.latency:r.latency + 24000] == [0.0] * 24000


# --- loudness bound ---------------------------------------------------------

def test_loudness_deviation_bounded():
    for name, x in materials.build().items():
        r = gain.loudness_gain(x)
        bound = r.loudness_bound_db + r.peak_attenuation_db
        assert abs(r.loudness_deviation_db) <= bound, (
            name, r.loudness_deviation_db, bound)


def test_constant_material_is_exact():
    for amp, target in ((0.1, -23.0), (0.03, -23.0), (0.5, -12.0)):
        x = [amp] * 4800
        r = gain.loudness_gain(x, target_db=target)
        assert abs(r.loudness_deviation_db) < 1e-9, (amp, target)


def test_louder_material_is_cut_quieter_is_lifted():
    quiet = materials.quiet_dialog()
    loud = materials.loud_master()
    rq = gain.loudness_gain(quiet)
    rl = gain.loudness_gain(loud)
    assert rq.global_gain_db > 0.0
    assert rl.global_gain_db < 0.0
    assert abs(rq.loudness_deviation_db) < 1.0
    assert abs(rl.loudness_deviation_db) < 1.0


# --- look-ahead -------------------------------------------------------------

def test_lookahead_removes_clipping_on_transients():
    x = materials.dynamic_mix()
    causal = gain.loudness_gain(x, lookahead_ms=0.0)
    backed = gain.loudness_gain(x, lookahead_ms=20.0)
    assert causal.latency == 0
    assert causal.ceiling_clips > 0, "material should require look-ahead"
    assert causal.max_ceiling_overshoot_db > 0.5
    assert backed.ceiling_clips == 0
    assert backed.max_ceiling_overshoot_db == 0.0
    # and the gain curve stays inside the attack rate in both cases
    assert backed.max_gain_rate_db_per_s <= 1200.0 + 1e-6


def test_lookahead_ducks_before_the_transient():
    x = materials.dynamic_mix()
    r = gain.loudness_gain(x, lookahead_ms=20.0)
    centre = int(materials.TRANSIENTS_S[2] * FS)  # click at 2.5 s
    g = r.gain_db
    # 20 ms before the click the gain is already on its way down, i.e.
    # below the gain that was in force 20 ms earlier
    before = g[centre - r.latency]
    earlier = g[centre - r.latency - 240]
    assert before < earlier + 1e-9, (earlier, before)


def test_zero_lookahead_zero_delay():
    x = materials.quiet_dialog()[:24000]
    r = gain.loudness_gain(x, lookahead_ms=0.0)
    assert r.latency == 0
    assert len(r.y) == len(x)


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
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
