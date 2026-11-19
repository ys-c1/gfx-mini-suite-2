#ifndef CHROMA_H
#define CHROMA_H

/*
 * RGB <-> YUV 4:2:0 planar conversion.
 *
 * All strides are in BYTES and describe the distance between the first
 * byte of consecutive rows.  A stride may be larger than the number of
 * bytes actually used by a row; the extra bytes ([used, stride) at the end
 * of every row) are padding.  Padding is never read and never written.
 * A stride that is negative, zero, or smaller than the row's used width is
 * rejected and both functions return -1.
 *
 * Minimum strides:
 *   rgb        w * 3 bytes  (3 bytes per pixel)
 *   y          w     bytes
 *   u, v       cw    bytes, cw = (w + 1) / 2
 *
 * 4:2:0 geometry for odd dimensions is fixed:
 *   chroma plane size  cw x ch, cw = (w+1)/2, ch = (h+1)/2
 *   chroma sample (i,j) covers source pixels (2i, 2j) .. (2i+1, 2j+1);
 *   coordinates that fall outside the image are clamped to the last
 *   column/row (edge replication), so a lone last column or row is
 *   averaged as if duplicated.  Decoding maps pixel (x,y) back to
 *   chroma sample (x/2, y/2) with truncating division.
 */
int rgb_to_yuv420(const unsigned char* rgb, int w, int h, int rgb_stride,
                  unsigned char* y, int y_stride,
                  unsigned char* u, int u_stride,
                  unsigned char* v, int v_stride);
int yuv420_to_rgb(const unsigned char* y, int y_stride,
                  const unsigned char* u, int u_stride,
                  const unsigned char* v, int v_stride,
                  int w, int h, unsigned char* rgb, int rgb_stride);

#endif
