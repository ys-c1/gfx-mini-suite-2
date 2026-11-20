"""Gain stage for interleaved float audio.

Two paths are provided:

* :func:`apply_gain` -- the original constant-gain path.  It is untouched and
  its output is bit-for-bit what it always was.
* :func:`loudness_gain` -- a loudness-targeted, time-varying gain with a
  look-ahead peak stage that guarantees the sample peak never exceeds 0 dBFS.

The module is pure Python and dependency free (no numpy / scipy).

Signal model
------------
The gain is built in three steps.

1. **Static normalisation.**  The integrated loudness ``L_int`` of the input
   is its full-length RMS in dBFS.  The global gain is
   ``g_int = target_db - L_int``, clamped to ``[min_gain_db, max_gain_db]``.
   A material 30 dB below the target is lifted right here, and a constant
   offset is always rate-legal, so it costs no slew.

2. **Bounded dynamic trim.**  The short-term loudness ``L_short[k]`` is the
   RMS over a sliding ``window_ms`` window.  The trim corrects the deviation
   from the integrated loudness, ``clamp(L_int - L_short[k], -trim_db,
   +trim_db)``, and is then rate limited to ``max_slew_db_per_s`` (silence
   holds the previous trim, so it never pumps up noise).  ``trim_db`` bounds
   the dynamics the correction may remove and is the structural bound on the
   loudness error: without peak protection, ``target - trim_db <= L_out <=
   target + trim_db``, and the peak stage can only lower the level further.

3. **Peak stage.**  The ceiling is ``ceiling_db <= 0``.  The largest gain a
   sample tolerates is ``safe[k] = ceiling_db - 20*log10(|x[k]|)``.  The
   peak gain may duck at ``peak_attack_db_per_s`` and recover at
   ``peak_release_db_per_s``, never above the loudness gain, and its descent
   is anticipated by the look-ahead cap

       cap[k] = min over i in [k, k+L] of (safe[i] + attack * (i - k))

   (O(N), monotonic deque).  ``cap`` is the highest gain at ``k`` from which
   the attack rate can still reach every peak within ``L`` samples; a
   transient is therefore ducked *before* it is heard.

Gain-rate hierarchy
-------------------
Every applied gain curve is bounded sample by sample, and the bound that
matters is the largest one:

* loudness trim      ``|d trim| <= max_slew_db_per_s / fs``     (default 12 dB/s)
* peak duck          ``|d g|     <= peak_attack_db_per_s / fs`` (default 1200 dB/s)
* peak recovery      ``|d g|     <= peak_release_db_per_s / fs``(default 80 dB/s)

so the applied gain never moves faster than ``peak_attack_db_per_s``
(validated: ``max_slew <= attack`` and ``release <= attack``).  The split is
deliberate: loudness corrections move slowly, while peak protection needs a
fast duck.  ``LoudnessGainResult.max_gain_rate_db_per_s`` reports the largest
rate actually observed.

Look-ahead vs overshoot
-----------------------
Over a horizon of ``L`` samples the stage can duck at most ``attack * L`` dB
*before* the peak.  If a transient needs more attenuation than that, the
excess cannot be reached in time and the back-stop clamp removes it, so

    overshoot(L) = max(0, required_duck - attack * L)

and the number of clipped samples falls monotonically to 0 once
``attack * L >= required_duck``.  ``report_normalize.py`` measures this on
the transient material: 1.055 dB at L = 0, 0.455 dB at L = 0.5 ms
(1200 dB/s * 0.5 ms = 0.6 dB), 0 dB for L >= 1 ms.

Look-ahead and delay
--------------------
The horizon is ``L = round(lookahead_ms * fs / 1000)`` samples.  It costs
exactly ``L`` samples of latency: ``y`` is ``x`` delayed by ``L`` (the first
``L`` samples are ``0.0``), and for every ``k``

    y[L + k] == x[k] * 10 ** (gain_db[k] / 20)

exactly, with no compensation by anybody else.  ``result.aligned`` (or
:func:`delay_compensate`) removes the delay.

Determinism
-----------
``window_ms == 0`` disables the dynamic trim (the static gain still applies),
``target_db == 0`` is a valid unity-loudness target, and an all-zero or empty
input yields all-zero output (plus delay), gain 0 dB, and no numeric NaNs.
Every step is plain IEEE double arithmetic in a fixed order, so repeated runs
are bit-identical.
"""

