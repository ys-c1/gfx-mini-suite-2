#ifndef GRAD_H
#define GRAD_H

/* Shared error codes. */
#define GRAD_ERR_ARG (-1)
#define GRAD_ERR_CAP (-2)

int grad_magnitude(const unsigned char* src, int w, int h, float* out);
int grad_edge_count(const float* mag, int w, int h, float thresh);

/* Hysteresis threshold.  mag[i] >= high seeds the edge mask; mag[i] >= low is
 * kept only when it is 8-connected to a seed through kept pixels.  mask[i] is
 * written as 0 or 255.  Returns 0, GRAD_ERR_ARG on bad arguments. */
int grad_hysteresis(const float* mag, int w, int h, float low, float high,
                    unsigned char* mask);

/* One chain point, image coordinates. */
typedef struct {
    int x;
    int y;
} GradPoint;

/* A chain occupies pts[offset .. offset + len - 1].  closed != 0 marks an
 * isolated simple cycle; its first point is not repeated at the end. */
typedef struct {
    int offset;
    int len;
    int closed;
} GradChain;

/* Chainize a binary edge mask (any non-zero byte is an edge) into disjoint,
 * ordered 8-connected chains.  Skeleton/region semantics:
 *
 *   deg(p)     number of 8-neighbours that are edges
 *   terminal   deg == 1
 *   junction   deg >= 3
 *
 * Rules, in order of application:
 *
 *   R1  Seeds are terminals in raster order.  After that pass, any remaining
 *       edge pixel (junction-only blobs and pure cycles) seeds a chain in
 *       raster order.
 *   R2  From a pixel with a previous direction, the next free neighbour is
 *       the one with the smallest absolute turn (0 = straight, 4 = U-turn).
 *       Ties are resolved by the clockwise (right-hand) turn, i.e. the step
 *       with the smaller clockwise offset.  The first step of a seed has no
 *       history and takes the free neighbour with the lowest direction index
 *       (a terminal has exactly one anyway).
 *   R3  Junctions are traversed, not treated as walls: the straightest
 *       continuation wins.  A junction pixel belongs to the first chain that
 *       enters it; later chains see it as used and therefore stop at the
 *       pixel in front of it.  No pixel is written to two chains.  (A later
 *       chain never crosses a claimed pixel, so an X crossing yields one
 *       straight through-chain plus two arms that stop short, not two
 *       crossing chains.)
 *   R4  A chain closes when it has no free neighbour left, is adjacent to its
 *       first point, len >= 3, and EVERY one of its points (interior points
 *       included) has degree 2.  In other words closed means "isolated simple
 *       cycle".  Checking only the two ends is not enough because R3 lets a
 *       chain pass through a junction: a 3-pixel loop with a branch hanging
 *       off its middle would otherwise be reported closed.  Cycles with a
 *       branch anywhere on them are reported as open chains.
 *   R5  Direction is canonical: an open chain starts at the lexicographically
 *       smaller endpoint (row-major: smaller y, then smaller x); a closed
 *       chain starts at its lexicographically smallest point and runs toward
 *       the smaller of its two cycle neighbours.  Re-running the tracer or
 *       translating the image therefore does not reverse a chain.
 *
 * Points and chains are written to caller buffers.  Both capacities must be
 * at least the number of edge pixels (every pixel belongs to exactly one
 * chain), otherwise GRAD_ERR_CAP is returned before any output is produced.
 * Returns the number of chains, GRAD_ERR_ARG on bad arguments, or
 * GRAD_ERR_CAP on insufficient capacity.  An empty mask yields 0 chains. */
int grad_chainize(const unsigned char* mask, int w, int h,
                  GradPoint* pts, int pts_cap,
                  GradChain* chains, int chains_cap);

#endif
