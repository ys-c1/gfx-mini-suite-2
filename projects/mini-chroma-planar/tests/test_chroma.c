#include "../src/chroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------- helpers ---------- */

static uint64_t fnv(const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static void fill_ramp(unsigned char* rgb, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + ((size_t)j * w + i) * 3;
            p[0] = (unsigned char)(i * 16);
            p[1] = (unsigned char)(j * 16);
            p[2] = 100;
        }
}

static void fill_checker(unsigned char* rgb, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = rgb + ((size_t)j * w + i) * 3;
            p[0] = (unsigned char)((i * 5 + j * 3) & 255);
            p[1] = (unsigned char)((i * 11 + j * 7) & 255);
            p[2] = (unsigned char)((i * i + j * j) & 255);
        }
}

static uint32_t lcg_state;
static unsigned rnd_next(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return (lcg_state >> 16) & 0x7fffu;
}

static void fill_noise(unsigned char* rgb, int w, int h) {
    lcg_state = 0x12345678u;
    for (int i = 0; i < w * h * 3; ++i) rgb[i] = (unsigned char)(rnd_next() & 255);
}

typedef void (*fill_fn)(unsigned char*, int, int);
static fill_fn dataset_fill(int id) {
    return id == 0 ? fill_ramp : id == 1 ? fill_checker : fill_noise;
}
static const char* dataset_name(int id) {
    return id == 0 ? "ramp 9x7" : id == 1 ? "checker 64x48" : "noise 37x23";
}

/* Allocate `n` payload bytes followed by a 64-byte guard area. */
enum { GUARD = 64 };
static unsigned char* guarded_alloc(size_t n, unsigned char fill) {
    unsigned char* p = (unsigned char*)malloc(n + GUARD);
    if (!p) { printf("out of memory\n"); exit(2); }
    memset(p, fill, n + GUARD);
    return p;
}
static int guard_intact(const unsigned char* p, size_t n, unsigned char fill) {
    for (size_t i = n; i < n + GUARD; ++i)
        if (p[i] != fill) return 0;
    return 1;
}
/* Every byte of every row past `used` must still hold `fill`. */
static int padding_intact(const unsigned char* p, int rows, int stride, int used, unsigned char fill) {
    for (int j = 0; j < rows; ++j)
        for (int i = used; i < stride; ++i)
            if (p[(size_t)j * stride + i] != fill) {
                printf("  padding written at row %d byte %d (stride %d, used %d)\n", j, i, stride, used);
                return 0;
            }
    return 1;
}

/* Copy a packed frame into a strided buffer and poison the padding. */
static void scatter_packed_to_strided(unsigned char* dst, const unsigned char* src,
                                      int rows, int stride, int used, unsigned char pad) {
    for (int j = 0; j < rows; ++j) {
        memcpy(dst + (size_t)j * stride, src + (size_t)j * used, (size_t)used);
        memset(dst + (size_t)j * stride + used, pad, (size_t)(stride - used));
    }
}
static int used_rows_equal(const unsigned char* a, int a_stride,
                           const unsigned char* b, int b_stride,
                           int rows, int used) {
    for (int j = 0; j < rows; ++j)
        if (memcmp(a + (size_t)j * a_stride, b + (size_t)j * b_stride, (size_t)used) != 0) {
            printf("  row %d differs\n", j);
            return 0;
        }
    return 1;
}

/* ---------- 1. packed results must match the pre-change implementation ---------- */
/*
 * Golden FNV-1a hashes produced by the implementation at commit ea29d88
 * (the commit immediately before the stride-hardening change), using the
 * exact same fill patterns and fully packed buffers.
 */
typedef struct {
    int w, h, ds;
    uint64_t hy, hu, hv, hrgb;
} Golden;

static const Golden golden[] = {
    { 9, 7, 0, 0x36eaf3324cb6ddf6ULL, 0x8fdd02a8fce083d6ULL, 0x3449740e8ea7f357ULL, 0x03cf2fa774326d2eULL },
    { 64, 48, 1, 0x0d1c03364039e702ULL, 0x8c2f154dbc7b53bdULL, 0x87102655d7d1fe4cULL, 0x0c6295a9d93ab434ULL },
    { 37, 23, 2, 0x64701ba173d8473aULL, 0x7d2cd693cebadd31ULL, 0xb65bda4aa265859eULL, 0x7f7da4ea1bb2b015ULL },
};

