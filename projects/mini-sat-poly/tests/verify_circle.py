#!/usr/bin/env python3
"""Independent verification of the analytic circle/polygon overlap area.

The C implementation is analytic (Green's theorem).  This script computes the
same quantities by a completely different method -- vertical-slice integration
with scipy.quad -- and compares.  It shares no code with the C side.

Reference formula, for each x with a non-empty vertical section:

    area = integral over x of [ overlap length of (polygon section) and
                                (circle chord) ]

The polygon's vertical section is the union of intervals from its edges, the
circle's is [cx - h, cx + h] with h = sqrt(r^2 - (x-cx)^2).  Both are clamped
to a dense set of breakpoints (all vertex x's, and the x's where the circle
meets a vertex or an edge endpoint), and quad integrates each sub-piece.

Usage: python3 -I tests/verify_circle.py ./build/circle_cli
Exit status 0 iff every case matches within the tolerance.
"""
import math
import subprocess
import sys

# scipy is only present in the user site-packages on the development machine;
# python3 -I must still find it, so re-add the user site directory explicitly.
import site
sys.path.append(site.getusersitepackages())

from scipy.integrate import quad

TOL = 1e-9            # absolute tolerance required by the task
QUAD_EPS = 1e-13


# ---------------------------------------------------------------- geometry
def poly_edges(pts):
    return [(pts[i], pts[(i + 1) % len(pts)]) for i in range(len(pts))]


def x_intervals(pts, x):
    """Closed y-intervals of the polygon's section at x (even-odd rule)."""
    ys = []
    for (x0, y0), (x1, y1) in poly_edges(pts):
        if x0 == x1:
            if x0 == x:
                ys.extend([y0, y1])
            continue
        if min(x0, x1) <= x <= max(x0, x1):
            t = (x - x0) / (x1 - x0)
            ys.append(y0 + t * (y1 - y0))
    ys.sort()
    # even-odd pairing
    out = []
    for i in range(0, len(ys) - 1, 2):
        out.append((ys[i], ys[i + 1]))
    return out


def circle_y(cx, cy, r, x):
    d = r * r - (x - cx) ** 2
    if d <= 0.0:
        return None
    h = math.sqrt(d)
    return (cy - h, cy + h)


def overlap_len(pts, cx, cy, r, x):
    pi = x_intervals(pts, x)
    pc = circle_y(cx, cy, r, x)
    if not pi or pc is None:
        return 0.0
    tot = 0.0
    for a0, a1 in pi:
        lo = max(a0, pc[0])
        hi = min(a1, pc[1])
        if hi > lo:
            tot += hi - lo
    return tot


def breakpoints(pts, cx, cy, r):
    xs = set(p[0] for p in pts)
    if r > 0:
        p0 = pts[0]
        xs.add(cx - r)
        xs.add(cx + r)
        for (x0, y0), (x1, y1) in poly_edges(pts):
            for (px, py) in ((x0, y0), (x1, y1)):
                d = math.hypot(px - cx, py - cy)
                if d > 0 and d <= r:
                    xs.add(cx + (r / d) * (px - cx))
                    xs.add(cx - (r / d) * (px - cx))
            # x where circle meets the edge's line, clipped to the segment
            dx, dy = x1 - x0, y1 - y0
            if dx != 0:
                A = dx * dx + dy * dy
                fx, fy = x0 - cx, y0 - cy
                B = 2 * (fx * dx + fy * dy)
                C = fx * fx + fy * fy - r * r
                disc = B * B - 4 * A * C
                if disc >= 0:
                    for t in ((-B - math.sqrt(disc)) / (2 * A),
                              (-B + math.sqrt(disc)) / (2 * A)):
                        if 0.0 <= t <= 1.0:
                            xs.add(x0 + t * dx)
    lo = min(xs)
    hi = max(xs)
    return sorted(xs), lo, hi


