/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FRAME_H__
#define __CLAP_PCG_FRAME_H__

#include <math.h>
#include "error.h"
#include "linmath.h"
#include "poly2d.h"

/* lengths and other derived measures that can fail */
cres_ret(float);

/**
 * struct pcg_frame - a yaw-only placement frame
 * @org:    origin in the parent's coordinates; y is the floor the element
 *          stands on
 * @dir:    unit run direction in the parent's (x, z): the local +a axis
 * @mirror: local +c points to the right of @dir instead of the left
 *
 * Local coordinates are (a, y, c): a along @dir, y up, c across, toward
 * the left normal (-dir.z, dir.x), negated under @mirror. A frame placed
 * inside another one is in its parent's local coordinates, reading
 * (a, y, c) as (x, y, z). Everything a generator emits is transformed
 * into sink space before it lands in a mesh: static trimesh colliders
 * ignore an entity's rotation, so rotation and mirroring are baked into
 * the vertices and placement only translates. A mirrored frame turns the
 * winding over; the shapes wind their faces the other way to keep them
 * pointing out.
 */
typedef struct pcg_frame {
    vec3    org;
    vec2    dir;
    bool    mirror;
} pcg_frame;

/** PCG_FRAME_IDENTITY - the parent's own coordinates */
#define PCG_FRAME_IDENTITY ((pcg_frame){ .dir = { 1.0f, 0.0f } })

/**
 * pcg_frame_check() - check that a frame places anything anywhere
 * @f:  the frame
 *
 * Everything an element emits goes through its frame: an origin or a
 * direction that isn't finite turns every vertex into NaN, a zero
 * direction folds the element into a line, and one that isn't a unit
 * vector stretches the element along it. A site's frame is checked
 * with the rest of the site, before anything is emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @f, an origin
 * or a direction that isn't finite, or a direction that isn't a unit
 * vector
 */
static inline cerr_check pcg_frame_check(const pcg_frame *f)
{
    if (!f)     return CERR_INVALID_ARGUMENTS;
    if (!isfinite(f->org[0]) || !isfinite(f->org[1]) || !isfinite(f->org[2]) ||
        !isfinite(f->dir[0]) || !isfinite(f->dir[1]))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "frame origin or direction isn't finite");
    if (!(fabsf(vec2_len(f->dir) - 1.0f) <= 1e-4f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "frame direction isn't a unit vector");

    return CERR_OK;
}

/**
 * pcg_frame_across() - a frame's local +c axis
 * @f:      the frame
 * @out:    the +c axis in the parent's (x, z)
 */
static inline void pcg_frame_across(const pcg_frame *f, vec2 out)
{
    float x = -f->dir[1], z = f->dir[0];

    out[0] = f->mirror ? -x : x;
    out[1] = f->mirror ? -z : z;
}

/**
 * pcg_frame_point() - a local point in the parent's coordinates
 * @f:      the frame
 * @local:  the point in (a, y, c)
 * @out:    the point in the parent's (x, y, z); may be @local
 */
static inline void pcg_frame_point(const pcg_frame *f, const vec3 local, vec3 out)
{
    float a = local[0], y = local[1], c = local[2];
    vec2 across;

    pcg_frame_across(f, across);
    out[0] = f->org[0] + a * f->dir[0] + c * across[0];
    out[1] = f->org[1] + y;
    out[2] = f->org[2] + a * f->dir[1] + c * across[1];
}

/**
 * pcg_frame_vector() - a local direction in the parent's coordinates
 * @f:      the frame
 * @local:  the direction in (a, y, c)
 * @out:    the direction in the parent's (x, y, z); may be @local
 */
static inline void pcg_frame_vector(const pcg_frame *f, const vec3 local, vec3 out)
{
    float a = local[0], y = local[1], c = local[2];
    vec2 across;

    pcg_frame_across(f, across);
    out[0] = a * f->dir[0] + c * across[0];
    out[1] = y;
    out[2] = a * f->dir[1] + c * across[1];
}

/**
 * pcg_frame_point_to_local() - a parent's point in a frame's coordinates
 * @f:      the frame
 * @p:      the point in the parent's (x, y, z)
 * @out:    the point in (a, y, c); may be @p
 *
 * The inverse of pcg_frame_point() for a frame with a unit @f->dir.
 */
static inline void pcg_frame_point_to_local(const pcg_frame *f, const vec3 p, vec3 out)
{
    float dx = p[0] - f->org[0], dy = p[1] - f->org[1], dz = p[2] - f->org[2];
    vec2 across;

    pcg_frame_across(f, across);
    out[0] = dx * f->dir[0] + dz * f->dir[1];
    out[1] = dy;
    out[2] = dx * across[0] + dz * across[1];
}

/**
 * pcg_frame_vector_to_local() - a parent's direction in a frame's coordinates
 * @f:      the frame
 * @v:      the direction in the parent's (x, y, z)
 * @out:    the direction in (a, y, c); may be @v
 *
 * The inverse of pcg_frame_vector() for a frame with a unit @f->dir.
 */
static inline void pcg_frame_vector_to_local(const pcg_frame *f, const vec3 v, vec3 out)
{
    float x = v[0], y = v[1], z = v[2];
    vec2 across;

    pcg_frame_across(f, across);
    out[0] = x * f->dir[0] + z * f->dir[1];
    out[1] = y;
    out[2] = x * across[0] + z * across[1];
}

/**
 * pcg_frame_compose() - place a local frame inside a parent frame
 * @parent: the outer frame
 * @local:  a frame in @parent's local coordinates
 * @out:    the composed frame, in @parent's parent's coordinates; may
 *          be either input
 *
 * A point in @out's coordinates lands where @local then @parent would put
 * it. Mirroring composes as an exclusive or: two mirrors cancel out.
 */
static inline void pcg_frame_compose(const pcg_frame *parent, const pcg_frame *local, pcg_frame *out)
{
    vec3 org, dir = { local->dir[0], 0.0f, local->dir[1] };
    bool mirror = parent->mirror != local->mirror;

    pcg_frame_point(parent, local->org, org);
    pcg_frame_vector(parent, dir, dir);

    vec3_dup(out->org, org);
    out->dir[0] = dir[0];
    out->dir[1] = dir[2];
    out->mirror = mirror;
}

/**
 * pcg_frame_run() - a run frame from one point toward another
 * @f:      the frame: @org at @from, at height @y, @dir toward @to, no mirror
 * @from:   run start in the parent's (x, z)
 * @to:     run end in the parent's (x, z)
 * @y:      the frame's floor height
 *
 * Return: the run's length, or CERR_INVALID_ARGUMENTS for coincident
 * points, which give no direction; @f is left alone then
 */
static inline cres_check(float) pcg_frame_run(pcg_frame *f, const vec2 from, const vec2 to, float y)
{
    vec2 dir, left;
    float len = poly2d_edge_frame(from, to, dir, left);

    if (!(len > 0.0f))  return cres_error(float, CERR_INVALID_ARGUMENTS, .fmt = "run of zero length");

    f->org[0] = from[0];
    f->org[1] = y;
    f->org[2] = from[1];
    f->dir[0] = dir[0];
    f->dir[1] = dir[1];
    f->mirror = false;

    return cres_val(float, len);
}

#endif /* __CLAP_PCG_FRAME_H__ */
