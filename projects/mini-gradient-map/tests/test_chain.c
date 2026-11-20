#define _POSIX_C_SOURCE 200809L
#include "../src/grad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const int kDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int kDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

/* ================================================================== */
/* Brute-force reference tracer (independent implementation)           */
/* ================================================================== */
/*
 * Shares the rule set with the library but not the code: membership is a
 * local map, degrees are recomputed from the mask on demand, and the
 * canonical direction is picked by materialising both full candidate
 * sequences and comparing them, rather than by reversing in place.
 */

typedef struct { int x, y; } RP;
typedef struct { RP* p; int len; int closed; } RChain;

static int r_deg(const unsigned char* m, int w, int h, int x, int y) {
    int n = 0;
    for (int d = 0; d < 8; ++d) {
        int nx = x + kDx[d], ny = y + kDy[d];
        if (nx >= 0 && nx < w && ny >= 0 && ny < h && m[ny * w + nx]) ++n;
    }
    return n;
}

static int r_step(int dir, const int* cand, int nc) {
    if (dir < 0) {
        int best = cand[0];
        for (int i = 1; i < nc; ++i) if (cand[i] < best) best = cand[i];
        return best;
    }
    int best = -1, best_turn = 99, best_cw = 99;
    for (int i = 0; i < nc; ++i) {
        int d = cand[i];
        int cw = (d - dir + 8) % 8;
        int turn = cw <= 4 ? cw : 8 - cw;
        if (turn < best_turn || (turn == best_turn && cw < best_cw)) {
            best = d; best_turn = turn; best_cw = cw;
        }
    }
    return best;
}

static void r_reverse(RP* p, int n) {
    for (int i = 0, j = n - 1; i < j; ++i, --j) { RP t = p[i]; p[i] = p[j]; p[j] = t; }
}

static int r_cmp(const RP* a, const RP* b, int n) {
    for (int i = 0; i < n; ++i) {
        if (a[i].y != b[i].y) return a[i].y < b[i].y ? -1 : 1;
        if (a[i].x != b[i].x) return a[i].x < b[i].x ? -1 : 1;
    }
    return 0;
}

static void r_canonicalize(RP* p, int n, int closed, RP* tmp) {
    if (!closed) {
        memcpy(tmp, p, (size_t)n * sizeof(RP));
        r_reverse(tmp, n);
        if (r_cmp(tmp, p, n) < 0) memcpy(p, tmp, (size_t)n * sizeof(RP));
        return;
    }
    int m = 0;
    for (int i = 1; i < n; ++i)
        if (p[i].y < p[m].y || (p[i].y == p[m].y && p[i].x < p[m].x)) m = i;
    RP* a = tmp;
    RP* b = tmp + n;
    for (int i = 0; i < n; ++i) {
        a[i] = p[(m + i) % n];
        b[i] = p[(m - i + n) % n];
    }
    if (r_cmp(b, a, n) < 0) memcpy(p, b, (size_t)n * sizeof(RP));
    else memcpy(p, a, (size_t)n * sizeof(RP));
}

static int r_chainize(const unsigned char* mask, int w, int h, RChain** out) {
    int n = w * h, edges = 0;
    for (int i = 0; i < n; ++i) if (mask[i]) ++edges;
    *out = NULL;
    if (edges == 0) return 0;

    unsigned char* used = (unsigned char*)calloc((size_t)n, 1);
    RChain* chains = (RChain*)calloc((size_t)edges, sizeof(RChain));
    RP* tmp = (RP*)malloc((size_t)edges * 2 * sizeof(RP));
    if (!used || !chains || !tmp) { free(used); free(chains); free(tmp); return -1; }

    int nc = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (!mask[y * w + x] || used[y * w + x]) continue;
                if (pass == 0 && r_deg(mask, w, h, x, y) != 1) continue;
                RP* p = (RP*)malloc((size_t)edges * sizeof(RP));
                int len = 0, closed = 0, dir = -1, px = x, py = y;
                used[py * w + px] = 1;
                p[len++] = (RP){ px, py };
                for (;;) {
                    int cand[8], k = 0;
                    for (int d = 0; d < 8; ++d) {
                        int nx = px + kDx[d], ny = py + kDy[d];
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                        if (mask[ny * w + nx] && !used[ny * w + nx]) cand[k++] = d;
                    }
                    if (k == 0) {
                        int adj = (px != x || py != y) &&
                                  px >= x - 1 && px <= x + 1 &&
                                  py >= y - 1 && py <= y + 1;
                        int interior_junction = 0;
                        for (int q = 1; q < len - 1; ++q)
                            if (r_deg(mask, w, h, p[q].x, p[q].y) > 2) {
                                interior_junction = 1;
                                break;
                            }
                        closed = adj && len >= 3 && !interior_junction &&
                                 r_deg(mask, w, h, x, y) == 2 &&
                                 r_deg(mask, w, h, px, py) == 2;
                        break;
                    }
                    int d = r_step(dir, cand, k);
                    px += kDx[d]; py += kDy[d]; dir = d;
                    used[py * w + px] = 1;
                    p[len++] = (RP){ px, py };
                }
                r_canonicalize(p, len, closed, tmp);
                chains[nc].p = p;
                chains[nc].len = len;
                chains[nc].closed = closed;
                ++nc;
            }
    }
    free(used);
    free(tmp);
    *out = chains;
    return nc;
}

static void r_free(RChain* chains, int n) {
    if (!chains) return;
    for (int i = 0; i < n; ++i) free(chains[i].p);
    free(chains);
}

/* ================================================================== */
/* Comparison and invariants                                           */
/* ================================================================== */

