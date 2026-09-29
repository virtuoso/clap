// SPDX-License-Identifier: Apache-2.0
/*
 * The transition table from a model's GLTF extras: the "sequences" grammar
 * (json.c) as flat strings on the armature node, or any node, following the
 * clap::physics::* convention the scene loader reads.
 *
 *   clap::anim::moving         = "motion_start,motion*"
 *   clap::anim::jumping>moving = "jump_to_motion,motion*"
 *   clap::anim::fade           = 0.1
 */
#include <string.h>
#include "common.h"
#include "gltf.h"
#include "gpsm.h"
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include "internal.h"

#define ANIM_EXTRAS_PREFIX  "clap::anim::"

cerr anim_lib_transitions_from_gltf(anim_lib_t *lib, struct gltf_data *gd, double fade,
                                    const struct gpsm_state *states, unsigned int nr_states)
{
    int nr_nodes = gltf_get_nodes(gd), node;

    /* the first node carrying any: the armature's, in a rigged asset */
    for (node = 0; node < nr_nodes; node++)
        if (gltf_node_extras_present(gd, node, ANIM_EXTRAS_PREFIX))     break;
    if (node == nr_nodes)                                               return CERR_NOT_FOUND;

    double asset_fade;
    if (fade < 0.0) fade = gltf_node_extras_number(gd, node, ANIM_EXTRAS_PREFIX "fade", &asset_fade) ? asset_fade : 0.0;

    const char *key, *clips;
    for (cres(int) it = gltf_node_extras_prefixed(gd, node, ANIM_EXTRAS_PREFIX, 0, &key, &clips);
         !IS_CERR(it); it = gltf_node_extras_prefixed(gd, node, ANIM_EXTRAS_PREFIX, it.val, &key, &clips)) {
        if (!strcmp(key, "fade"))   continue;
        if (!clips) {
            err("extras: '%s%s' is not a string\n", ANIM_EXTRAS_PREFIX, key);
            return CERR_PARSE_FAILED;
        }

        cerr err = anim_lib_transition_from_spec(lib, key, clips, fade, states, nr_states);

        /* a clip the model lacks leaves the transition out, as the JSON loader does */
        if (IS_CERR_CODE(err, CERR_NOT_FOUND)) {
            warn("extras: '%s%s': clip not found, transition left out\n", ANIM_EXTRAS_PREFIX, key);
            continue;
        }
        CERR_RET_CERR(err);
    }

    return CERR_OK;
}