static void test_packed_golden(void) {
    for (size_t g = 0; g < sizeof(golden) / sizeof(golden[0]); ++g) {
        int w = golden[g].w, h = golden[g].h;
        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        unsigned char* rgb = (unsigned char*)malloc((size_t)w * h * 3);
        unsigned char* back = (unsigned char*)malloc((size_t)w * h * 3);
        unsigned char* y = (unsigned char*)malloc((size_t)w * h);
        unsigned char* u = (unsigned char*)malloc((size_t)cw * ch);
        unsigned char* v = (unsigned char*)malloc((size_t)cw * ch);
        dataset_fill(golden[g].ds)(rgb, w, h);
        CHECK(rgb_to_yuv420(rgb, w, h, w * 3, y, w, u, cw, v, cw) == 0);
        CHECK(yuv420_to_rgb(y, w, u, cw, v, cw, w, h, back, w * 3) == 0);
        CHECK(fnv(y, (size_t)w * h) == golden[g].hy);
        CHECK(fnv(u, (size_t)cw * ch) == golden[g].hu);
        CHECK(fnv(v, (size_t)cw * ch) == golden[g].hv);
        CHECK(fnv(back, (size_t)w * h * 3) == golden[g].hrgb);
        if (fnv(back, (size_t)w * h * 3) != golden[g].hrgb)
            printf("  golden mismatch: %s\n", dataset_name(golden[g].ds));
        free(rgb); free(back); free(y); free(u); free(v);
    }
}

/* ---------- 2. padded input/output must equal packed, padding untouched ---------- */

static void test_strided_equivalence(int w, int h, int ds, int rgb_pad, int y_pad, int c_pad) {
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    const int rgb_used = w * 3, y_used = w, c_used = cw;
    const int rgb_stride = rgb_used + rgb_pad;
    const int y_stride = y_used + y_pad;
    const int c_stride = c_used + c_pad;
    const unsigned char po = 0xA5, py = 0x5A, pc = 0xC3;

    /* packed references */
    unsigned char* prgb = guarded_alloc((size_t)rgb_used * h, 0);
    unsigned char* pyuv = guarded_alloc((size_t)y_used * h, 0);
    unsigned char* pu = guarded_alloc((size_t)c_used * ch, 0);
    unsigned char* pv = guarded_alloc((size_t)c_used * ch, 0);
    unsigned char* pback = guarded_alloc((size_t)rgb_used * h, 0);

    /* strided buffers; padding pre-poisoned */
    unsigned char* srgb = guarded_alloc((size_t)rgb_stride * h, po);
    unsigned char* sy = guarded_alloc((size_t)y_stride * h, py);
    unsigned char* su = guarded_alloc((size_t)c_stride * ch, pc);
    unsigned char* sv = guarded_alloc((size_t)c_stride * ch, pc);
    unsigned char* sback = guarded_alloc((size_t)rgb_stride * h, po);

    dataset_fill(ds)(prgb, w, h);
    scatter_packed_to_strided(srgb, prgb, h, rgb_stride, rgb_used, po);

    CHECK(rgb_to_yuv420(prgb, w, h, rgb_used, pyuv, y_used, pu, c_used, pv, c_used) == 0);
    CHECK(rgb_to_yuv420(srgb, w, h, rgb_stride, sy, y_stride, su, c_stride, sv, c_stride) == 0);

    /* Input padding must not have been written. */
    CHECK(padding_intact(srgb, h, rgb_stride, rgb_used, po));
    CHECK(guard_intact(srgb, (size_t)rgb_stride * h, po));
    /* Output padding must not have been written. */
    CHECK(padding_intact(sy, h, y_stride, y_used, py));
    CHECK(padding_intact(su, ch, c_stride, c_used, pc));
    CHECK(padding_intact(sv, ch, c_stride, c_used, pc));
    CHECK(guard_intact(sy, (size_t)y_stride * h, py));
    CHECK(guard_intact(su, (size_t)c_stride * ch, pc));
    CHECK(guard_intact(sv, (size_t)c_stride * ch, pc));

    /* Strided output equals packed output. */
    CHECK(used_rows_equal(pyuv, y_used, sy, y_stride, h, y_used));
    CHECK(used_rows_equal(pu, c_used, su, c_stride, ch, c_used));
    CHECK(used_rows_equal(pv, c_used, sv, c_stride, ch, c_used));

    /* Decode both, packed vs strided in AND strided vs strided. */
    CHECK(yuv420_to_rgb(pyuv, y_used, pu, c_used, pv, c_used, w, h, pback, rgb_used) == 0);
    CHECK(yuv420_to_rgb(sy, y_stride, su, c_stride, sv, c_stride, w, h, sback, rgb_stride) == 0);
    CHECK(used_rows_equal(pback, rgb_used, sback, rgb_stride, h, rgb_used));
    /* Output RGB padding untouched. */
    CHECK(padding_intact(sback, h, rgb_stride, rgb_used, po));
    CHECK(guard_intact(sback, (size_t)rgb_stride * h, po));

    /* A poisoned chroma/luma padding must not influence the decode. */
    for (int j = 0; j < h; ++j) memset(sy + (size_t)j * y_stride + y_used, 0x37, (size_t)y_pad);
    for (int j = 0; j < ch; ++j) {
        memset(su + (size_t)j * c_stride + c_used, 0x37, (size_t)c_pad);
        memset(sv + (size_t)j * c_stride + c_used, 0x37, (size_t)c_pad);
    }
    CHECK(yuv420_to_rgb(sy, y_stride, su, c_stride, sv, c_stride, w, h, sback, rgb_stride) == 0);
    CHECK(used_rows_equal(pback, rgb_used, sback, rgb_stride, h, rgb_used));

    free(prgb); free(pyuv); free(pu); free(pv); free(pback);
    free(srgb); free(sy); free(su); free(sv); free(sback);
}

