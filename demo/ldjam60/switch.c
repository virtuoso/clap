// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "clap.h"
#include "gltf.h"
#include "memory.h"
#include "scene.h"
#include "ldjam60.h"

/* A switch's extras: SWITCH_EXTRAS "rotatable" and SWITCH_EXTRAS "direction" */
#define SWITCH_EXTRAS   "switch."
/* The extra that glues a node's entity to another's */
#define PARENT_EXTRA    "parent.name"

/**
 * struct switch_node - a glTF node's entity and what its extras say
 * @e:          the entity the node spawned
 * @name:       the node's name
 * @rotatable:  "switch.rotatable", the node the switch turns; nullptr if the
 *              node isn't a switch
 * @parent:     "parent.name", the node it is glued to; nullptr if it isn't
 * @turn:       "switch.direction", the turn the switch asks for
 */
struct switch_node {
    entity3d        *e;
    char            *name;
    char            *rotatable;
    char            *parent;
    quarter_turn    turn;
};

cresp_ret(switch_node, struct);

/**
 * struct floor_switch - a floor switch
 * @e:          its entity
 * @rotatable:  the rotatable it turns
 * @switches:   the switches it is one of, for their sounds
 * @turn:       the turn it asks of @rotatable
 */
typedef struct floor_switch {
    entity3d        *e;
    rotatable       *rotatable;
    switches        *switches;
    quarter_turn    turn;
} floor_switch;

static DEFINE_CLEANUP(floor_switch, if (*p) mem_free(*p))

/* Equal temperament at A4 = 440 Hz, as synth_pitch_frequency() computes it */
#define NOTE_GS2        103.826f
#define NOTE_A2         110.000f
#define NOTE_A4         440.000f
#define NOTE_E5         659.255f

/* A chirp up a fifth: the switch sets a turn going */
static const synth_note accept_notes[] = { { NOTE_A4, 70.0f }, { NOTE_E5, 140.0f } };

static const synth_config accept_patch = {
    .oscillators    = {
        { .waveform = SYNTH_WAVE_SQUARE, .gain = 0.5f },
        { .waveform = SYNTH_WAVE_SQUARE, .detune = 7.0f, .gain = 0.3f },
    },
    .nr_oscillators = 2,
    .category       = SOUND_CAT_SFX,
    .filter         = { .cutoff = 1800.0f, .resonance = 1.5f, .env_amount = 1.5f },
    .amp_env        = { .attack_ms = 2.0f, .decay_ms = 60.0f, .sustain = 0.5f, .release_ms = 80.0f },
    .filter_env     = { .attack_ms = 0.0f, .decay_ms = 80.0f, .sustain = 0.2f, .release_ms = 60.0f },
    .gain           = 0.4f,
    .gate           = 0.8f,
    .nr_notes       = array_size(accept_notes),
    .notes          = accept_notes,
};

/* A low buzz that sags a semitone: the rotatable is busy turning */
static const synth_note refuse_notes[] = { { NOTE_A2, 120.0f }, { NOTE_GS2, 220.0f } };

static const synth_config refuse_patch = {
    .oscillators    = {
        { .waveform = SYNTH_WAVE_SAWTOOTH, .gain = 0.5f },
        { .waveform = SYNTH_WAVE_SQUARE, .detune = -12.0f, .gain = 0.4f },
    },
    .nr_oscillators = 2,
    .category       = SOUND_CAT_SFX,
    .filter         = { .cutoff = 700.0f, .resonance = 2.0f, .env_amount = 1.0f },
    .amp_env        = { .attack_ms = 5.0f, .decay_ms = 100.0f, .sustain = 0.7f, .release_ms = 100.0f },
    .filter_env     = { .attack_ms = 5.0f, .decay_ms = 150.0f, .sustain = 0.3f, .release_ms = 100.0f },
    .gain           = 0.4f,
    .gate           = 0.9f,
    .nr_notes       = array_size(refuse_notes),
    .notes          = refuse_notes,
};