import math
from collections import deque

# Defaults tuned for speech/music material at 48 kHz.
DEFAULT_FS = 48000
DEFAULT_TARGET_DB = -23.0
DEFAULT_WINDOW_MS = 400.0
DEFAULT_LOOKAHEAD_MS = 20.0
DEFAULT_SLEW_DB_PER_S = 12.0
DEFAULT_TRIM_DB = 6.0
DEFAULT_CEILING_DB = 0.0
DEFAULT_MIN_GAIN_DB = -60.0
DEFAULT_MAX_GAIN_DB = 40.0
# The peak stage may duck much faster than the loudness trim may move; that
# asymmetry is what lets a 20 ms look-ahead absorb a transient.
DEFAULT_PEAK_ATTACK_DB_PER_S = 1200.0
DEFAULT_PEAK_RELEASE_DB_PER_S = 80.0
# Overshoots smaller than this are floating-point dust around the ceiling (a
# sample whose peak is exactly 1.0), not a failure of the rate limiter.
CLIP_EPS_DB = 1e-9


# ---------------------------------------------------------------------------
# Original constant-gain path (do not change: results must stay identical)
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def amplitude_to_db(amplitude):
    return 20.0 * math.log10(amplitude) if amplitude > 0.0 else float("-inf")


def _db_from_power(power):
    if power <= 0.0:
        return None
    return 10.0 * math.log10(power)


def _window_samples(window_ms, fs):
    if window_ms <= 0.0:
        return 0
    return max(1, int(round(window_ms * fs / 1000.0)))


def _lookahead_samples(lookahead_ms, fs):
    if lookahead_ms <= 0.0:
        return 0
    return max(1, int(round(lookahead_ms * fs / 1000.0)))


def _clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def sliding_rms_db(samples, fs=DEFAULT_FS, window_ms=DEFAULT_WINDOW_MS):
    """Short-term loudness, one dBFS value per sample.

    The window is causal: samples ``k-W+1 .. k``.  ``window_ms == 0``
    measures the instantaneous sample.  ``None`` is returned where the window
    holds no energy (pure silence).
    """
    n = len(samples)
    out = [None] * n
    if n == 0:
        return out
    w = _window_samples(window_ms, fs)
    if w == 0:
        for k, x in enumerate(samples):
            out[k] = _db_from_power(x * x)
        return out
    acc = 0.0
    for k, x in enumerate(samples):
        acc += x * x
        if k >= w:
            old = samples[k - w]
            acc -= old * old
        count = k + 1 if k + 1 < w else w
        out[k] = _db_from_power(acc / count)
    return out


def slew_limit(values, max_step, initial=None):
    """Rate-limit a dB curve by ``max_step`` dB per sample (symmetric)."""
    n = len(values)
    if n == 0:
        return []
    out = [0.0] * n
    prev = values[0] if initial is None else initial
    out[0] = prev
    for k in range(1, n):
        v = values[k]
        lo = prev - max_step
        hi = prev + max_step
        if v < lo:
            prev = lo
        elif v > hi:
            prev = hi
        else:
            prev = v
        out[k] = prev
    return out


def lookahead_cap(safe_db, horizon, attack_step):
    """Highest gain that can still duck to every ``safe_db`` within reach.

    ``out[k] = min_{i in [k, k+horizon]} (safe_db[i] + attack_step*(i-k))``.

    ``attack_step`` is the per-sample peak-attack rate (dB/sample).  O(N) with
    a monotonic deque over ``safe_db[i] + attack_step*i``.
    """
    n = len(safe_db)
    if n == 0:
        return []
    out = [0.0] * n
    dq = deque()
    offset = attack_step
    for k in range(n - 1, -1, -1):
        v = safe_db[k] + offset * k
        while dq and dq[-1][1] >= v:
            dq.pop()
        dq.append((k, v))
        limit = k + horizon
        while dq[0][0] > limit:
            dq.popleft()
        out[k] = dq[0][1] - offset * k
    return out


def _max_rate(series_db, fs):
    """Largest absolute dB/second change between neighbouring samples."""
    rate = 0.0
    for k in range(1, len(series_db)):
        d = abs(series_db[k] - series_db[k - 1]) * fs
        if d > rate:
            rate = d
    return rate


