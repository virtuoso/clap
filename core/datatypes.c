// SPDX-License-Identifier: Apache-2.0

#include "datatypes.h"
#include "util.h"

static const char *type_name[] = {
    [DT_NONE]   = "none",
    [DT_BYTE]   = "byte",
    [DT_SHORT]  = "short",
    [DT_USHORT] = "ushort",
    [DT_INT]    = "int",
    [DT_UINT]   = "uint",
    [DT_FLOAT]  = "float",
    [DT_IVEC2]  = "ivec2",
    [DT_IVEC3]  = "ivec3",
    [DT_IVEC4]  = "ivec4",
    [DT_UVEC2]  = "uvec2",
    [DT_UVEC3]  = "uvec3",
    [DT_UVEC4]  = "uvec4",
    [DT_VEC2]   = "vec2",
    [DT_VEC3]   = "vec3",
    [DT_VEC4]   = "vec4",
    [DT_MAT2]   = "mat2",
    [DT_MAT3]   = "mat3",
    [DT_MAT4]   = "mat4",
};

const char *data_type_name(data_type type)
{
    if (unlikely(type >= array_size(type_name)))
        return "<unknown>";

    return type_name[type];
}

data_type data_type_by_name(const char *name)
{
    for (data_type t = DT_BYTE; t < array_size(type_name); t++)
        if (!strcasecmp(type_name[t], name))
            return t;

    if (!strcasecmp(name, "scalar"))
        return DT_FLOAT;

    return DT_NONE;
}

static const unsigned int comp_count[] = {
    [DT_BYTE]   = 1,
    [DT_SHORT]  = 1,
    [DT_USHORT] = 1,
    [DT_INT]    = 1,
    [DT_UINT]   = 1,
    [DT_FLOAT]  = 1,
    [DT_IVEC2]  = 2,
    [DT_IVEC3]  = 3,
    [DT_IVEC4]  = 4,
    [DT_UVEC2]  = 2,
    [DT_UVEC3]  = 3,
    [DT_UVEC4]  = 4,
    [DT_VEC2]   = 2,
    [DT_VEC3]   = 3,
    [DT_VEC4]   = 4,
    [DT_MAT2]   = 4,
    [DT_MAT3]   = 9,
    [DT_MAT4]   = 16,
};

unsigned int data_comp_count(data_type type)
{
    if (unlikely(type >= array_size(comp_count)))
        return 0;

    return comp_count[type];
}

static const size_t comp_size[] = {
    [DT_BYTE]   = sizeof(uchar),
    [DT_SHORT]  = sizeof(short),
    [DT_USHORT] = sizeof(unsigned short),
    [DT_INT]    = sizeof(int),
    [DT_UINT]   = sizeof(unsigned int),
    [DT_FLOAT]  = sizeof(float),
    [DT_IVEC2]  = sizeof(int),
    [DT_IVEC3]  = sizeof(int),
    [DT_IVEC4]  = sizeof(int),
    [DT_UVEC2]  = sizeof(unsigned int),
    [DT_UVEC3]  = sizeof(unsigned int),
    [DT_UVEC4]  = sizeof(unsigned int),
    [DT_VEC2]   = sizeof(float),
    [DT_VEC3]   = sizeof(float),
    [DT_VEC4]   = sizeof(float),
    [DT_MAT2]   = sizeof(float),
    [DT_MAT3]   = sizeof(float),
    [DT_MAT4]   = sizeof(float),
};

size_t data_comp_size(data_type type)
{
    if (unlikely(type >= array_size(comp_size)))
        return 0;

    return comp_size[type];
}

static const data_type comp_subtype[] = {
    [DT_BYTE]   = DT_BYTE,
    [DT_SHORT]  = DT_SHORT,
    [DT_USHORT] = DT_USHORT,
    [DT_INT]    = DT_INT,
    [DT_UINT]   = DT_UINT,
    [DT_FLOAT]  = DT_FLOAT,
    [DT_IVEC2]  = DT_INT,
    [DT_IVEC3]  = DT_INT,
    [DT_IVEC4]  = DT_INT,
    [DT_UVEC2]  = DT_UINT,
    [DT_UVEC3]  = DT_UINT,
    [DT_UVEC4]  = DT_UINT,
    [DT_VEC2]   = DT_FLOAT,
    [DT_VEC3]   = DT_FLOAT,
    [DT_VEC4]   = DT_FLOAT,
    [DT_MAT2]   = DT_FLOAT,
    [DT_MAT3]   = DT_FLOAT,
    [DT_MAT4]   = DT_FLOAT,
};

size_t data_type_size(data_type type)
{
    if (unlikely(type >= array_size(comp_size)))
        return 0;

    return comp_size[type] * comp_count[type];
}

data_type data_type_subtype(data_type type)
{
    if (unlikely(type >= array_size(comp_subtype)))
        return 0;

    return comp_subtype[type];
}