static int check_against_reference(const unsigned char* mask, int w, int h,
                                   const char* what) {
    int n = w * h, edges = 0;
    for (int i = 0; i < n; ++i) if (mask[i]) ++edges;

    int cap = edges ? edges : 1;
    GradPoint* pts = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradChain* ch = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    size_t n_alloc = (size_t)w * (size_t)h;
    unsigned char* seen = (unsigned char*)calloc(n_alloc ? n_alloc : 1, 1);
    RChain* rc = NULL;

    int got = grad_chainize(mask, w, h, pts, edges, ch, edges);
    int want = r_chainize(mask, w, h, &rc);
    CHECK(got == want);
    CHECK(got >= 0);
    if (got != want || got < 0) {
        free(pts); free(ch); free(seen); r_free(rc, want);
        return got;
    }

    int bad = 0, off = 0;
    for (int i = 0; i < got; ++i) {
        const RChain* r = &rc[i];
        if (ch[i].offset != off || ch[i].len != r->len ||
            ch[i].closed != r->closed) { bad = 1; break; }
        if (ch[i].len < 1) bad = 1;
        for (int k = 0; k < ch[i].len; ++k) {
            GradPoint p = pts[ch[i].offset + k];
            if (p.x != r->p[k].x || p.y != r->p[k].y) { bad = 1; break; }
            if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) { bad = 1; break; }
            int idx = p.y * w + p.x;
            if (!mask[idx] || seen[idx]) { bad = 1; break; }
            seen[idx] = 1;
            if (k > 0) {
                int dx = p.x - pts[ch[i].offset + k - 1].x;
                int dy = p.y - pts[ch[i].offset + k - 1].y;
                if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) bad = 1;
            }
        }
        if (bad) break;
        if (ch[i].closed) {
            if (ch[i].len < 3) bad = 1;
            else {
                GradPoint a = pts[ch[i].offset + ch[i].len - 1];
                GradPoint b = pts[ch[i].offset];
                int dx = a.x - b.x, dy = a.y - b.y;
                if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) bad = 1;
            }
        }
        off += ch[i].len;
    }
    CHECK(!bad);
    if (bad) printf("  mismatch in %s (%dx%d)\n", what, w, h);
    CHECK(off == edges);

    int covered = 0;
    for (int i = 0; i < n; ++i) if (mask[i] && seen[i]) ++covered;
    CHECK(covered == edges);

    free(pts); free(ch); free(seen);
    r_free(rc, want);
    return got;
}

/* Chainize twice: output must be byte-identical. */
static void check_deterministic(const unsigned char* mask, int w, int h) {
    int n = w * h, edges = 0;
    for (int i = 0; i < n; ++i) if (mask[i]) ++edges;
    int cap = edges ? edges : 1;
    GradPoint* p1 = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradPoint* p2 = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradChain* c1 = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    GradChain* c2 = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    int n1 = grad_chainize(mask, w, h, p1, edges, c1, edges);
    int n2 = grad_chainize(mask, w, h, p2, edges, c2, edges);
    CHECK(n1 == n2);
    if (n1 > 0) {
        int total = 0;
        for (int i = 0; i < n1; ++i) total += c1[i].len;
        CHECK(memcmp(p1, p2, (size_t)total * sizeof(GradPoint)) == 0);
        CHECK(memcmp(c1, c2, (size_t)n1 * sizeof(GradChain)) == 0);
    }
    free(p1); free(p2); free(c1); free(c2);
}

/* Translating the mask must translate the chains without reversing them. */
static void check_translation(const unsigned char* mask, int w, int h,
                              int ox, int oy) {
    int n = w * h, edges = 0;
    for (int i = 0; i < n; ++i) if (mask[i]) ++edges;
    int W = w + ox, H = h + oy;
    unsigned char* big = (unsigned char*)calloc((size_t)W * H, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (mask[y * w + x]) big[(y + oy) * W + (x + ox)] = 255;

    int cap = edges ? edges : 1;
    GradPoint* pa = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradPoint* pb = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradChain* ca = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    GradChain* cb = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    int na = grad_chainize(mask, w, h, pa, edges, ca, edges);
    int nb = grad_chainize(big, W, H, pb, edges, cb, edges);
    CHECK(na == nb);
    int same = (na == nb);
    if (same && na > 0) {
        for (int i = 0; i < na && same; ++i) {
            if (ca[i].len != cb[i].len || ca[i].closed != cb[i].closed) {
                same = 0;
                break;
            }
            for (int k = 0; k < ca[i].len; ++k) {
                GradPoint a = pa[ca[i].offset + k];
                GradPoint b = pb[cb[i].offset + k];
                if (a.x + ox != b.x || a.y + oy != b.y) { same = 0; break; }
            }
        }
    }
    CHECK(same);
    free(big); free(pa); free(pb); free(ca); free(cb);
}

/* ================================================================== */
/* Fixed cases                                                         */
/* ================================================================== */

static unsigned char* mask_from_rows(int w, int h, const char* const* rows) {
    unsigned char* m = (unsigned char*)calloc((size_t)w * h, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; rows[y][x]; ++x)
            if (rows[y][x] == '#') m[y * w + x] = 255;
    return m;
}

static void test_empty(void) {
    enum { W = 16, H = 16 };
    unsigned char m[W * H] = { 0 };
    GradPoint p[W * H];
    GradChain c[W * H];
    CHECK(grad_chainize(m, W, H, p, W * H, c, W * H) == 0);
    CHECK(grad_chainize(m, W, H, p, 0, c, 0) == 0);
    check_against_reference(m, W, H, "empty");
    check_deterministic(m, W, H);
}

static void test_single_point(void) {
    enum { W = 7, H = 5 };
    for (int pos = 0; pos < 4; ++pos) {
        int px = (pos & 1) ? W - 1 : 0;
        int py = (pos & 2) ? H - 1 : 0;
        unsigned char m[W * H] = { 0 };
        GradPoint p[W * H];
        GradChain c[W * H];
        m[py * W + px] = 255;
        CHECK(grad_chainize(m, W, H, p, W * H, c, W * H) == 1);
        CHECK(c[0].len == 1 && c[0].closed == 0);
        CHECK(p[0].x == px && p[0].y == py);
        check_against_reference(m, W, H, "single point");
        check_translation(m, W, H, 3, 2);
    }
}

/* Plus, arms 2: vertical arm owns the centre (straight through); the two
 * horizontal arms stop in front of it.  3 chains. */
static void test_plus_small(void) {
    enum { W = 5, H = 5 };
    static const char* rows[W] = { "..#..", "..#..", "#####", "..#..", "..#.." };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 3);
    CHECK(c[0].len == 5 && c[0].closed == 0);
    CHECK(p[c[0].offset].x == 2 && p[c[0].offset].y == 0);
    CHECK(p[c[0].offset + 4].x == 2 && p[c[0].offset + 4].y == 4);
    CHECK(c[1].len == 2 && p[c[1].offset].x == 0 && p[c[1].offset].y == 2);
    CHECK(c[2].len == 2 && p[c[2].offset].x == 3 && p[c[2].offset].y == 2);
    check_against_reference(m, W, H, "plus (small)");
    check_translation(m, W, H, 3, 2);
    free(m);
}

