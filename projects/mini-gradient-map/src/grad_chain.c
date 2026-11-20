#include "grad.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Hysteresis threshold                                                */
/* ------------------------------------------------------------------ */

int grad_hysteresis(const float* mag, int w, int h, float low, float high,
                    unsigned char* mask) {
    if (!mag || !mask || w <= 0 || h <= 0 || low < 0.0f || high < low)
        return GRAD_ERR_ARG;

    int n = w * h;
    unsigned char* state = (unsigned char*)calloc((size_t)n, 1);
    int* queue = (int*)malloc((size_t)n * sizeof(int));
    if (!state || !queue) {
        free(state);
        free(queue);
        return GRAD_ERR_ARG;
    }

    int head = 0, tail = 0;
    for (int i = 0; i < n; ++i) {
        mask[i] = 0;
        if (mag[i] >= high) {
            state[i] = 1;
            queue[tail++] = i;
        }
    }

    while (head < tail) {
        int idx = queue[head++];
        int cx = idx % w, cy = idx / w;
        for (int dy = -1; dy <= 1; ++dy) {
            int ny = cy + dy;
            if (ny < 0 || ny >= h) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx;
                if (nx < 0 || nx >= w) continue;
                int ni = ny * w + nx;
                if (state[ni] || mag[ni] < low) continue;
                state[ni] = 1;
                queue[tail++] = ni;
            }
        }
    }

    for (int i = 0; i < n; ++i)
        if (state[i]) mask[i] = 255;

    free(state);
    free(queue);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Chainization                                                        */
/* ------------------------------------------------------------------ */
/*
 * The precise rules live in grad.h.  This translation keeps them in one
 * place.  Direction indices run clockwise from east:
 *
 *     0 E (1,0)    1 SE (1,1)   2 S (0,1)    3 SW (-1,1)
 *     4 W (-1,0)   5 NW (-1,-1) 6 N (0,-1)   7 NE (1,-1)
 *
 * so a step d has clockwise offset (d - dir + 8) & 7 from the previous
 * direction dir, and the absolute turn is min(offset, 8 - offset).
 */

static const int kDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int kDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

typedef struct {
    const unsigned char* mask;
    unsigned char* used;
    int w, h;
    GradPoint* pts;
    int npts;
    GradChain* chains;
    int nchains;
} ChainCtx;

static int in_bounds(const ChainCtx* c, int x, int y) {
    return x >= 0 && x < c->w && y >= 0 && y < c->h;
}

static int is_edge(const ChainCtx* c, int x, int y) {
    return in_bounds(c, x, y) && c->mask[y * c->w + x] != 0;
}

static int is_free(const ChainCtx* c, int x, int y) {
    return is_edge(c, x, y) && !c->used[y * c->w + x];
}

/* Static 8-neighbour degree of a pixel (chain membership not considered). */
static int edge_degree(const ChainCtx* c, int x, int y) {
    int d = 0;
    for (int k = 0; k < 8; ++k)
        if (is_edge(c, x + kDx[k], y + kDy[k])) ++d;
    return d;
}

static int pt_less(GradPoint a, GradPoint b) {
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
}

static void reverse_range(GradPoint* p, int off, int n) {
    for (int i = 0, j = n - 1; i < j; ++i, --j) {
        GradPoint t = p[off + i];
        p[off + i] = p[off + j];
        p[off + j] = t;
    }
}

/* Rotate pts[off..off+n) left by 'shift'.  Uses the triple reversal trick. */
static void rotate_range(GradPoint* p, int off, int n, int shift) {
    if (shift <= 0 || shift >= n) return;
    reverse_range(p, off, shift);
    reverse_range(p, off + shift, n - shift);
    reverse_range(p, off, n);
}

/* R5: make the stored direction a function of the chain geometry only. */
static void canonicalize(GradPoint* p, int off, int n, int closed) {
    if (!closed) {
        if (pt_less(p[off + n - 1], p[off])) reverse_range(p, off, n);
        return;
    }
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (pt_less(p[off + i], p[off + best])) best = i;
    rotate_range(p, off, n, best);
    /* n >= 3 for a closed chain, so p[off+1] and p[off+n-1] exist. */
    if (pt_less(p[off + n - 1], p[off + 1])) reverse_range(p, off + 1, n - 1);
}

