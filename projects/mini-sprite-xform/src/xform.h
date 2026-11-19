#ifndef XFORM_H
#define XFORM_H
struct SpritePoint { float x, y; };
int xform_sprite(const SpritePoint* src, int n, float sx, float sy, float tx, float ty,
                 SpritePoint* dst);
int xform_bounds(const SpritePoint* pts, int n, float* minx, float* miny, float* maxx, float* maxy);
#endif
