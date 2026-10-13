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

static cerr pose_eval(const void *cfg, void *priv, entity3d *e, double t, double dt, struct joint *out)
{
    return CERR_OK;
}

static const anim_pose_ops pose_ops = { .eval = pose_eval };

/* the same clip, the same hold and the same class over the same cfg are the same handle */
static int lib_test_dedup(void)
{
    anim_lib_t lib;
    int ret = EXIT_FAILURE;
    int cfg_a, cfg_b;

    anim_lib_init(&lib, nullptr);

    cres(anim_t) a = anim_lib_clip(&lib, "idle"), b = anim_lib_clip(&lib, "idle");
    cres(anim_t) h0 = anim_lib_hold(&lib, 0.0), h1 = anim_lib_hold(&lib, 0.0), h2 = anim_lib_hold(&lib, 0.5);
    cres(anim_t) p0 = anim_lib_pose(&lib, &pose_ops, &cfg_a), p1 = anim_lib_pose(&lib, &pose_ops, &cfg_a);
    cres(anim_t) p2 = anim_lib_pose(&lib, &pose_ops, &cfg_b);

    if (IS_CERR(a) || IS_CERR(b) || a.val != b.val)         goto out;
    if (IS_CERR(h0) || IS_CERR(h1) || h0.val != h1.val)     goto out;
    if (IS_CERR(h2) || h2.val == h0.val)                    goto out;
    if (IS_CERR(p0) || IS_CERR(p1) || p0.val != p1.val)     goto out;
    if (IS_CERR(p2) || p2.val == p0.val)                    goto out;
    if (anim_lib_kind(&lib, p0.val) != ANIM_POSE)           goto out;

    cres(anim_t) nope = anim_lib_clip(&lib, "nope");
    if (!IS_CERR_CODE(nope, CERR_NOT_FOUND))                goto out;
    cres(anim_t) noeval = anim_lib_pose(&lib, &(anim_pose_ops){}, nullptr);
    if (!IS_CERR_CODE(noeval, CERR_INVALID_ARGUMENTS))      goto out;
    if (anim_lib_nr(&lib) != 5)                             goto out;

    ret = EXIT_SUCCESS;
out:
    anim_lib_done(&lib);
    return ret;
}

/*
 * A copied configuration is the library's own: it outlives the caller's,
 * every copy is a new handle, and the class's first handle is what
 * anim_lib_pose_find() finds
 */
static int lib_test_pose_copy(void)
{
    static const anim_pose_ops other_ops = { .eval = pose_eval };
    anim_lib_t lib;
    int ret = EXIT_FAILURE;
    int cfg = 42;

    anim_lib_init(&lib, nullptr);

    cres(anim_t) nope = anim_lib_pose_find(&lib, &pose_ops);
    if (!IS_CERR_CODE(nope, CERR_NOT_FOUND))                goto out;

    cres(anim_t) c0 = anim_lib_pose_copy(&lib, &pose_ops, &cfg, sizeof(cfg));
    cres(anim_t) c1 = anim_lib_pose_copy(&lib, &pose_ops, &cfg, sizeof(cfg));
    cfg = 0;

    if (IS_CERR(c0) || IS_CERR(c1) || c0.val == c1.val)     goto out;
    if (anim_lib_kind(&lib, c0.val) != ANIM_POSE)           goto out;

    const int *copy = anim_lib_def(&lib, c0.val)->pose.cfg;
    if (copy == &cfg || *copy != 42)                        goto out;

    cres(anim_t) found = anim_lib_pose_find(&lib, &pose_ops), other = anim_lib_pose_find(&lib, &other_ops);
    if (IS_CERR(found) || found.val != c0.val)              goto out;
    if (!IS_CERR_CODE(other, CERR_NOT_FOUND))               goto out;

    cres(anim_t) nocfg = anim_lib_pose_copy(&lib, &pose_ops, nullptr, sizeof(cfg));
    if (!IS_CERR_CODE(nocfg, CERR_INVALID_ARGUMENTS))       goto out;

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

/* the flat form the GLTF extras use: comma-separated, spaces ignored, the same entries */
static int lib_test_from_spec(void)
{
    anim_lib_t lib;
    int ret = EXIT_FAILURE;

    anim_lib_init(&lib, nullptr);

    cerr err = anim_lib_transition_from_spec(&lib, "moving", "motion_start, motion*", 0.1, states, S_NR);
    if (IS_CERR(err))   goto out;
    err = anim_lib_transition_from_spec(&lib, "jumping>idle", "jump,idle*", 0.1, states, S_NR);
    if (IS_CERR(err))   goto out;

    /* a clip the model lacks: the caller's call */
    err = anim_lib_transition_from_spec(&lib, "jumping", "fall*", 0.1, states, S_NR);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))     goto out;

    /* an empty element, an unknown state, a bad key */
    static const char *bad[][2] = {
        { "idle", "idle*," }, { "flying", "idle*" }, { "idle>moving>idle", "idle*" },
    };
    for (int i = 0; i < array_size(bad); i++) {
        err = anim_lib_transition_from_spec(&lib, bad[i][0], bad[i][1], 0.0, states, S_NR);
        if (!IS_CERR_CODE(err, CERR_PARSE_FAILED))  goto out;
    }

    if (anim_lib_nr_transitions(&lib) != 2)     goto out;

    double fade = 0.0;
    cres(anim_t) t = anim_lib_transition_find(&lib, S_MOVING, S_IDLE, &fade);
    if (IS_CERR(t) || fade != 0.1)                                          goto out;
    if (!seq_is(&lib, t.val, (int[]){ 1, 2 }, (bool[]){ false, true }, 2))  goto out;

    t = anim_lib_transition_find(&lib, S_IDLE, S_JUMPING, nullptr);
    if (IS_CERR(t) || !seq_is(&lib, t.val, (int[]){ 3, 0 }, (bool[]){ false, true }, 2))    goto out;

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
    { .name = "lib pose copy",      .test = lib_test_pose_copy },
    { .name = "lib from json",      .test = lib_test_from_json },
    { .name = "lib from json bad",  .test = lib_test_from_json_bad },
    { .name = "lib from spec",      .test = lib_test_from_spec },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
