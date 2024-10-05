/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GLTF_H__
#define __CLAP_GLTF_H__

struct gltf_data;
struct gltf_data *gltf_load(struct scene *scene, const char *name);
void gltf_free(struct gltf_data *gd);
int gltf_root_mesh(struct gltf_data *gd);
int gltf_mesh_by_name(struct gltf_data *gd, const char *name);
int gltf_instantiate_one(struct gltf_data *gd, int mesh);
void gltf_instantiate_all(struct gltf_data *gd);
int gltf_get_meshes(struct gltf_data *gd);
int gltf_mesh(struct gltf_data *gd, const char *name);
const char *gltf_mesh_name(struct gltf_data *gd, int mesh);
void gltf_mesh_data(struct gltf_data *gd, int mesh, float **vx, size_t *vxsz, unsigned short **idx, size_t *idxsz,
                    float **tx, size_t *txsz, float **norm, size_t *normsz);
void *gltf_accessor_buf(struct gltf_data *gd, int accr);
void *gltf_accessor_sz(struct gltf_data *gd, int accr);
float *gltf_vx(struct gltf_data *gd, int mesh);
unsigned int gltf_vxsz(struct gltf_data *gd, int mesh);
unsigned short *gltf_idx(struct gltf_data *gd, int mesh);
unsigned int gltf_idxsz(struct gltf_data *gd, int mesh);
float *gltf_tx(struct gltf_data *gd, int mesh);
unsigned int gltf_txsz(struct gltf_data *gd, int mesh);
float *gltf_norm(struct gltf_data *gd, int mesh);
unsigned int gltf_normsz(struct gltf_data *gd, int mesh);
float *gltf_color(struct gltf_data *gd, int mesh);
unsigned int gltf_colorsz(struct gltf_data *gd, int mesh);
float *gltf_joints(struct gltf_data *gd, int mesh);
unsigned int gltf_jointssz(struct gltf_data *gd, int mesh);
float *gltf_weights(struct gltf_data *gd, int mesh);
unsigned int gltf_weightssz(struct gltf_data *gd, int mesh);
void *gltf_tex(struct gltf_data *gd, int mesh);
unsigned int gltf_texsz(struct gltf_data *gd, int mesh);

#endif /* __CLAP_GLTF_H__ */