/* ---------- 3. stride validation ---------- */

static void test_stride_rejection(void) {
    /* Strides up to CW+1 / W+1 are exercised below, so size for one extra row. */
    enum { W = 8, H = 6, CW = 4, CH = 3, EXTRA = 1 };
    unsigned char rgb[(W + EXTRA) * H * 3], y[(W + EXTRA) * H];
    unsigned char u[(CW + EXTRA) * CH], v[(CW + EXTRA) * CH];
    unsigned char back[(W + EXTRA) * H * 3];
    unsigned char rgb_copy[sizeof rgb];
    memset(rgb, 0x11, sizeof rgb);
    memcpy(rgb_copy, rgb, sizeof rgb);

    /* Negative and zero strides are rejected for every plane, both directions. */
    const int nonpos[] = { -1, -7, 0, -1000 };
    for (size_t k = 0; k < sizeof(nonpos) / sizeof(nonpos[0]); ++k) {
        int s = nonpos[k];
        CHECK(rgb_to_yuv420(rgb, W, H, s, y, W, u, CW, v, CW) == -1);
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, s, u, CW, v, CW) == -1);
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, s, v, CW) == -1);
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, CW, v, s) == -1);
        CHECK(yuv420_to_rgb(y, s, u, CW, v, CW, W, H, back, W * 3) == -1);
        CHECK(yuv420_to_rgb(y, W, u, s, v, CW, W, H, back, W * 3) == -1);
        CHECK(yuv420_to_rgb(y, W, u, CW, v, s, W, H, back, W * 3) == -1);
        CHECK(yuv420_to_rgb(y, W, u, CW, v, CW, W, H, back, s) == -1);
    }
    /* Strides just below each plane's used row width are rejected. */
    const int under_rgb[] = { W * 3 - 1, W * 3 - 2, 1 };
    const int under_y[] = { W - 1, W - 2, 1 };
    const int under_c[] = { CW - 1, 1 };
    for (size_t k = 0; k < sizeof(under_rgb) / sizeof(under_rgb[0]); ++k) {
        CHECK(rgb_to_yuv420(rgb, W, H, under_rgb[k], y, W, u, CW, v, CW) == -1);
        CHECK(yuv420_to_rgb(y, W, u, CW, v, CW, W, H, back, under_rgb[k]) == -1);
    }
    for (size_t k = 0; k < sizeof(under_y) / sizeof(under_y[0]); ++k) {
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, under_y[k], u, CW, v, CW) == -1);
        CHECK(yuv420_to_rgb(y, under_y[k], u, CW, v, CW, W, H, back, W * 3) == -1);
    }
    for (size_t k = 0; k < sizeof(under_c) / sizeof(under_c[0]); ++k) {
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, under_c[k], v, CW) == -1);
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, CW, v, under_c[k]) == -1);
        CHECK(yuv420_to_rgb(y, W, u, under_c[k], v, CW, W, H, back, W * 3) == -1);
        CHECK(yuv420_to_rgb(y, W, u, CW, v, under_c[k], W, H, back, W * 3) == -1);
    }
    /* A stride valid for luma but illegal for chroma (chroma is narrower). */
    {
        int wide_enough_for_both = CW + 1;
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, wide_enough_for_both, v, wide_enough_for_both) == 0);
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, CW - 1, v, CW) == -1);
    }
    /* Minimum legal stride is the row width exactly. */
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, CW, v, CW) == 0);
    CHECK(yuv420_to_rgb(y, W, u, CW, v, CW, W, H, back, W * 3) == 0);
    /* An undersized RGB stride must not have consumed or modified anything. */
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3 - 1, y, W, u, CW, v, CW) == -1);
    CHECK(memcmp(rgb, rgb_copy, sizeof rgb) == 0);
    /* NULL pointers and non-positive dimensions still rejected. */
    CHECK(rgb_to_yuv420(NULL, W, H, W * 3, y, W, u, CW, v, CW) == -1);
    CHECK(yuv420_to_rgb(y, W, u, CW, v, CW, W, H, NULL, W * 3) == -1);
    CHECK(rgb_to_yuv420(rgb, 0, H, 0, y, W, u, CW, v, CW) == -1);
    CHECK(yuv420_to_rgb(y, W, u, CW, v, CW, W, 0, back, W * 3) == -1);
}