/* Plus, arms 4: 3 chains, vertical 9 points, arms 4 points each. */
static void test_plus_large(void) {
    enum { W = 9, H = 9 };
    static const char* rows[W] = {
        "....#....", "....#....", "....#....", "....#....",
        "#########",
        "....#....", "....#....", "....#....", "....#....",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 3);
    CHECK(c[0].len == 9 && c[0].closed == 0);
    CHECK(p[0].x == 4 && p[0].y == 0);
    CHECK(p[8].x == 4 && p[8].y == 8);
    CHECK(c[1].len == 4 && p[c[1].offset].x == 0 && p[c[1].offset].y == 4);
    CHECK(c[2].len == 4 && p[c[2].offset].x == 5 && p[c[2].offset].y == 4);
    check_against_reference(m, W, H, "plus (large)");
    free(m);
}

/* X crossing, two full diagonals meeting in one pixel.  This is the case
 * that decides the disambiguation rule: the diagonal found first in raster
 * order (NW-SE) owns the centre and runs straight; the other two arms stop
 * one pixel short.  No pixel is shared; nothing doubles back. */
static void test_x_cross(void) {
    enum { W = 9, H = 9 };
    static const char* rows[W] = {
        "#.......#", ".#.....#.", "..#...#..", "...#.#...",
        "....#....",
        "...#.#...", "..#...#..", ".#.....#.", "#.......#",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 3);
    CHECK(c[0].len == 9 && p[0].x == 0 && p[0].y == 0);
    CHECK(p[4].x == 4 && p[4].y == 4);
    CHECK(p[8].x == 8 && p[8].y == 8);
    CHECK(c[1].len == 4 && p[c[1].offset].x == 8 && p[c[1].offset].y == 0);
    CHECK(c[2].len == 4 && p[c[2].offset].x == 3 && p[c[2].offset].y == 5);
    check_against_reference(m, W, H, "x cross");
    free(m);
}

/* T junction: the bar runs straight through, the stem stops in front of it.
 * 3 chains (top stem, bar, bottom stem). */
static void test_t_cross(void) {
    enum { W = 11, H = 11 };
    static const char* rows[W] = {
        "...........", ".....#.....", ".....#.....", ".....#.....",
        "#########..",
        ".....#.....", ".....#.....", ".....#.....", ".....#.....",
        "...........", "...........",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 3);
    /* The vertical stem runs straight through the junction: 8 points.  The
     * bar is split by the already-claimed junction pixel into 5 + 3. */
    CHECK(c[0].len == 8 && p[0].x == 5 && p[0].y == 1);
    CHECK(p[c[0].offset + 7].x == 5 && p[c[0].offset + 7].y == 8);
    CHECK(c[1].len == 5 && p[c[1].offset].x == 0 && p[c[1].offset].y == 4);
    CHECK(c[2].len == 3 && p[c[2].offset].x == 6 && p[c[2].offset].y == 4);
    check_against_reference(m, W, H, "t cross");
    free(m);
}

/* 2x2 block: all four pixels are junctions; one open chain in raster order. */
static void test_block(void) {
    enum { W = 6, H = 6 };
    unsigned char m[W * H] = { 0 };
    GradPoint p[W * H];
    GradChain c[W * H];
    m[2 * W + 2] = m[2 * W + 3] = m[3 * W + 2] = m[3 * W + 3] = 255;
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 1);
    CHECK(c[0].len == 4 && c[0].closed == 0);
    CHECK(p[0].x == 2 && p[0].y == 2);
    check_against_reference(m, W, H, "2x2 block");
    check_translation(m, W, H, 4, 1);
}

/* Isolated ring (octagon): every pixel has exactly two 8-neighbours, so the
 * chain closes.  A 3x3 hollow square does NOT: its edge middles touch four
 * pixels diagonally and are junctions. */
static void test_ring(void) {
    enum { W = 7, H = 5 };
    static const char* rows[W] = {
        "...#...",
        "..#.#..",
        ".#...#.",
        "..#.#..",
        "...#...",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 1);
    CHECK(c[0].len == 8 && c[0].closed == 1);
    CHECK(p[c[0].offset].x == 3 && p[c[0].offset].y == 0);
    /* Two possible neighbours of (3,0): (2,1) and (4,1).  The canonical
     * direction picks the lexicographically smaller of the two sequences,
     * which starts with (2,1). */
    CHECK(p[c[0].offset + 1].x == 2 && p[c[0].offset + 1].y == 1);
    check_against_reference(m, W, H, "octagon ring");
    check_translation(m, W, H, 2, 3);
    free(m);
}

