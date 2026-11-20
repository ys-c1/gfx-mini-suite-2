"""Priority-arbitrated sidechain ducking for mono float audio.

The background bus is gain-reduced according to the highest-priority
voiceover whose per-sample peak level exceeds a threshold.  Gain reduction
is computed feed-forward in dB, clamped to a maximum depth, and smoothed with
separate attack/release one-pole coefficients so a priority handoff between
two talking voice chains cannot step the background gain abruptly.

Conventions
-----------
* Levels and threshold are linear amplitudes (threshold must be > 0).
* Gain reduction is tracked as a positive dB amount; the applied bus gain is
  ``10 ** (-reduction_db / 20)``.
* Higher ``priorities`` values win.  By default chain 0 has the highest
  priority.  Ties are resolved in favour of the earlier chain.
* Attack/release are one-pole time constants: after ``t`` seconds a step
  settles to ``1 - 1/e`` (~63%) of its target.
"""

import math


class SidechainDucker:
    def __init__(self, sample_rate, threshold=0.25, ratio=4.0,
                 max_gain_reduction_db=18.0, attack_ms=5.0,
                 release_ms=120.0, priorities=None):
        if sample_rate <= 0:
            raise ValueError("sample_rate must be > 0")
        if threshold <= 0.0:
            raise ValueError("threshold must be > 0")
        if ratio < 1.0:
            raise ValueError("ratio must be >= 1")
        if max_gain_reduction_db < 0.0:
            raise ValueError("max_gain_reduction_db must be >= 0")
        if attack_ms <= 0.0 or release_ms <= 0.0:
            raise ValueError("attack and release times must be > 0")

        self.sample_rate = sample_rate
        self.threshold = float(threshold)
        self.ratio = float(ratio)
        self.max_gain_reduction_db = float(max_gain_reduction_db)
        self._attack = math.exp(-1.0 / (sample_rate * attack_ms / 1000.0))
        self._release = math.exp(-1.0 / (sample_rate * release_ms / 1000.0))
        self.priorities = None if priorities is None else list(priorities)

        self._gr_db = 0.0   # smoothed gain reduction, positive dB
        self._winner = -1   # chain index driving the current frame

    def reset(self):
        self._gr_db = 0.0
        self._winner = -1

    def target_gain_reduction_db(self, level):
        """Gain computer: reduction for one level, clamped to the max."""
        if level <= self.threshold:
            return 0.0
        excess_db = 20.0 * math.log10(level / self.threshold)
        gr = (1.0 - 1.0 / self.ratio) * excess_db
        return min(gr, self.max_gain_reduction_db)

    def process(self, background, voices):
        """Process one block; state carries across calls for streaming.

        ``background`` and every chain in ``voices`` are equal-length mono
        float lists.  Returns a dict with the ducked bus, per-sample linear
        gain, reduction in dB, the winning chain per sample and handoff
        statistics.
        """
        n = len(background)
        if not voices:
            raise ValueError("at least one voice chain is required")
        for chain in voices:
            if len(chain) != n:
                raise ValueError("all chains must match background length")

        if self.priorities is None:
            # Default: earlier chain wins (descending priority values).
            priorities = list(range(len(voices) - 1, -1, -1))
        else:
            if len(self.priorities) != len(voices):
                raise ValueError("priorities length must match voice count")
            priorities = self.priorities

        output = [0.0] * n
        gains = [0.0] * n
        reduction_db = [0.0] * n
        winners = [-1] * n

        handoffs = 0
        target_jump_max_db = 0.0
        applied_jump_max = 0.0
        prev_winner = self._winner
        prev_target = 0.0
        prev_gain = 1.0

        for i in range(n):
            # 1. Reference selection: highest priority chain above threshold.
            winner = -1
            winner_level = 0.0
            best_priority = None
            for c, chain in enumerate(voices):
                level = abs(chain[i])
                if level > self.threshold and (
                    best_priority is None or priorities[c] > best_priority
                ):
                    best_priority = priorities[c]
                    winner = c
                    winner_level = level

            # 2. Gain computer with depth clamp.
            target = (self.target_gain_reduction_db(winner_level)
                      if winner >= 0 else 0.0)

            # 3. Smooth the reduction: attack into more reduction, release
            #    when backing off.  This bounds the per-sample gain step a
            #    priority handoff can produce.
            coef = self._attack if target > self._gr_db else self._release
            self._gr_db = coef * self._gr_db + (1.0 - coef) * target
            gain = 10.0 ** (-self._gr_db / 20.0)

            if winner >= 0 and prev_winner >= 0 and winner != prev_winner:
                handoffs += 1
                target_jump_max_db = max(
                    target_jump_max_db, abs(target - prev_target))
                applied_jump_max = max(
                    applied_jump_max, abs(gain - prev_gain))

            output[i] = background[i] * gain
            gains[i] = gain
            reduction_db[i] = self._gr_db
            winners[i] = winner
            prev_winner = winner
            prev_target = target
            prev_gain = gain

        self._winner = prev_winner
        return {
            "output": output,
            "gain": gains,
            "reduction_db": reduction_db,
            "winner": winners,
            "max_reduction_db": max(reduction_db, default=0.0),
            "handoffs": handoffs,
            "target_jump_max_db": target_jump_max_db,
            "applied_jump_max": applied_jump_max,
        }


def duck(background, voices, sample_rate, **kwargs):
    """One-shot convenience wrapper around :class:`SidechainDucker`."""
    return SidechainDucker(sample_rate, **kwargs).process(background, voices)
