/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LOGO_H__
#define __CLAP_LOGO_H__

#include "error.h"
#include "font.h"
#include "linmath.h"
#include "mesh.h"

/**
 * struct magnet_emit_opts - horseshoe magnet mesh emitter options
 * @mesh:        mesh to append the geometry to (required)
 * @org:         center of the magnet ring
 * @width:       total X extent, including the detached poles (required)
 * @depth:       extrusion depth along -Z; 0 skips the side faces
 * @taper:       angle by which the pole bars converge toward the magnet
 *               axis, degrees
 * @pole_len:    detached pole length along X; overrides @pole_ratio
 * @pole_ratio:  detached pole length as a fraction of the ring radius
 * @bar_ratio:   bar thickness as a fraction of the ring radius
 * @cut_ratio:   pole cut X position as a fraction of the ring radius
 * @gap_ratio:   body-to-pole air gap as a fraction of the ring radius
 * @round_ratio: pole tip outer corner rounding radius as a fraction of
 *               the ring radius; the inner corner is rounded at half
 *               that
 * @nr_segments: points per full circle; 0 picks the default
 * @closed:      also emit the back face, closing the volume
 *
 * The magnet is a C-shaped ring whose arms straighten out into bars
 * tangent to the ring at (90 - @taper) degrees, cut off vertically at
 * @cut_ratio, with detached pole pieces continuing the bars after an
 * air gap. Zero parameters default to the clap logo proportions. The
 * front face lies in the XY plane at @org, facing +Z, the mouth opens
 * toward +X.
 */
typedef struct magnet_emit_opts {
    struct mesh     *mesh;
    vec3            org;
    float           width;
    float           depth;
    float           taper;
    float           pole_len;
    float           pole_ratio;
    float           bar_ratio;
    float           cut_ratio;
    float           gap_ratio;
    float           round_ratio;
    unsigned int    nr_segments;
    bool            closed;
} magnet_emit_opts;

/**
 * _magnet_emit() - append a horseshoe magnet to a mesh
 * @opts:   emitter options
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS, CERR_NOMEM, or an error of the
 *         extrusion emitter, in which case nothing was emitted
 */
cerr_check _magnet_emit(const magnet_emit_opts *opts);

/**
 * define magnet_emit - append a horseshoe magnet to a mesh
 *
 * Syntax sugar for _magnet_emit(), taking magnet_emit_opts initializers.
 */
#define magnet_emit(args...) _magnet_emit(&(magnet_emit_opts){ args })

/**
 * struct logo_emit_opts - clap logo mesh emitter options
 * @mesh:         mesh to append the geometry to (required)
 * @font:         the lettering font, Chivo for the canonical logo
 *                (required)
 * @tagline_font: the tagline font, Futura-like for the canonical logo;
 *                nullptr skips the tagline
 * @text:         the lettering; "LAP" when nullptr
 * @tagline:      the tagline; "world engine" when nullptr
 * @org:          center of the magnet ring
 * @width:        total logo width (required)
 * @depth:        extrusion depth along -Z; 0 keeps the logo flat
 * @taper:        magnet pole taper angle, degrees; 0: logo default
 * @closed:       also emit back faces, closing the volumes
 *
 * The horseshoe magnet stands in for the "C": it envelops @text, whose
 * size and position follow the prototype logo proportions, and its
 * detached poles stretch to end flush with the last letter. @tagline
 * is centered underneath.
 */
typedef struct logo_emit_opts {
    struct mesh     *mesh;
    struct font     *font;
    struct font     *tagline_font;
    const char      *text;
    const char      *tagline;
    vec3            org;
    float           width;
    float           depth;
    float           taper;
    bool            closed;
} logo_emit_opts;

/**
 * _logo_emit() - append the clap logo to a mesh
 * @opts:   emitter options
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS, or the first error of the
 *         magnet or text emitters; the mesh may be left partially emitted
 */
cerr_check _logo_emit(const logo_emit_opts *opts);

/**
 * define logo_emit - append the clap logo to a mesh
 *
 * Syntax sugar for _logo_emit(), taking logo_emit_opts initializers.
 */
#define logo_emit(args...) _logo_emit(&(logo_emit_opts){ args })

#endif /* __CLAP_LOGO_H__ */