/* Ring with a tail: not closed, one open chain of 10 points. */
static void test_ring_tail(void) {
    enum { W = 8, H = 8 };
    unsigned char m[W * H] = { 0 };
    GradPoint p[W * H];
    GradChain c[W * H];
    for (int x = 2; x <= 4; ++x) m[2 * W + x] = m[4 * W + x] = 255;
    m[3 * W + 2] = m[3 * W + 4] = 255;
    m[1 * W + 2] = m[0 * W + 2] = 255;
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 1);
    CHECK(c[0].closed == 0);
    CHECK(c[0].len == 10);
    CHECK(p[0].x == 2 && p[0].y == 0);
    check_against_reference(m, W, H, "ring+tail");
}

/* 3x3 hollow square: under 8-connectivity the edge middles see four pixels
 * diagonally, so the shape is a junction ring, not a simple cycle.  It is
 * reported as one open chain whose ends are the two raster-last pixels; the
 * point is that it is deterministic and partitioned, not that it closes. */
static void test_hollow_square(void) {
    enum { W = 5, H = 5 };
    static const char* rows[W] = {
        ".....", ".###.", ".#.#.", ".###.", ".....",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 1);
    CHECK(c[0].len == 8 && c[0].closed == 0);
    check_against_reference(m, W, H, "3x3 hollow square");
    check_deterministic(m, W, H);
    free(m);
}

/* Two disjoint octagon rings: two independent closed chains, each canonical. */
static void test_two_rings(void) {
    enum { W = 21, H = 7 };
    unsigned char m[W * H] = { 0 };
    GradPoint p[W * H];
    GradChain c[W * H];
    static const int cx[8] = { 2, 1, 0, 1, 2, 3, 4, 3 };
    static const int cy[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    for (int r = 0; r < 2; ++r) {
        int ox = r ? 11 : 1;
        for (int i = 0; i < 8; ++i) m[cy[i] * W + cx[i] + ox] = 255;
    }
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n == 2);
    CHECK(c[0].len == 8 && c[0].closed == 1);
    CHECK(c[1].len == 8 && c[1].closed == 1);
    CHECK(p[c[0].offset].x == 3 && p[c[0].offset].y == 0);
    CHECK(p[c[1].offset].x == 13 && p[c[1].offset].y == 0);
    check_against_reference(m, W, H, "two rings");
    check_translation(m, W, H, 3, 3);
}

/* A fat ring with a lobe: the lobe is attached at junctions, so the loop and
 * the lobe are all one open chain, and nothing is reported closed.  The
 * general law - a chain is closed exactly when every one of its pixels has
 * degree 2 (an isolated simple cycle) - is checked through the exhaustive
 * 4x4 sweep and the reference comparison; this case pins a shape whose
 * shortest cycles do contain junction pixels. */
static void test_figure_eight(void) {
    enum { W = 9, H = 7 };
    static const char* rows[W] = {
        ".#######.",
        ".#######.",
        "###...###",
        "###...###",
        ".#######.",
        ".#######.",
        ".........",
    };
    unsigned char* m = mask_from_rows(W, H, rows);
    GradPoint p[W * H];
    GradChain c[W * H];
    int n = grad_chainize(m, W, H, p, W * H, c, W * H);
    CHECK(n >= 1);
    for (int i = 0; i < n; ++i) CHECK(c[i].closed == 0);
    check_against_reference(m, W, H, "fat ring with junction cycles");
    check_deterministic(m, W, H);
    free(m);
}

static void test_args(void) {
    enum { W = 4, H = 4 };
    unsigned char m[W * H] = { 0 };
    GradPoint p[W * H];
    GradChain c[W * H];
    m[0] = m[1] = m[W] = m[W + 1] = 255;
    CHECK(grad_chainize(NULL, W, H, p, W * H, c, W * H) == GRAD_ERR_ARG);
    CHECK(grad_chainize(m, 0, H, p, W * H, c, W * H) == GRAD_ERR_ARG);
    CHECK(grad_chainize(m, W, H, NULL, W * H, c, W * H) == GRAD_ERR_ARG);
    CHECK(grad_chainize(m, W, H, p, W * H, NULL, W * H) == GRAD_ERR_ARG);
    CHECK(grad_chainize(m, W, H, p, 3, c, W * H) == GRAD_ERR_CAP);
    CHECK(grad_chainize(m, W, H, p, W * H, c, 3) == GRAD_ERR_CAP);
    int n = grad_chainize(m, W, H, p, 4, c, 4);
    CHECK(n == 1 && c[0].len == 4);
    check_against_reference(m, W, H, "2x2 corner");
}

/* ================================================================== */
/* Hysteresis                                                          */
/* ================================================================== */

static void test_hysteresis(void) {
    enum { W = 9, H = 7 };
    float mag[W * H];
    unsigned char mask[W * H];
    for (int i = 0; i < W * H; ++i) mag[i] = 0.0f;

    mag[2 * W + 2] = 100.0f;  /* seed */
    mag[3 * W + 2] = 50.0f;   /* weak, orthogonal to seed */
    mag[4 * W + 3] = 50.0f;   /* weak, chained */
    mag[5 * W + 7] = 50.0f;   /* weak island: must not survive */
    mag[1 * W + 3] = 40.0f;   /* weak, diagonal to seed */

    CHECK(grad_hysteresis(mag, W, H, 40.0f, 90.0f, mask) == 0);
    CHECK(mask[2 * W + 2] == 255);
    CHECK(mask[3 * W + 2] == 255);
    CHECK(mask[4 * W + 3] == 255);
    CHECK(mask[1 * W + 3] == 255);
    CHECK(mask[5 * W + 7] == 0);

    CHECK(grad_hysteresis(mag, W, H, 50.0f, 50.0f, mask) == 0);
    CHECK(mask[5 * W + 7] == 255);
    CHECK(mask[1 * W + 3] == 0);

    CHECK(grad_hysteresis(NULL, W, H, 0, 1, mask) == GRAD_ERR_ARG);
    CHECK(grad_hysteresis(mag, W, H, 5, 1, mask) == GRAD_ERR_ARG);
    CHECK(grad_hysteresis(mag, W, H, -1, 1, mask) == GRAD_ERR_ARG);
    CHECK(grad_hysteresis(mag, W, H, 0, 1, NULL) == GRAD_ERR_ARG);
}

