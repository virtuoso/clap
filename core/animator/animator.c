// SPDX-License-Identifier: Apache-2.0
/*
 * An entity's animation playback over its model's library: what handle plays
 * on the base layer, where in it and how fast, and the per-frame pass that
 * turns the current element into joints (a clip's keyframes) or leaves them
 * be (a hold), then advances. What plays next is the library's business. See core/animator.h.
 */
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include <stdlib.h>
#include "clap.h"
#include "scene.h"
#include "internal.h"

void animator_init(animator_t *a, struct entity3d *e)
{
    a->e = e;
    darray_init(a->layers);
}

void animator_done(animator_t *a)
{
    darray_clearout(a->layers);
}

struct entity3d *animator_entity(animator_t *a)
{
    return a->e;
}

anim_lib_t *animator_lib(animator_t *a)
{
    return model3d_anim_lib(a->e->txmodel->model);
}

unsigned int animator_nr_layers(animator_t *a)
{
    return darray_count(a->layers);
}

static struct anim_layer *layer_get(animator_t *a, unsigned int layer)
{
    if (layer >= darray_count(a->layers))   return nullptr;

    return &a->layers.x[layer];
}

/* the definition of the layer's current element and whether it loops */
static struct anim_def *layer_elem(animator_t *a, struct anim_layer *l, bool *repeat)
{
    anim_lib_t *lib = animator_lib(a);
    struct anim_def *def = anim_lib_def(lib, l->playing);

    if (!def)   return nullptr;

    if (def->kind != ANIM_SEQ) {
        *repeat = l->repeat;
        return def;
    }

    if (l->element >= darray_count(def->seq.elems))    return nullptr;

    struct anim_seq_elem *el = &def->seq.elems.x[l->element];
    *repeat = el->repeat;

    return anim_lib_def(lib, el->ani);
}

/* an element's length, seconds at speed 1 */
static double elem_duration(anim_lib_t *lib, const struct anim_def *def)
{
    switch (def->kind) {
        case ANIM_CLIP: return lib->model->anis.x[def->clip.index].time_end;
        case ANIM_HOLD: return def->hold.seconds;
        default:        return 0.0;
    }
}

/* start an element: rewind its clip, reset the sfx state, set the clock */
static void elem_start(animator_t *a, struct anim_layer *l, const struct anim_def *def,
                       double now, double phase)
{
    anim_lib_t *lib = animator_lib(a);

    if (def->kind == ANIM_CLIP) {
        animation_reset_cursors(a->e, &lib->model->anis.x[def->clip.index]);
        l->qa.animation = def->clip.index;
    }
    l->qa.sfx_state = 0;
    timeline_start(&l->tl, now, elem_duration(lib, def), l->speed, phase);
}

cerr _animator_play(animator_t *a, anim_t ani, const anim_play_opts *opts)
{
    struct anim_def *def = anim_lib_def(animator_lib(a), ani);

    if (!def)   return CERR_INVALID_ARGUMENTS;
    if (def->kind == ANIM_SEQ && !darray_count(def->seq.elems))
        return CERR_INVALID_ARGUMENTS;

    struct anim_layer *l = layer_get(a, 0);
    if (!l) {
        l = darray_add(a->layers);
        if (!l) return CERR_NOMEM;
    }

    *l = (struct anim_layer){
        .active     = true,
        .playing    = ani,
        .repeat     = opts->repeat,
        .pending    = true,
        .phase      = opts->phase,
        .speed      = opts->speed > 0.0 ? opts->speed : 1.0,
        .end        = opts->end,
        .end_priv   = opts->end_priv,
    };

    return CERR_OK;
}

cerr _animator_play_transition(animator_t *a, int from, int to, const anim_play_opts *opts)
{
    anim_t ani = CRES_RET_CERR(anim_lib_transition_find(animator_lib(a), to, from, nullptr));

    return _animator_play(a, ani, opts);
}

void animator_stop(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    if (l)  l->active = false;
}