def ref_area(pts, cx, cy, r):
    if r <= 0:
        return 0.0
    xs, lo, hi = breakpoints(pts, cx, cy, r)
    # integrate between consecutive breakpoints, where the section structure
    # is constant; add small margin so we do not miss any support
    total = 0.0
    for a, b in zip(xs, xs[1:]):
        if b <= a:
            continue
        val, err = quad(lambda x: overlap_len(pts, cx, cy, r, x),
                        a, b, epsabs=QUAD_EPS, epsrel=QUAD_EPS,
                        limit=200)
        total += val
    # support can extend only between the breakpoints by construction, but
    # include any interval where the circle protrudes beyond the vertex hull
    return total


def poly_signed_area(pts):
    s = 0.0
    for (x0, y0), (x1, y1) in poly_edges(pts):
        s += x0 * y1 - x1 * y0
    return 0.5 * s


# ---------------------------------------------------------------- harness
def run_c(cli, cases):
    lines = []
    for pts, cx, cy, r in cases:
        flat = " ".join(f"{v!r}" for p in pts for v in p)
        lines.append(f"{len(pts)} {flat} {cx!r} {cy!r} {r!r}")
    out = subprocess.run([cli], input="\n".join(lines) + "\n",
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.strip().splitlines():
        st, cont, area, pa = line.split()
        rows.append((int(st), int(cont), float(area), float(pa)))
    return rows


def main():
    cli = sys.argv[1] if len(sys.argv) > 1 else "./build/circle_cli"

    sq = [(0, 0), (10, 0), (10, 10), (0, 10)]
    star = [(2, 0), (3, 3), (6, 2), (5, 5), (2, 6), (1, 3), (-1, 4), (0, 1)]
    tri = [(0, 0), (7, 0), (2, 5)]
    notch = [(0, 0), (8, 0), (8, 8), (5, 3), (2, 8), (0, 8)]
    cw = [(0, 0), (0, 10), (10, 10), (10, 0)]          # clockwise
    cases = [
        # boundary rows: fully outside, both containments, intersections
        (sq, 20.0, 5.0, 2.0),
        (sq, 5.0, 5.0, 1.0),
        (sq, 5.0, 5.0, 8.0),
        (sq, 10.0, 5.0, 4.0),
        (sq, 0.0, 0.0, 3.0),
        (sq, 5.0, 5.0, 5.0),
        (sq, 5.0, 5.0, 50.0),
        (sq, 12.0, 5.0, 2.0),
        # generic offsets
        (sq, 3.3, 4.7, 2.2),
        (sq, 9.1, 9.9, 3.7),
        (sq, -1.5, 4.0, 3.0),
        (sq, 5.0, -6.0, 7.0),
        (star, 2.5, 2.5, 1.5),
        (star, 2.0, 3.0, 2.0),
        (star, 3.1, 2.9, 0.7),
        (tri, 2.0, 1.0, 1.5),
        (tri, 6.0, 1.0, 2.5),
        (tri, 3.5, 2.0, 4.0),
        (notch, 4.0, 4.0, 2.3),
        (notch, 5.0, 6.0, 3.0),
        (cw, 4.0, 4.0, 3.0),          # clockwise input must give same area
        (cw, 10.0, 10.0, 4.0),
    ]

    rows = run_c(cli, cases)
    worst = 0.0
    bad = 0
    for (pts, cx, cy, r), (st, cont, area, pa) in zip(cases, rows):
        ref = ref_area(pts, cx, cy, r)
        err = abs(area - ref)
        worst = max(worst, err)
        pa_ref = poly_signed_area(pts)
        ok = err <= TOL and abs(pa - pa_ref) <= 1e-9 * max(1.0, abs(pa_ref))
        if not ok:
            bad += 1
        print(f"{'ok ' if ok else 'BAD'} n={len(pts):2d} c=({cx:6.2f},{cy:6.2f}) "
              f"r={r:5.2f} st={st:+d} cont={cont:+d} "
              f"c={area: .12f} ref={ref: .12f} err={err:.3e}")
    print(f"\nworst |analytic - quad reference| = {worst:.3e}  (tol {TOL:.0e})")
    print("PASS" if bad == 0 else f"FAIL ({bad} cases)")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