/* ================================================================== */
/* Fuzz                                                                */
/* ================================================================== */

static unsigned rng_state = 0x12345678u;
static unsigned rng_next(unsigned* state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}
static unsigned rnext(void) { return rng_next(&rng_state); }

/* The library and the reference tracer both encode the step rule as integer
 * ring distances, so a shared misreading ("smallest turn, right wins ties")
 * could pass both.  This check states the rule geometrically instead:
 * compare the incoming direction with each candidate using the dot product
 * (cosine of the turn) and choose the largest; when two candidates are
 * exactly tied, choose the one whose cross product is positive (to the right
 * of the incoming heading, screen y down).  Exhaustive over all 8 incoming
 * directions and all 256 free-neighbour subsets. */
static void test_step_rule_geometry(void) {
    const int dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    const int dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    int bad = 0;
    for (int dir = 0; dir < 8; ++dir)
        for (int subset = 1; subset < 256; ++subset) {
            int cand[8], nc = 0;
            for (int d = 0; d < 8; ++d)
                if ((subset >> d) & 1) cand[nc++] = d;
            int got = r_step(dir, cand, nc);

            /* Incoming heading is the vector just travelled: direction 'dir'
             * means we moved along (dx[dir], dy[dir]).  Rank candidates by
             * the true angle between the heading and the step (normalised
             * cosine, so diagonals are not favoured by their length). */
            int want = cand[0];
            double want_cos = -2.0;
            int want_right = 0;
            for (int i = 0; i < nc; ++i) {
                int c = cand[i];
                double dot = (double)dx[dir] * dx[c] + (double)dy[dir] * dy[c];
                double la = sqrt((double)dx[dir] * dx[dir] + (double)dy[dir] * dy[dir]);
                double lb = sqrt((double)dx[c] * dx[c] + (double)dy[c] * dy[c]);
                double cosang = dot / (la * lb);
                /* cross > 0 => candidate is clockwise of the heading on
                 * screen (y down), i.e. the right-hand turn. */
                int cross = dx[dir] * dy[c] - dy[dir] * dx[c];
                int right = cross > 0;
                if (cosang > want_cos + 1e-12 ||
                    (fabs(cosang - want_cos) <= 1e-12 && right && !want_right)) {
                    want = c;
                    want_cos = cosang;
                    want_right = right;
                }
            }
            if (got != want) {
                if (bad < 5)
                    printf("  step rule differs: dir=%d subset=%02x got=%d want=%d\n",
                           dir, subset, got, want);
                ++bad;
            }
        }
    CHECK(bad == 0);
}

/* Exhaustive sweep over every 4x4 binary mask (65536 of them).  This is the
 * strongest form of the "no pixel twice / no pixel lost" check: it is not
 * sampling, it is every possible 8-neighbour configuration that fits in a
 * small window.
 *
 * It also pins the figure-eight invariant.  Two loops that meet at a pixel
 * make that pixel degree 4, and a closed chain needs degree 2 at both ends,
 * so a connected component can never hold two closed chains.  That is checked
 * per component here, together with: an open chain may only end on a terminal
 * or next to a pixel owned by another chain (a junction stop), never in the
 * middle of nothing. */
static void test_exhaustive_4x4(void) {
    enum { W = 4, H = 4, N = W * H };
    GradPoint p[N];
    GradChain c[N];
    unsigned char m[N], seen[N];
    int fails_here = 0;
    for (int bits = 0; bits < (1 << N); ++bits) {
        int e = 0;
        for (int i = 0; i < N; ++i) {
            m[i] = (unsigned char)((bits >> i) & 1 ? 255 : 0);
            if (m[i]) ++e;
            seen[i] = 0;
        }
        int k = grad_chainize(m, W, H, p, e, c, e);
        if (k < 0) { ++fails_here; continue; }

        /* Label 8-connected components of the mask. */
        int label[N], ncc = 0;
        for (int i = 0; i < N; ++i) label[i] = -1;
        for (int i = 0; i < N; ++i) {
            if (!m[i] || label[i] >= 0) continue;
            int stack[N], top = 0;
            stack[top++] = i;
            label[i] = ncc;
            while (top) {
                int v = stack[--top];
                int vx = v % W, vy = v / W;
                for (int dd = 0; dd < 8; ++dd) {
                    int nx = vx + kDx[dd], ny = vy + kDy[dd];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                    int j = ny * W + nx;
                    if (m[j] && label[j] < 0) { label[j] = ncc; stack[top++] = j; }
                }
            }
            ++ncc;
        }
        int closed_per_cc[N] = { 0 };

        int tot = 0, dup = 0, gap = 0, oob = 0;
        for (int i = 0; i < k; ++i) {
            tot += c[i].len;
            if (c[i].closed) ++closed_per_cc[label[p[c[i].offset].y * W +
                                                 p[c[i].offset].x]];
            if (c[i].len < 1) ++fails_here;
            if (c[i].closed && c[i].len < 3) ++fails_here;
            for (int j = 0; j < c[i].len; ++j) {
                int x = p[c[i].offset + j].x, y = p[c[i].offset + j].y;
                if (x < 0 || x >= W || y < 0 || y >= H) { oob = 1; continue; }
                int idx = y * W + x;
                if (seen[idx]) dup = 1;
                seen[idx] = 1;
                if (!m[idx]) gap = 1;
                if (j) {
                    int dx = x - p[c[i].offset + j - 1].x;
                    int dy = y - p[c[i].offset + j - 1].y;
                    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy))
                        ++fails_here;
                }
            }
            if (c[i].closed) {
                int dx = p[c[i].offset + c[i].len - 1].x - p[c[i].offset].x;
                int dy = p[c[i].offset + c[i].len - 1].y - p[c[i].offset].y;
                if (dx < -1 || dx > 1 || dy < -1 || dy > 1) ++fails_here;
                /* A closed chain must be an isolated simple cycle: no point,
                 * including interior ones, may be a junction. */
                for (int j = 0; j < c[i].len; ++j) {
                    int x = p[c[i].offset + j].x, y = p[c[i].offset + j].y;
                    int nd = 0;
                    for (int dd = 0; dd < 8; ++dd) {
                        int nx = x + kDx[dd], ny = y + kDy[dd];
                        if (nx >= 0 && nx < W && ny >= 0 && ny < H && m[ny * W + nx]) ++nd;
                    }
                    if (nd != 2) ++fails_here;
                }
            }
        }
        for (int cc = 0; cc < ncc; ++cc)
            if (closed_per_cc[cc] > 1) ++fails_here;
        if (tot != e || dup || gap || oob) ++fails_here;
    }
    CHECK(fails_here == 0);
}

