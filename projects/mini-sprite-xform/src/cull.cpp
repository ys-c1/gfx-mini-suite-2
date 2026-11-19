#include "cull.h"
#include <cmath>

int cull_box_intersects_viewport(const SpriteBox* box, const Viewport* vp) {
    if (!box || !vp) return 0;

    // Empty viewport (zero/inverted extent or any NaN): nothing intersects.
    // NaN makes every comparison false, so a NaN coordinate lands here too.
    if (!(vp->minx < vp->maxx) || !(vp->miny < vp->maxy)) return 0;

    // Ambiguous boxes are conservatively kept so a malformed entry can never
    // be mistaken for an outside sprite and silently dropped.
    if (!(box->minx <= box->maxx) || !(box->miny <= box->maxy)) return 1;
    if (std::isnan(box->minx) || std::isnan(box->miny) ||
        std::isnan(box->maxx) || std::isnan(box->maxy)) return 1;

    // Closed-interval AABB test: strict separation on either axis means
    // outside; equality means touching the edge and is kept.
    if (box->maxx < vp->minx || box->minx > vp->maxx) return 0;
    if (box->maxy < vp->miny || box->miny > vp->maxy) return 0;
    return 1;
}

int cull_sprites(const SpriteBox* boxes, int n, const Viewport* vp,
                 int* kept_indices, int* culled_indices) {
    if (n < 0) return -1;
    if (n == 0) return 0;  // empty input is a deterministic no-op
    if (!boxes || !vp) return -1;

    int kept = 0, culled = 0;
    for (int i = 0; i < n; ++i) {
        if (cull_box_intersects_viewport(&boxes[i], vp)) {
            if (kept_indices) kept_indices[kept] = i;
            ++kept;
        } else {
            if (culled_indices) culled_indices[culled] = i;
            ++culled;
        }
    }
    return kept;
}
