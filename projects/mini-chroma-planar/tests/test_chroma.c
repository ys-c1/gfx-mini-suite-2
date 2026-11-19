#include "../src/chroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    enum { W = 9, H = 7 };
    unsigned char rgb[W * H * 3], back[W * H * 3];
    unsigned char y[W * H], u[8 * 4], v[8 * 4];
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            unsigned char* p = rgb + (j * W + i) * 3;
            p[0] = (unsigned char)(i * 16);
            p[1] = (unsigned char)(j * 16);
            p[2] = 100;
        }
    CHECK(rgb_to_yuv420(rgb, W, H, W * 3, y, W, u, 8, v, 8) == 0);
    memset(back, 0, sizeof(back));
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, back, W * 3) == 0);
    int worst = 0;
    for (int i = 0; i < W * H * 3; ++i) {
        int d = (int)back[i] - (int)rgb[i];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    CHECK(worst <= 60);
    CHECK(yuv420_to_rgb(y, W, u, 8, v, 8, W, H, NULL, W * 3) == -1);
    printf("worst delta %d\n", worst);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
