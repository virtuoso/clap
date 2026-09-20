/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANIMATOR_H__
#define __CLAP_ANIMATOR_H__

#include "error.h"
#include "linmath.h"
#include "typedef.h"
#include "util.h"

/*
 * Animation playback, in two halves. A model's animation library (anim_lib_t,
 * embedded in model3d) holds what can be played: clips, holds, sequences of
 * them, and the table mapping state transitions to them, built once and
 * shared by every entity of the model. An entity's animator (animator_t,
 * embedded in entity3d) plays handles out of that library: what is playing,
 * where it is, how fast. Brains -- the character's state machine, an NPC's
 * gpsm -- never see clip names: they play handles, or ask the table for the
 * transition they are making.
 *
 * Both types are opaque (core/typedef.h): the sources in core/animator/ see
 * the fields, everyone else goes through the functions below.
 */

struct entity3d;
struct model3d;
struct scene;

TYPE_FORWARD(anim_lib);
TYPE_FORWARD(animator);

/**
 * typedef anim_t - a handle into a model's animation library
 *
 * An index, only ever obtained from the cres(anim_t) of the library that
 * defined it, so a held handle is valid by construction: there is no "no
 * animation" value. It means something against its own library only.
 */
typedef unsigned int anim_t;
cres_ret(anim_t);
cres_ret(double);

/**
 * enum anim_kind - what a handle refers to
 * @ANIM_CLIP:  a model3d animation, played by keyframe interpolation
 * @ANIM_HOLD:  nothing, for a while: the joints keep what they have
 * @ANIM_SEQ:   a sequence of the above, played in order
 */
typedef enum anim_kind {
    ANIM_CLIP,
    ANIM_HOLD,
    ANIM_SEQ,
} anim_kind;

/**
 * typedef anim_end_fn - the thing played by animator_play() has ended
 * @a:      the animator
 * @s:      the scene
 * @priv:   anim_play_opts::end_priv
 *
 * Fires once, from animator_update(), when a non-repeating last element
 * runs out; never from animator_play() itself, so a brain's state machine
 * can be advanced from it (see gpsm_enter_fn()).
 */
typedef void (*anim_end_fn)(animator_t *a, struct scene *s, void *priv);

struct anim_def;
struct anim_transition;
struct anim_layer;

/**
 * anim_lib_t - a model's animation library
 * @model:          the model3d it is embedded in
 * @defs:           the definitions, indexed by anim_t
 * @transitions:    the state transition table
 */
TYPE(anim_lib,
    struct model3d                  *model;
    darray(struct anim_def,         defs);
    darray(struct anim_transition,  transitions);
);

/**
 * animator_t - an entity's animation playback
 * @e:      the entity3d it is embedded in
 * @layers: what is playing; [0] is the base layer, created on first play
 */
TYPE(animator,
    struct entity3d             *e;
    darray(struct anim_layer,   layers);
);

/**
 * anim_lib_init() - set a model's library up, empty
 * @lib:    the library; fully overwritten
 * @m:      the model3d it belongs to
 *
 * Called by model3d_make(); allocates nothing.
 */
void anim_lib_init(anim_lib_t *lib, struct model3d *m);

/**
 * anim_lib_done() - free a library
 * @lib:    the library
 *
 * Called by model3d_drop(). Handles out of it die with it.
 */
void anim_lib_done(anim_lib_t *lib);

/**
 * anim_lib_clip() - define a clip
 * @lib:    the library
 * @name:   the model's animation name (see animation_by_name())
 *
 * Resolves the name at definition time; defining the same clip twice hands
 * back the same handle.
 * Return: the handle, CERR_NOT_FOUND or CERR_NOMEM
 */
cres(anim_t) anim_lib_clip(anim_lib_t *lib, const char *name);

/**
 * anim_lib_hold() - define a hold
 * @lib:        the library
 * @seconds:    how long; 0 ends on the next update, a beat
 *
 * Return: the handle or CERR_NOMEM
 */
cres(anim_t) anim_lib_hold(anim_lib_t *lib, double seconds);

/**
 * anim_lib_seq_make() - define an empty sequence
 * @lib:    the library
 *
 * Return: the handle or CERR_NOMEM
 */
cres(anim_t) anim_lib_seq_make(anim_lib_t *lib);

/**
 * anim_lib_seq_push() - append an element to a sequence
 * @lib:    the library
 * @seq:    the sequence
 * @ani:    a clip or a hold; sequences don't nest
 * @repeat: loop this element instead of moving on, so the sequence never
 *          ends on it
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS or CERR_NOMEM
 */
cerr anim_lib_seq_push(anim_lib_t *lib, anim_t seq, anim_t ani, bool repeat);

/**
 * anim_lib_transition_add() - map a state transition to an animation
 * @lib:    the library
 * @to:     the state being entered
 * @from:   the state being left
 * @ani:    what to play
 * @fade:   cross-fade seconds, stored with the transition and not applied
 *
 * States are the brain's gpsm states, plain ints. An exact (@to, @from)
 * entry is found before an anim_lib_transition_add_any() one for @to.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS or CERR_NOMEM
 */
cerr anim_lib_transition_add(anim_lib_t *lib, int to, int from, anim_t ani, double fade);

/**
 * anim_lib_transition_add_any() - map entering a state from anywhere
 * @lib:    the library
 * @to:     the state being entered
 * @ani:    what to play
 * @fade:   as for anim_lib_transition_add()
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS or CERR_NOMEM
 */
