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

/* Append a vertex to a mesh */
#define prim_emit_vertex(_p, args...) \
    _prim_emit_vertex((_p), &(prim_emit_opts){ args })
/* Append a triangle to a mesh */
#define prim_emit_triangle(_t, args...) \
    _prim_emit_triangle((_t), &(prim_emit_opts){ args })
/* Append a quad to a mesh */
#define prim_emit_quad(_t, args...) \
    _prim_emit_quad((_t), &(prim_emit_opts){ args })

struct model3d *model3d_new_cube(struct shader_prog *p);
struct model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_quadrev(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t);

#endif /* __CLAP_PRIMITIVES_H__ */
