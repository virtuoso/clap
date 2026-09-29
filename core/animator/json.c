// SPDX-License-Identifier: Apache-2.0
/*
 * The transition table from data: the scene's "sequences" object, one entry
 * per state transition, its value the clips to play in order, and the same
 * entries as flat strings for a model's GLTF extras (extras.c).
 *
 *   "sequences": {
 *       "moving":         [ "motion_start", "motion*" ],
 *       "jumping>moving": [ "jump_to_motion", "motion*" ]
 *   }
 *
 *   clap::anim::moving         = "motion_start,motion*"
 *   clap::anim::jumping>moving = "jump_to_motion,motion*"
 *
 * A key is a state name, or "from>to" for one particular transition; a bare
 * state is entered from anywhere. A clip name with a "*" after it repeats.
 * The names are the states' (struct gpsm_state::name) and the clips'
 * (after the scene's "animations" renames), so the same loader serves any
 * machine with named states.
 */
#include <string.h>
#include "common.h"
#include "gpsm.h"
#include "json.h"
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include "internal.h"

/* the index of the state called @name, @len characters of it */
static cres(int) state_by_name(const struct gpsm_state *states, unsigned int nr_states,
                               const char *name, size_t len)
{
    for (unsigned int i = 0; i < nr_states; i++)
        if (states[i].name && strlen(states[i].name) == len && !strncmp(states[i].name, name, len))
            return cres_val(int, i);

    return cres_error(int, CERR_NOT_FOUND);
}

/* "to" or "from>to"; @from is left GPSM_NO_STATE for the former */
static cerr key_parse(const char *key, const struct gpsm_state *states, unsigned int nr_states,
                      int *to, int *from)
{
    const char *sep = strchr(key, '>');

    *from = GPSM_NO_STATE;
    if (sep) {
        *from = CRES_RET_CERR(state_by_name(states, nr_states, key, sep - key));
        key = sep + 1;
    }
    *to = CRES_RET_CERR(state_by_name(states, nr_states, key, strlen(key)));

    return CERR_OK;
}

/* one element, "clip" or "clip*", @len characters of it, onto @seq */
static cerr seq_push_spec(anim_lib_t *lib, anim_t seq, const char *spec, size_t len)
{
    bool repeat = len && spec[len - 1] == '*';

    if (repeat)     len--;
    if (!len)       return CERR_PARSE_FAILED;

    /* the name without the marker */
    LOCAL_SET(char, name) = mem_alloc(len + 1);
    if (!name)  return CERR_NOMEM;

    memcpy(name, spec, len);
    name[len] = '\0';

    anim_t ani = CRES_RET_CERR(anim_lib_clip(lib, name));

    return anim_lib_seq_push(lib, seq, ani, repeat);
}

/* the entry for @to from @from, or from anywhere */
static cerr transition_add(anim_lib_t *lib, int to, int from, anim_t seq, double fade)
{
    if (from == GPSM_NO_STATE)  return anim_lib_transition_add_any(lib, to, seq, fade);

    return anim_lib_transition_add(lib, to, from, seq, fade);
}

cerr anim_lib_transition_from_spec(anim_lib_t *lib, const char *key, const char *clips, double fade,
                                   const struct gpsm_state *states, unsigned int nr_states)
{
    int to, from;

    if (!key || !clips)     return CERR_PARSE_FAILED;

    CERR_RET(key_parse(key, states, nr_states, &to, &from), {
        err_cerr(__cerr, "sequences: unknown state in '%s'\n", key);
        return CERR_PARSE_FAILED;
    });

    anim_t seq = CRES_RET_CERR(anim_lib_seq_make(lib));
    const char *p = clips;

    /* comma-separated elements, spaces around them ignored */
    for (;;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        while (len && *p == ' ')                { p++; len--; }
        while (len && p[len - 1] == ' ')        len--;

        CERR_RET_CERR(seq_push_spec(lib, seq, p, len));
        if (!end)   break;
        p = end + 1;
    }

    return transition_add(lib, to, from, seq, fade);
}

cerr anim_lib_transitions_from_json(anim_lib_t *lib, JsonNode *sequences, double fade,
                                    const struct gpsm_state *states, unsigned int nr_states)
{
    if (!sequences || sequences->tag != JSON_OBJECT)    return CERR_PARSE_FAILED;

    for (JsonNode *p = sequences->children.head; p; p = p->next) {
        int to, from;

        if (p->tag != JSON_ARRAY || !p->key)    return CERR_PARSE_FAILED;

        CERR_RET(key_parse(p->key, states, nr_states, &to, &from), {
            err_cerr(__cerr, "sequences: unknown state in '%s'\n", p->key);
            return CERR_PARSE_FAILED;
        });

        anim_t seq = CRES_RET_CERR(anim_lib_seq_make(lib));
        cerr err = CERR_OK;

        for (JsonNode *el = p->children.head; el; el = el->next) {
            if (el->tag != JSON_STRING)     return CERR_PARSE_FAILED;

            err = seq_push_spec(lib, seq, el->string_, strlen(el->string_));
            if (IS_CERR(err))   break;
        }

        /*
         * A clip the model lacks leaves the transition out, as the default
         * table does, so a scene can name clips its every model doesn't have
         */
        if (IS_CERR_CODE(err, CERR_NOT_FOUND)) {
            warn("sequences: '%s': clip not found, transition left out\n", p->key);
            continue;
        }
        CERR_RET_CERR(err);

        CERR_RET_CERR(transition_add(lib, to, from, seq, fade));
    }

    return CERR_OK;
}
