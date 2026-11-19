/*
 * Hard proof that the conversion never touches row padding.
 *
 * Each plane is laid out so that its per-row payload and its per-row
 * padding start on page boundaries.  The padding pages are then marked
 * PROT_NONE with mprotect.  Any read or write of a single padding byte
 * raises SIGSEGV and kills the process.
 *
 * The geometry is chosen so this is exact:
 *   w  = 8192   -> rgb payload 24576 B (6 pages), y payload 8192 B
 *                  (2 pages), chroma payload (w+1)/2 = 4096 B (1 page)
 *   padding = one full page per row on every plane.
 *
 * The test:
 *   1. computes a packed reference conversion,
 *   2. runs encode with padding of the RGB input and of all three YUV
 *      outputs protected,
 *   3. runs decode with padding of the YUV inputs and of the RGB output
 *      protected,
 *   4. after unprotecting, checks every padding byte still holds its
 *      original poison value and that used pixels match the reference.
 */
#define _POSIX_C_SOURCE 200809L

#include "../src/chroma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#define HAVE_MPROTECT 1
#endif

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#ifdef HAVE_MPROTECT

#include <sys/mman.h>
#include <unistd.h>

static size_t page_size(void) {
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (size_t)p : 4096u;
}

/* Zeroed, page-aligned block of exactly `bytes` bytes (bytes is page aligned). */
static unsigned char* page_alloc(size_t bytes) {
    void* p = NULL;
    if (posix_memalign(&p, page_size(), bytes) != 0) return NULL;
    return (unsigned char*)p;
}

static int protect(unsigned char* p, size_t bytes, int prot) {
    return mprotect(p, bytes, prot) == 0;
}

