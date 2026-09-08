/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_FONT_H__
#define __CLAP_FONT_H__

#include "draw.h"
#include "linmath.h"
#include "triangulate.h"

struct glyph {
    canvas          *canvas;
    unsigned int width;
    unsigned int height;
    int     bearing_x;
    int     bearing_y;
    int     advance_x;
    int     advance_y;
    bool    loaded;
};

/**
 * struct glyph_outline - a glyph outline flattened into closed polylines
 * @points:      contour points in em units, X right, Y up, origin at the
 *               pen position on the baseline (bearing included)
 * @contours:    the contours as rings of @points, implicitly closed
 * @nr_points:   total number of points
 * @nr_contours: number of contours
 * @advance:     horizontal pen advance to the next glyph, em units
 *
 * Winding order of the contours is not normalized; fill contours are
 * told from holes by containment (e.g. with poly_rings_classify()).
 */
typedef struct glyph_outline {
    vec2            *points;
    poly_ring       *contours;
    size_t          nr_points;
    size_t          nr_contours;
    float           advance;
} glyph_outline;

cresp_ret(glyph_outline);
DECLARE_CLEANUP(glyph_outline);

struct font;
typedef struct font_context font_context;

DEFINE_REFCLASS_INIT_OPTIONS(font,
    font_context    *ctx;
    const char      *name;
    unsigned int    size;
);
DECLARE_REFCLASS(font);

cresp_ret(font_context);

cresp(font_context) font_init(const char *default_font_name);
void font_done(font_context *ctx);
const char *font_name(struct font *font);
void         font_put(struct font *font);
struct font *font_get(struct font *font);
struct font *font_get_default(font_context *ctx);
struct glyph *font_get_glyph(struct font *font, unsigned char c);

/**
 * font_glyph_outline() - extract a glyph's outline as flattened contours
 * @font:        font
 * @c:           character
 * @nr_segments: points per outline curve; 0 picks adaptively
 *
 * Load a glyph's outline and flatten its Bezier curves into closed
 * polylines. Empty glyphs (e.g. space) yield a valid outline with zero
 * contours and a meaningful glyph_outline::advance.
 *
 * Return: glyph_outline to be freed with glyph_outline_free(), or
 * CERR_NOT_FOUND if the glyph can't be loaded, CERR_NOT_SUPPORTED if
 * the font isn't an outline font, CERR_NOMEM.
 */
cresp_check(glyph_outline) font_glyph_outline(struct font *font, unsigned char c,
                                              unsigned int nr_segments);

/**
 * glyph_outline_free() - free a glyph outline
 * @outline:    outline from font_glyph_outline(), or nullptr
 */
void glyph_outline_free(glyph_outline *outline);

/**
 * font_glyph_kerning() - kerning adjustment for a glyph pair
 * @font:   font
 * @left:   left character of the pair
 * @right:  right character of the pair
 *
 * Return: horizontal pen adjustment between @left and @right, em units;
 * 0.0 when the font has no kerning for the pair.
 */
float font_glyph_kerning(struct font *font, unsigned char left, unsigned char right);

/**
 * font_line_height() - baseline-to-baseline distance
 * @font:   font
 *
 * Return: the font's default line height, em units.
 */
float font_line_height(struct font *font);

#endif /* __CLAP_FONT_H__ */
