// SPDX-License-Identifier: Apache-2.0
/*
 * An entity's animation playback over its model's library: what handle plays
 * on the base layer, where in it and how fast, and the per-frame pass that
 * turns the current element into joints (a clip's keyframes, a pose class's
 * eval) or leaves them be (a hold), recomputes the skeleton once, then
 * advances. What plays next is the library's business. See core/animator.h.
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
    darray_init(a->bindings);
    darray_init(a->layers);
}

void animator_done(animator_t *a)
{
    darray_clearout(a->bindings);
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

static struct anim_binding *binding_find(animator_t *a, anim_t ani)
{
    struct anim_binding *b;

    darray_for_each(b, a->bindings)
        if (b->ani == ani)  return b;

    return nullptr;
}

cerr animator_bind(animator_t *a, anim_t ani, void *priv)
{
    if (anim_lib_kind(animator_lib(a), ani) != ANIM_POSE)   return CERR_INVALID_ARGUMENTS;

    struct anim_binding *b = binding_find(a, ani);

    if (!b) {
        b = darray_add(a->bindings);
        if (!b) return CERR_NOMEM;
    }
    *b = (struct anim_binding){ .ani = ani, .priv = priv };

    return CERR_OK;
}

cresp(void) animator_binding(animator_t *a, anim_t ani)
{
    struct anim_binding *b = binding_find(a, ani);

    if (!b) return cresp_error(void, CERR_NOT_FOUND);

    return cresp_val(void, b->priv);
}

/* a pose class plays here only once it is bound; a sequence needs all of its */
static bool def_bound(animator_t *a, anim_lib_t *lib, struct anim_def *def)
{
    if (def->kind == ANIM_POSE)     return !!binding_find(a, (anim_t)(def - lib->defs.x));
    if (def->kind != ANIM_SEQ)      return true;

    struct anim_seq_elem *el;
    darray_for_each(el, def->seq.elems)
        if (!def_bound(a, lib, anim_lib_def(lib, el->ani))) return false;

    return true;
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

/* an element's pose class instance on this entity; a play checked it is there */
static void *elem_binding(animator_t *a, anim_lib_t *lib, const struct anim_def *def)
{
    struct anim_binding *b = binding_find(a, (anim_t)(def - lib->defs.x));

    return b ? b->priv : nullptr;
}

/* start an element: rewind its clip or anchor its class, reset the sfx state, set the clock */
static void elem_start(animator_t *a, struct anim_layer *l, const struct anim_def *def,
                       double now, double phase)
{
    anim_lib_t *lib = animator_lib(a);

    if (def->kind == ANIM_CLIP) {
        animation_reset_cursors(a->e, &lib->model->anis.x[def->clip.index]);
        l->qa.animation = def->clip.index;
    } else if (def->kind == ANIM_POSE && def->pose.ops->start) {
        def->pose.ops->start(def->pose.cfg, elem_binding(a, lib, def), a->e);
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
    if (!def_bound(a, animator_lib(a), def))
        return CERR_INVALID_OPERATION;

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

    bool started = l->pending;

    if (l->pending) {
        elem_start(a, l, def, now, l->phase);
        l->pending = false;
    }
    if (l->speed_pending) {
        if (timeline_set_speed(&l->tl, now, elem_duration(lib, def), l->speed)) l->qa.sfx_state = 0;
        l->speed_pending = false;
    }

    double dt = started ? 0.0 : now - l->now;
    l->now = now;

    double t = timeline_elapsed(&l->tl, now);
    bool over;

    switch (def->kind) {
        case ANIM_CLIP: {
            struct animation *an = &lib->model->anis.x[def->clip.index];

            /* the sfx state rewinds with the clip */
            if (t == 0.0)   l->qa.sfx_state = 0;

            /* a clip writes every channel it has and leaves the rest */
            animation_apply(e, an, t);
            entity3d_pose_update(e);

            if (an->frame_sfx)
                an->frame_sfx(&l->qa, e, s, t / an->time_end);
            over = t >= an->time_end;
            break;
        }
        case ANIM_POSE: {
            const anim_pose_ops *ops = def->pose.ops;
            void *priv = elem_binding(a, lib, def);

            /* the class writes over the bind pose; the skeleton follows once */
            entity3d_pose_reset(e);
            CERR_RET(ops->eval(def->pose.cfg, priv, e, t, dt, e->joints),
                     warn_once("%s: pose eval: %s\n", entity_name(e), cerr_str(__cerr.err)));
            entity3d_pose_update(e);

            over = ops->done && ops->done(def->pose.cfg, priv, t);
            break;
        }
        case ANIM_HOLD:
            over = t >= def->hold.seconds;
            break;
        default:
            /* a nested sequence, which the library refuses to build */
            over = true;
            break;
    }

    if (!over)
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