def _apply_with_ceiling(samples, gain_db, ceiling_lin):
    """Multiply, clamp to the ceiling, and report the clamp precisely.

    Returns ``(body, gain_applied_db, clips, over_db)``.  ``gain_applied_db``
    is the gain the output actually carries: equal to ``gain_db`` unless the
    back-stop lowered a sample that the rate-limited stage could not reach.
    """
    n = len(samples)
    body = [0.0] * n
    applied = list(gain_db)
    ceiling_db = 20.0 * math.log10(ceiling_lin)
    clips = 0
    over = 0.0
    for k in range(n):
        x = samples[k]
        v = x * 10.0 ** (gain_db[k] / 20.0)
        if v > ceiling_lin:
            d = 20.0 * math.log10(v) - ceiling_db
            if d > over:
                over = d
            # The gain that would put this sample exactly at the ceiling.
            applied[k] = ceiling_db - amplitude_to_db(abs(x))
            v = ceiling_lin
            if d > CLIP_EPS_DB:
                # More than floating-point dust: the rate-limited stage
                # genuinely could not reach the required gain in time.
                clips += 1
        elif v < -ceiling_lin:
            d = 20.0 * math.log10(-v) - ceiling_db
            if d > over:
                over = d
            applied[k] = ceiling_db - amplitude_to_db(abs(x))
            v = -ceiling_lin
            if d > CLIP_EPS_DB:
                clips += 1
        body[k] = v
    if over <= CLIP_EPS_DB:
        over = 0.0
    return body, applied, clips, over


# ---------------------------------------------------------------------------
# Time-varying loudness gain with look-ahead
# ---------------------------------------------------------------------------

class LoudnessGainResult:
    """Result of :func:`loudness_gain`.

    Attributes
    ----------
    y : list[float]
        Output, ``len(x) + latency`` samples.  The input is delayed by
        ``latency``; the first ``latency`` samples are exactly ``0.0``, and
        ``y[latency + k] == x[k] * 10**(gain_db[k]/20)``.
    gain_db : list[float]
        Effective gain (dB) applied to each input sample (``len(x)``).
    latency : int
        Look-ahead delay in samples.
    integrated_loudness_in_db : float
        Full-length RMS of the input in dBFS.
    global_gain_db : float
        Static gain (``target - integrated loudness``, clamped).
    trim_peak_db : float
        Largest dynamic trim actually applied, in dB.
    ceiling_clips : int
        Samples caught by the back-stop clamp.  This is a pure arithmetic
        matter: a sample whose full-scale peak is exactly 1.0 cannot be
        attenuated (it is already at the ceiling), and is counted here.
        0 means the gain stage alone held every sample inside the ceiling.
    max_ceiling_overshoot_db : float
        Largest pre-clamp excursion above the ceiling, in dB.
    loudness_deviation_db : float
        Integrated output loudness (full-length RMS of the delay-compensated
        output) minus the target.
    peak_attenuation_db : float
        Largest *extra* attenuation the peak stage added on top of the
        loudness gain, in dB.  0.0 means peak protection never had to reduce
        the loudness gain on this material.
    max_gain_rate_db_per_s : float
        Largest observed ``|gain_db[k] - gain_db[k-1]| * fs`` of the
        rate-limited gain curve *before* the back-stop clamp.  Bounded by
        ``peak_attack_db_per_s`` (see "Gain-rate hierarchy" in the module
        docstring).
    max_applied_rate_db_per_s : float
        Same, after the back-stop clamp.  Can exceed the bound only where
        ``ceiling_clips > 0`` (a clamped sample next to quiet neighbours is an
        unavoidable step).
    loudness_bound_db : float
        Structural bound on the integrated deviation:
        ``|loudness_deviation_db| <= trim_db + peak_attenuation_db``.  The
        upper side is simply ``trim_db`` (peak protection only lowers the
        level); the lower side grows when protection must duck beyond the
        trim budget.
    """

    __slots__ = ("y", "gain_db", "latency", "target_db",
                 "integrated_loudness_in_db", "global_gain_db", "trim_peak_db",
                 "ceiling_clips", "max_ceiling_overshoot_db",
                 "loudness_deviation_db", "peak_attenuation_db",
                 "max_gain_rate_db_per_s", "max_applied_rate_db_per_s",
                 "loudness_bound_db")

    def __init__(self, target_db, bound_db):
        self.y = []
        self.gain_db = []
        self.latency = 0
        self.target_db = target_db
        self.integrated_loudness_in_db = float("-inf")
        self.global_gain_db = 0.0
        self.trim_peak_db = 0.0
        self.ceiling_clips = 0
        self.max_ceiling_overshoot_db = 0.0
        self.loudness_deviation_db = float("-inf")
        self.peak_attenuation_db = 0.0
        self.max_gain_rate_db_per_s = 0.0
        self.max_applied_rate_db_per_s = 0.0
        self.loudness_bound_db = bound_db

    @property
    def aligned(self):
        """Output with the look-ahead delay removed: length ``len(x)``."""
        return self.y[self.latency:]

    def __repr__(self):
        return ("LoudnessGainResult(n=%d, latency=%d, clips=%d, over=%.3f dB,"
                " dev=%.3f dB, rate=%.2f dB/s)"
                % (len(self.y), self.latency, self.ceiling_clips,
                   self.max_ceiling_overshoot_db, self.loudness_deviation_db,
                   self.max_gain_rate_db_per_s))