static void test_fuzz(void) {
    enum { MAXW = 14, MAXH = 14, ITERS = 3000 };
    unsigned char m[MAXW * MAXH];
    int structures = 0;
    for (int iter = 0; iter < ITERS; ++iter) {
        int w = 1 + (int)(rnext() % MAXW);
        int h = 1 + (int)(rnext() % MAXH);
        int mode = iter % 5;
        int density = 3 + (int)(rnext() % 60);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int on;
                if (mode == 0) on = (int)(rnext() % 100) < density;
                else if (mode == 1) on = ((x + y) & 1) && (int)(rnext() % 100) < density;
                else if (mode == 2) on = (x == y || x + y == w - 1) &&
                                       (int)(rnext() % 100) < 70;
                else if (mode == 3) on = (int)(rnext() % 100) < 8;
                else on = 1; /* fully dense: no terminals anywhere */
                m[y * w + x] = (unsigned char)(on ? 255 : 0);
            }
        check_against_reference(m, w, h, "fuzz");
        if (iter % 97 == 0) check_deterministic(m, w, h);
        ++structures;
    }
    CHECK(structures == ITERS);
}

/* ================================================================== */
/* Sobel magnitude and count stay unchanged                            */
/* ================================================================== */

static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void ref_magnitude(const unsigned char* s, int w, int h, float* out) {
    static const int wx[9] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };
    static const int wy[9] = { -1, -1, -1, 0, 0, 0, 1, 1, 1 };
    static const int kx[9] = { 1, 2, 1, 0, 0, 0, -1, -2, -1 };
    static const int ky[9] = { -1, 0, 1, -2, 0, 2, -1, 0, 1 };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int gx = 0, gy = 0;
            for (int k = 0; k < 9; ++k) {
                int xx = clamp_i(x + wx[k], 0, w - 1);
                int yy = clamp_i(y + wy[k], 0, h - 1);
                gx += kx[k] * s[yy * w + xx];
                gy += ky[k] * s[yy * w + xx];
            }
            out[y * w + x] = sqrtf((float)(gx * gx + gy * gy));
        }
}

/* Verifies magnitude against the reference formula and the count against a
 * direct scan; returns 1 on agreement. */
static int check_magnitude_stable(const unsigned char* img, int w, int h,
                                  float thresh) {
    float* a = (float*)malloc((size_t)w * h * sizeof(float));
    float* b = (float*)malloc((size_t)w * h * sizeof(float));
    CHECK(grad_magnitude(img, w, h, a) == 0);
    ref_magnitude(img, w, h, b);
    float worst = 0.0f;
    for (int i = 0; i < w * h; ++i) {
        float d = fabsf(a[i] - b[i]);
        if (d > worst) worst = d;
    }
    CHECK(worst < 1e-3f);
    int ca = grad_edge_count(a, w, h, thresh);
    int cb = 0;
    for (int i = 0; i < w * h; ++i) if (b[i] >= thresh) ++cb;
    CHECK(ca == cb);
    int ok = (worst < 1e-3f) && (ca == cb);
    free(a);
    free(b);
    return ok;
}

/* ================================================================== */
/* Four image groups and statistics                                    */
/* ================================================================== */

typedef struct {
    char name[28];
    int w, h;
    unsigned char* img;   /* NULL when the mask is given directly */
    float* mag;           /* NULL when there is no Sobel stage */
    unsigned char* mask;
    float low, high;
    int edges;
    double mag_sum;
    int count50;
    int chains;
    int longest;
    double chain_us;      /* best chainize call */
    double full_us;       /* mag + hysteresis + chainize, when applicable */
} Sample;