/* R2: choose a step from the free neighbours.  dir < 0 means the chain
 * has no history yet (start pixel), in which case the lowest ring index wins.
 * Otherwise minimise the absolute turn; ties go to the clockwise-most step
 * (the smaller clockwise offset), i.e. turn right. */
static int choose_step(int dir, const int* cand, int nc) {
    int best = cand[0], best_turn = 9, best_cw = 9;
    for (int i = 0; i < nc; ++i) {
        int d = cand[i];
        if (dir < 0) {
            if (d < best) best = d;
            continue;
        }
        int cw = (d - dir + 8) & 7;
        int turn = cw <= 4 ? cw : 8 - cw;
        if (turn < best_turn || (turn == best_turn && cw < best_cw)) {
            best_turn = turn;
            best_cw = cw;
            best = d;
        }
    }
    return best;
}

/* R2-R4: walk from a free start pixel, claiming every visited pixel. */
static void trace_from(ChainCtx* c, int sx, int sy) {
    int off = c->npts;
    int px = sx, py = sy, dir = -1, closed = 0;

    c->used[sy * c->w + sx] = 1;
    c->pts[c->npts++] = (GradPoint){ sx, sy };

    for (;;) {
        int cand[8], nc = 0;
        for (int d = 0; d < 8; ++d) {
            int nx = px + kDx[d], ny = py + kDy[d];
            if (is_free(c, nx, ny)) cand[nc++] = d;
        }

        if (nc == 0) {
            /* R4: close only an isolated simple cycle.  The start must be
             * adjacent, neither end may be a junction, the chain must be long
             * enough not to fold onto itself, and no interior point may be a
             * junction.  The last clause matters because R3 lets a chain pass
             * through a junction: without it a 3-pixel loop touching a branch
             * could be reported closed even though the branch hangs off one of
             * its middle points. */
            int adj = (px != sx || py != sy) &&
                      (px - sx >= -1 && px - sx <= 1) &&
                      (py - sy >= -1 && py - sy <= 1);
            int interior_junction = 0;
            for (int i = off + 1; i < c->npts - 1; ++i)
                if (edge_degree(c, c->pts[i].x, c->pts[i].y) > 2) {
                    interior_junction = 1;
                    break;
                }
            if (adj && c->npts - off >= 3 && !interior_junction &&
                edge_degree(c, sx, sy) == 2 && edge_degree(c, px, py) == 2)
                closed = 1;
            break;
        }

        int step = choose_step(dir, cand, nc);
        px += kDx[step];
        py += kDy[step];
        dir = step;
        c->used[py * c->w + px] = 1;
        c->pts[c->npts++] = (GradPoint){ px, py };
    }

    canonicalize(c->pts, off, c->npts - off, closed);
    c->chains[c->nchains].offset = off;
    c->chains[c->nchains].len = c->npts - off;
    c->chains[c->nchains].closed = closed;
    ++c->nchains;
}

int grad_chainize(const unsigned char* mask, int w, int h,
                  GradPoint* pts, int pts_cap,
                  GradChain* chains, int chains_cap) {
    if (!mask || !pts || !chains || w <= 0 || h <= 0 ||
        pts_cap < 0 || chains_cap < 0)
        return GRAD_ERR_ARG;

    int n = w * h, edges = 0;
    for (int i = 0; i < n; ++i)
        if (mask[i]) ++edges;

    /* Every chain claims at least one pixel, so both buffers must be able to
     * hold the whole mask.  Checking up front avoids partial output. */
    if (pts_cap < edges || chains_cap < edges) return GRAD_ERR_CAP;
    if (edges == 0) return 0;

    ChainCtx c;
    c.mask = mask;
    c.used = (unsigned char*)calloc((size_t)n, 1);
    c.w = w;
    c.h = h;
    c.pts = pts;
    c.npts = 0;
    c.chains = chains;
    c.nchains = 0;
    if (!c.used) return GRAD_ERR_ARG;

    /* Pass 1: real terminals (degree 1), raster order. */
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (is_free(&c, x, y) && edge_degree(&c, x, y) == 1)
                trace_from(&c, x, y);

    /* Pass 2: components with no terminal at all (junctions-only blobs and
     * pure cycles), raster order. */
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (is_free(&c, x, y))
                trace_from(&c, x, y);

    free(c.used);
    return c.nchains;
}
