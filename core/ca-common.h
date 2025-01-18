/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CA_COMMON_H__
#define __CLAP_CA_COMMON_H__

struct xyzarray;

/*
 * Cell automaton configuration
 */
struct cell_automaton {
    const char      *name;
    /*
     * Bitmask of the amounts of neighbors, with which a cell is "born"
     * (set to nr_states)
     */
    unsigned int    born_mask;
    /*
     * Bitmask of the amounts of neighbors, with which a cell "survives":
     * doesn't decay if @decay is set (XXX: on in ca3d)
     */
    unsigned int    surv_mask;
    /* Initial value of a cell when it is "born" */
    unsigned int    nr_states;
    /* Decrease the value of a cell if it's not in @surv_mask (XXX: ignored in ca3d) */
    bool            decay;
    /* 3-byte hole */
    union {
        /* XXX: @arr is an xyzarray under the hood */
        int         (*neigh_2d)(unsigned char *arr, int x, int y);
        int         (*neigh_3d)(struct xyzarray *xyz, int x, int y, int z);
    };
};

#endif /* __CLAP_CA_COMMON_H__ */