static void fill_pattern(unsigned char* img, int w, int h, int kind) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v = 0;
            switch (kind) {
            case 0: v = 0; break;
            case 1: v = 128; break;
            case 2: v = x / 8; break;
            case 3: v = ((x + y) & 1) ? 255 : 0; break;
            case 4: {
                int dx = x - w / 2, dy = y - h / 2;
                v = dx * dx + dy * dy <= (w / 4) * (w / 4) ? 255 : 0;
                break;
            }
            case 5:
                v = ((x > w / 8 && x < w / 2 && y > h / 5 && y < h / 2) ||
                     (x > w / 2 && x < w - w / 8 && y > h / 2 && y < h - h / 5))
                    ? 200 : 20;
                break;
            case 6:
                v = ((((x * 2 - y * 3) % 17) + 17) % 17 < 2) ||
                    ((((x + y - w) % 13) + 13) % 13 < 2) ? 240 : 10;
                break;
            case 7: {
                double vv = 127.5 + 100.0 * sin(x * 0.19) * cos(y * 0.13);
                v = clamp_i((int)vv, 0, 255);
                break;
            }
            default: {
                /* Blob crossed by two diagonal bands: one long contour with
                 * genuine T/X junctions where the bands cut it, plus a little
                 * texture that creates short spurs without hiding the shape.
                 * Own generator: the picture must not depend on how many
                 * random masks the fuzz stage consumed. */
                static unsigned pat_state = 0xC0FFEEu;
                unsigned r = rng_next(&pat_state);
                int dx = x - w / 3, dy = y - h / 3;
                int inside = dx * dx + dy * dy < (w / 4) * (w / 4);
                int band = (abs(x - 2 * y / 3) % 29 < 3) ||
                           (abs(x - w / 2 + y) % 23 < 3);
                v = inside ? 200 : 40;
                if (band) v = 120;
                v += (int)(r % 9) - 4;
                v = clamp_i(v, 0, 255);
                break;
            }
            }
            img[y * w + x] = (unsigned char)v;
        }
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static void measure(Sample* s, int repeats) {
    int cap = s->edges ? s->edges : 1;
    GradPoint* pts = (GradPoint*)malloc((size_t)cap * sizeof(GradPoint));
    GradChain* ch = (GradChain*)malloc((size_t)cap * sizeof(GradChain));
    int n = grad_chainize(s->mask, s->w, s->h, pts, s->edges, ch, s->edges);
    if (n < 0) CHECK(0);
    s->chains = n;
    s->longest = 0;
    for (int i = 0; i < n; ++i)
        if (ch[i].len > s->longest) s->longest = ch[i].len;
    double best_chain = 1e30, best_full = 1e30;
    for (int rep = 0; rep < repeats; ++rep) {
        double t0 = now_us();
        int r = grad_chainize(s->mask, s->w, s->h, pts, s->edges, ch, s->edges);
        double t1 = now_us();
        if (r < 0) CHECK(0);
        if (t1 - t0 < best_chain) best_chain = t1 - t0;

        if (s->img) {
            double u0 = now_us();
            CHECK(grad_magnitude(s->img, s->w, s->h, s->mag) == 0);
            CHECK(grad_hysteresis(s->mag, s->w, s->h, s->low, s->high, s->mask) == 0);
            int q = grad_chainize(s->mask, s->w, s->h, pts, s->edges, ch, s->edges);
            double u1 = now_us();
            if (q < 0) CHECK(0);
            if (u1 - u0 < best_full) best_full = u1 - u0;
        }
    }
    s->chain_us = best_chain;
    s->full_us = s->img ? best_full : 0.0;
    free(pts);
    free(ch);
}

static void add_sample(Sample* s, const char* name, int w, int h,
                       int kind, float low, float high, int direct_mask_kind) {
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->w = w;
    s->h = h;
    s->low = low;
    s->high = high;
    if (direct_mask_kind >= 0) {
        s->mask = (unsigned char*)calloc((size_t)w * h, 1);
    } else {
        s->img = (unsigned char*)malloc((size_t)w * h);
        s->mag = (float*)malloc((size_t)w * h * sizeof(float));
        s->mask = (unsigned char*)malloc((size_t)w * h);
        fill_pattern(s->img, w, h, kind);
        if (!check_magnitude_stable(s->img, w, h, 50.0f)) CHECK(0);
        CHECK(grad_magnitude(s->img, w, h, s->mag) == 0);
        CHECK(grad_hysteresis(s->mag, w, h, low, high, s->mask) == 0);
        for (int k = 0; k < w * h; ++k) s->mag_sum += s->mag[k];
        s->count50 = grad_edge_count(s->mag, w, h, 50.0f);
    }
    for (int k = 0; k < w * h; ++k) if (s->mask[k]) ++s->edges;
}

static void free_sample(Sample* s) {
    free(s->img);
    free(s->mag);
    free(s->mask);
}

static void report_group(const char* title, Sample* g, int ng,
                         int* out_chains, int* out_longest, double* out_us) {
    printf("[%s]\n", title);
    int nc = 0, longest = 0;
    double sum = 0.0;
    for (int i = 0; i < ng; ++i) {
        if (g[i].edges > 0)
            check_against_reference(g[i].mask, g[i].w, g[i].h, g[i].name);
        measure(&g[i], g[i].img ? 100 : 500);
        printf("  %-20s %3dx%-3d edges=%5d mag_sum=%11.0f count>=50=%5d "
               "chains=%4d longest=%4d chain=%7.1fus",
               g[i].name, g[i].w, g[i].h, g[i].edges, g[i].mag_sum,
               g[i].count50, g[i].chains, g[i].longest, g[i].chain_us);
        if (g[i].img) printf(" full=%8.1fus", g[i].full_us);
        printf("\n");
        nc += g[i].chains;
        if (g[i].longest > longest) longest = g[i].longest;
        sum += g[i].full_us > 0.0 ? g[i].full_us : g[i].chain_us;
    }
    printf("  => %d chains, longest %d, %.1f us total\n\n", nc, longest, sum);
    *out_chains = nc;
    *out_longest = longest;
    *out_us = sum;
}

