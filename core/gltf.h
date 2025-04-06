/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GLTF_H__
#define __CLAP_GLTF_H__

struct gltf_data;

typedef struct gltf_load_options {
    const char  *name;
    struct mq   *mq;
    pipeline    *pipeline;
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

#endif /* __CLAP_GLTF_H__ */
