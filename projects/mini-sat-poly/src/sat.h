#ifndef SAT_H
#define SAT_H
typedef struct { float x, y; } SatVec;
typedef struct { SatVec p[16]; int n; } SatPoly;

int sat_overlap(const SatPoly* a, const SatPoly* b);
int sat_point_inside(const SatPoly* a, float x, float y);
void sat_centroid(const SatPoly* a, float* cx, float* cy);

/* Minimum translation vector: when the polygons overlap, *out is the vector
   that moves a out of b along the shallowest axis. Returns 1 = overlap,
   0 = separated, -1 = invalid polygon. */
int sat_mtv(const SatPoly* a, const SatPoly* b, SatVec* out);

/* Per-pair frame-to-frame push state. Zero-initialize via sat_push_init. */
typedef struct {
    SatVec dir;      /* last smoothed unit push direction */
    float  mag;      /* last smoothed separation magnitude */
    int    have;     /* whether a contact has initialized the state */
    int    gap;      /* consecutive non-contact frames since last contact */
} SatPushState;

typedef struct {
    float max_mag_delta; /* max |delta magnitude| per contact frame */
    float max_ang_delta; /* max direction rotation per contact frame, rad */
    int   hold_frames;   /* retain state this many gap frames before reset */
    float tie_tol;       /* depth tolerance for near-tied MTV axis pick */
} SatPushConfig;

void sat_push_default_config(SatPushConfig* cfg);
void sat_push_init(SatPushState* st);

/* Smooth push for one frame. Returns the push to apply to a; zero vector on
   non-contact frames (contact itself is never dropped while overlapping). */
SatVec sat_push_update(SatPushState* st, const SatPoly* a, const SatPoly* b,
                       const SatPushConfig* cfg);
#endif
