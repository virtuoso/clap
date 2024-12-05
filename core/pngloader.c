// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <png.h>
#include "util.h"
#include "librarian.h"
#include "logger.h"
#include "pngloader.h"

static unsigned char *parse_png(png_structp png, png_infop info, int *width, int *height,
                                int *has_alpha)
{
    png_bytep *row_pointers;
    png_byte  *buffer = NULL;
    png_byte color_type;
    png_byte bit_depth;
    int y, rowsz;

    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    *width  = png_get_image_width(png, info);
    *height = png_get_image_height(png, info);
    color_type = png_get_color_type(png, info);
    *has_alpha = color_type & PNG_COLOR_MASK_ALPHA ? 1 : 0;
    bit_depth = png_get_bit_depth(png, info);
    rowsz = png_get_rowbytes(png, info);
    dbg("image %dx%u color_type %d bit_depth %d rowbytes %d\n", *width, *height, color_type, bit_depth, rowsz);

    //number_of_passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);

    /* read file */
    if (setjmp(png_jmpbuf(png))) {
        err("[read_png_file] Error during read_image\n");
        goto out;
    }

    row_pointers = malloc(sizeof(png_bytep) * *height);
    buffer = malloc(sizeof(png_byte) * *height * rowsz);
    for (y = 0; y < *height; y++)
        row_pointers[y] = &buffer[rowsz * y];

    png_read_image(png, row_pointers);

    png_destroy_read_struct(&png, &info, NULL);
    free(row_pointers);

out:
    return buffer;
}

unsigned char *fetch_png(const char *file_name, int *width, int *height, int *has_alpha)
{
    LOCAL(lib_handle, lh);
    size_t size;
    void *buf;

    lh = lib_read_file(RES_ASSET, file_name, &buf, &size);
    if (!lh)
        return NULL;

    return decode_png(buf, size, width, height, has_alpha);
}

struct png_cursor {
    void *buf;
    off_t offset;
    size_t length;
};

static void png_read_mem(png_structp png, png_bytep data, png_size_t length)
{
    struct png_cursor *c = png_get_io_ptr(png);

    if (c->offset + length > c->length)
        length = c->length - c->offset;

    memcpy(data, c->buf + c->offset, length);
    c->offset += length;
}

unsigned char *decode_png(void *buf, size_t length, int *width, int *height, int *has_alpha)
{
    struct png_cursor cursor = { .buf = buf, .offset = 8, .length = length - 8 };
    unsigned char *header = buf;
    png_structp png;
    png_infop info;

    if (png_sig_cmp(header, 0, 8)) {
        err("buffer is not recognized as a PNG file\n");
        return NULL;
    }

    /* initialize stuff */
    CHECK(png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL));
    CHECK(info = png_create_info_struct(png));

    if (setjmp(png_jmpbuf(png))) {
        err("error during something\n");
        return NULL;
    }

    png_set_read_fn(png, &cursor, png_read_mem);

    return parse_png(png, info, width, height, has_alpha);
}
