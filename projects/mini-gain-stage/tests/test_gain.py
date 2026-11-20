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


# A non-uniform float signal so summation order is actually exercised.
SIGNAL = [
    0.5, -0.5, 0.33333333, -0.25, 0.125, 0.0,
    -0.7, 0.9, 0.625, -0.4, 0.1, -0.2,
    0.3, -0.3, 0.45, -0.45, 0.08, -0.09, 0.75, -0.8,
]


def _rms_close(streamed, whole):
    # CPython sum() uses an extended-precision accumulator; a double-
    # precision running total can only agree within float rounding.
    return abs(streamed - whole) <= 1e-12 * max(1.0, abs(whole))


def test_batched_output_concatenates_to_whole():
    stage = gain.GainStage(1.5)
    batched = []
    for batch in gain.iter_batches(SIGNAL, 7):
        batched += stage.process(batch)
    stats = stage.finish()
    # Per-batch returns concatenated must match the whole-signal call
    # sample for sample (same float multiply, no reordering).
    assert batched == gain.apply_gain(SIGNAL, 1.5)
    assert stats.samples == len(SIGNAL)


def test_batched_stats_match_whole_within_rounding():
    stage = gain.GainStage(2.0)
    for batch in gain.iter_batches(SIGNAL, 4):
        stage.process(batch)
    stats = stage.finish()
    # Peak is an exact max; RMS differs only at the float-rounding level.
    assert stats.peak == gain.peak(SIGNAL)
    assert _rms_close(stats.rms, gain.rms(SIGNAL))
    assert stage.samples == len(SIGNAL)


def test_partial_last_batch():
    # 20 samples, batch 6 -> 6,6,6,2: the short final batch must be returned.
    stage = gain.GainStage(1.0)
    batched = []
    sizes = []
    for batch in gain.iter_batches(SIGNAL, 6):
        out = stage.process(batch)
        assert len(out) == len(batch)
        sizes.append(len(batch))
        batched += out
    assert sizes == [6, 6, 6, 2]
    assert batched == SIGNAL
    stats = stage.finish()
    assert stats.peak == gain.peak(SIGNAL)
    assert _rms_close(stats.rms, gain.rms(SIGNAL))


def test_early_stop_returns_prefix():
    # Stop at a batch boundary after 15 of 20 samples: finish() must flush
    # stats for exactly the prefix that was fed in.
    stop_at = 15
    stage = gain.GainStage(0.5)
    produced = []
    for batch in gain.iter_batches(SIGNAL, 5):
        produced += stage.process(batch)
        if stage.samples == stop_at:
            break
    stats = stage.finish()
    prefix = SIGNAL[:stop_at]
    assert produced == gain.apply_gain(prefix, 0.5)
    assert stats.samples == stop_at
    assert stats.peak == gain.peak(prefix)
    assert _rms_close(stats.rms, gain.rms(prefix))


def test_empty_batches_and_empty_stream():
    stage = gain.GainStage(1.0)
    assert stage.process([]) == []
    assert stage.samples == 0
    assert stage.peak == 0.0
    assert stage.rms == 0.0
    stats = stage.finish()
    assert stats == gain.StageStats(0.0, 0.0, 0)

    # finish() on a stage that never received a batch behaves the same.
    fresh = gain.GainStage(1.0)
    assert fresh.finish() == gain.StageStats(0.0, 0.0, 0)


def test_running_stats_mid_stream():
    stage = gain.GainStage(1.0)
    first = SIGNAL[:8]
    stage.process(first)
    assert stage.samples == 8
    assert stage.peak == gain.peak(first)
    assert _rms_close(stage.rms, gain.rms(first))
    stage.process(SIGNAL[8:])
    stats = stage.finish()
    assert stats.peak == gain.peak(SIGNAL)
    assert _rms_close(stats.rms, gain.rms(SIGNAL))


def test_batch_size_one_and_larger_than_signal():
    for size in (1, len(SIGNAL), len(SIGNAL) + 7):
        stage = gain.GainStage(1.25)
        out = []
        for batch in gain.iter_batches(SIGNAL, size):
            out += stage.process(batch)
        assert out == gain.apply_gain(SIGNAL, 1.25)
        assert _rms_close(stage.finish().rms, gain.rms(SIGNAL))


def test_invalid_batch_size():
    for bad in (0, -1):
        try:
            list(gain.iter_batches(SIGNAL, bad))
        except ValueError:
            continue
        raise AssertionError("expected ValueError for batch_size=%r" % bad)


def test_process_after_finish_rejected():
    stage = gain.GainStage(1.0)
    stage.process(SIGNAL[:3])
    first = stage.finish()
    # finish() is idempotent ...
    assert stage.finish() == first
    # ... but the stage cannot be reused.
    try:
        stage.process([0.1])
    except RuntimeError:
        return
    raise AssertionError("expected RuntimeError")


def test_stage_rejects_negative_gain():
    try:
        gain.GainStage(-1.0)
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
