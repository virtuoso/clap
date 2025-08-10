#ifndef __CLAP_PRIMITIVES_H__
#define __CLAP_PRIMITIVES_H__

#include "linmath.h"
#include "model.h"
#include "shader.h"

extern struct mesh cube_mesh;

/**
 * struct prim_emit_opts - vertex/primitive emitting options
 * @mesh:   mesh, to which the vertices will be appended
 * @uv:     optional texture coordinates per vertex
 */
typedef struct prim_emit_opts {
    struct mesh     *mesh;
    float           *uv;
} prim_emit_opts;

/**
 * _prim_calc_normals() - calculate normal vectors for a triangle
 * @vx_idx: first vertex of a triangle
 * @opts:   primitive emitter options
 *
 * For an emitted triangle, calculate normal vectors of its vertices.
 * Assumes that all 3 triangle vertices are back-to-back and start, as
 * the rest of the triangles in the mesh, at a multiple of 3.
 */
void _prim_calc_normals(size_t vx_idx, const prim_emit_opts *opts);
void _prim_emit_vertex(vec3 pos, const prim_emit_opts *opts);
void _prim_emit_triangle(vec3 triangle[3], const prim_emit_opts *opts);

/**
 * _prim_emit_triangle3() - emit a triangle from 3 separate vertices
 * @v0:     first vertex
 * @v1:     second vertex
 * @v2:     third vertex
 *
 * Same as _prim_emit_triangle(), but takes 3 separate vertices.
 */
void _prim_emit_triangle3(vec3 v0, vec3 v1, vec3 v2, const prim_emit_opts *opts);
void _prim_emit_quad(vec3 quad[4], const prim_emit_opts *opts);

/**
 * define prim_calc_normals - calculate normal vectors for a triangle
 * @vx_idx: first vertex of a triangle
 * @args:   list of primitive emitter options
 *
 * Syntax sugar for _prim_calc_normals().
 */
#define prim_calc_normals(_p, args...) \
    _prim_calc_normals((_p), &(prim_emit_opts){ args })
/* Append a vertex to a mesh */
#define prim_emit_vertex(_p, args...) \
    _prim_emit_vertex((_p), &(prim_emit_opts){ args })
/* Append a triangle to a mesh */
#define prim_emit_triangle(_t, args...) \
    _prim_emit_triangle((_t), &(prim_emit_opts){ args })

/**
 * define prim_emit_triangle3 - emit a triangle from 3 separate vertices
 * @v0:     first vertex
 * @v1:     second vertex
 * @v2:     third vertex
 *
 * Syntax sugar for _prim_emit_triangle3().
 */
#define prim_emit_triangle3(_v0, _v1, _v2, args...) \
    _prim_emit_triangle3((_v0), (_v1), (_v2), &(prim_emit_opts){ args })
/* Append a quad to a mesh */
#define prim_emit_quad(_t, args...) \
    _prim_emit_quad((_t), &(prim_emit_opts){ args })

model3d *model3d_new_cube(struct shader_prog *p, bool skip_aabb);
model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h);
model3d *model3d_new_quadrev(struct shader_prog *p, float x, float y, float z, float w, float h);
model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t);

/*
 * Generate a cylinder model
 * @org: origin point (center of the bottom face), may be a good idea to leave this as
 *       (vec3){} and position the cylinder with entity3d coordinates
 * @height: cylinder height
 * @radius: cylinder radius
 * @nr_segments: number of the segments of horizontal faces' circumference
 */
cresp(model3d) model3d_new_cylinder(struct shader_prog *p, vec3 org, float height, float radius, int nr_serments);

#endif /* __CLAP_PRIMITIVES_H__ */
