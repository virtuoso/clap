/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GLTF_H__
#define __CLAP_GLTF_H__

#include "linmath.h"
#include "mesh.h"
#include "pipeline.h"

typedef struct gltf_data gltf_data;

DECLARE_CLEANUP(gltf_data);

typedef struct gltf_load_options {
    const char  *name;
    struct mq   *mq;
    pipeline    *pipeline;
    bool        fix_origin;
} gltf_load_options;

#define gltf_load(args...) \
    _gltf_load(&(gltf_load_options){ args })

struct gltf_data *_gltf_load(const gltf_load_options *opts);
void gltf_free(struct gltf_data *gd);
int gltf_root_mesh(struct gltf_data *gd);
int gltf_mesh_by_name(struct gltf_data *gd, const char *name);
cerr_check gltf_instantiate_one(struct gltf_data *gd, int mesh);
void gltf_instantiate_all(struct gltf_data *gd);
int gltf_get_meshes(struct gltf_data *gd);
const char *gltf_mesh_name(struct gltf_data *gd, int mesh);
void gltf_mesh_data(struct gltf_data *gd, int mesh, float **vx, size_t *vxsz, unsigned short **idx, size_t *idxsz,
                    float **tx, size_t *txsz, float **norm, size_t *normsz);

int gltf_get_mesh_groups(struct gltf_data *gd);
cres(int) gltf_group_first(struct gltf_data *gd, int group);
cres(int) gltf_group_count(struct gltf_data *gd, int group);
cres(int) gltf_mesh_group(struct gltf_data *gd, int mesh);
bool gltf_mesh_fallback(struct gltf_data *gd, int mesh);
int gltf_get_nodes(struct gltf_data *gd);
cresp(const_char) gltf_node_name(struct gltf_data *gd, int node);
cres(int) gltf_node_mesh(struct gltf_data *gd, int node);
cerr gltf_node_translation(struct gltf_data *gd, int node, vec3 out);
cerr gltf_node_rotation(struct gltf_data *gd, int node, quat out);
cerr gltf_node_scale(struct gltf_data *gd, int node, vec3 out);
bool gltf_node_extras_present(struct gltf_data *gd, int node, const char *prefix);
bool gltf_node_extras_string(struct gltf_data *gd, int node, const char *key, const char **out);
bool gltf_node_extras_number(struct gltf_data *gd, int node, const char *key, double *out);
bool gltf_node_extras_bool(struct gltf_data *gd, int node, const char *key, bool *out);

/**
 * gltf_mesh_new() - build a mesh from a gltf mesh's attributes
 * @gd:     loaded gltf data
 * @mesh:   mesh index
 *
 * The mesh construction half of gltf_instantiate_one(): a new mesh with
 * every attribute the gltf mesh carries (vertices, UVs, indices,
 * normals, skin, tangents), optimized. Use it to build custom models
 * from gltf data, e.g. to rig an unrigged mesh procedurally before
 * model3d creation.
 * Return: a mesh reference the caller owns, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS / mesh_attr_dup() errors.
 */
cresp_check(mesh) gltf_mesh_new(struct gltf_data *gd, int mesh);

/*
 * Texture buffer accessors: pointers into the gltf's own buffers, valid
 * until gltf_free(); sizes in bytes. gltf_textures_png() tells whether
 * the buffers hold PNG files (model3dtx's buffers_png).
 */
bool gltf_textures_png(struct gltf_data *gd);
bool gltf_has_tex(struct gltf_data *gd, int mesh);
void *gltf_tex(struct gltf_data *gd, int mesh);
unsigned int gltf_texsz(struct gltf_data *gd, int mesh);
bool gltf_has_nmap(struct gltf_data *gd, int mesh);
void *gltf_nmap(struct gltf_data *gd, int mesh);
unsigned int gltf_nmapsz(struct gltf_data *gd, int mesh);
bool gltf_has_em(struct gltf_data *gd, int mesh);
void *gltf_em(struct gltf_data *gd, int mesh);
unsigned int gltf_emsz(struct gltf_data *gd, int mesh);

#endif /* __CLAP_GLTF_H__ */
