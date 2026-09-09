/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TEXT_MESH_H__
#define __CLAP_TEXT_MESH_H__

#include "error.h"
#include "font.h"
#include "linmath.h"
#include "mesh.h"

/**
 * struct text_mesh_opts - glyph/string mesh emitter options
 * @mesh:        mesh to append the geometry to (required)
 * @org:         pen position on the baseline; glyphs extend up/right of it
 * @size:        world units per em; 0 defaults to 1.0
 * @depth:       extrusion depth: emit side faces extending @depth behind
 *               the front face (along -Z); 0 skips the sides
 * @nr_segments: points per outline curve; 0 picks adaptively
 * @closed:      also emit the back face @depth behind the front face,
 *               closing the glyph volume (or making a double sided sheet
 *               when @depth is 0)
 *
 * The front face lies in the XY plane at @org, facing +Z.
 */
typedef struct text_mesh_opts {
    struct mesh     *mesh;
    vec3            org;
    float           size;
    float           depth;
    unsigned int    nr_segments;
    bool            closed;
} text_mesh_opts;

/**
 * _text_mesh_emit_glyph() - append one glyph's geometry to a mesh
 * @font:   font to take the glyph from
 * @c:      character
 * @opts:   emitter options
 *
 * Triangulate @c's outline and append it to @opts->mesh: the front face,
 * plus the sides and the back face if requested (see text_mesh_opts).
 * Glyphs with no outline (e.g. space) emit nothing.
 *
 * Return: CERR_OK or an error, in which case nothing was emitted;
 * CERR_TOO_LARGE means the mesh can't fit the glyph in 16-bit vertex
 * indices.
 */
cerr_check _text_mesh_emit_glyph(struct font *font, unsigned char c,
                                 const text_mesh_opts *opts);

/**
 * _text_mesh_emit_string() - append a string of glyphs to a mesh
 * @font:   font to take the glyphs from
 * @str:    the string
 * @opts:   emitter options
 *
 * Emit @str one glyph at a time, advancing the pen by each glyph's
 * advance and the pairwise kerning; '\n' returns the pen and moves it a
 * line down. On error, the mesh may be left partially emitted.
 *
 * Return: CERR_OK or the error of the first failing glyph.
 */
cerr_check _text_mesh_emit_string(struct font *font, const char *str,
                                  const text_mesh_opts *opts);

/**
 * define text_mesh_emit_glyph - append one glyph's geometry to a mesh
 * @_f:     font to take the glyph from
 * @_c:     character
 *
 * Syntax sugar for _text_mesh_emit_glyph().
 */
#define text_mesh_emit_glyph(_f, _c, args...) \
    _text_mesh_emit_glyph((_f), (_c), &(text_mesh_opts){ args })

/**
 * define text_mesh_emit_string - append a string of glyphs to a mesh
 * @_f:     font to take the glyphs from
 * @_s:     the string
 *
 * Syntax sugar for _text_mesh_emit_string().
 */
#define text_mesh_emit_string(_f, _s, args...) \
    _text_mesh_emit_string((_f), (_s), &(text_mesh_opts){ args })

#endif /* __CLAP_TEXT_MESH_H__ */
