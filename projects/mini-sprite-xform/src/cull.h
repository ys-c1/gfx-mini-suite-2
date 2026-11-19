#ifndef CULL_H
#define CULL_H

// Axis-aligned bounding box of one sprite, in the same coordinate space as the
// viewport (typically the post-transform space, e.g. built from xform_bounds).
struct SpriteBox {
    float minx, miny, maxx, maxy;
};

// Axis-aligned viewport rectangle.
//
// A viewport is EMPTY when !(minx < maxx) or !(miny < maxy): zero extent,
// inverted extent, or any NaN coordinate all count as empty. An empty viewport
// culls every sprite deterministically (kept count is 0).
struct Viewport {
    float minx, miny, maxx, maxy;
};

// Inclusive intersection test.
//
// Returns 1 when the box intersects the viewport, 0 otherwise. A box that only
// touches a viewport edge or corner (equality on any boundary) is considered
// intersecting and is KEPT. A zero-size box (point or horizontal/vertical
// line) is kept exactly when it lies inside or on the viewport boundary.
//
// Conservative safety rules (a box is never culled on ambiguous input):
//   * an inverted box (min > max on either axis) is kept;
//   * a box containing a NaN coordinate is kept;
//   * an empty or NaN viewport culls everything.
// Null pointers return 0.
int cull_box_intersects_viewport(const SpriteBox* box, const Viewport* vp);

// Batch viewport culling.
//
// boxes          n sprite boxes in caller order; n may be 0.
// vp             viewport rectangle (see Viewport for the empty rule).
// kept_indices   if non-null, receives the ORIGINAL indices of kept sprites in
//                ascending order; must have room for at least n ints.
// culled_indices if non-null, receives the ORIGINAL indices of culled sprites
//                in ascending order; must have room for at least n ints.
//
// Returns the number of kept sprites (>= 0). n == 0 is a deterministic
// no-op that returns 0 even when boxes is null. Returns -1 only on invalid
// arguments: n < 0, or null boxes/vp with n > 0. Inputs are never modified
// and no memory is allocated; auxiliary space is O(1), running time O(n).
int cull_sprites(const SpriteBox* boxes, int n, const Viewport* vp,
                 int* kept_indices, int* culled_indices);

#endif
