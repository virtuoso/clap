/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_OBJFILE_H__
#define __CLAP_OBJFILE_H__

#include <stdint.h>

struct bin_vec_header {
    uint64_t   magic;
    uint64_t   ver;
    uint64_t   nr_vertices;
    uint64_t   vxsz;
    uint64_t   txsz;
    uint64_t   idxsz;
};

struct model_data {
    unsigned long   nr_v;
    unsigned long   nr_vt;
    unsigned long   nr_vn;
    unsigned long   nr_f;
    unsigned long   loaded_v;
    unsigned long   loaded_vt;
    unsigned long   loaded_vn;
    unsigned long   loaded_f;
    float           *v;
    float           *vt;
    float           *vn;
    int             *f;
};

int model_data_init(struct model_data *md,
                    unsigned long nr_v,
                    unsigned long nr_vt,
                    unsigned long nr_vn,
                    unsigned long nr_f);
int model_data_push_v(struct model_data *md, float v0, float v1, float v2);
int model_data_push_vt(struct model_data *md, float v0, float v1);
int model_data_push_vn(struct model_data *md, float v0, float v1, float v2);
int model_data_push_f(struct model_data *md, int f[9]);
struct model_data *model_data_new_from_obj(const char *base, size_t size);
void model_data_free(struct model_data *md);
int model_data_to_vectors(struct model_data *md,
                          float **tx, size_t *txszp,
                          float **norm, size_t *vxszp,
                          unsigned short **idx, size_t *idxszp);

#endif /* __CLAP_OBJFILE_H__ */
