/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_EXR_H__
#define __CLAP_EXR_H__

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

/**
 * struct exr_image - an interleaved half/float image to write out
 * @pixels:         interleaved R,G,B[,A] rows
 * @chromaticities: 8 floats (Rx,Ry,Gx,Gy,Bx,By,Wx,Wy), NULL for none
 * @row_pitch:      bytes from one row to the next
 * @width:          pixels per row
 * @height:         rows
 * @nr_comps:       components per pixel: 3 (RGB) or 4 (RGBA)
 * @comp_size:      bytes per component: 2 (half) or 4 (float)
 * @flip:           rows are stored bottom-up (GL readbacks)
 */
typedef struct exr_image {
    const void      *pixels;
    const float     *chromaticities;
    size_t          row_pitch;
    unsigned int    width;
    unsigned int    height;
    unsigned int    nr_comps;
    unsigned int    comp_size;
    bool            flip;
} exr_image;

/**
 * exr_write_file() - write an uncompressed scanline OpenEXR file
 * @path:   output file name
 * @img:    the image
 *
 * Emits the minimal single-part scanline layout: the required header
 * attributes with the channels in the byte-wise sorted (A,B,G,R) order
 * the reference library assumes, the line offset table and one
 * uncompressed chunk per scanline, de-interleaving each row on the way.
 * No compression anywhere: at recording resolutions the pixel data runs
 * into gigabytes per second and every extra pass over it drops frames.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a missing path or an image
 *         it doesn't write (no pixels, a zero size, other than 3 or 4
 *         components of 2 or 4 bytes), or the I/O or allocation error
 */
cerr_check exr_write_file(const char *path, const exr_image *img);

#endif /* __CLAP_EXR_H__ */