/* ---------- 4. odd geometry: chroma plane size and edge replication ---------- */

static int luma_expected(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}
static int chroma_v_expected(int r, int g, int b) {
    return ((128 * r - 107 * g - 21 * b + 128) >> 8) + 128;
}

static void test_odd_geometry(void) {
    /* 1x1: a single chroma sample, decoded back to the same pixel. */
    {
        unsigned char rgb[3] = { 200, 30, 90 }, back[3] = { 0, 0, 0 };
        unsigned char y[1], u[1], v[1];
        CHECK(rgb_to_yuv420(rgb, 1, 1, 3, y, 1, u, 1, v, 1) == 0);
        CHECK(y[0] == (unsigned char)luma_expected(200, 30, 90));
        CHECK(v[0] == (unsigned char)chroma_v_expected(200, 30, 90));
        CHECK(yuv420_to_rgb(y, 1, u, 1, v, 1, 1, 1, back, 3) == 0);
        CHECK(abs((int)back[0] - 200) <= 12);
        CHECK(abs((int)back[1] - 30) <= 12);
        CHECK(abs((int)back[2] - 90) <= 12);
    }
    /* 3x1: cw = 2, ch = 1.  The lone last column is replicated. */
    {
        enum { W = 3, H = 1 };
        unsigned char rgb[W * 3];
        for (int i = 0; i < W; ++i) {
            rgb[i * 3 + 0] = (unsigned char)(i == 2 ? 240 : 10);
            rgb[i * 3 + 1] = (unsigned char)(i == 2 ? 20 : 200);
            rgb[i * 3 + 2] = (unsigned char)(i == 2 ? 60 : 40);
        }
        unsigned char y[W], u[2], v[2];
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 2, v, 2) == 0);
        /* Chroma sample 1 covers pixels x=2,2,2,2 -> exactly the x=2 value. */
        int su = 0, sv = 0;
        int rr = rgb[6], gg = rgb[7], bb = rgb[8];
        for (int k = 0; k < 4; ++k) {
            su += ((-43 * rr - 85 * gg + 128 * bb + 128) >> 8) + 128;
            sv += ((128 * rr - 107 * gg - 21 * bb + 128) >> 8) + 128;
        }
        CHECK(u[1] == (unsigned char)(su / 4));
        CHECK(v[1] == (unsigned char)(sv / 4));
        /* Sample 0 averages columns 0 and 1. */
        int su0 = 0, sv0 = 0;
        for (int x = 0; x < 2; ++x) {
            rr = rgb[x * 3]; gg = rgb[x * 3 + 1]; bb = rgb[x * 3 + 2];
            su0 += ((-43 * rr - 85 * gg + 128 * bb + 128) >> 8) + 128;
            sv0 += ((128 * rr - 107 * gg - 21 * bb + 128) >> 8) + 128;
        }
        CHECK(u[0] == (unsigned char)(su0 / 2));
        CHECK(v[0] == (unsigned char)(sv0 / 2));
    }
    /* 3x3: chroma is 2x2; last chroma row replicates row 2. */
    {
        enum { W = 3, H = 3 };
        unsigned char rgb[W * H * 3];
        for (int i = 0; i < W * H * 3; ++i) rgb[i] = (unsigned char)(i * 7 + 3);
        unsigned char y[W * H], u[4], v[4];
        CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 2, v, 2) == 0);
        /* yuv420 layout for w=h=3 is cw=2, ch=2 -> 4 samples. */
        CHECK(yuv420_to_rgb(y, W, u, 2, v, 2, W, H, rgb, W * 3) == 0);
    }
    /* Decode chroma lookup: pixel (x,y) uses sample (x/2, y/2). */
    {
        enum { W = 5, H = 5 };
        unsigned char y[W * H];
        unsigned char u[9], v[9];
        memset(y, 128, sizeof y);
        for (int i = 0; i < 9; ++i) { u[i] = (unsigned char)(100 + i); v[i] = (unsigned char)(120 + i); }
        unsigned char out[W * H * 3];
        CHECK(yuv420_to_rgb(y, W, u, 3, v, 3, W, H, out, W * 3) == 0);
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                int si = (j / 2) * 3 + (i / 2);
                unsigned char ref[3];
                /* independent decode of the same sample */
                int yy = 128, uu = u[si] - 128, vv = v[si] - 128;
                int r = yy + ((1436 * vv) >> 10);
                int g = yy - ((352 * uu + 731 * vv) >> 10);
                int b = yy + ((1815 * uu) >> 10);
                ref[0] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
                ref[1] = (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g);
                ref[2] = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b);
                CHECK(memcmp(out + ((size_t)j * W + i) * 3, ref, 3) == 0);
            }
    }
}