/* A nudge of the camera sells a switch going off, as ldjam57's do */
static const camera_shake_options switch_shake = { .duration_ms = 300, .frequency = 8.0f, .roll = 0.75f };

/*
 * A switch's sound is made the first time it plays: a synth needs the sound
 * engine running, and on web that takes an input gesture (as the menu's click
 * does, see ui_click_play()); until it runs, the switches are silent
 */
static void switches_play(switches *sws, synth **sound, const synth_config *patch)
{
    if (!*sound)    *sound = CRES_RET(ref_new_checked(synth, .ctx = sws->sound, .config = patch), return);

    synth_start(*sound);
}

/* The switch's rotatable turns unless it's turning already, and the switch says which */
static void switch_trigger(floor_switch *fs)
{
    switches *sws = fs->switches;
    cerr err = rotatable_turn(fs->rotatable, fs->turn);

    dbg("%s: %s %s\n", entity_name(fs->e), entity_name(rotatable_entity(fs->rotatable)),
        IS_CERR(err) ? "is busy turning" : "turns");

    if (IS_CERR(err))   switches_play(sws, &sws->refuse, &refuse_patch);
    else                switches_play(sws, &sws->accept, &accept_patch);

    struct camera *camera = sws->scene->camera;
    if (IS_CERR(err) || !camera)    return;

    CERR_RET(_camera_shake_push(camera, &switch_shake),
             err_cerr(__cerr, "%s: couldn't shake the camera\n", entity_name(fs->e)));
}

/* A character steps on the switch */
static void switch_connect(entity3d *e, entity3d *connection, void *data)
{
    switch_trigger(data);
}

void switches_init(switches *sws, struct clap_context *ctx)
{
    *sws = (switches){ .sound = clap_get_sound(ctx), .scene = clap_get_scene(ctx) };
    darray_init(sws->nodes);
    darray_init(sws->switches);
    darray_init(sws->rotatables);
}

cerr switches_node_spawn(struct scene *scene, struct gltf_data *gd, int node, entity3d *e, void *priv)
{
    const char *name = CRES_RET_CERR(gltf_node_name(gd, node));
    const char *rot_name = nullptr, *direction = nullptr, *parent = nullptr, *key, *str;
    quarter_turn turn = QUARTER_TURN_X_POS;
    switches *sws = priv;

    /* every "switch." extra: what isn't one of the two is a typo, say "switch.directions" */
    for (cres(int) it = gltf_node_extras_prefixed(gd, node, SWITCH_EXTRAS, 0, &key, &str);
         !IS_CERR(it); it = gltf_node_extras_prefixed(gd, node, SWITCH_EXTRAS, it.val, &key, &str)) {
        if (!str) {
            warn("%s: '" SWITCH_EXTRAS "%s' isn't a string\n", name, key);
        } else if (!strcmp(key, "rotatable")) {
            rot_name = str;
        } else if (!strcmp(key, "direction")) {
            direction = str;
        } else {
            warn("%s: unknown extra '" SWITCH_EXTRAS "%s'\n", name, key);
        }
    }

    if (rot_name && !direction) {
        warn("%s: switch for '%s' has no '" SWITCH_EXTRAS "direction'\n", name, rot_name);
        rot_name = nullptr;
    } else if (!rot_name && direction) {
        warn("%s: switch has no '" SWITCH_EXTRAS "rotatable' to turn '%s'\n", name, direction);
    } else if (rot_name) {
        cres(quarter_turn) res = quarter_turn_parse(direction);

        if (IS_CERR(res)) {
            warn("%s: switch direction '%s' isn't one of +x, -x, +y, -y, +z, -z, x, y, z\n", name, direction);
            rot_name = nullptr;
        } else {
            turn = res.val;
        }
    }

    if (!gltf_node_extras_string(gd, node, PARENT_EXTRA, &parent) && gltf_node_extras_present(gd, node, PARENT_EXTRA))
        warn("%s: '" PARENT_EXTRA "' isn't a string\n", name);

    /* every node goes on record: the extras of the nodes yet to come may name it */
    LOCAL_SET(char, node_name) = strdup(name);
    LOCAL(char, rot_copy);
    LOCAL(char, parent_copy);

    if (!node_name)                                 return CERR_NOMEM;
    if (rot_name && !(rot_copy = strdup(rot_name))) return CERR_NOMEM;
    if (parent && !(parent_copy = strdup(parent)))  return CERR_NOMEM;

    struct switch_node *sn = darray_add(sws->nodes);
    if (!sn)    return CERR_NOMEM;

    *sn = (struct switch_node){
        .e          = e,
        .name       = NOCU(node_name),
        .rotatable  = NOCU(rot_copy),
        .parent     = NOCU(parent_copy),
        .turn       = turn,
    };

    return CERR_OK;
}

