// SPDX-License-Identifier: Apache-2.0
#include "interp.h"
#include "model.h"
#include "blend.h"

void joint_blend(struct joint *dst, const struct joint *a, const struct joint *b, float w)
{
    vec3 t, s;
    quat q;

    vec3_interp(t, (float *)a->translation, (float *)b->translation, w);
    vec3_interp(s, (float *)a->scale, (float *)b->scale, w);
    quat_interp(q, (float *)a->rotation, (float *)b->rotation, w);

    vec3_dup(dst->translation, t);
    vec3_dup(dst->scale, s);
    vec4_dup(dst->rotation, q);
}

void joints_copy_trs(struct joint *dst, const struct joint *src, unsigned int nr)
{
    for (unsigned int j = 0; j < nr; j++) {
        vec3_dup(dst[j].translation, src[j].translation);
        vec4_dup(dst[j].rotation, src[j].rotation);
        vec3_dup(dst[j].scale, src[j].scale);
    }
}
