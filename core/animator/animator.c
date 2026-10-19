// SPDX-License-Identifier: Apache-2.0
/*
 * An entity's animation playback over its model's library: what handle plays
 * on each layer, where in it and how fast, and the per-frame pass that turns
 * the current elements into joints (a clip's keyframes, a pose class's eval)
 * or leaves them be (a hold), blends the layers, recomputes the skeleton
 * once, then advances. Layer 0 is the base and writes the joints outright,
 * cross-faded from its outgoing element while a fade runs; the overlays
 * above it evaluate into a scratch copy and blend the joints their mask
 * names by their weight. What plays next is the library's business. See core/animator.h.
 */
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "clap.h"
#include "scene.h"
#include "blend.h"
#include "internal.h"

void animator_init(animator_t *a, struct entity3d *e)
{
    a->e = e;
    darray_init(a->bindings);
    darray_init(a->layers);
    a->contact = nullptr;
    a->contact_priv = nullptr;
    darray_init(a->contacts);
    a->pins = (struct bitmap){};
    a->pin_pose = nullptr;
    a->pin_fade_start = -1.0;
    a->pin_fade_dur = 0.0;
    a->pin_weight = 1.0;
}


/* the queue's references go with its entries */
static void contacts_clear(animator_t *a)
{
    struct anim_contact *c;

    darray_for_each(c, a->contacts)
        ref_put(c->other);
    darray_clearout(a->contacts);
}

void animator_done(animator_t *a)
{
    struct anim_layer *l;

    contacts_clear(a);
    animator_unpin(a, -1);
    darray_for_each(l, a->layers) {
        if (l->scratch)     mem_free(l->scratch);
        if (l->held)        mem_free(l->held);
        if (l->mask.size)   bitmap_done(&l->mask);
    }
    darray_clearout(a->bindings);
    darray_clearout(a->layers);
}

void animator_set_contact_callback(animator_t *a, anim_contact_fn fn, void *priv)
{
    a->contact = fn;
    a->contact_priv = priv;
    if (fn)
        entity3d_set(a->e, ENTITY3D_CONTACT_EVENTS, nullptr);
    else
        entity3d_clear(a->e, ENTITY3D_CONTACT_EVENTS);
}

void animator_contact_enqueue(struct entity3d *e, int joint, struct entity3d *other,
                              const vec3 pos, const vec3 normal)
{
    animator_t *a = &e->animator;
    struct anim_contact *c;

    if (!a->contact || entity3d_matches(e, ENTITY3D_SKIP_UPDATE))   return;

    /* the step runs in sub-steps: one report per pair per update */
    darray_for_each(c, a->contacts)
        if (c->joint == joint && c->other == other)     return;

    if (!ref_get(other))    return;

    c = darray_add(a->contacts);
    if (!c) { ref_put(other); return; }

    c->joint = joint;
    c->other = other;
    vec3_dup(c->pos, (float *)pos);
    vec3_dup(c->normal, (float *)normal);
}