/* The node that @sn's @what names @name; warns that the name is broken if it matches no node, or several */
static cresp(switch_node) switches_node_ref(switches *sws, struct switch_node *sn, const char *what, const char *name)
{
    struct switch_node *it, *found = nullptr;
    unsigned int nr = 0;

    darray_for_each(it, sws->nodes)
        if (!strcmp(it->name, name) && !nr++)   found = it;

    if (nr == 1)    return cresp_val(switch_node, found);

    warn("%s: '%s' names %s node '%s'\n", sn->name, what, nr ? "more than one" : "no", name);

    return cresp_error(switch_node, CERR_NOT_FOUND);
}

/*
 * Glue @e to @parent where it stands in the world: unlike the scene file's
 * "attach", which takes the entity's transform as relative to its parent,
 * the relative transform is the difference between the two world transforms
 */
static void glue_to_parent(entity3d *e, entity3d *parent)
{
    quat parent_rot, parent_inv, e_rot, rot;
    vec3 pos;

    entity3d_world_to_model(parent, transform_pos(&e->xform, nullptr), pos);
    vec4_dup(parent_rot, transform_rotation_quat(&parent->xform));
    vec4_dup(e_rot, transform_rotation_quat(&e->xform));
    quat_conj(parent_inv, parent_rot);
    quat_mul(rot, parent_inv, e_rot);

    e->parent = parent;
    entity3d_position(e, pos);
    transform_set_quat(&e->xform, rot);
    entity3d_scale(e, e->scale / parent->scale);
}

/* The rotatable of @e: the one an earlier switch made, or a new one */
static cresp(rotatable) switches_rotatable(switches *sws, entity3d *e)
{
    rotatable **it;

    darray_for_each(it, sws->rotatables)
        if (rotatable_entity(*it) == e)     return cresp_val(rotatable, *it);

    LOCAL_SET(rotatable, r) = CRES_RET_T(rotatable_new(e), rotatable);

    rotatable **slot = darray_add(sws->rotatables);
    if (!slot)  return cresp_error(rotatable, CERR_NOMEM);

    *slot = NOCU(r);

    return cresp_val(rotatable, *slot);
}

/* @sn's entity becomes a switch that turns @target's */
static cerr switches_add(switches *sws, struct switch_node *sn, struct switch_node *target)
{
    rotatable *r = CRES_RET_CERR(switches_rotatable(sws, target->e));

    LOCAL_SET(floor_switch, fs) = mem_alloc(sizeof(*fs));
    if (!fs)    return CERR_NOMEM;

    floor_switch **slot = darray_add(sws->switches);
    if (!slot)  return CERR_NOMEM;

    *fs = (floor_switch){
        .e          = sn->e,
        .rotatable  = r,
        .switches   = sws,
        .turn       = sn->turn,
    };
    sn->e->connect = switch_connect;
    sn->e->connect_priv = fs;
    *slot = NOCU(fs);

    return CERR_OK;
}

