#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;
#define SAT_MAX_VERT 16
#define SAT_MAX_PIECES 16
int sat_overlap(const SatPoly* a, const SatPoly* b);      /* convex-only SAT */
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);
double sat_area(const SatPoly* a);                        /* signed shoelace area */
/* Ear-clip triangulation + Hertel-Mehlhorn greedy merge.
 * Returns piece count (<= max_out), or -1 on bad input. */
int sat_decompose(const SatPoly* in, SatPoly* out, int max_out);
/* Overlap test valid for concave polygons: decompose both, test piece pairs. */
int sat_overlap_any(const SatPoly* a, const SatPoly* b);
#endif