static void test_four_groups(void) {
    enum { G1 = 3, G2 = 4, G3 = 5, G4 = 5 };
    Sample g1[G1], g2[G2], g3[G3], g4[G4];

    printf("\nchain statistics  (time = best of N runs, single core)\n");

    /* G1: no edges. */
    add_sample(&g1[0], "flat-64x48", 64, 48, 1, 50.0f, 50.0f, -1);
    add_sample(&g1[1], "zero-32x32", 32, 32, 0, 50.0f, 50.0f, -1);
    add_sample(&g1[2], "ramp-64x48", 64, 48, 2, 20.0f, 50.0f, -1);
    CHECK(g1[0].edges == 0 && g1[1].edges == 0 && g1[2].edges == 0);

    /* G2: isolated points and short segments (no Sobel stage). */
    {
        struct { const char* n; int w, h; int kind; } defs[G2] = {
            { "single-dot-32x32", 32, 32, 0 },
            { "dot-grid-32x32", 32, 32, 1 },
            { "short-seg-40x20", 40, 20, 2 },
            { "corner-dots-9x9", 9, 9, 3 },
        };
        for (int i = 0; i < G2; ++i) {
            int w = defs[i].w, h = defs[i].h;
            add_sample(&g2[i], defs[i].n, w, h, 0, 0.0f, 0.0f, 1);
            unsigned char* m = g2[i].mask;
            if (defs[i].kind == 0) m[(h / 2) * w + w / 2] = 255;
            if (defs[i].kind == 1)
                for (int y = 4; y < h; y += 8)
                    for (int x = 4; x < w; x += 8) m[y * w + x] = 255;
            if (defs[i].kind == 2)
                for (int y = 3; y < h; y += 5)
                    for (int x = 2; x < w; x += 11) {
                        m[y * w + x] = 255;
                        if (x + 1 < w) m[y * w + x + 1] = 255;
                    }
            if (defs[i].kind == 3) m[0] = m[(h - 1) * w + (w - 1)] = 255;
            for (int k = 0; k < w * h; ++k) if (m[k]) ++g2[i].edges;
        }
    }

    /* G3: crossings (mask given directly). */
    {
        struct { const char* n; int w, h; const char* rows[16]; int rh; } defs[G3] = {
            { "plus-9x9", 9, 9,
              { "....#....", "....#....", "....#....", "....#....",
                "#########", "....#....", "....#....", "....#....", "....#...." }, 9 },
            { "x-cross-9x9", 9, 9,
              { "#.......#", ".#.....#.", "..#...#..", "...#.#...",
                "....#....", "...#.#...", "..#...#..", ".#.....#.", "#.......#" }, 9 },
            { "t-cross-11x11", 11, 11,
              { "...........", ".....#.....", ".....#.....", ".....#.....",
                "#########..", ".....#.....", ".....#.....", ".....#.....",
                ".....#.....", "...........", "..........." }, 11 },
            { "block-2x2", 8, 8,
              { "........", "........", "..##....", "..##....",
                "........", "........", "........", "........" }, 8 },
            { "cross-grid-32x32", 32, 32, { 0 }, 0 },
        };
        for (int i = 0; i < G3; ++i) {
            int w = defs[i].w, h = defs[i].h;
            add_sample(&g3[i], defs[i].n, w, h, 0, 0.0f, 0.0f, 1);
            unsigned char* m = g3[i].mask;
            if (i == 4) {
                for (int gy = 0; gy < 4; ++gy)
                    for (int gx = 0; gx < 4; ++gx) {
                        int cx = 4 + gx * 8, cy = 4 + gy * 8;
                        for (int k = -2; k <= 2; ++k) {
                            m[cy * w + cx + k] = 255;
                            m[(cy + k) * w + cx] = 255;
                        }
                    }
            } else {
                for (int y = 0; y < defs[i].rh; ++y)
                    for (int x = 0; defs[i].rows[y][x]; ++x)
                        if (defs[i].rows[y][x] == '#') m[y * w + x] = 255;
            }
            for (int k = 0; k < w * h; ++k) if (m[k]) ++g3[i].edges;
        }
    }

    /* G4: Sobel + hysteresis on 128x96 synthetic pictures. */
    add_sample(&g4[0], "disk-128x96", 128, 96, 4, 40.0f, 120.0f, -1);
    add_sample(&g4[1], "rects-128x96", 128, 96, 5, 40.0f, 120.0f, -1);
    add_sample(&g4[2], "diagonals-128x96", 128, 96, 6, 40.0f, 120.0f, -1);
    add_sample(&g4[3], "sinusoid-128x96", 128, 96, 7, 30.0f, 90.0f, -1);
    add_sample(&g4[4], "noisy-blob-128x96", 128, 96, 8, 40.0f, 120.0f, -1);
    for (int i = 0; i < G4; ++i) CHECK(g4[i].edges > 0);

    int c1, c2, c3, c4, l1, l2, l3, l4;
    double t1, t2, t3, t4;
    report_group("G1 no-edge", g1, G1, &c1, &l1, &t1);
    report_group("G2 points/segments", g2, G2, &c2, &l2, &t2);
    report_group("G3 crossings", g3, G3, &c3, &l3, &t3);
    report_group("G4 sobel+hysteresis", g4, G4, &c4, &l4, &t4);

    int longest = l1;
    int all_longest[4] = { l1, l2, l3, l4 };
    for (int i = 1; i < 4; ++i) if (all_longest[i] > longest) longest = all_longest[i];
    printf("all groups: chains=%d longest=%d time=%.1f us\n",
           c1 + c2 + c3 + c4, longest, t1 + t2 + t3 + t4);

    for (int i = 0; i < G1; ++i) free_sample(&g1[i]);
    for (int i = 0; i < G2; ++i) free_sample(&g2[i]);
    for (int i = 0; i < G3; ++i) free_sample(&g3[i]);
    for (int i = 0; i < G4; ++i) free_sample(&g4[i]);
}

int main(void) {
    test_empty();
    test_single_point();
    test_plus_small();
    test_plus_large();
    test_x_cross();
    test_t_cross();
    test_block();
    test_ring();
    test_ring_tail();
    test_hollow_square();
    test_two_rings();
    test_figure_eight();
    test_args();
    test_hysteresis();
    test_fuzz();
    test_step_rule_geometry();
    test_exhaustive_4x4();
    test_four_groups();
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
