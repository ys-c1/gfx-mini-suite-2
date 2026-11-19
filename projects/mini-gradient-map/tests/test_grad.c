#include "../src/grad.h"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    enum { W = 16, H = 16 };
    unsigned char img[W * H];
    float mag[W * H];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) img[y * W + x] = (unsigned char)(x < 8 ? 0 : 255);
    CHECK(grad_magnitude(img, W, H, mag) == 0);
    float mid = mag[8 * W + 7];
    CHECK(mid > 100.0f);
    CHECK(mag[0] < 1e-3f);
    int edges = grad_edge_count(mag, W, H, 50.0f);
    CHECK(edges > 0 && edges < W * H);
    CHECK(grad_edge_count(mag, W, H, -1.0f) == -1);
    CHECK(grad_magnitude(NULL, W, H, mag) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