cerr anim_lib_transition_add_any(anim_lib_t *lib, int to, anim_t ani, double fade);

/**
 * anim_lib_transition_find() - the animation for a state transition
 * @lib:    the library
 * @to:     the state being entered
 * @from:   the state being left
 * @fade:   receives the entry's fade; nullptr to skip
 *
 * Return: the handle or CERR_NOT_FOUND
 */
cres(anim_t) anim_lib_transition_find(anim_lib_t *lib, int to, int from, double *fade);

/* how many definitions and transitions the library holds */
unsigned int anim_lib_nr(anim_lib_t *lib);
unsigned int anim_lib_nr_transitions(anim_lib_t *lib);

/* what a handle is; a clip's name, or its kind's, for the logs */
anim_kind   anim_lib_kind(anim_lib_t *lib, anim_t ani);
const char *anim_lib_name(anim_lib_t *lib, anim_t ani);

/**
 * animator_init() - set an entity's animator up, idle
 * @a:  the animator; fully overwritten
 * @e:  the entity3d it belongs to
 *
 * Called by entity3d_make(); allocates nothing.
 */
void animator_init(animator_t *a, struct entity3d *e);

/**
 * animator_done() - free an animator
 * @a:  the animator
 *
 * Called by entity3d_drop().
 */
void animator_done(animator_t *a);

/**
 * struct anim_play_opts - animator_play() options
 * @speed:      playback rate, 1.0 as authored; 0 means 1.0
 * @phase:      where in the first element to start, [0, 1) of its length
 * @repeat:     loop a bare clip or hold; a sequence's elements carry their own
 * @end:        called once when the played thing ends
 * @end_priv:   its argument
 */
typedef struct anim_play_opts {
    double          speed;
    double          phase;
    anim_end_fn     end;
    void            *end_priv;
    bool            repeat;
} anim_play_opts;

cerr _animator_play(animator_t *a, anim_t ani, const anim_play_opts *opts);
cerr _animator_play_transition(animator_t *a, int from, int to, const anim_play_opts *opts);

/**
 * animator_play() - play an animation on the base layer
 * @_a:     the animator
 * @_ani:   a handle out of the entity's model's library
 * @args:   &struct anim_play_opts field initializers
 *
 * Replaces whatever the layer was playing, cut: the interrupted thing's end
 * callback does not fire. The clock starts at the next animator_update(),
 * where the first pose is written.
 * Context: anywhere but from an anim_end_fn of the same layer's outgoing
 * play, where it is fine too: the callback is the last thing that play does
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS (a foreign or empty handle)
 */
#define animator_play(_a, _ani, args...) _animator_play((_a), (_ani), &(anim_play_opts){ args })

/**
 * animator_play_transition() - play the library's animation for a transition
 * @_a:     the animator
 * @_from:  the state being left
 * @_to:    the state being entered
 * @args:   &struct anim_play_opts field initializers
 *
 * anim_lib_transition_find() then animator_play().
 * Return: CERR_OK, CERR_NOT_FOUND (no such transition) or animator_play()'s
 */
#define animator_play_transition(_a, _from, _to, args...) \
    _animator_play_transition((_a), (_from), (_to), &(anim_play_opts){ args })

/**
 * animator_stop() - stop a layer where it stands
 * @a:      the animator
 * @layer:  0, the base layer, the only one
 *
 * The joints keep the last written pose; no end callback fires.
 */
void animator_stop(animator_t *a, unsigned int layer);

/**
 * animator_set_speed() - change a layer's playback rate
 * @a:      the animator
 * @layer:  0, the base layer, the only one
 * @speed:  the new rate; 0 means 1.0
 *
 * Takes effect at the next animator_update(). The elapsed time is not
 * rescaled, it jumps with the rate; an element that lands past its end
 * restarts.
 * Return: CERR_OK or CERR_INVALID_OPERATION (nothing plays there)
 */
cerr animator_set_speed(animator_t *a, unsigned int layer, double speed);

/* the entity, its library, and how many layers exist */
struct entity3d *animator_entity(animator_t *a);
anim_lib_t      *animator_lib(animator_t *a);
unsigned int     animator_nr_layers(animator_t *a);

/*
 * What a layer is up to, as of the last animator_update(): the handle
 * (CERR_NOT_FOUND when nothing plays), whether it is a given one, the
 * sequence element, seconds into it, its progress 0..1 (CERR_NOT_SUPPORTED
 * for an element with no length to measure against) and the rate.
 */
cres(anim_t)    animator_playing(animator_t *a, unsigned int layer);
bool            animator_is_playing(animator_t *a, unsigned int layer, anim_t ani);
cres(int)       animator_element(animator_t *a, unsigned int layer);
double          animator_elapsed(animator_t *a, unsigned int layer);
cres(double)    animator_progress(animator_t *a, unsigned int layer);
double          animator_speed(animator_t *a, unsigned int layer);

/**
 * animator_update() - the per-frame pass
 * @a:  the animator
 * @s:  the scene, for the animation clock
 *
 * Starts what animator_play() asked for, writes the current element's pose
 * (a clip's keyframes into the joints, then the skeleton), runs the clip's
 * frame_sfx, and advances: loops a repeating element, moves a sequence on,
 * or fires the end callback once and keeps the pose. An entity whose model
 * has clips and nothing to play idles ("idle", looping, out of phase with
 * its siblings).
 * Context: default_update(), for every entity
 */
void animator_update(animator_t *a, struct scene *s);

#endif /* __CLAP_ANIMATOR_H__ */
