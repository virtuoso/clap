/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GLTF_H__
#define __CLAP_GLTF_H__

#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "pipeline.h"

typedef struct gltf_data gltf_data;

DECLARE_CLEANUP(gltf_data);

/**
 * gltf_model_make_fn - hook to build a model3d from a freshly loaded gltf mesh
 * @gd:     the gltf data being instantiated
 * @mesh:   index of the mesh being instantiated
 * @me:     the mesh gltf_instantiate_one() just built with gltf_mesh_new();
 *          borrowed - build the model from it, do not take ownership
 * @out:    receives the model3d on CERR_OK; instantiate takes ownership
 * @priv:   the caller's cookie from gltf_load_options::model_make_priv
 *
 * Fires inside gltf_instantiate_one(), once per instantiated mesh, between
 * gltf_mesh_new() and model3d creation. On CERR_OK write @out, and instantiate
 * wraps that model into the model3dtx with this mesh's textures and material:
 * the hook owns the mesh side (e.g. fitting a procedural skeleton), instantiate
 * keeps the texture/material/skin side keyed by @mesh. Return CERR_NOT_HANDLED
 * to decline, and instantiate builds the model the usual way; any other error
 * aborts the instantiation.
 *
 * Mirror this signature in struct clap_config::model_make, which the scene
 * loader passes through; clap.h spells it out to avoid pulling in gltf.h.
 * Return: CERR_OK (with @out set), CERR_NOT_HANDLED to decline, or an error.
 */
typedef cerr (*gltf_model_make_fn)(struct gltf_data *gd, int mesh, mesh_t *me,
                                   model3d **out, void *priv);

typedef struct gltf_load_options {
    /* the asset gltf_read() reads, and messages name */
    const char          *name;
    /* its contents, NUL-terminated, from gltf_read(); the caller's */
    void                *buf;
    size_t              size;
    gltf_model_make_fn  model_make;
    void                *model_make_priv;
    bool                fix_origin;
    /* split the skinned meshes into their rigid pieces, model3d_init_options::parts */
    bool                parts;
} gltf_load_options;

/**
 * gltf_read() - read a glTF or GLB asset for gltf_load()
 * @opts:   gltf_load_options: .name to read; .buf and .size receive the contents
 *
 * The librarian's handles are reference counted, so reading is the main
 * thread's half of loading a glTF, and gltf_load() the half that a worker
 * job can run.
 * Context: the main thread
 * Return: CERR_OK with @opts->buf the caller's to mem_free(); CERR_NOT_FOUND
 *         if the asset doesn't read (logged)
 */
cerr_check gltf_read(gltf_load_options *opts);

/**
 * define gltf_load - parse a glTF or GLB file's contents
 * @args:   gltf_load_options: .name, .buf and .size from gltf_read(), .fix_origin, .parts
 *          and .model_make
 *
 * Builds the tables the rest of gltf.c works from out of the buffer alone:
 * no file, pipeline, model queue or reference counting. It logs, so off the
 * main thread it runs where the log lines are held (log_hold()), as in a
 * worker job's run(). The buffer stays the caller's.
 * Return: the glTF data for gltf_free(); nullptr if it doesn't parse (logged)
 */
#define gltf_load(args...) \
    _gltf_load(&(gltf_load_options){ args })

struct gltf_data *_gltf_load(const gltf_load_options *opts);
void gltf_free(struct gltf_data *gd);
/* the pipeline of the gltf_instantiate_one() under way, for its model_make hook to source shaders */
pipeline *gltf_pipeline(struct gltf_data *gd);
int gltf_root_mesh(struct gltf_data *gd);
int gltf_mesh_by_name(struct gltf_data *gd, const char *name);

/**
 * gltf_instantiate_one() - make a textured model of a primitive and queue it
 * @gd:     glTF data
 * @mesh:   the primitive (flat mesh index)
 * @mq:     the model queue it goes into
 * @pl:     the pipeline whose render options and shaders it's made for
 *
 * Context: the main thread
 * Return: CERR_OK or an error
 */
cerr_check gltf_instantiate_one(struct gltf_data *gd, int mesh, struct mq *mq, pipeline *pl);
void gltf_instantiate_all(struct gltf_data *gd, struct mq *mq, pipeline *pl);

int gltf_get_meshes(struct gltf_data *gd);
const char *gltf_mesh_name(struct gltf_data *gd, int mesh);
void gltf_mesh_data(struct gltf_data *gd, int mesh, float **vx, size_t *vxsz, unsigned short **idx, size_t *idxsz,
                    float **tx, size_t *txsz, float **norm, size_t *normsz);

int gltf_get_mesh_groups(struct gltf_data *gd);
cres(int) gltf_group_first(struct gltf_data *gd, int group);
cres(int) gltf_group_count(struct gltf_data *gd, int group);
cres(int) gltf_mesh_group(struct gltf_data *gd, int mesh);
/**
 * gltf_mesh_skin() - the skin a skinned primitive's node binds it to
 * @gd:     gltf data
 * @mesh:   the primitive
 *
 * Return: the skin's index; CERR_NOT_FOUND for a primitive without joints
 * and weights, or one that no skinned node holds
 */
cres_check(int) gltf_mesh_skin(struct gltf_data *gd, int mesh);
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
 * gltf_node_extras_prefixed() - walk a node's extras whose keys start with a prefix
 * @gd:     the loaded asset
 * @node:   node index
 * @prefix: the key prefix
 * @idx:    0 to start, then what the previous call returned
 * @key:    receives the key past @prefix
 * @str:    receives the string value, or nullptr for a number or a bool
 *
 * Return: where the next call starts, CERR_NOT_FOUND when there are no
 *         more, or the error of a bad @node
 */
cres(int) gltf_node_extras_prefixed(struct gltf_data *gd, int node, const char *prefix, int idx,
                                    const char **key, const char **str);

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
