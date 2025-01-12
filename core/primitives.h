#ifndef __CLAP_PRIMITIVES_H__
#define __CLAP_PRIMITIVES_H__

extern struct mesh cube_mesh;

struct model3d *model3d_new_cube(struct shader_prog *p);
struct model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_quadrev(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t);

#endif /* __CLAP_PRIMITIVES_H__ */