/* ---------- 5. packed roundtrip bound retained ---------- */

static void test_roundtrip_bound(void) {
    enum { W = 9, H = 7 };
    unsigned char rgb[W * H * 3], back[W * H * 3];
    unsigned char y[W * H], u[8 * 4], v[8 * 4];
    fill_ramp(rgb, W, H);
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 8, v, 8) == 0);
    memset(back, 0, sizeof back);
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, back, W * 3) == 0);
    int worst = 0;
    for (int i = 0; i < W * H * 3; ++i) {
        int d = (int)back[i] - (int)rgb[i];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    CHECK(worst <= 60);
    printf("packed roundtrip worst delta %d\n", worst);
}

int main(void) {
    printf("== packed golden (must match pre-change packed output) ==\n");
    test_packed_golden();
    printf("== strided vs packed ==\n");
    test_strided_equivalence(9, 7, 0, 1, 1, 1);
    test_strided_equivalence(9, 7, 2, 7, 5, 3);
    test_strided_equivalence(64, 48, 1, 16, 11, 9);
    test_strided_equivalence(37, 23, 2, 5, 32, 8);
    test_strided_equivalence(1, 1, 0, 13, 7, 3);
    test_strided_equivalence(1, 3, 1, 2, 2, 2);
    test_strided_equivalence(3, 1, 2, 2, 2, 2);
    test_strided_equivalence(3, 3, 0, 31, 17, 5);
    printf("== stride validation ==\n");
    test_stride_rejection();
    printf("== odd geometry ==\n");
    test_odd_geometry();
    printf("== packed roundtrip ==\n");
    test_roundtrip_bound();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