/* deliver what the step reported: one reference in per entry, one out */
static void contacts_drain(animator_t *a)
{
    if (!darray_count(a->contacts))     return;

    for (unsigned int i = 0; i < darray_count(a->contacts); i++) {
        struct anim_contact *c = &a->contacts.x[i];

        if (a->contact && entity3d_matches(c->other, ENTITY3D_ALIVE))
            a->contact(a, c->joint, c->other, c->pos, c->normal, a->contact_priv);
        ref_put(c->other);
    }
    darray_clearout(a->contacts);
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

static unsigned int nr_joints(animator_t *a)
{
    return a->e->txmodel->model->nr_joints;
}

cerr animator_pin(animator_t *a, int joint)
{
    struct entity3d *e = a->e;

    if (!e->joints || joint < 0 || (unsigned int)joint >= nr_joints(a)) return CERR_INVALID_ARGUMENTS;

    if (!a->pin_pose) {
        a->pin_pose = mem_alloc(sizeof(struct joint), .nr = nr_joints(a));
        if (!a->pin_pose)   return CERR_NOMEM;
        joints_copy_trs(a->pin_pose, e->joints, nr_joints(a));
    }
    if (!a->pins.size)  bitmap_init(&a->pins, nr_joints(a));
    if (!a->pins.size)  return CERR_NOMEM;

    bitmap_set(&a->pins, joint);

    /* a release under way is off: the pins hold again */
    a->pin_fade_dur = 0.0;
    a->pin_fade_start = -1.0;
    a->pin_weight = 1.0;

    return CERR_OK;
}

void animator_unpin(animator_t *a, int joint)
{
    if (joint >= 0) {
        if (!a->pins.size || (unsigned int)joint >= nr_joints(a))   return;

        bitmap_clear(&a->pins, joint);

        /* with the last pin goes the pose the pins held */
        cres(int) pinned = bitmap_find_first_set(&a->pins);
        if (!IS_CERR(pinned))   return;
    }

    if (a->pins.size)   bitmap_done(&a->pins);
    a->pins = (struct bitmap){};
    if (a->pin_pose)    mem_free(a->pin_pose);
    a->pin_pose = nullptr;
    a->pin_fade_dur = 0.0;
    a->pin_fade_start = -1.0;
    a->pin_weight = 1.0;
}

void animator_unpin_fade(animator_t *a, double fade)
{
    if (!a->pins.size)  return;
    if (fade <= 0.0)    { animator_unpin(a, -1); return; }

    a->pin_fade_dur = fade;
    a->pin_fade_start = -1.0;
}

double animator_pin_weight(animator_t *a)
{
    return a->pin_weight;
}

bool animator_is_pinned(animator_t *a, int joint)
{
    return a->pins.size && joint >= 0 && (unsigned int)joint < nr_joints(a) && bitmap_is_set(&a->pins, joint);
}

const struct joint *animator_pinned_pose(animator_t *a)
{
    return a->pins.size ? a->pin_pose : nullptr;
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

/* the layer, created with every one below it on first use */
static struct anim_layer *layer_make(animator_t *a, unsigned int layer)
{
    while (darray_count(a->layers) <= layer) {
        struct anim_layer *l = darray_add(a->layers);

        if (!l) return nullptr;
        *l = (struct anim_layer){ .weight = 1.0, .weight_to = 1.0 };
    }

    return &a->layers.x[layer];
}

/* a layer's nr_joints evaluation buffer, allocated on first use */
static struct joint *layer_scratch(animator_t *a, struct anim_layer *l)
{
    if (!l->scratch)    l->scratch = mem_alloc(sizeof(struct joint), .nr = nr_joints(a));

    return l->scratch;
}

/* a sequence's @element, or the bare definition itself */
static struct anim_def *elem_of(anim_lib_t *lib, anim_t ani, unsigned int element, bool *repeat)
{
    struct anim_def *def = anim_lib_def(lib, ani);

    if (!def || def->kind != ANIM_SEQ)              return def;
    if (element >= darray_count(def->seq.elems))    return nullptr;

    struct anim_seq_elem *el = &def->seq.elems.x[element];
    *repeat = el->repeat;

    return anim_lib_def(lib, el->ani);
}

/* the definition of the layer's current element and whether it loops */
static struct anim_def *layer_elem(animator_t *a, struct anim_layer *l, bool *repeat)
{
    *repeat = l->repeat;

    return elem_of(animator_lib(a), l->playing, l->element, repeat);
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

/* start an element: rewind its clip in @out or anchor its class, reset the sfx state, set the clock */
static void elem_start(animator_t *a, struct anim_layer *l, const struct anim_def *def,
                       double now, double phase, struct joint *out)
{
    anim_lib_t *lib = animator_lib(a);

    if (def->kind == ANIM_CLIP) {
        if (out)    animation_reset_cursors(&lib->model->anis.x[def->clip.index], out);
        l->qa.animation = def->clip.index;
    } else if (def->kind == ANIM_POSE && def->pose.ops->start) {
        def->pose.ops->start(def->pose.cfg, elem_binding(a, lib, def), a->e);
    }
    l->qa.sfx_state = 0;
    timeline_start(&l->tl, now, elem_duration(lib, def), l->speed, phase);
}

/*
 * The base keeps its outgoing element evaluating into scratch while the
 * incoming one fades in over it; from a clip or a hold only, a pose class
 * writes the joints itself and can't be redirected, so it cuts. A fade
 * that starts while another runs freezes the blend as it stands.
 */
static void fade_begin(animator_t *a, struct anim_layer *l, double fade, bool was_fading)
{
    bool repeat;
    struct anim_def *out = layer_elem(a, l, &repeat);

    if (!out || out->kind == ANIM_POSE)     return;

    struct joint *scratch = layer_scratch(a, l);
    if (!scratch)   return;

    memcpy(scratch, a->e->joints, sizeof(*scratch) * nr_joints(a));
    l->fading = true;
    l->fade_start = -1.0;
    l->fade_dur = fade;
    l->fading_ani = l->playing;
    l->fading_element = l->element;
    l->fading_repeat = repeat;
    l->fading_tl = l->tl;
    l->fading_ended = l->ended || was_fading;
}

cerr _animator_play(animator_t *a, anim_t ani, const anim_play_opts *opts)
{
    anim_lib_t *lib = animator_lib(a);
    struct anim_def *def = anim_lib_def(lib, ani);

    if (!def)                                                   return CERR_INVALID_ARGUMENTS;
    if (def->kind == ANIM_SEQ && !darray_count(def->seq.elems)) return CERR_INVALID_ARGUMENTS;
    if (!def_bound(a, lib, def))                                return CERR_INVALID_OPERATION;
    /* an overlay blends joints; without an armature there is nothing to blend */
    if (opts->layer && !a->e->joints)   return CERR_INVALID_OPERATION;

    struct anim_layer *l = layer_make(a, opts->layer);
    if (!l) return CERR_NOMEM;

    bool was_active = l->active && !l->pending;
    bool was_fading = l->fading;

    l->fading = false;
    if (opts->fade > 0.0 && opts->layer == 0 && was_active && a->e->joints) fade_begin(a, l, opts->fade, was_fading);

    l->active = true;
    l->ended = false;
    l->playing = ani;
    l->element = 0;
    l->repeat = opts->repeat;
    l->pending = true;
    l->phase = opts->phase;
    l->speed = opts->speed > 0.0 ? opts->speed : 1.0;
    l->speed_pending = false;
    l->end = opts->end;
    l->end_priv = opts->end_priv;

    if (opts->layer) {
        /* an overlay's weight: fades in from nothing, or from where it was, to the play's */
        l->weight_to = opts->weight > 0.0 ? min(opts->weight, 1.0) : 1.0;
        if (opts->fade > 0.0) {
            if (!was_active)    l->weight = 0.0;
            l->weight_rate = fabs(l->weight_to - l->weight) / opts->fade;
        } else {
            l->weight = l->weight_to;
            l->weight_rate = 0.0;
        }
        l->stop_at_zero = false;
    }

    return CERR_OK;
}

cerr _animator_play_transition(animator_t *a, int from, int to, const anim_play_opts *opts)
{
    double fade = 0.0;
    anim_t ani = CRES_RET_CERR(anim_lib_transition_find(animator_lib(a), to, from, &fade));
    anim_play_opts o = *opts;

    if (o.fade <= 0.0)  o.fade = fade;

    return _animator_play(a, ani, &o);
}

void animator_stop(animator_t *a, unsigned int layer, double fade)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active)   return;

    if (!layer || fade <= 0.0 || l->weight <= 0.0) {
        l->active = false;
        l->fading = false;
        return;
    }

    l->weight_to = 0.0;
    l->weight_rate = l->weight / fade;
    l->stop_at_zero = true;
}

cerr animator_set_speed(animator_t *a, unsigned int layer, double speed)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!l || !l->active)   return CERR_INVALID_OPERATION;

    l->speed = speed > 0.0 ? speed : 1.0;
    l->speed_pending = true;

    return CERR_OK;
}

