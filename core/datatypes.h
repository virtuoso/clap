/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DATATYPES_H__
#define __CLAP_DATATYPES_H__

typedef enum data_type {
    DT_NONE = 0,
    DT_BYTE,
    DT_SHORT,
    DT_USHORT,
    DT_INT,
    DT_FLOAT,
    DT_IVEC2,
    DT_IVEC3,
    DT_IVEC4,
    DT_VEC2,
    DT_VEC3,
    DT_VEC4,
    DT_MAT2,
    DT_MAT3,
    DT_MAT4,
} data_type;

const char *data_type_name(data_type type);
data_type data_type_by_name(const char *name);
unsigned int data_comp_count(data_type type);
size_t data_comp_size(data_type type);
size_t data_type_size(data_type type);
data_type data_type_subtype(data_type type);

#endif /* __CLAP_DATATYPES_H__ */
