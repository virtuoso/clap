/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GLTF_H__
#define __CLAP_GLTF_H__

#include "linmath.h"
#include "pipeline.h"

struct gltf_data;

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
int gltf_group_first(struct gltf_data *gd, int group);
int gltf_group_count(struct gltf_data *gd, int group);
int gltf_mesh_group(struct gltf_data *gd, int mesh);
bool gltf_mesh_fallback(struct gltf_data *gd, int mesh);
int gltf_get_nodes(struct gltf_data *gd);
const char *gltf_node_name(struct gltf_data *gd, int node);
int gltf_node_mesh(struct gltf_data *gd, int node);
void gltf_node_translation(struct gltf_data *gd, int node, vec3 out);
void gltf_node_rotation(struct gltf_data *gd, int node, quat out);
void gltf_node_scale(struct gltf_data *gd, int node, vec3 out);
bool gltf_node_extras_string(struct gltf_data *gd, int node, const char *key, const char **out);
bool gltf_node_extras_number(struct gltf_data *gd, int node, const char *key, double *out);
bool gltf_node_extras_bool(struct gltf_data *gd, int node, const char *key, bool *out);

#endif /* __CLAP_GLTF_H__ */
