/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CA2D_H__
#define __CLAP_CA2D_H__

#include <stdbool.h>

struct cell_automaton {
    const char      *name;
    unsigned int    born;
    unsigned int    surv;
    unsigned int    nr_states;
    bool            decay;
    int             (*neigh)(unsigned char *arr, int side, int x, int y);
};

int ca2d_neigh_vn1(unsigned char *arr, int side, int x, int y);
int ca2d_neigh_m1(unsigned char *arr, int side, int x, int y);
int ca2d_neigh_vnv(unsigned char *arr, int side, int x, int y);
int ca2d_neigh_mv(unsigned char *arr, int side, int x, int y);
void ca2d_step(const struct cell_automaton *ca, unsigned char *arr, int side);
unsigned char *ca2d_generate(const struct cell_automaton *ca, int side, int steps);

#endif /* __CLAP_CA2D_H__ */
