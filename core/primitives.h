#ifndef __CLAP_PRIMITIVES_H__
#define __CLAP_PRIMITIVES_H__

extern struct mesh cube_mesh;

/*
 * Vertex emitting options:
 * @mesh: mesh, to which the vertices will be appended
 * @uv: optional texture coordinates per vertex
 */
typedef struct prim_emit_opts {
    struct mesh     *mesh;
    float           *uv;
} prim_emit_opts;

void _prim_emit_vertex(vec3 pos, const prim_emit_opts *opts);
void _prim_emit_triangle(vec3 triangle[3], const prim_emit_opts *opts);
void _prim_emit_quad(vec3 quad[4], const prim_emit_opts *opts);

/* Append a vertex to a mesh */
#define prim_emit_vertex(_p, args...) \
    _prim_emit_vertex((_p), &(prim_emit_opts){ args })
/* Append a triangle to a mesh */
#define prim_emit_triangle(_t, args...) \
    _prim_emit_triangle((_t), &(prim_emit_opts){ args })
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
