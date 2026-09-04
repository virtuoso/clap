// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "memory.h"
#include "util.h"
#include "exr.h"

/*
 * Single-part uncompressed scanline OpenEXR, the subset ffmpeg, macOS
 * and libOpenEXR all agree on: magic and version, a header of
 * "name\0type\0size data" attributes ended by an empty name, one uint64
 * offset per scanline, then per scanline "int32 y, int32 size" and the
 * channels' planes back to back in header order.
 */

/** EXR_MAGIC - the first four bytes of every OpenEXR file */
#define EXR_MAGIC               20000630
/** EXR_VERSION - format version 2, no flags: single-part scanline, short names */
#define EXR_VERSION             2
/** EXR_PIXEL_HALF - the chlist pixel type of 16-bit floats */
#define EXR_PIXEL_HALF          1
/** EXR_PIXEL_FLOAT - the chlist pixel type of 32-bit floats */
#define EXR_PIXEL_FLOAT         2
/** EXR_COMPRESSION_NONE - the compression attribute's value for no compression */
#define EXR_COMPRESSION_NONE    0
/** EXR_LINE_INCREASING_Y - the lineOrder attribute's value for top-down scanlines */
#define EXR_LINE_INCREASING_Y   0

/** EXR_HEADER_MAX - room for every attribute this writer emits */
#define EXR_HEADER_MAX          512

/**
 * EXR_WRITE_BUFFER - the stdio buffer exr_write_file() writes through
 *
 * A scanline chunk is a few kB, more than stdio's default buffer holds,
 * so each one goes out in writes of its own: two write() calls per line
 * with glibc, and in a browser a round trip per line to the main thread,
 * where the file system lives. With this much buffer a frame takes a
 * handful of writes.
 */
#define EXR_WRITE_BUFFER        (4u << 20)

/**
 * struct exr_header - the file header up to the offset table, in memory
 * @buf:        magic, version and the attributes
 * @len:        bytes of @buf in use
 * @overflow:   an append didn't fit in @buf
 *
 * The scanline offsets depend on its length, so it's built in full
 * before anything is written.
 */
typedef struct exr_header {
    uint8_t     buf[EXR_HEADER_MAX];
    size_t      len;
    bool        overflow;
} exr_header;

/**
 * exr_header_put() - append bytes to the header
 * @h:      the header
 * @data:   the bytes
 * @size:   their size
 *
 * An append that doesn't fit sets @h->overflow, which drops every append
 * after it, for exr_header_build() to report once.
 */
static void exr_header_put(exr_header *h, const void *data, size_t size)
{
    if (h->overflow || h->len + size > sizeof(h->buf)) {
        h->overflow = true;
        return;
    }

    memcpy(h->buf + h->len, data, size);
    h->len += size;
}

/**
 * exr_header_str() - append a string and its NUL terminator to the header
 * @h:  the header
 * @s:  the string
 */
static void exr_header_str(exr_header *h, const char *s)
{
    exr_header_put(h, s, strlen(s) + 1);
}

/**
 * exr_header_attr() - append an attribute to the header
 * @h:      the header
 * @name:   the attribute's name
 * @type:   the attribute's type name
 * @data:   the attribute's value
 * @size:   size of @data in bytes
 */
static void exr_header_attr(exr_header *h, const char *name, const char *type,
                            const void *data, int32_t size)
{
    exr_header_str(h, name);
    exr_header_str(h, type);
    exr_header_put(h, &size, sizeof(size));
    exr_header_put(h, data, size);
}

/*
 * Component names in interleaved order: the chlist must be sorted by
 * name, and walking this backwards gives "A" < "B" < "G" < "R", for RGB
 * just as much as for RGBA
 */
static const char exr_comp_names[] = "RGBA";

/**
 * exr_header_build() - build the file header for an image
 * @h:      an empty header
 * @img:    the image
 *
 * Return: CERR_OK, or CERR_BUFFER_OVERRUN if the header doesn't fit in
 *         EXR_HEADER_MAX bytes
 */
static cerr exr_header_build(exr_header *h, const exr_image *img)
{
    int32_t magic = EXR_MAGIC, version = EXR_VERSION;
    exr_header_put(h, &magic, sizeof(magic));
    exr_header_put(h, &version, sizeof(version));

    /* chlist: name, int pixel type, uchar pLinear, 3 reserved, int x/y sampling */
    uint8_t chlist[4 * (2 + 16) + 1];
    size_t chlen = 0;
    for (int c = (int)img->nr_comps - 1; c >= 0; c--) {
        int32_t pixel_type = img->comp_size == 2 ? EXR_PIXEL_HALF : EXR_PIXEL_FLOAT;
        int32_t sampling = 1;

        chlist[chlen++] = exr_comp_names[c];
        chlist[chlen++] = 0;
        memcpy(chlist + chlen, &pixel_type, 4);     chlen += 4;
        memset(chlist + chlen, 0, 4);               chlen += 4;
        memcpy(chlist + chlen, &sampling, 4);       chlen += 4;
        memcpy(chlist + chlen, &sampling, 4);       chlen += 4;
    }
    chlist[chlen++] = 0;
    exr_header_attr(h, "channels", "chlist", chlist, (int32_t)chlen);

    uint8_t compression = EXR_COMPRESSION_NONE;
    exr_header_attr(h, "compression", "compression", &compression, 1);

    int32_t window[4] = { 0, 0, (int32_t)img->width - 1, (int32_t)img->height - 1 };
    exr_header_attr(h, "dataWindow", "box2i", window, sizeof(window));
    exr_header_attr(h, "displayWindow", "box2i", window, sizeof(window));

    uint8_t line_order = EXR_LINE_INCREASING_Y;
    exr_header_attr(h, "lineOrder", "lineOrder", &line_order, 1);

    float one = 1.0f, center[2] = { 0.0f, 0.0f };
    exr_header_attr(h, "pixelAspectRatio", "float", &one, sizeof(one));
    exr_header_attr(h, "screenWindowCenter", "v2f", center, sizeof(center));
    exr_header_attr(h, "screenWindowWidth", "float", &one, sizeof(one));

    if (img->chromaticities)
        exr_header_attr(h, "chromaticities", "chromaticities", img->chromaticities,
                        8 * sizeof(float));

    /* end of header */
    uint8_t end = 0;
    exr_header_put(h, &end, 1);

    return h->overflow ? CERR_BUFFER_OVERRUN : CERR_OK;
}

