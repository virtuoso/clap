// SPDX-License-Identifier: Apache-2.0
/*
 * Library tests, built as the standalone anim_lib_tests binary: lib.c and the
 * JSON loader over a stand-in for the model's clip lookup, so no model3d has
 * to be built.
 */
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "test.h"
#include "gpsm.h"
#include "json.h"
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include "internal.h"

/* what the model would say: the clips it has, by index */
static const char *clips[] = { "idle", "motion_start", "motion", "jump" };

int animation_by_name(model3d *m, const char *name)
{
    for (int i = 0; i < array_size(clips); i++)
        if (!strcmp(clips[i], name))    return i;

    return -1;
}

enum { S_IDLE, S_MOVING, S_JUMPING, S_NR };

static const struct gpsm_state states[S_NR] = {
    [S_IDLE]    = { .name = "idle" },
    [S_MOVING]  = { .name = "moving" },
    [S_JUMPING] = { .name = "jumping" },
};

/* a sequence's elements, in order, as (index, repeat) pairs */
static bool seq_is(anim_lib_t *lib, anim_t seq, const int *idx, const bool *rep, unsigned int nr)
{
    struct anim_def *def = anim_lib_def(lib, seq);

    if (!def || def->kind != ANIM_SEQ || darray_count(def->seq.elems) != nr)    return false;

    for (unsigned int i = 0; i < nr; i++) {
        struct anim_seq_elem *el = &def->seq.elems.x[i];
        struct anim_def *ed = anim_lib_def(lib, el->ani);

        if (!ed || ed->kind != ANIM_CLIP || ed->clip.index != idx[i] || el->repeat != rep[i])   return false;
    }

    return true;
}

/* the same clip and the same hold are the same handle */
static int lib_test_dedup(void)
{
    anim_lib_t lib;
    int ret = EXIT_FAILURE;

    anim_lib_init(&lib, nullptr);

    cres(anim_t) a = anim_lib_clip(&lib, "idle"), b = anim_lib_clip(&lib, "idle");
    cres(anim_t) h0 = anim_lib_hold(&lib, 0.0), h1 = anim_lib_hold(&lib, 0.0), h2 = anim_lib_hold(&lib, 0.5);

    if (IS_CERR(a) || IS_CERR(b) || a.val != b.val)         goto out;
    if (IS_CERR(h0) || IS_CERR(h1) || h0.val != h1.val)     goto out;
    if (IS_CERR(h2) || h2.val == h0.val)                    goto out;

    cres(anim_t) nope = anim_lib_clip(&lib, "nope");
    if (!IS_CERR_CODE(nope, CERR_NOT_FOUND))                goto out;
    if (anim_lib_nr(&lib) != 3)                             goto out;

    ret = EXIT_SUCCESS;
out:
    anim_lib_done(&lib);
    return ret;
}

/* keys "to" and "from>to", "*" for repeat, the exact entry before the any one */
static int lib_test_from_json(void)
{
    LOCAL_SET(JsonNode, root) = json_decode(
        "{ \"moving\":         [ \"motion_start\", \"motion*\" ],"
        "  \"idle\":           [ \"idle*\" ],"
        "  \"jumping>idle\":   [ \"jump\", \"idle*\" ],"
        "  \"jumping\":        [ \"fall*\" ] }"
    );
    anim_lib_t lib;
    int ret = EXIT_FAILURE;

    anim_lib_init(&lib, nullptr);
    if (!root)  goto out;

    cerr err = anim_lib_transitions_from_json(&lib, root, 0.25, states, S_NR);
    if (IS_CERR(err))   goto out;

    /* "jumping" names a clip the model lacks: left out, the rest loaded */
    if (anim_lib_nr_transitions(&lib) != 3)     goto out;

    double fade = 0.0;
    cres(anim_t) t = anim_lib_transition_find(&lib, S_MOVING, S_IDLE, &fade);
    if (IS_CERR(t) || fade != 0.25)                                         goto out;
    if (!seq_is(&lib, t.val, (int[]){ 1, 2 }, (bool[]){ false, true }, 2))  goto out;

    t = anim_lib_transition_find(&lib, S_IDLE, S_JUMPING, nullptr);
    if (IS_CERR(t) || !seq_is(&lib, t.val, (int[]){ 3, 0 }, (bool[]){ false, true }, 2))    goto out;

    t = anim_lib_transition_find(&lib, S_IDLE, S_MOVING, nullptr);
    if (IS_CERR(t) || !seq_is(&lib, t.val, (int[]){ 0 }, (bool[]){ true }, 1))              goto out;

    t = anim_lib_transition_find(&lib, S_JUMPING, S_IDLE, nullptr);
    if (!IS_CERR_CODE(t, CERR_NOT_FOUND))       goto out;

    ret = EXIT_SUCCESS;
out:
    anim_lib_done(&lib);
    return ret;
}

/* an unknown state, a non-array value or an empty clip name is a parse error */
static int lib_test_from_json_bad(void)
{
    static const char *bad[] = {
        "{ \"flying\": [ \"idle*\" ] }",
        "{ \"idle>moving>idle\": [ \"idle*\" ] }",
        "{ \"idle\": \"idle*\" }",
        "{ \"idle\": [ \"*\" ] }",
    };

    for (int i = 0; i < array_size(bad); i++) {
        LOCAL_SET(JsonNode, root) = json_decode(bad[i]);
        anim_lib_t lib;

        anim_lib_init(&lib, nullptr);
        cerr err = anim_lib_transitions_from_json(&lib, root, 0.0, states, S_NR);
        anim_lib_done(&lib);

        if (!IS_CERR_CODE(err, CERR_PARSE_FAILED))  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "lib dedup",          .test = lib_test_dedup },
    { .name = "lib from json",      .test = lib_test_from_json },
    { .name = "lib from json bad",  .test = lib_test_from_json_bad },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
