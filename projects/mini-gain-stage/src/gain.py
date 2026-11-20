"""Static gain stage for interleaved float audio.

Whole-signal helpers (apply_gain/peak/rms) are kept unchanged.  For long
material, GainStage processes fixed-size batches with O(1) retained state:
per-batch outputs concatenate sample-for-sample with apply_gain, the running
peak equals peak() exactly, and the running RMS matches rms() to floating-
point rounding tolerance (CPython's sum() accumulates in platform extended
precision, so a double-precision running total cannot be bit-identical).
"""

from collections import namedtuple

StageStats = namedtuple("StageStats", "peak rms samples")


def apply_gain(samples, gain):
    if gain < 0:
        raise ValueError("gain must be >= 0")
    return [s * gain for s in samples]


def peak(samples):
    return max((abs(s) for s in samples), default=0.0)


def rms(samples):
    if not samples:
        return 0.0
    return (sum(s * s for s in samples) / len(samples)) ** 0.5


def iter_batches(samples, batch_size):
    """Yield successive slices of samples, each at most batch_size long.

    The final slice may be shorter; batch_size must be a positive integer.
    """
    if batch_size <= 0:
        raise ValueError("batch_size must be > 0")
    for start in range(0, len(samples), batch_size):
        yield samples[start:start + batch_size]


class GainStage:
    """Streaming gain stage fed one batch at a time.

    process() returns the gained batch immediately while accumulating level
    statistics over the *input* samples (independent of the gain setting,
    matching the standalone peak()/rms() helpers).  Retained state is three
    scalars, so memory use does not grow with signal length.  Call finish()
    once the stream ends — it flushes the stats for everything fed so far,
    which also covers a partial last batch or an early stop.
    """

    def __init__(self, gain):
        if gain < 0:
            raise ValueError("gain must be >= 0")
        self.gain = gain
        self._peak = 0.0
        self._sum_sq = 0.0
        self._samples = 0
        self._finished = False

    def process(self, samples):
        """Apply gain to one batch and fold it into the running stats."""
        if self._finished:
            raise RuntimeError("finish() already called")
        output = [s * self.gain for s in samples]
        self._peak = max(
            self._peak, max((abs(s) for s in samples), default=0.0)
        )
        # Per-batch sum() keeps the same extended-precision accumulator
        # semantics as the whole-signal rms(); only the cross-batch fold
        # is done in double precision, so drift is bounded by ordinary
        # float rounding rather than batch count.
        self._sum_sq += sum(s * s for s in samples)
        self._samples += len(samples)
        return output

    @property
    def samples(self):
        """Number of input samples consumed so far."""
        return self._samples

    @property
    def peak(self):
        """Running peak over every sample fed in so far."""
        return self._peak

    @property
    def rms(self):
        """Running RMS over every sample fed in so far."""
        if self._samples == 0:
            return 0.0
        return (self._sum_sq / self._samples) ** 0.5

    def finish(self):
        """Flush cumulative stats for all samples fed in.

        Works for the partial last batch and for early stops; idempotent.
        """
        self._finished = True
        return StageStats(self._peak, self.rms, self._samples)