cerr animator_set_speed(animator_t *a, unsigned int layer, double speed)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active)   return CERR_INVALID_OPERATION;

    l->speed = speed > 0.0 ? speed : 1.0;
    l->speed_pending = true;

    return CERR_OK;
}

cres(anim_t) animator_playing(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active)   return cres_error(anim_t, CERR_NOT_FOUND);

    return cres_val(anim_t, l->playing);
}

bool animator_is_playing(animator_t *a, unsigned int layer, anim_t ani)
{
    struct anim_layer *l = layer_get(a, layer);

    return l && l->active && l->playing == ani;
}

cres(int) animator_element(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active)   return cres_error(int, CERR_NOT_FOUND);

    return cres_val(int, (int)l->element);
}

double animator_elapsed(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active || l->pending)     return 0.0;

    return timeline_elapsed(&l->tl, l->now);
}

cres(double) animator_progress(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);
    bool repeat;

    if (!l || !l->active)   return cres_error(double, CERR_NOT_FOUND);
    if (l->ended)           return cres_val(double, 1.0);
    if (l->pending)         return cres_val(double, 0.0);

    struct anim_def *def = layer_elem(a, l, &repeat);
    double duration = def ? elem_duration(animator_lib(a), def) : 0.0;

    if (duration <= 0.0)    return cres_error(double, CERR_NOT_SUPPORTED);

    return cres_val(double, timeline_progress(&l->tl, l->now, duration));
}

double animator_speed(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    return l && l->active ? l->speed : 0.0;
}

void animator_update(animator_t *a, struct scene *s)
{
    struct entity3d *e = a->e;
    anim_lib_t *lib = animator_lib(a);
    struct anim_layer *l = layer_get(a, 0);

    /*
     * The idle fallback: an entity whose model has clips and nothing to
     * play idles, out of phase with its siblings.
     * The phase stays randomized here until instantiation seeds it.
     */
    if ((!l || !l->active) && entity_animated(e)) {
        anim_t idle = CRES_RET(anim_lib_clip(lib, "idle"), return);

        CERR_RET(animator_play(a, idle, .repeat = true, .phase = drand48()), return);
        l = layer_get(a, 0);
    }
    if (!l || !l->active || l->ended)
        return;

    double now = clap_get_current_time(s->clap_ctx);
    bool repeat;
    struct anim_def *def = layer_elem(a, l, &repeat);

    if (!def) {
        l->active = false;
        return;
    }

    if (l->pending) {
        elem_start(a, l, def, now, l->phase);
        l->pending = false;
    }
    if (l->speed_pending) {
        if (timeline_set_speed(&l->tl, now, elem_duration(lib, def), l->speed)) l->qa.sfx_state = 0;
        l->speed_pending = false;
    }
    l->now = now;

    double t = timeline_elapsed(&l->tl, now);
    double duration = elem_duration(lib, def);

    if (def->kind == ANIM_CLIP) {
        struct animation *an = &lib->model->anis.x[def->clip.index];

        /* the sfx state rewinds with the clip */
        if (t == 0.0)   l->qa.sfx_state = 0;

        animation_apply(e, an, t);
        entity3d_pose_update(e);

        if (an->frame_sfx)
            an->frame_sfx(&l->qa, e, s, t / an->time_end);
    }

    if (t < duration)
        return;

    /* the element ran out: loop it, move the sequence on, or say so once */
    if (repeat) {
        elem_start(a, l, def, now, 0.0);
        return;
    }

    struct anim_def *seq = anim_lib_def(lib, l->playing);
    if (seq->kind == ANIM_SEQ && l->element + 1 < darray_count(seq->seq.elems)) {
        l->element++;
        def = layer_elem(a, l, &repeat);
        elem_start(a, l, def, now, 0.0);
        return;
    }

    /* the pose holds; the callback is the last thing this play does */
    l->ended = true;
    anim_end_fn end = l->end;
    l->end = nullptr;
    if (end)
        end(a, s, l->end_priv);
}
