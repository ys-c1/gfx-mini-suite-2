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
