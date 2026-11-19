#include "../src/xform.h"
#include <cstdio>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    SpritePoint src[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    SpritePoint dst[4];
    CHECK(xform_sprite(src, 4, 2.0f, 3.0f, 1.0f, -1.0f, dst) == 0);
    CHECK(dst[1].x == 5.0f && dst[1].y == -1.0f);
    float a, b, c, d;
    CHECK(xform_bounds(dst, 4, &a, &b, &c, &d) == 0);
    CHECK(a == 1.0f && b == -1.0f && c == 5.0f && d == 2.0f);
    CHECK(xform_sprite(src, 0, 1, 1, 0, 0, dst) == -1);
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
