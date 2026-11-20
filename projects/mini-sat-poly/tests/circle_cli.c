/* Command-line front end for the circle/polygon query, used by
 * verify_circle.py to compare against an independent reference.
 *
 * stdin: one case per line
 *   n x0 y0 x1 y1 ... x(n-1) y(n-1) cx cy r
 * stdout: one line per case
 *   status contained area poly_area
 */
#include "../src/sat.h"
#include <stdio.h>

int main(void) {
    int n;
    while (scanf("%d", &n) == 1) {
        SatPoly p;
        double cx, cy, r;
        SatCirclePoly res;
        int i;
        if (n < 0) n = 0;
        if (n > 16) n = 16;
        for (i = 0; i < n; ++i) {
            double x, y;
            if (scanf("%lf %lf", &x, &y) != 2) return 1;
            p.p[i].x = (float)x;
            p.p[i].y = (float)y;
        }
        p.n = n;
        if (scanf("%lf %lf %lf", &cx, &cy, &r) != 3) return 1;
        sat_circle_polygon(&p, cx, cy, r, &res);
        printf("%d %d %.17g %.17g\n", res.status, res.contained,
               res.area, res.poly_area);
    }
    return 0;
}