int main(void) {
    /* w chosen so every plane's payload is a whole number of pages. */
    const int w = 8192, h = 4;
    const int cw = (w + 1) / 2, ch = (h + 1) / 2; /* 4096, 2 */
    const size_t pg = page_size();
    const size_t rgb_used = (size_t)w * 3, y_used = (size_t)w, c_used = (size_t)cw;
    /* one page of padding per row, on every plane */
    const size_t rgb_stride = rgb_used + pg, y_stride = y_used + pg, c_stride = c_used + pg;

    CHECK(rgb_used % pg == 0 && y_used % pg == 0 && c_used % pg == 0);

    /* ---- packed reference (unprotected) ---- */
    unsigned char* prgb = malloc(rgb_used * (size_t)h);
    unsigned char* pyuv = malloc(y_used * (size_t)h);
    unsigned char* pu = malloc(c_used * (size_t)ch);
    unsigned char* pv = malloc(c_used * (size_t)ch);
    unsigned char* pback = malloc(rgb_used * (size_t)h);
    if (!prgb || !pyuv || !pu || !pv || !pback) { printf("OOM\n"); return 2; }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            unsigned char* p = prgb + (size_t)j * rgb_used + (size_t)i * 3;
            p[0] = (unsigned char)((i * 7 + j * 3) & 255);
            p[1] = (unsigned char)((i * 13 + j * 5) & 255);
            p[2] = (unsigned char)((i * 3 + j * 29) & 255);
        }
    CHECK(rgb_to_yuv420(prgb, w, h, (int)rgb_used, pyuv, (int)y_used, pu, cw, pv, cw) == 0);
    CHECK(yuv420_to_rgb(pyuv, (int)y_used, pu, cw, pv, cw, w, h, pback, (int)rgb_used) == 0);

    /* ---- strided buffers, padding filled with unique poison ---- */
    const unsigned char po_rgb = 0x5A, po_y = 0xA5, po_c = 0xC3;
    unsigned char* srgb = page_alloc(rgb_stride * (size_t)h);
    unsigned char* sy = page_alloc(y_stride * (size_t)h);
    unsigned char* su = page_alloc(c_stride * (size_t)ch);
    unsigned char* sv = page_alloc(c_stride * (size_t)ch);
    unsigned char* sback = page_alloc(rgb_stride * (size_t)h);
    if (!srgb || !sy || !su || !sv || !sback) { printf("OOM\n"); return 2; }
    memset(srgb, po_rgb, rgb_stride * (size_t)h);
    memset(sy, po_y, y_stride * (size_t)h);
    memset(su, po_c, c_stride * (size_t)ch);
    memset(sv, po_c, c_stride * (size_t)ch);
    memset(sback, po_rgb, rgb_stride * (size_t)h);
    for (int j = 0; j < h; ++j)
        memcpy(srgb + (size_t)j * rgb_stride, prgb + (size_t)j * rgb_used, rgb_used);

    /* ---- mark every padding page PROT_NONE ---- */
    int prot_ok = 1;
    for (int j = 0; j < h; ++j) {
        prot_ok &= protect(srgb + (size_t)j * rgb_stride + rgb_used, pg, PROT_NONE);
        prot_ok &= protect(sy + (size_t)j * y_stride + y_used, pg, PROT_NONE);
        prot_ok &= protect(sback + (size_t)j * rgb_stride + rgb_used, pg, PROT_NONE);
    }
    for (int j = 0; j < ch; ++j) {
        prot_ok &= protect(su + (size_t)j * c_stride + c_used, pg, PROT_NONE);
        prot_ok &= protect(sv + (size_t)j * c_stride + c_used, pg, PROT_NONE);
    }
    CHECK(prot_ok);
    if (!prot_ok) { printf("mprotect unavailable, aborting\n"); return 2; }

    /*
     * Encode: reads RGB payload only, writes Y/U/V payload only.
     * Decode: reads Y/U/V payload only, writes RGB payload only.
     * A single padding byte access would fault here.
     */
    CHECK(rgb_to_yuv420(srgb, w, h, (int)rgb_stride, sy, (int)y_stride,
                        su, (int)c_stride, sv, (int)c_stride) == 0);
    CHECK(yuv420_to_rgb(sy, (int)y_stride, su, (int)c_stride, sv, (int)c_stride,
                        w, h, sback, (int)rgb_stride) == 0);

    /* ---- unprotect and verify ---- */
    for (int j = 0; j < h; ++j) {
        protect(srgb + (size_t)j * rgb_stride + rgb_used, pg, PROT_READ | PROT_WRITE);
        protect(sy + (size_t)j * y_stride + y_used, pg, PROT_READ | PROT_WRITE);
        protect(sback + (size_t)j * rgb_stride + rgb_used, pg, PROT_READ | PROT_WRITE);
    }
    for (int j = 0; j < ch; ++j) {
        protect(su + (size_t)j * c_stride + c_used, pg, PROT_READ | PROT_WRITE);
        protect(sv + (size_t)j * c_stride + c_used, pg, PROT_READ | PROT_WRITE);
    }

    size_t checked_padding = 0, changed_padding = 0;
    for (int j = 0; j < h; ++j) {
        for (size_t i = rgb_used; i < rgb_stride; ++i, ++checked_padding)
            if (srgb[(size_t)j * rgb_stride + i] != po_rgb) ++changed_padding;
        for (size_t i = y_used; i < y_stride; ++i, ++checked_padding)
            if (sy[(size_t)j * y_stride + i] != po_y) ++changed_padding;
        for (size_t i = rgb_used; i < rgb_stride; ++i, ++checked_padding)
            if (sback[(size_t)j * rgb_stride + i] != po_rgb) ++changed_padding;
    }
    for (int j = 0; j < ch; ++j) {
        for (size_t i = c_used; i < c_stride; ++i, ++checked_padding) {
            if (su[(size_t)j * c_stride + i] != po_c) ++changed_padding;
            if (sv[(size_t)j * c_stride + i] != po_c) ++changed_padding;
        }
    }
    CHECK(checked_padding > 0);
    CHECK(changed_padding == 0);
    printf("padding bytes checked byte-for-byte: %zu, changed: %zu\n", checked_padding, changed_padding);

    /* Compare the payloads row by row; the strided planes are not contiguous. */
    for (int j = 0; j < h; ++j)
        CHECK(memcmp(sy + (size_t)j * y_stride, pyuv + (size_t)j * y_used, y_used) == 0);
    for (int j = 0; j < ch; ++j) {
        CHECK(memcmp(su + (size_t)j * c_stride, pu + (size_t)j * c_used, c_used) == 0);
        CHECK(memcmp(sv + (size_t)j * c_stride, pv + (size_t)j * c_used, c_used) == 0);
    }
    for (int j = 0; j < h; ++j)
        CHECK(memcmp(sback + (size_t)j * rgb_stride, pback + (size_t)j * rgb_used, rgb_used) == 0);
    for (int j = 0; j < h; ++j)
        CHECK(memcmp(srgb + (size_t)j * rgb_stride, prgb + (size_t)j * rgb_used, rgb_used) == 0);

    free(prgb); free(pyuv); free(pu); free(pv); free(pback);
    free(srgb); free(sy); free(su); free(sv); free(sback);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}

#else

int main(void) {
    printf("mprotect not available on this platform; guard test skipped\n");
    return 0;
}

#endif
