#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;
int sat_overlap(const SatPoly* a, const SatPoly* b);
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);
#endif
