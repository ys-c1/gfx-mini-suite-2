"""Static gain stage for interleaved float audio."""

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


def protect_peaks(samples, threshold=0.9):
    """Soft peak limiter. Output is strictly inside (-1, 1), the mapping is
    strictly monotonic (so it never creates plateaus of identical samples),
    and |out| <= |in| sample-wise (loudness can only decrease, dB change <= 0).
    threshold must be in [0, 1); threshold == 0 degenerates to x/(1+|x|).
    """
    if threshold < 0:
        raise ValueError("threshold must be >= 0")
    if threshold >= 1:
        raise ValueError("threshold must be < 1")
    headroom = 1.0 - threshold
    out = []
    for s in samples:
        a = abs(s)
        if a <= threshold:
            out.append(s)
        else:
            excess = a - threshold
            y = threshold + headroom * excess / (excess + headroom)
            out.append(y if s > 0 else -y)
    return out