/**
 * exr_plane_gather() - gather one component's plane out of an interleaved row
 * @dst:    the plane, @img->width components long
 * @row:    the interleaved row
 * @comp:   the component's index in a pixel
 * @img:    the image @row belongs to
 */
static void exr_plane_gather(void *dst, const void *row, unsigned int comp,
                             const exr_image *img)
{
    if (img->comp_size == 2) {
        const uint16_t *src = (const uint16_t *)row + comp;
        uint16_t *out = dst;

        for (unsigned int x = 0; x < img->width; x++)   out[x] = src[(size_t)x * img->nr_comps];
    } else {
        const uint32_t *src = (const uint32_t *)row + comp;
        uint32_t *out = dst;

        for (unsigned int x = 0; x < img->width; x++)   out[x] = src[(size_t)x * img->nr_comps];
    }
}

/**
 * exr_io_error() - turn a failed stdio write or close into a cerr
 *
 * Return: the cerr for errno, or CERR_INVALID_OPERATION for a short write
 *         that leaves errno unset
 */
static cerr exr_io_error(void)
{
    return errno ? errno_to_cerr(errno) : CERR_INVALID_OPERATION_REASON(.fmt = "short write");
}

cerr exr_write_file(const char *path, const exr_image *img)
{
    if (!path || !img || !img->pixels || !img->width || !img->height ||
        (img->nr_comps != 3 && img->nr_comps != 4) ||
        (img->comp_size != 2 && img->comp_size != 4))
        return CERR_INVALID_ARGUMENTS;

    exr_header hdr = {};
    CERR_RET_CERR(exr_header_build(&hdr, img));

    size_t plane_bytes = (size_t)img->width * img->comp_size;
    size_t line_bytes = plane_bytes * img->nr_comps;
    size_t chunk_bytes = 2 * sizeof(int32_t) + line_bytes;
    uint64_t body = hdr.len + sizeof(uint64_t) * img->height;
    size_t buffer_size = min(body + (uint64_t)chunk_bytes * img->height, EXR_WRITE_BUFFER);

    LOCAL_SET(void, offsets_buf) = mem_alloc(sizeof(uint64_t), .nr = img->height);
    LOCAL_SET(uchar, chunk) = mem_alloc(chunk_bytes);
    LOCAL_SET(void, buffer) = mem_alloc(buffer_size);
    if (!offsets_buf || !chunk || !buffer)  return CERR_NOMEM;

    uint64_t *offsets = offsets_buf;

    for (unsigned int y = 0; y < img->height; y++)  offsets[y] = body + (uint64_t)y * chunk_bytes;

    FILE *f = fopen(path, "wb");
    if (!f) return errno_to_cerr(errno);

    /* outlives the stream: fclose() flushes from it */
    cerr err = CERR_OK;
    if (setvbuf(f, buffer, _IOFBF, buffer_size)) {
        err = CERR_INVALID_OPERATION_REASON(.fmt = "can't buffer the output");
        goto out;
    }

    if (fwrite(hdr.buf, hdr.len, 1, f) != 1 ||
        fwrite(offsets, sizeof(uint64_t) * img->height, 1, f) != 1) {
        err = exr_io_error();
        goto out;
    }

    int32_t line_size = (int32_t)line_bytes;
    for (unsigned int y = 0; y < img->height; y++) {
        unsigned int src_y = img->flip ? img->height - 1 - y : y;
        const void *row = (const char *)img->pixels + (size_t)src_y * img->row_pitch;
        int32_t line_y = (int32_t)y;
        uchar *plane = chunk + 2 * sizeof(int32_t);

        memcpy(chunk, &line_y, sizeof(line_y));
        memcpy(chunk + sizeof(line_y), &line_size, sizeof(line_size));
        for (int c = (int)img->nr_comps - 1; c >= 0; c--, plane += plane_bytes)
            exr_plane_gather(plane, row, (unsigned int)c, img);

        if (fwrite(chunk, chunk_bytes, 1, f) != 1) {
            err = exr_io_error();
            goto out;
        }
    }

out:
    if (fclose(f) && !IS_CERR(err)) err = exr_io_error();

    return err;
}
