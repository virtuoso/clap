// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exr.h"
#include "memory.h"
#include "test.h"
#include "util.h"

/*
 * The writer's output, read back the way an EXR reader walks it: magic
 * and version, the header attributes, one offset per scanline, then per
 * scanline its y, its size and the planes in channel order (A, B, G, R).
 * The expected bytes come from the file layout, not from the writer.
 */

/** TEST_EXR_PATH - where the tests write, in the test's working directory */
#define TEST_EXR_PATH "test-exr.exr"

/** EXR_MAGIC - the first four bytes of every OpenEXR file */
#define EXR_MAGIC 20000630
/** EXR_VERSION - format version 2, no flags: single-part scanline, short names */
#define EXR_VERSION 2
/** EXR_PIXEL_HALF - the chlist pixel type of 16-bit floats */
#define EXR_PIXEL_HALF 1
/** EXR_PIXEL_FLOAT - the chlist pixel type of 32-bit floats */
#define EXR_PIXEL_FLOAT 2
/** EXR_CHANNEL_SIZE - a chlist entry with a one-letter name: name and NUL, type, pLinear, reserved, sampling */
#define EXR_CHANNEL_SIZE (2 + 4 + 1 + 3 + 4 + 4)

/**
 * file_read() - read a whole file into a buffer of its own
 * @path:   the file
 * @size:   set to the file's size
 *
 * Return: the buffer, for the caller to free; CERR_NOT_FOUND if the file
 *         can't be opened, CERR_NOMEM if the buffer can't be allocated,
 *         CERR_INVALID_OPERATION if it can't be read
 */
static cresp(void) file_read(const char *path, size_t *size)
{
    LOCAL_SET(FILE, f) = fopen(path, "rb");
    if (!f)                                 return cresp_error(void, CERR_NOT_FOUND);
    if (fseek(f, 0, SEEK_END))              return cresp_error(void, CERR_INVALID_OPERATION);

    long len = ftell(f);
    if (len <= 0 || fseek(f, 0, SEEK_SET))  return cresp_error(void, CERR_INVALID_OPERATION);

    LOCAL_SET(uchar, buf) = mem_alloc(len);
    if (!buf)                               return cresp_error(void, CERR_NOMEM);
    if (fread(buf, len, 1, f) != 1)         return cresp_error(void, CERR_INVALID_OPERATION);

    *size = len;
    return cresp_val(void, NOCU(buf));
}

/* An attribute the header must hold, byte for byte */
typedef struct expected_attr {
    const char      *name;
    const char      *type;
    const void      *data;
    size_t          size;
    bool            seen;
} expected_attr;

/**
 * header_check() - walk the header's attributes against the expected ones
 * @buf:            the file
 * @len:            its size
 * @expected:       the attributes the header must hold, in any order
 * @nr_expected:    how many there are
 * @header_end:     set to where the header ends and the offsets start
 *
 * Return: CERR_OK if the header holds exactly @expected, each once and
 *         nothing else; CERR_NOT_FOUND if one is missing;
 *         CERR_INVALID_ARGUMENTS for anything else: a header running past
 *         the file, an attribute that isn't expected or is repeated, or
 *         one whose type, size or bytes differ
 */
static cerr header_check(const uchar *buf, size_t len, expected_attr *expected, size_t nr_expected,
                         size_t *header_end)
{
    size_t pos = 2 * sizeof(int32_t);

    while (pos < len && buf[pos]) {
        const char *name = (const char *)buf + pos;
        size_t name_len = strnlen(name, len - pos);
        if (pos + name_len + 1 >= len)              return CERR_INVALID_ARGUMENTS;

        const char *type = name + name_len + 1;
        size_t type_len = strnlen(type, len - pos - name_len - 1);
        size_t data = pos + name_len + 1 + type_len + 1 + sizeof(int32_t);
        if (data > len)                             return CERR_INVALID_ARGUMENTS;

        int32_t size;
        memcpy(&size, buf + data - sizeof(int32_t), sizeof(size));
        if (size < 0 || (size_t)size > len - data)  return CERR_INVALID_ARGUMENTS;

        expected_attr *attr = NULL;
        for (size_t i = 0; i < nr_expected; i++)
            if (!strcmp(expected[i].name, name))    attr = &expected[i];

        if (!attr || attr->seen)                    return CERR_INVALID_ARGUMENTS;
        if (strcmp(attr->type, type))               return CERR_INVALID_ARGUMENTS;
        if ((size_t)size != attr->size)             return CERR_INVALID_ARGUMENTS;
        if (memcmp(buf + data, attr->data, size))   return CERR_INVALID_ARGUMENTS;

        attr->seen = true;
        pos = data + size;
    }

    if (pos >= len)     return CERR_INVALID_ARGUMENTS;

    for (size_t i = 0; i < nr_expected; i++)
        if (!expected[i].seen)  return CERR_NOT_FOUND;

    *header_end = pos + 1;
    return CERR_OK;
}

