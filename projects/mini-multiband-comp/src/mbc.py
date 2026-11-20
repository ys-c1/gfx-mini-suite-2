"""Two-band (low / high) feed-forward compressor for interleaved float audio.

Pipeline (three stages):
  1. split_mono: complementary one-pole crossover. The low band is an EMA low
     pass, y += a * (x - y), and the high band is hp = x - lp, so
     lp + hp == x algebraically and reconstruction error is only float noise.
  2. compress_band: per-band peak envelope follower (attack/release) feeding a
     static threshold/ratio curve, y = T * (env/T)^(1/R) above threshold.
  3. merge + peak check: bands are summed back per channel; the output peak and
     full-scale overload (peak > 1.0) are reported in the stats dict.

multiband_compress(..., enabled=False) bypasses every stage and returns a copy
of the input, bit for bit, so disabling the split matches the current output.
"""

import math

FULL_SCALE = 1.0


def crossover_coeff(cutoff, sr):
    """EMA one-pole coefficient a = 1 - exp(-2*pi*cutoff/sr)."""
    if not (0.0 < cutoff < sr / 2.0):
        raise ValueError(
            "cutoff must lie strictly inside (0, nyquist=%.6g), got %r"
            % (sr / 2.0, cutoff))
    return 1.0 - math.exp(-2.0 * math.pi * cutoff / sr)


def validate_edges(edges, sr):
    """Crossover edges must be strictly ascending and inside (0, nyquist).

    An inverted boundary (e.g. [800, 200]), a duplicate, or anything outside
    the open interval raises ValueError. Returns the edges as floats.
    """
    edges = list(edges)
    if not edges:
        raise ValueError("at least one crossover edge is required")
    if sr <= 0.0:
        raise ValueError("sample rate must be > 0")
    prev = 0.0
    for e in edges:
        e = float(e)
        if not (0.0 < e < sr / 2.0):
            raise ValueError(
                "band edge %r outside (0, nyquist=%.6g)" % (e, sr / 2.0))
        if e <= prev:
            raise ValueError(
                "band edges must be strictly ascending, got %r" % (edges,))
        prev = e
    return edges


def split_mono(samples, edges, sr):
    """Split a mono signal into len(edges)+1 complementary bands.

    Each stage low-passes the remainder and takes the high band as the
    complementary remainder, so the bands always sum back to the input.
    """
    edges = validate_edges(edges, sr)
    bands = []
    remainder = list(samples)
    for e in edges:
        a = crossover_coeff(e, sr)
        lp = []
        y = 0.0
        for s in remainder:
            y += a * (s - y)
            lp.append(y)
        hp = [s - y for s, y in zip(remainder, lp)]
        bands.append(lp)
        remainder = hp
    bands.append(remainder)
    return bands


def _time_coef(tau, sr):
    # tau == 0 means instant tracking: coefficient 1.0. On attack this jumps
    # env to the signal level; on release (env -= kr*(env - mag)) it jumps
    # env down to mag. Both stay bounded because the release update subtracts.
    if tau < 0.0:
        raise ValueError("time constants must be >= 0")
    return 1.0 if tau == 0.0 else 1.0 - math.exp(-1.0 / (tau * sr))


def compress_band(band, threshold, ratio, attack=0.005, release=0.05,
                  sr=48000.0, makeup=1.0):
    """Compress one mono band. Returns (processed_band, min_gain_factor).

    gain = 1 below threshold; (env/T)^(1/R - 1) above it. ratio must be > 0
    because 1/R is undefined at zero. min_gain_factor excludes makeup and is
    1.0 when nothing was compressed.
    """
    if threshold <= 0.0:
        raise ValueError("threshold must be > 0, got %r" % (threshold,))
    if ratio <= 0.0:
        raise ValueError("ratio must be > 0, got %r (zero ratio is invalid)"
                         % (ratio,))
    if makeup < 0.0:
        raise ValueError("makeup must be >= 0")
    ka = _time_coef(attack, sr)
    kr = _time_coef(release, sr)
    out = []
    env = 0.0
    min_gain = 1.0
    for s in band:
        mag = abs(s)
        if mag > env:
            env += ka * (mag - env)
        else:
            # release: pull the envelope DOWN toward the signal magnitude
            env -= kr * (env - mag)
        if env <= threshold:
            g = 1.0
        else:
            g = (env / threshold) ** (1.0 / ratio - 1.0)
        if g < min_gain:
            min_gain = g
        out.append(s * g * makeup)
    return out, min_gain


def gain_reduction_db(min_gain):
    """Positive dB number for a min gain factor <= 1.0."""
    if min_gain >= 1.0:
        return 0.0
    return -20.0 * math.log10(min_gain)


def _deinterleave(samples, channels):
    return [samples[c::channels] for c in range(channels)]


def _interleave(chan_signals):
    channels = len(chan_signals)
    frames = len(chan_signals[0]) if channels else 0
    out = [0.0] * (frames * channels)
    for c, sig in enumerate(chan_signals):
        out[c::channels] = sig
    return out


def _peak_stats(out, channels):
    per_channel = []
    for c in range(channels):
        seg = out[c::channels]
        per_channel.append(max((abs(v) for v in seg), default=0.0))
    peak = max(per_channel, default=0.0)
    return {
        "peak": peak,
        "per_channel_peak": per_channel,
        "overload": peak > FULL_SCALE,
    }


def multiband_compress(samples, channels, sr, *, enabled=True,
                       cutoff=200.0, low_thr=0.5, low_ratio=4.0,
                       high_thr=0.5, high_ratio=4.0,
                       attack=0.005, release=0.05, makeup=1.0):
    """Run the full split / compress / merge pipeline on interleaved samples.

    Returns (out_samples, stats). With enabled=False the input is copied
    through untouched (identical floats) and only peak stats are measured.
    Raises ValueError on an inverted/out-of-range crossover boundary, a zero
    (or negative) ratio, or a sample count not divisible by the channel count.
    """
    if channels < 1:
        raise ValueError("channels must be >= 1, got %r" % (channels,))
    if len(samples) % channels != 0:
        raise ValueError(
            "sample count %d does not match channel count %d"
            % (len(samples), channels))
    if sr <= 0.0:
        raise ValueError("sample rate must be > 0")

    samples = list(samples)
    if not enabled:
        out = list(samples)
        stats = _peak_stats(out, channels)
        stats["enabled"] = False
        stats["low_gr_db"] = 0.0
        stats["high_gr_db"] = 0.0
        return out, stats

    validate_edges([cutoff], sr)  # raises on inverted / out-of-range boundary
    min_gains = [1.0, 1.0]
    out_chans = []
    for sig in _deinterleave(samples, channels):
        lp, hp = split_mono(sig, [cutoff], sr)
        lp_c, g_low = compress_band(
            lp, low_thr, low_ratio, attack, release, sr, makeup)
        hp_c, g_high = compress_band(
            hp, high_thr, high_ratio, attack, release, sr, makeup)
        min_gains[0] = min(min_gains[0], g_low)
        min_gains[1] = min(min_gains[1], g_high)
        out_chans.append([a + b for a, b in zip(lp_c, hp_c)])

    out = _interleave(out_chans)
    stats = _peak_stats(out, channels)
    stats["enabled"] = True
    stats["low_min_gain"] = min_gains[0]
    stats["high_min_gain"] = min_gains[1]
    stats["low_gr_db"] = gain_reduction_db(min_gains[0])
    stats["high_gr_db"] = gain_reduction_db(min_gains[1])
    return out, stats
