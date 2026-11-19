#ifndef GRAD_H
#define GRAD_H
int grad_magnitude(const unsigned char* src, int w, int h, float* out);
int grad_edge_count(const float* mag, int w, int h, float thresh);
#endif