cerr animator_set_weight(animator_t *a, unsigned int layer, double weight, double fade)
{
    struct anim_layer *l = layer_get(a, layer);

    if (!layer)             return CERR_INVALID_ARGUMENTS;
    if (!l || !l->active)   return CERR_INVALID_OPERATION;

    l->weight_to = clampd(weight, 0.0, 1.0);
    l->weight_rate = fade > 0.0 ? fabs(l->weight_to - l->weight) / fade : 0.0;
    if (fade <= 0.0)    l->weight = l->weight_to;
    l->stop_at_zero = false;

    return CERR_OK;
}

/* an overlay's mask, allocated on first use; the base has none */
static cerr layer_mask(animator_t *a, unsigned int layer, struct anim_layer **lp)
{
    if (!layer)             return CERR_INVALID_ARGUMENTS;
    if (!a->e->joints)      return CERR_INVALID_OPERATION;

    struct anim_layer *l = layer_make(a, layer);
    if (!l) return CERR_NOMEM;

    if (!l->mask.size) {
        bitmap_init(&l->mask, nr_joints(a));
        if (!l->mask.size)  return CERR_NOMEM;
    }
    *lp = l;

    return CERR_OK;
}

cerr animator_mask_chain(animator_t *a, unsigned int layer, int tip, unsigned int nr)
{
    model3d *m = a->e->txmodel->model;
    struct anim_layer *l;

    if (tip < 0 || (unsigned int)tip >= nr_joints(a))   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(layer_mask(a, layer, &l));

    for (int j = tip; nr && j >= 0; nr--, j = m->joints[j].parent)  bitmap_set(&l->mask, j);

    return CERR_OK;
}

