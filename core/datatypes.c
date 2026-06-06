// SPDX-License-Identifier: Apache-2.0

#include "datatypes.h"
#include "util.h"

static const struct {
    const char      *name;
    unsigned int    comp_count;
    size_t          comp_size;
    data_type       comp_subtype;
} type_desc[] = {
    [DT_NONE]   = { "none",   0,  0,                      DT_NONE   },
    [DT_BYTE]   = { "byte",   1,  sizeof(uchar),          DT_BYTE   },
    [DT_SHORT]  = { "short",  1,  sizeof(short),          DT_SHORT  },
    [DT_USHORT] = { "ushort", 1,  sizeof(unsigned short), DT_USHORT },
    [DT_INT]    = { "int",    1,  sizeof(int),            DT_INT    },
    [DT_UINT]   = { "uint",   1,  sizeof(unsigned int),   DT_UINT   },
    [DT_FLOAT]  = { "float",  1,  sizeof(float),          DT_FLOAT  },
    [DT_IVEC2]  = { "ivec2",  2,  sizeof(int),            DT_INT    },
    [DT_IVEC3]  = { "ivec3",  3,  sizeof(int),            DT_INT    },
    [DT_IVEC4]  = { "ivec4",  4,  sizeof(int),            DT_INT    },
    [DT_UVEC2]  = { "uvec2",  2,  sizeof(unsigned int),   DT_UINT   },
    [DT_UVEC3]  = { "uvec3",  3,  sizeof(unsigned int),   DT_UINT   },
    [DT_UVEC4]  = { "uvec4",  4,  sizeof(unsigned int),   DT_UINT   },
    [DT_VEC2]   = { "vec2",   2,  sizeof(float),          DT_FLOAT  },
    [DT_VEC3]   = { "vec3",   3,  sizeof(float),          DT_FLOAT  },
    [DT_VEC4]   = { "vec4",   4,  sizeof(float),          DT_FLOAT  },
    [DT_MAT2]   = { "mat2",   4,  sizeof(float),          DT_FLOAT  },
    [DT_MAT3]   = { "mat3",   9,  sizeof(float),          DT_FLOAT  },
    [DT_MAT4]   = { "mat4",   16, sizeof(float),          DT_FLOAT  },
};

bool data_type_is_valid(data_type type)  { return type < array_size(type_desc); }

const char *data_type_name(data_type type)
{
    if (unlikely(!data_type_is_valid(type)))   return "<unknown>";

    return type_desc[type].name;
}

data_type data_type_by_name(const char *name)
{
    for (data_type t = DT_BYTE; t < array_size(type_desc); t++)
        if (!strcasecmp(type_desc[t].name, name))
            return t;

    if (!strcasecmp(name, "scalar"))
        return DT_FLOAT;

    return DT_NONE;
}

unsigned int data_comp_count(data_type type)
{
    if (unlikely(!data_type_is_valid(type)))   return 0;

    return type_desc[type].comp_count;
}

size_t data_comp_size(data_type type)
{
    if (unlikely(!data_type_is_valid(type)))   return 0;

    return type_desc[type].comp_size;
}

size_t data_type_size(data_type type)
{
    if (unlikely(!data_type_is_valid(type)))   return 0;

    return type_desc[type].comp_size * type_desc[type].comp_count;
}

data_type data_type_subtype(data_type type)
{
    if (unlikely(data_type_is_valid(type)))    return 0;

    return type_desc[type].comp_subtype;
}
