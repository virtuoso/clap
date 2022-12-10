/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_XYARRAY_H__
#define __CLAP_XYARRAY_H__

unsigned char xyarray_get(unsigned char *arr, int width, int x, int y);
void xyarray_set(unsigned char *arr, int width, int x, int y, unsigned char v);
void xyarray_print(unsigned char *arr, int width, int height);

#endif /* __CLAP_XYARRAY_H__ */