static void switches_nodes_clear(switches *sws)
{
    struct switch_node *sn;

    darray_for_each(sn, sws->nodes) {
        mem_free(sn->name);
        mem_free(sn->rotatable);
        mem_free(sn->parent);
    }

    darray_clearout(sws->nodes);
}

cerr switches_resolve(switches *sws)
{
    struct switch_node *sn;
    cerr err = CERR_OK;

    darray_for_each(sn, sws->nodes) {
        if (!sn->parent)    continue;

        auto parent = CRES_RET(switches_node_ref(sws, sn, PARENT_EXTRA, sn->parent), continue);
        glue_to_parent(sn->e, parent->e);
    }

    darray_for_each(sn, sws->nodes) {
        if (!sn->rotatable) continue;

        auto target = CRES_RET(switches_node_ref(sws, sn, SWITCH_EXTRAS "rotatable", sn->rotatable), continue);
        CERR_RET(switches_add(sws, sn, target), { err = __cerr; break; });
    }

    //dbg("%zu switch(es) turn %zu rotatable(s)\n", darray_count(sws->switches), darray_count(sws->rotatables));
    switches_nodes_clear(sws);

    return err;
}

void switches_reset(switches *sws)
{
    rotatable **r;

    darray_for_each(r, sws->rotatables) rotatable_reset(*r);
}

void switches_done(switches *sws)
{
    floor_switch **fs;
    rotatable **r;

    darray_for_each(fs, sws->switches) {
        (*fs)->e->connect = nullptr;
        (*fs)->e->connect_priv = nullptr;
        mem_free(*fs);
    }

    darray_for_each(r, sws->rotatables)
        rotatable_done(*r);

    darray_clearout(sws->switches);
    darray_clearout(sws->rotatables);
    switches_nodes_clear(sws);

    if (sws->accept)    ref_put(sws->accept);
    if (sws->refuse)    ref_put(sws->refuse);
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

/* Point the switch @turn's way, and say so in the log */
static void switch_set_turn(floor_switch *fs, quarter_turn turn)
{
    if (turn == fs->turn)   return;

    dbg("%s: " SWITCH_EXTRAS "direction %s -> %s\n", entity_name(fs->e), quarter_turn_name(fs->turn),
        quarter_turn_name(turn));
    fs->turn = turn;
}

void switches_debug(switches *sws)
{
    debug_module *dbgm = ui_igBegin_name(DEBUG_GAME, ImGuiWindowFlags_AlwaysAutoResize, "switches");

    if (!dbgm->display) return;

    if (dbgm->unfolded && ui_igTableHeader("switches", (const char *[]){ "switch", "rotatable", "direction", "" }, 4)) {
        floor_switch **it;

        darray_for_each(it, sws->switches) {
            floor_switch *fs = *it;

            igPushID_Ptr(fs);
            ui_igTableCell(true, "%s", entity_name(fs->e));
            ui_igTableCell(false, "%s", entity_name(rotatable_entity(fs->rotatable)));

            igTableNextColumn();
            if (igBeginCombo("##direction", quarter_turn_name(fs->turn), ImGuiComboFlags_WidthFitPreview)) {
                for (quarter_turn t = 0; t < QUARTER_TURN_NR; t++)
                    if (igSelectable_Bool(quarter_turn_name(t), t == fs->turn, 0, (ImVec2){}))
                        switch_set_turn(fs, t);

                igEndCombo();
            }

            igTableNextColumn();
            if (igButton("flip", (ImVec2){}))   switch_set_turn(fs, quarter_turn_reversed(fs->turn));
            igSameLine(0.0f, 4.0f);
            if (igButton("turn", (ImVec2){}))   switch_trigger(fs);

            igPopID();
        }

        igEndTable();
    }

    ui_igEnd(DEBUG_GAME);
}
#endif /* !CONFIG_FINAL */