static void mask_subtree(model3d *m, struct bitmap *mask, int root)
{
    int *child;

    bitmap_set(mask, root);
    darray_for_each(child, m->joints[root].children)
        mask_subtree(m, mask, *child);
}

cerr animator_mask_subtree(animator_t *a, unsigned int layer, int root)
{
    struct anim_layer *l;

    if (root < 0 || (unsigned int)root >= nr_joints(a))     return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(layer_mask(a, layer, &l));

    mask_subtree(a->e->txmodel->model, &l->mask, root);

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

double animator_weight(animator_t *a, unsigned int layer)
{
    struct anim_layer *l = layer_get(a, layer);

    return l && l->active ? l->weight : 0.0;
}

/*
 * A layer's frame, part one: start a pending element, run the clock, and
 * write the current element into @out (the entity's joints for the base,
 * the layer's scratch for an overlay). Returns the element, nullptr for a
 * layer that ran off its sequence, and whether it is over.
 */
static struct anim_def *layer_step(animator_t *a, struct anim_layer *l, double now, struct scene *s,
                                   struct joint *out, bool sfx, bool *repeat, bool *over)
{
    anim_lib_t *lib = animator_lib(a);
    struct entity3d *e = a->e;
    struct anim_def *def = layer_elem(a, l, repeat);

    if (!def) {
        l->active = false;
        return nullptr;
    }

    bool started = l->pending;

    if (l->pending) {
        elem_start(a, l, def, now, l->phase, out);
        l->pending = false;
    }
    if (l->speed_pending) {
        if (timeline_set_speed(&l->tl, now, elem_duration(lib, def), l->speed)) l->qa.sfx_state = 0;
        l->speed_pending = false;
    }

    double dt = started ? 0.0 : now - l->now;
    l->now = now;

    double t = timeline_elapsed(&l->tl, now);

    /* the weight eases toward where it was sent */
    if (l->weight_rate > 0.0) {
        double step = l->weight_rate * dt;

        if (fabs(l->weight_to - l->weight) <= step) {
            l->weight = l->weight_to;
            l->weight_rate = 0.0;
        } else {
            l->weight += l->weight_to > l->weight ? step : -step;
        }
    }

    switch (def->kind) {
        case ANIM_CLIP: {
            struct animation *an = &lib->model->anis.x[def->clip.index];

            /* the sfx state rewinds with the clip */
            if (t == 0.0)   l->qa.sfx_state = 0;

            /* a clip writes every channel it has and leaves the rest */
            if (out)    animation_apply(an, t, out);

            if (sfx && an->frame_sfx)   an->frame_sfx(&l->qa, e, s, t / an->time_end);
            *over = t >= an->time_end;
            break;
        }
        case ANIM_POSE: {
            const anim_pose_ops *ops = def->pose.ops;
            void *priv = elem_binding(a, lib, def);

            /* on the base the class writes over the bind pose; an overlay's scratch is the pose so far */
            if (out) {
                if (out == e->joints)   entity3d_pose_reset(e);
                CERR_RET(ops->eval(def->pose.cfg, priv, e, t, dt, out),
                         warn_once("%s: pose eval: %s\n", entity_name(e), cerr_str(__cerr.err)));
            }
            *over = ops->done && ops->done(def->pose.cfg, priv, t);
            break;
        }
        case ANIM_HOLD:
            *over = t >= def->hold.seconds;
            break;
        default:
            /* a nested sequence, which the library refuses to build */
            *over = true;
            break;
    }

    return def;
}

/*
 * A layer's frame, part two: the element ran out. Loop it, move the sequence
 * on, or mark the play ended: the base holds its pose, an overlay clears.
 * The end callback is the caller's to fire, once every layer is done with.
 */
static void layer_advance(animator_t *a, struct anim_layer *l, struct anim_def *def, bool repeat,
                          double now, struct joint *out, bool overlay)
{
    if (repeat) {
        elem_start(a, l, def, now, 0.0, out);
        return;
    }

    struct anim_def *seq = anim_lib_def(animator_lib(a), l->playing);
    if (seq->kind == ANIM_SEQ && l->element + 1 < darray_count(seq->seq.elems)) {
        l->element++;
        def = layer_elem(a, l, &repeat);
        elem_start(a, l, def, now, 0.0, out);
        return;
    }

    l->ended = true;
    if (overlay)    l->active = false;
}

/* the base's outgoing element, into scratch, for as long as the fade runs */
static void fading_step(animator_t *a, struct anim_layer *l, double now)
{
    anim_lib_t *lib = animator_lib(a);
    bool repeat = l->fading_repeat;
    struct anim_def *def = elem_of(lib, l->fading_ani, l->fading_element, &repeat);

    if (!def || l->fading_ended || def->kind != ANIM_CLIP)  return;     /* scratch holds its pose */

    struct animation *an = &lib->model->anis.x[def->clip.index];
    double t = timeline_elapsed(&l->fading_tl, now);

    if (t >= an->time_end) {
        if (repeat && an->time_end > 0.0) {
            timeline_start(&l->fading_tl, now, an->time_end, l->fading_tl.speed, 0.0);
            t = 0.0;
        } else {
            l->fading_ended = true;
            t = an->time_end;
        }
    }
    animation_apply(an, t, l->scratch);
}

static void animator_idle(animator_t *a, anim_lib_t *lib)
{
    anim_t idle = CRES_RET(anim_lib_clip(lib, "idle"), return);

    CERR_RET(animator_play(a, idle, .repeat = true, .phase = drand48()), return);
}

/* the joints as the base left them, into its @held; none on a failed allocation */
static void base_hold(animator_t *a, struct anim_layer *l)
{
    unsigned int nr = nr_joints(a);

    if (!l->held)   l->held = mem_alloc(sizeof(struct joint), .nr = nr);
    if (l->held)    joints_copy_trs(l->held, a->e->joints, nr);
}

/* an overlay is about to evaluate */
static bool overlays_running(animator_t *a)
{
    for (unsigned int i = 1; i < darray_count(a->layers); i++)
        if (a->layers.x[i].active && !a->layers.x[i].ended)     return true;

    return false;
}

void animator_update(animator_t *a, struct scene *s)
{
    struct entity3d *e = a->e;
    anim_lib_t *lib = animator_lib(a);

    /* what the step found first: the brain may play or change state on it */
    contacts_drain(a);

    struct anim_layer *l = layer_get(a, 0);

    /*
     * The idle fallback: an entity whose model has clips and nothing to
     * play idles, out of phase with its siblings.
     * The phase stays randomized here until instantiation seeds it. Clips
     * without an idle among them leave the base empty, and the overlays
     * still run
     */
    if ((!l || !l->active) && entity_animated(e)) {
        animator_idle(a, lib);
        l = layer_get(a, 0);
    }
    if (!l) return;

    double now = clap_get_current_time(s->clap_ctx);
    unsigned int nr = e->joints ? nr_joints(a) : 0;
    bool overlays = nr && overlays_running(a);
    bool posed = false, base_wrote = false;

    /* a fading pin release: the weight the pin-honouring classes blend by this frame */
    if (a->pins.size && a->pin_fade_dur > 0.0) {
        if (a->pin_fade_start < 0.0)    a->pin_fade_start = now;
        a->pin_weight = 1.0 - (now - a->pin_fade_start) / a->pin_fade_dur;
        if (a->pin_weight <= 0.0)       animator_unpin(a, -1);
    }

    /* nothing on the base: the overlays go over the bind pose */
    if (!l->active && overlays)     entity3d_pose_reset(e);

    /* the base: into the joints, cross-faded from the outgoing element while that runs */
    if (l->active && !l->ended) {
        bool repeat, over;
        struct anim_def *def = layer_step(a, l, now, s, e->joints, true, &repeat, &over);

        if (def) {
            base_wrote = def->kind != ANIM_HOLD;
            posed |= base_wrote;
            if (over)   layer_advance(a, l, def, repeat, now, e->joints, false);
        }
    }
    if (l->active && l->fading && nr) {
        if (l->fade_start < 0.0)    l->fade_start = now;

        double w = (now - l->fade_start) / l->fade_dur;

        if (w >= 1.0) {
            l->fading = false;
        } else {
            fading_step(a, l, now);
            for (unsigned int j = 0; j < nr; j++)   joint_blend(&e->joints[j], &l->scratch[j], &e->joints[j], (float)w);
            posed = base_wrote = true;
        }
    }

    /*
     * With no clip rewriting the joints every frame, the overlays would go
     * over their own last result, and a look-at's turn or a tilt would
     * compound frame after frame. A base that holds (its play ended, or a
     * hold plays) holds the pose it last wrote, not the joints as the
     * overlays left them: it is kept from the overlays' first frame on, and
     * on every base write after that.
     */
    bool base_set = base_wrote || (overlays && !l->active);

    if (overlays && !base_set && l->held) {
        joints_copy_trs(e->joints, l->held, nr);
        posed = true;
    } else if (overlays || (base_set && l->held)) {
        base_hold(a, l);
    }

    /* the overlays: each into its scratch copy of the pose so far, then blended in by weight */
    for (unsigned int i = 1; i < darray_count(a->layers) && nr; i++) {
        struct anim_layer *o = &a->layers.x[i];

        if (!o->active || o->ended)     continue;

        struct joint *scratch = layer_scratch(a, o);
        if (!scratch) {
            o->active = false;
            continue;
        }
        joints_copy_trs(scratch, e->joints, nr);

        bool repeat, over;
        struct anim_def *def = layer_step(a, o, now, s, scratch, false, &repeat, &over);

        if (!def)   continue;

        if (def->kind != ANIM_HOLD && o->weight > 0.0) {
            for (unsigned int j = 0; j < nr; j++)
                if (!o->mask.size || bitmap_is_set(&o->mask, j))
                    joint_blend(&e->joints[j], &e->joints[j], &scratch[j], (float)o->weight);
            posed = true;
        }
        if (over)                                   layer_advance(a, o, def, repeat, now, scratch, true);
        if (o->stop_at_zero && o->weight <= 0.0)    o->active = false;
    }

    if (posed)  entity3d_pose_update(e);

    /*
     * The end callbacks, last: they may play, which may grow the layers,
     * so each is fetched by index and cleared before it runs
     */
    for (unsigned int i = 0; i < darray_count(a->layers); i++) {
        struct anim_layer *x = layer_get(a, i);

        if (!x || !x->ended || !x->end)     continue;

        anim_end_fn end = x->end;
        x->end = nullptr;
        end(a, s, x->end_priv);

        /*
         * A play chained from the callback starts now, not at the next
         * update: its first frame is then a frame into it, the way the
         * outgoing element's last frame ran out, and a sequence of chained
         * plays runs without a one-frame gap at every link
         */
        x = layer_get(a, i);
        if (!x || !x->active || !x->pending)    continue;

        bool repeat;
        struct anim_def *def = layer_elem(a, x, &repeat);

        if (!def)   continue;

        elem_start(a, x, def, now, x->phase, i ? layer_scratch(a, x) : e->joints);
        x->pending = false;
        x->now = now;
    }
}
