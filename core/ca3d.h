/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CA3D_H__
#define __CLAP_CA3D_H__

#define CA_0  (1 << 0)
#define CA_1  (1 << 1)
#define CA_2  (1 << 2)
#define CA_3  (1 << 3)
#define CA_4  (1 << 4)
#define CA_5  (1 << 5)
#define CA_6  (1 << 6)
#define CA_7  (1 << 7)
#define CA_8  (1 << 8)
#define CA_9  (1 << 9)
#define CA_10 (1 << 10)
#define CA_11 (1 << 11)
#define CA_12 (1 << 12)
#define CA_13 (1 << 13)
#define CA_14 (1 << 14)
#define CA_15 (1 << 15)
#define CA_16 (1 << 16)
#define CA_17 (1 << 17)
#define CA_18 (1 << 18)
#define CA_19 (1 << 19)
#define CA_20 (1 << 20)
#define CA_21 (1 << 21)
#define CA_22 (1 << 22)
#define CA_23 (1 << 23)
#define CA_24 (1 << 24)
#define CA_25 (1 << 25)
#define CA_26 (1 << 26)
#define CA_RANGE(_start, _end) (((1 << ((_end) - (_start))) - 1) << (_start))

typedef int ivec3[3];

struct xyzarray {
    ivec3   dim;
    int     arr[0];
};

struct xyzarray *xyzarray_new(ivec3 dim);
bool xyzarray_valid(struct xyzarray *xyz, ivec3 pos);
bool xyzarray_edgemost(struct xyzarray *xyz, ivec3 pos);
int xyzarray_get(struct xyzarray *xyz, ivec3 pos);
int xyzarray_getat(struct xyzarray *xyz, int x, int y, int z);
void xyzarray_set(struct xyzarray *xyz, ivec3 pos, int val);
void xyzarray_setat(struct xyzarray *xyz, int x, int y, int z, int val);
void xyzarray_print(struct xyzarray *xyz);
int xyzarray_count(struct xyzarray *xyz);

struct cell_automn {
    unsigned int    nr_states;
    unsigned long   surv_mask;
    unsigned long   born_mask;
    int             (*neigh_fn)(struct xyzarray *, int, int, int);
    const char      *name;
};

enum {
    ca_445m = 0,
    ca_678_678_3m,
    ca_pyroclastic,
    ca_amoeba,
    ca_builder,
    ca_slow_decay,
    ca_spiky_growth,
    ca_coral,
    ca_crystal_1,
};

int ca3d_neighbors_vn1(struct xyzarray *xyz, int x, int y, int z);
void ca3d_prune(struct xyzarray *xyz);
int ca3d_run(struct xyzarray *xyz, int nca, int steps);
struct xyzarray *ca3d_make(int d0, int d1, int d2);

#endif /* __CLAP_CA3D_H__ */