#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <png.h>
#include "util.h"
#include "librarian.h"
#include "logger.h"

unsigned char *fetch_png(const char *file_name, int *width, int *height)
{
    LOCAL(FILE, fp);
    LOCAL(char, uri);
    unsigned char header[8]; // 8 is the maximum size that can be checked
    png_byte color_type;
    png_byte bit_depth;
    png_structp png_ptr;
    png_infop info_ptr;
    //int number_of_passes;
    png_bytep *row_pointers;
    png_byte *buffer = NULL;
    int y, rowsz;

    uri = lib_figure_uri(RES_ASSET, file_name);
    if (!uri)
        return NULL;
    /* open file and test for it being a png */
    fp = fopen(uri, "rb");
    if (!fp) {
        err("[read_png_file] File %s could not be opened for reading", file_name);
        goto out;
    }
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        err("[read_png_file] File %s is not recognized as a PNG file", file_name);
        goto out;
    }


    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) {
        err("[read_png_file] png_create_read_struct failed\n");
        goto out;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        err("[read_png_file] png_create_info_struct failed\n");
        goto out;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        err("[read_png_file] Error during init_io\n");
        goto out;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    *width = png_get_image_width(png_ptr, info_ptr);
    *height = png_get_image_height(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    rowsz = png_get_rowbytes(png_ptr, info_ptr);
    dbg("image %dx%u color_type %d bit_depth %d rowbytes %d\n", *width, *height, color_type, bit_depth,
        rowsz);

    //number_of_passes = png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);


    /* read file */
    if (setjmp(png_jmpbuf(png_ptr))) {
        err("[read_png_file] Error during read_image\n");
        goto out;
    }

    row_pointers = malloc(sizeof(png_bytep) * *height);
    buffer = malloc(sizeof(png_byte) * *height * rowsz);
    for (y = 0; y < *height; y++)
        row_pointers[y] = &buffer[rowsz * y];

    png_read_image(png_ptr, row_pointers);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    free(row_pointers);

out:
    return buffer;
}
