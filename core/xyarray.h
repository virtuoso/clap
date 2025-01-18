/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_XYARRAY_H__
#define __CLAP_XYARRAY_H__

typedef int ivec3[3];

struct xyzarray {
    ivec3           dim;
    unsigned char   arr[0];
};

struct xyzarray *xyzarray_new(ivec3 dim);
bool xyzarray_valid(struct xyzarray *xyz, ivec3 pos);
bool xyzarray_edgemost(struct xyzarray *xyz, ivec3 pos);
int xyzarray_get(struct xyzarray *xyz, ivec3 pos);
void xyzarray_set(struct xyzarray *xyz, ivec3 pos, int val);
void xyzarray_print(struct xyzarray *xyz);
int xyzarray_count(struct xyzarray *xyz);

unsigned char *xyarray_new(int width);
void xyarray_free(unsigned char *arr);
unsigned char xyarray_get(unsigned char *arr, int x, int y);
void xyarray_set(unsigned char *arr, int x, int y, unsigned char v);
void xyarray_print(unsigned char *arr);

#endif /* __CLAP_XYARRAY_H__ */