/*
 * The source component a plane value comes from: a different value per
 * pixel, row and channel, until half components keep only the low 16 bits
 */
static uint32_t pattern(unsigned int x, unsigned int y, unsigned int c, unsigned int width, unsigned int nr_comps)
{
    return ((y * width + x) * nr_comps + c) * 7 + 1;
}

typedef struct layout_case {
    unsigned int    width;
    unsigned int    height;
    unsigned int    nr_comps;
    unsigned int    comp_size;
    /* bytes after each row's pixels */
    unsigned int    row_padding;
    bool            flip;
    bool            chromaticities;
} layout_case;

static int exr_layout_check(const layout_case *lc)
{
    static const float chromaticities[] = { 0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f };
    size_t row_pitch = (size_t)lc->width * lc->nr_comps * lc->comp_size + lc->row_padding;

    LOCAL_SET(uchar, pixels) = mem_alloc(row_pitch * lc->height, .zero = 1);
    if (!pixels)    return EXIT_FAILURE;

    for (unsigned int y = 0; y < lc->height; y++)
        for (unsigned int x = 0; x < lc->width; x++)
            for (unsigned int c = 0; c < lc->nr_comps; c++) {
                uint32_t v = pattern(x, y, c, lc->width, lc->nr_comps);
                uchar *dst = pixels + y * row_pitch + ((size_t)x * lc->nr_comps + c) * lc->comp_size;

                if (lc->comp_size == 2) memcpy(dst, &(uint16_t){ v }, 2);
                else                    memcpy(dst, &v, 4);
            }

    exr_image img = {
        .pixels         = pixels,
        .chromaticities = lc->chromaticities ? chromaticities : NULL,
        .row_pitch      = row_pitch,
        .width          = lc->width,
        .height         = lc->height,
        .nr_comps       = lc->nr_comps,
        .comp_size      = lc->comp_size,
        .flip           = lc->flip,
    };
    CERR_RET(exr_write_file(TEST_EXR_PATH, &img), return EXIT_FAILURE);

    size_t len = 0;
    LOCAL_SET(uchar, buf) = CRES_RET(file_read(TEST_EXR_PATH, &len), { remove(TEST_EXR_PATH); return EXIT_FAILURE; });
    remove(TEST_EXR_PATH);

    int32_t magic, version;
    if (len < sizeof(magic) + sizeof(version))          return EXIT_FAILURE;

    memcpy(&magic, buf, sizeof(magic));
    memcpy(&version, buf + sizeof(magic), sizeof(version));
    if (magic != EXR_MAGIC || version != EXR_VERSION)   return EXIT_FAILURE;

    /* channels sorted by name, the reverse of the interleaved order */
    uchar chlist[4 * EXR_CHANNEL_SIZE + 1];
    size_t chlist_len = 0;
    for (unsigned int i = 0; i < lc->nr_comps; i++) {
        int32_t pixel_type = lc->comp_size == 2 ? EXR_PIXEL_HALF : EXR_PIXEL_FLOAT, sampling = 1;
        uchar *ch = chlist + chlist_len;

        ch[0] = "RGBA"[lc->nr_comps - 1 - i];
        ch[1] = 0;
        memcpy(ch + 2, &pixel_type, 4);
        /* pLinear and the three reserved bytes */
        memset(ch + 6, 0, 4);
        memcpy(ch + 10, &sampling, 4);
        memcpy(ch + 14, &sampling, 4);
        chlist_len += EXR_CHANNEL_SIZE;
    }
    chlist[chlist_len++] = 0;

    int32_t window[] = { 0, 0, (int32_t)lc->width - 1, (int32_t)lc->height - 1 };
    float one = 1.0f, center[] = { 0.0f, 0.0f };
    uchar zero = 0;
    expected_attr expected[] = {
        { .name = "channels",           .type = "chlist",           .data = chlist,     .size = chlist_len },
        { .name = "compression",        .type = "compression",      .data = &zero,      .size = 1 },
        { .name = "dataWindow",         .type = "box2i",            .data = window,     .size = sizeof(window) },
        { .name = "displayWindow",      .type = "box2i",            .data = window,     .size = sizeof(window) },
        { .name = "lineOrder",          .type = "lineOrder",        .data = &zero,      .size = 1 },
        { .name = "pixelAspectRatio",   .type = "float",            .data = &one,       .size = sizeof(one) },
        { .name = "screenWindowCenter", .type = "v2f",              .data = center,     .size = sizeof(center) },
        { .name = "screenWindowWidth",  .type = "float",            .data = &one,       .size = sizeof(one) },
        { .name = "chromaticities",     .type = "chromaticities",   .data = chromaticities,
          .size = sizeof(chromaticities) },
    };
    size_t nr_expected = array_size(expected) - !lc->chromaticities;

    size_t header_end;
    CERR_RET(header_check(buf, len, expected, nr_expected, &header_end), return EXIT_FAILURE);

    /* the offsets, then the chunks they point at, back to back to the end of the file */
    size_t line_bytes = (size_t)lc->width * lc->nr_comps * lc->comp_size;
    size_t chunk_bytes = 2 * sizeof(int32_t) + line_bytes;
    size_t body = header_end + sizeof(uint64_t) * lc->height;
    if (len != body + chunk_bytes * lc->height) return EXIT_FAILURE;

    for (unsigned int y = 0; y < lc->height; y++) {
        uint64_t offset;
        int32_t line_y, line_size;

        memcpy(&offset, buf + header_end + y * sizeof(uint64_t), sizeof(offset));
        if (offset != body + y * chunk_bytes)                           return EXIT_FAILURE;

        const uchar *chunk = buf + offset;
        memcpy(&line_y, chunk, sizeof(line_y));
        memcpy(&line_size, chunk + sizeof(line_y), sizeof(line_size));
        if (line_y != (int32_t)y || line_size != (int32_t)line_bytes)   return EXIT_FAILURE;

        unsigned int src_y = lc->flip ? lc->height - 1 - y : y;
        const uchar *plane = chunk + 2 * sizeof(int32_t);
        for (unsigned int i = 0; i < lc->nr_comps; i++, plane += (size_t)lc->width * lc->comp_size) {
            unsigned int c = lc->nr_comps - 1 - i;

            for (unsigned int x = 0; x < lc->width; x++) {
                uint32_t want = pattern(x, src_y, c, lc->width, lc->nr_comps), got = 0;

                if (lc->comp_size == 2) {
                    uint16_t h;
                    memcpy(&h, plane + x * 2, 2);
                    got = h;
                    want &= 0xffff;
                } else {
                    memcpy(&got, plane + x * 4, 4);
                }

                if (got != want)    return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}

static int exr_layout_test(void)
{
    static const layout_case cases[] = {
        { .width = 7, .height = 5, .nr_comps = 4, .comp_size = 2 },
        { .width = 7, .height = 5, .nr_comps = 4, .comp_size = 2, .flip = true, .row_padding = 6 },
        { .width = 3, .height = 4, .nr_comps = 3, .comp_size = 4, .chromaticities = true },
        { .width = 5, .height = 3, .nr_comps = 4, .comp_size = 4, .flip = true, .chromaticities = true,
          .row_padding = 12 },
        { .width = 1, .height = 1, .nr_comps = 3, .comp_size = 2 },
        /* a frame's worth, for the size arithmetic */
        { .width = 1280, .height = 720, .nr_comps = 4, .comp_size = 2, .flip = true, .chromaticities = true },
    };

    for (size_t i = 0; i < array_size(cases); i++)
        if (exr_layout_check(&cases[i]))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* Does the writer refuse @img with CERR_INVALID_ARGUMENTS? */
static bool exr_refuses(const char *path, const exr_image *img)
{
    cerr err = exr_write_file(path, img);
    return IS_CERR_CODE(err, CERR_INVALID_ARGUMENTS);
}

static int exr_invalid_test(void)
{
    uint16_t pixel[4] = {};
    exr_image img = { .pixels = pixel, .row_pitch = sizeof(pixel), .width = 1, .height = 1,
                      .nr_comps = 4, .comp_size = 2 };

    exr_image bad = img;
    bad.nr_comps = 2;
    if (!exr_refuses(TEST_EXR_PATH, &bad))  return EXIT_FAILURE;
    bad = img;
    bad.comp_size = 1;
    if (!exr_refuses(TEST_EXR_PATH, &bad))  return EXIT_FAILURE;
    bad = img;
    bad.width = 0;
    if (!exr_refuses(TEST_EXR_PATH, &bad))  return EXIT_FAILURE;
    bad = img;
    bad.pixels = NULL;
    if (!exr_refuses(TEST_EXR_PATH, &bad))  return EXIT_FAILURE;
    if (!exr_refuses(NULL, &img))           return EXIT_FAILURE;

    /* no such directory: an error, not a crash */
    cerr err = exr_write_file("no-such-directory/test.exr", &img);
    if (!IS_CERR(err))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "exr layout",             .test = exr_layout_test },
    { .name = "exr invalid arguments",  .test = exr_invalid_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