def delay_compensate(samples, latency):
    """Drop the first ``latency`` samples of a look-ahead stage's output."""
    if latency < 0:
        raise ValueError("latency must be >= 0")
    return list(samples[latency:])


def loudness_gain(samples, target_db=DEFAULT_TARGET_DB, fs=DEFAULT_FS,
                  window_ms=DEFAULT_WINDOW_MS,
                  lookahead_ms=DEFAULT_LOOKAHEAD_MS,
                  max_slew_db_per_s=DEFAULT_SLEW_DB_PER_S,
                  trim_db=DEFAULT_TRIM_DB,
                  ceiling_db=DEFAULT_CEILING_DB,
                  min_gain_db=DEFAULT_MIN_GAIN_DB,
                  max_gain_db=DEFAULT_MAX_GAIN_DB,
                  peak_attack_db_per_s=DEFAULT_PEAK_ATTACK_DB_PER_S,
                  peak_release_db_per_s=DEFAULT_PEAK_RELEASE_DB_PER_S):
    """Apply a loudness-targeted, rate-limited, look-ahead gain.

    Parameters
    ----------
    samples : sequence of float
        Mono input (one channel; ``apply_gain`` remains the constant-gain
        path for interleaved buffers).
    target_db : float
        Desired integrated loudness in dBFS.  ``0.0`` is valid and tested.
    fs : int
        Sample rate in Hz.
    window_ms : float
        RMS window of the short-term loudness; ``0.0`` disables the dynamic
        trim (the static gain still applies).
    lookahead_ms : float
        Look-ahead horizon.  ``0`` disables pre-ducking: the delay is then
        0 and the rate-limited stage cannot anticipate a transient, so the
        back-stop clamp acts and ``max_ceiling_overshoot_db`` reports it.
    max_slew_db_per_s : float
        Rate limit of the dynamic trim.
    trim_db : float
        Maximum dynamic trim away from the static gain; also the structural
        loudness-deviation bound.
    ceiling_db : float
        Peak ceiling, must be ``<= 0`` (0 dBFS or lower).
    min_gain_db, max_gain_db : float
        Clamp of the static gain.
    peak_attack_db_per_s, peak_release_db_per_s : float
        Rate limits of the peak stage (duck / recover).

    Returns
    -------
    LoudnessGainResult
    """
    if fs <= 0:
        raise ValueError("fs must be > 0")
    if not math.isfinite(target_db):
        raise ValueError("target_db must be finite")
    if window_ms < 0.0:
        raise ValueError("window_ms must be >= 0")
    if lookahead_ms < 0.0:
        raise ValueError("lookahead_ms must be >= 0")
    if max_slew_db_per_s < 0.0:
        raise ValueError("max_slew_db_per_s must be >= 0")
    if trim_db < 0.0:
        raise ValueError("trim_db must be >= 0")
    if ceiling_db > 0.0:
        raise ValueError("ceiling_db must be <= 0 (never above 0 dBFS)")
    if peak_attack_db_per_s < 0.0 or peak_release_db_per_s < 0.0:
        raise ValueError("peak rates must be >= 0")
    if max_slew_db_per_s > peak_attack_db_per_s:
        raise ValueError("max_slew_db_per_s must be <= peak_attack_db_per_s")
    if peak_release_db_per_s > peak_attack_db_per_s:
        raise ValueError("peak_release_db_per_s must be <= peak_attack_db_per_s")
    if min_gain_db > max_gain_db:
        raise ValueError("min_gain_db must be <= max_gain_db")

    n = len(samples)
    result = LoudnessGainResult(target_db, trim_db)
    if n == 0:
        return result
    latency = _lookahead_samples(lookahead_ms, fs)
    result.latency = latency
    ceiling_lin = 10.0 ** (ceiling_db / 20.0)

    integrated = amplitude_to_db(rms(samples))
    result.integrated_loudness_in_db = integrated
    if integrated == float("-inf"):
        # Defined pure-silence result: 0 dB gain, silence out plus delay.
        result.global_gain_db = 0.0
        result.y = [0.0] * (n + latency)
        result.gain_db = [0.0] * n
        return result

    g_int = _clamp(target_db - integrated, min_gain_db, max_gain_db)
    result.global_gain_db = g_int

    # Dynamic trim from short-term loudness, relative to the integrated
    # loudness, bounded and rate limited.
    w = _window_samples(window_ms, fs)
    trim = [0.0] * n
    if w == 0:
        # An instantaneous "loudness" is just |x[k]|; correcting it towards
        # the target would distort every sample individually.  With a zero
        # window the trim is therefore defined as 0 dB and only the static
        # gain applies.
        short_db = None
    else:
        short_db = sliding_rms_db(samples, fs=fs, window_ms=window_ms)
    if short_db is not None:
        held = 0.0
        for k in range(n):
            v = short_db[k]
            if v is None:
                trim[k] = held
            else:
                held = _clamp(integrated - v, -trim_db, trim_db)
                trim[k] = held
        # Warm-up: until the window is full its mean is dominated by a
        # handful of samples and is not a loudness estimate; hold the first
        # full-window value instead of slewing from a meaningless start.
        first_full = min(w - 1, n - 1)
        for k in range(first_full):
            trim[k] = trim[first_full]
        trim = slew_limit(trim, max_slew_db_per_s / fs)
    result.trim_peak_db = peak(trim)
    desired = [g_int + t for t in trim]

    # Peak stage.
    safe = [0.0] * n
    for k in range(n):
        x = samples[k]
        if x == 0.0:
            safe[k] = max_gain_db      # any gain is safe for a zero sample
        else:
            safe[k] = ceiling_db - amplitude_to_db(abs(x))
    attack_step = peak_attack_db_per_s / fs
    release_step = peak_release_db_per_s / fs
    cap = lookahead_cap(safe, latency, attack_step)
    g_peak = [0.0] * n
    prev = desired[0] if desired[0] < cap[0] else cap[0]
    g_peak[0] = prev
    for k in range(1, n):
        v = cap[k]
        if desired[k] < v:
            v = desired[k]             # never above the loudness gain
        hi = prev + release_step
        if v > hi:
            v = hi                     # recover at the release rate
        lo = prev - attack_step
        if v < lo:
            v = lo                     # duck at the attack rate
        prev = v
        g_peak[k] = v

    # Applied gain, delayed output, and honest clip accounting.
    applied = [0.0] * n
    peak_extra = 0.0
    for k in range(n):
        g = desired[k] if desired[k] < g_peak[k] else g_peak[k]
        applied[k] = g
        d = desired[k] - g
        if d > peak_extra:
            peak_extra = d
    preclamp = list(applied)
    body, applied, clips, over = _apply_with_ceiling(
        samples, applied, ceiling_lin)

    result.y = [0.0] * latency + body
    result.gain_db = applied
    result.ceiling_clips = clips
    result.max_ceiling_overshoot_db = over
    result.peak_attenuation_db = peak_extra
    result.max_gain_rate_db_per_s = _max_rate(preclamp, fs)
    result.max_applied_rate_db_per_s = _max_rate(applied, fs)
    result.loudness_deviation_db = amplitude_to_db(rms(body)) - target_db
    return result


# ---------------------------------------------------------------------------
# Measurement helpers used by tests and the report
# ---------------------------------------------------------------------------

def loudness_db(samples):
    """Integrated loudness of a finished signal: full-length RMS in dBFS."""
    return amplitude_to_db(rms(samples))


def peak_db(samples):
    return amplitude_to_db(peak(samples))
