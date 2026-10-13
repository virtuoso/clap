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
struct joint;
struct JsonNode;
struct gpsm_state;

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
 * @ANIM_POSE:  a procedural pose class (anim_pose_ops), a rig or a constraint
 * @ANIM_SEQ:   a sequence of the above, played in order
 */
typedef enum anim_kind {
    ANIM_CLIP,
    ANIM_HOLD,
    ANIM_POSE,
    ANIM_SEQ,
} anim_kind;

/**
 * struct anim_pose_ops - a procedural pose class
 * @start:  optional; the class is (re)entered on an entity: anchor whatever
 *          it anchors on (a gait's speed measurement, a constraint's target)
 * @eval:   write the local TRS of the joints the class owns into @out,
 *          nr_joints entries laid out as entity3d::joints; on the base layer
 *          @out is the entity's joints themselves, reset to the bind pose
 *          beforehand. @t is seconds since start(), @dt since the last eval
 *          (0 on the first)
 * @done:   optional; a finite class says it has finished; nullptr: plays
 *          until stopped or replaced
 *
 * @cfg is the definition's configuration, model-level data every entity
 * shares (nullptr if the class has none); @priv is this entity's binding
 * (animator_bind()), the instance state: chains, phases, tracked targets.
 * A class that writes only @out composes with anything; one that reaches
 * into the entity's joints directly (the rigs, through ik_chain_solve())
 * is a base-layer source.
 */
typedef struct anim_pose_ops {
    void (*start)(const void *cfg, void *priv, struct entity3d *e);
    cerr (*eval)(const void *cfg, void *priv, struct entity3d *e, double t, double dt,
                 struct joint *out);
    bool (*done)(const void *cfg, void *priv, double t);
} anim_pose_ops;

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

/**
 * typedef anim_contact_fn - the entity's body or one of its limbs touched something
 * @a:      the animator
 * @joint:  the joint the touching limb rides; -1 for the entity's own body
 * @other:  what it touched: valid for the duration of the call; to keep it,
 *          take your own reference and drop it where you consume it
 * @pos:    the contact point, world space
 * @normal: the surface normal at it, pointing from @other toward the entity
 * @priv:   animator_set_contact_callback()'s
 *
 * Fires from animator_update(), once per (joint, other) pair per frame,
 * for what the physics step found touching since the last update; a
 * brain's state machine can be advanced from it.
 */
typedef void (*anim_contact_fn)(animator_t *a, int joint, struct entity3d *other,
                                const vec3 pos, const vec3 normal, void *priv);

struct anim_def;
struct anim_contact;
struct anim_transition;
struct anim_binding;
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
 * @e:          the entity3d it is embedded in
 * @bindings:   the pose classes' instances on this entity (animator_bind())
 * @layers:     what is playing; [0] is the base layer, the overlays above it,
 *              each created on first use
 * @contact:    the contact callback (animator_set_contact_callback()), if any
 * @contact_priv: its argument
 * @contacts:   what the physics step reported since the last update, each
 *              holding a reference to its other entity until it is drained
 * @pins:       the pinned joints (animator_pin()); size 0: none
 * @pin_pose:   the pose as of the first pin, nr_joints joints, allocated
 *              on first use: where the pinned joints stay
 * @pin_fade_start: a fading release (animator_unpin_fade()) started at
 *              this time; negative until the first update after the call
 * @pin_fade_dur: how long it takes; 0: no release under way
 * @pin_weight: how much of the pinned pose is left, 1 down to 0 over a
 *              fading release; 1 otherwise
 */
TYPE(animator,
    struct entity3d             *e;
    darray(struct anim_binding, bindings);
    darray(struct anim_layer,   layers);
    anim_contact_fn             contact;
    void                        *contact_priv;
    darray(struct anim_contact, contacts);
    struct bitmap               pins;
    struct joint                *pin_pose;
    double                      pin_fade_start;
    double                      pin_fade_dur;
    double                      pin_weight;
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
 * The same length is the same handle.
 * Return: the handle or CERR_NOMEM
 */
cres(anim_t) anim_lib_hold(anim_lib_t *lib, double seconds);

/**
 * anim_lib_pose() - define a procedural pose class
 * @lib:    the library
 * @ops:    the class
 * @cfg:    its model-level configuration, or nullptr
 *
 * The same @ops and @cfg are the same handle. Playing it on an entity needs
 * animator_bind() first.
 * Return: the handle, CERR_INVALID_ARGUMENTS (no eval) or CERR_NOMEM
 */
cres(anim_t) anim_lib_pose(anim_lib_t *lib, const anim_pose_ops *ops, const void *cfg);

/**
 * anim_lib_pose_copy() - define a procedural pose class over a copy of its cfg
 * @lib:    the library
 * @ops:    the class
 * @cfg:    its model-level configuration
 * @size:   @cfg's size
 *
 * Like anim_lib_pose(), but the library keeps a copy of @cfg for as long
 * as it lives: for a configuration built at load time from the model's
 * data, which nothing else would keep. Every call is a new handle; find it
 * again with anim_lib_pose_find().
 * Return: the handle, CERR_INVALID_ARGUMENTS (no eval or no @cfg) or
 * CERR_NOMEM
 */
cres(anim_t) anim_lib_pose_copy(anim_lib_t *lib, const anim_pose_ops *ops, const void *cfg,
                                size_t size);

/**
 * anim_lib_pose_find() - the library's definition of a procedural pose class
 * @lib:    the library
 * @ops:    the class
 *
 * For a model whose data defines a class (anim_lib_pose_copy()) and code
 * that plays it without knowing the configuration.
 * Return: the first handle of @ops, or CERR_NOT_FOUND
 */
cres(anim_t) anim_lib_pose_find(anim_lib_t *lib, const anim_pose_ops *ops);

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
 * @fade:   cross-fade seconds, which animator_play_transition() applies
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

/**
 * anim_lib_transition_from_spec() - one transition table entry from strings
 * @lib:        the library
 * @key:        a state name, or "from>to" for one particular transition
 * @clips:      the clips to play in order, comma-separated, "*" after a
 *              name for repeat: "motion_start, motion*"
 * @fade:       the cross-fade seconds the entry gets
 * @states:     the machine's states, for their names
 * @nr_states:  how many
 *
 * The GLTF extras loader's parser, built from the same pieces as the JSON
 * loader's, for a loader of some other shape too. Clips are looked up under the names they have at the
 * call.
 * Return: CERR_OK, CERR_PARSE_FAILED (a bad key, an unknown state, an empty
 * element), CERR_NOT_FOUND (a clip the model lacks) or CERR_NOMEM
 */
cerr anim_lib_transition_from_spec(anim_lib_t *lib, const char *key, const char *clips, double fade,
                                   const struct gpsm_state *states, unsigned int nr_states);

/**
 * anim_lib_transitions_from_json() - the transition table from a scene's JSON
 * @lib:        the library
 * @sequences:  the "sequences" object: state name or "from>to" -> an array of
 *              clip names, "*" after a name for repeat (core/animator/json.c)
 * @fade:       the cross-fade seconds every entry gets
 * @states:     the machine's states, for their names
 * @nr_states:  how many
 *
 * Clips are looked up under the names they have at the call, so this comes
 * after the scene's "animations" renames. An entry naming a clip the model
 * lacks is left out with a warning; the rest load.
 * Return: CERR_OK, CERR_PARSE_FAILED (a key or a value of the wrong shape, an
 * unknown state) or CERR_NOMEM
 */
cerr anim_lib_transitions_from_json(anim_lib_t *lib, struct JsonNode *sequences, double fade,
                                    const struct gpsm_state *states, unsigned int nr_states);

struct gltf_data;

/**
 * anim_lib_transitions_from_gltf() - the transition table from a model's GLTF extras
 * @lib:        the library
 * @gd:         the loaded asset; the first node with "clap::anim::" extras
 *              carries the table, one string per entry in
 *              anim_lib_transition_from_spec()'s grammar
 *              (core/animator/extras.c)
 * @fade:       the cross-fade seconds every entry gets; negative takes the
 *              asset's "clap::anim::fade", 0 without one
 * @states:     the machine's states, for their names
 * @nr_states:  how many
 *
 * As anim_lib_transitions_from_json(): after the renames, an entry naming
 * a clip the model lacks is left out with a warning.
 * Return: CERR_OK, CERR_NOT_FOUND (no node has the extras: the caller falls
 * back), CERR_PARSE_FAILED or CERR_NOMEM
 */
cerr anim_lib_transitions_from_gltf(anim_lib_t *lib, struct gltf_data *gd, double fade,
                                    const struct gpsm_state *states, unsigned int nr_states);

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
 * Called by entity3d_drop(). The bindings' priv is the binders' to free.
 */
void animator_done(animator_t *a);

/**
 * animator_bind() - give a pose class its instance on this entity
 * @a:      the animator
 * @ani:    an anim_lib_pose() handle
 * @priv:   the instance, anim_pose_ops' @priv; the caller keeps it alive
 *          while the class can play here
 *
 * Required before @ani, or a sequence containing it, can play on this
 * entity. Binding again replaces the instance.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (not a pose class) or CERR_NOMEM
 */
cerr animator_bind(animator_t *a, anim_t ani, void *priv);

/**
 * animator_binding() - a pose class's instance on this entity
 * @a:      the animator
 * @ani:    the handle
 *
 * Return: what animator_bind() was given, or CERR_NOT_FOUND
 */
cresp(void) animator_binding(animator_t *a, anim_t ani);

/**
 * struct anim_play_opts - animator_play() options
 * @layer:      0, the base; above it an overlay, created on first use
 * @fade:       seconds; 0 is a cut. On the base a cross-fade from what was
 *              playing (a clip or a hold; a pose class writes the joints
 *              itself and cuts); on an overlay the weight fading in
 * @speed:      playback rate, 1.0 as authored; 0 means 1.0
 * @weight:     an overlay's blend weight, (0, 1]; 0 means 1.0; the base is 1
 * @phase:      where in the first element to start, [0, 1) of its length
 * @repeat:     loop a bare clip or hold; a sequence's elements carry their own
 * @end:        called once when the played thing ends
 * @end_priv:   its argument
 */
typedef struct anim_play_opts {
    double          fade;
    double          speed;
    double          weight;
    double          phase;
    anim_end_fn     end;
    void            *end_priv;
    unsigned int    layer;
    bool            repeat;
} anim_play_opts;

cerr _animator_play(animator_t *a, anim_t ani, const anim_play_opts *opts);
cerr _animator_play_transition(animator_t *a, int from, int to, const anim_play_opts *opts);

/**
 * animator_play() - play an animation on a layer
 * @_a:     the animator
 * @_ani:   a handle out of the entity's model's library
 * @args:   &struct anim_play_opts field initializers
 *
 * Replaces whatever the layer was playing, cut unless .fade says otherwise:
 * the interrupted thing's end callback does not fire. The clock starts at
 * the next animator_update(), where the first pose is written; a play made
 * from an end callback on its own layer starts at the end of that update,
 * so a chain of
 * plays runs without a frame's gap at each link. An overlay
 * writes the joints its mask names (animator_mask_chain(), all of them
 * without a mask) over the layers below it, by its weight; an overlay
 * that ends clears, the base holds its last pose.
 * Context: anywhere but from an anim_end_fn of the same layer's outgoing
 * play, where it is fine too: the callback is the last thing that play does
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (a foreign or empty handle),
 * CERR_INVALID_OPERATION (a pose class in it has no binding here, or an
 * overlay on an entity with no armature) or CERR_NOMEM
 */
#define animator_play(_a, _ani, args...) _animator_play((_a), (_ani), &(anim_play_opts){ args })

/**
 * animator_play_transition() - play the library's animation for a transition
 * @_a:     the animator
 * @_from:  the state being left
 * @_to:    the state being entered
 * @args:   &struct anim_play_opts field initializers; the table's fade
 *          applies unless .fade is set
 *
 * anim_lib_transition_find() then animator_play().
 * Return: CERR_OK, CERR_NOT_FOUND (no such transition) or animator_play()'s
 */
#define animator_play_transition(_a, _from, _to, args...) \
    _animator_play_transition((_a), (_from), (_to), &(anim_play_opts){ args })

/**
 * animator_stop() - stop a layer where it stands
 * @a:      the animator
 * @layer:  the layer
 * @fade:   an overlay fades its weight out over this many seconds first;
 *          0, and the base always, stops now
 *
 * The base's joints keep the last written pose, an overlay's contribution
 * goes; no end callback fires.
 */
void animator_stop(animator_t *a, unsigned int layer, double fade);

/**
 * animator_set_weight() - change an overlay's blend weight
 * @a:      the animator
 * @layer:  an overlay, 1 and up
 * @weight: [0, 1]: how much of the overlay's pose the masked joints take
 * @fade:   seconds to get there; 0 is now
 *
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (the base) or
 * CERR_INVALID_OPERATION (nothing plays there)
 */
cerr animator_set_weight(animator_t *a, unsigned int layer, double weight, double fade);

/**
 * animator_mask_chain() - add a joint chain to an overlay's mask
 * @a:      the animator
 * @layer:  an overlay, 1 and up; created if it does not exist yet
 * @tip:    the chain's last joint
 * @nr:     how many joints up from @tip, the root's parent stopping it
 *
 * A mask is the layer's, kept across plays; a layer with none writes every
 * joint. The base has no mask: it writes everything.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (the base, a joint out of range),
 * CERR_INVALID_OPERATION (no armature) or CERR_NOMEM
 */
cerr animator_mask_chain(animator_t *a, unsigned int layer, int tip, unsigned int nr);

/**
 * animator_mask_subtree() - add a joint and everything under it to an overlay's mask
 * @a:      the animator
 * @layer:  an overlay, 1 and up; created if it does not exist yet
 * @root:   the subtree's root joint
 *
 * Return: as animator_mask_chain()
 */
cerr animator_mask_subtree(animator_t *a, unsigned int layer, int root);

/**
 * animator_set_speed() - change a layer's playback rate
 * @a:      the animator
 * @layer:  the layer
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
 * for an element with no length to measure against), the rate and the
 * blend weight (1 on the base, 0 where nothing plays).
 */
cres(anim_t)    animator_playing(animator_t *a, unsigned int layer);
bool            animator_is_playing(animator_t *a, unsigned int layer, anim_t ani);
cres(int)       animator_element(animator_t *a, unsigned int layer);
double          animator_elapsed(animator_t *a, unsigned int layer);
cres(double)    animator_progress(animator_t *a, unsigned int layer);
double          animator_speed(animator_t *a, unsigned int layer);
double          animator_weight(animator_t *a, unsigned int layer);

/**
 * animator_pin() - hold a joint where it is
 * @a:      the animator
 * @joint:  the joint
 *
 * Records the current pose (the joints' local transforms, as of the last
 * update) if nothing is pinned yet, and marks @joint. A pose class that
 * honours pins (reach, through ik_chain_solve()'s .pinned and .pose)
 * keeps the chain up to the pinned joint as that pose has it and solves
 * the rest: a segment that hit something stays there while the tip keeps
 * going. Pins hold across plays, through a chain of them (a slam, its
 * hold), until animator_unpin() releases them: the brain that pinned
 * knows when the motion is over. From a contact callback, the pose is the
 * one that hit.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (no armature, a bad joint) or
 * CERR_NOMEM
 */
cerr animator_pin(animator_t *a, int joint);

/**
 * animator_unpin() - release a joint, or all of them
 * @a:      the animator
 * @joint:  the joint; negative releases every pin and the recorded pose
 *
 * At once: a class that honoured the pins solves free from the next
 * update, and the pose jumps by whatever the pins held it away from that.
 * animator_unpin_fade() blends out instead.
 */
void animator_unpin(animator_t *a, int joint);

/**
 * animator_unpin_fade() - release every pin over a fade
 * @a:      the animator
 * @fade:   seconds; 0 releases at once
 *
 * The pins and the pinned pose stay readable while animator_pin_weight()
 * falls from 1 to 0 over @fade, from the next update; a class that
 * honours pins blends what it solves under them with what it solves free
 * by that weight (the reach does), so the pose eases out of where the
 * pins held it instead of jumping. The pins clear when the weight is out.
 * A new animator_pin() during the fade cancels it and holds again.
 */
void animator_unpin_fade(animator_t *a, double fade);

/**
 * animator_pin_weight() - how much of the pinned pose is left
 *
 * Return: 1 while pinned, falling to 0 over a fading release, 1 with
 * nothing pinned
 */
double animator_pin_weight(animator_t *a);

/**
 * animator_is_pinned() - is a joint pinned
 */
bool animator_is_pinned(animator_t *a, int joint);

/**
 * animator_pinned_pose() - the pose the pinned joints hold
 *
 * Return: nr_joints joints laid out as entity3d::joints, or nullptr with
 * nothing pinned
 */
const struct joint *animator_pinned_pose(animator_t *a);

/**
 * animator_set_contact_callback() - ask for what the entity touches
 * @a:      the animator
 * @fn:     the callback; nullptr stops the reports
 * @priv:   its argument
 *
 * Sets ENTITY3D_CONTACT_EVENTS on the entity: from the next physics step
 * the backend reports what its body and limbs (phys_body_new_limb()) are
 * touching, and animator_update() delivers them through @fn.
 */
void animator_set_contact_callback(animator_t *a, anim_contact_fn fn, void *priv);

/**
 * animator_contact_enqueue() - the physics step found the entity touching something
 * @e:      the entity with ENTITY3D_CONTACT_EVENTS
 * @joint:  the limb's joint, -1 for the entity's own body
 * @other:  what it touches
 * @pos:    the contact point, world space
 * @normal: the surface normal at it, from @other toward @e
 *
 * The backends' half: queued for the entity's next animator_update(),
 * which delivers it to the callback and drops it. One entry per (joint,
 * other) pair between updates; nothing for an entity with
 * ENTITY3D_SKIP_UPDATE, which would never drain it. Holds a reference to
 * @other until then, and skips it if it is gone by then.
 * Context: the physics step
 */
void animator_contact_enqueue(struct entity3d *e, int joint, struct entity3d *other,
                              const vec3 pos, const vec3 normal);

/**
 * animator_update() - the per-frame pass
 * @a:  the animator
 * @s:  the scene, for the animation clock
 *
 * Delivers the contacts the physics step queued (anim_contact_fn) first,
 * so the brain can act on them before the pose is written. Then starts
 * what animator_play() asked for and writes the current elements'
 * poses: the base's into the joints (a clip's keyframes; a pose class's
 * eval over the bind pose), cross-faded from the outgoing element while a
 * fade runs, then each overlay's into its scratch copy of the result and
 * blended into the masked joints by its weight; then the skeleton, once.
 * Runs the base clip's frame_sfx and advances every layer: loops a
 * repeating element, moves a sequence on (a pose class ends when its
 * done() says so), or fires the end callback once and keeps the pose (the
 * base) or clears (an overlay). The end callbacks come last, after every
 * layer is done with, so they may play. No pose source calls
 * entity3d_pose_update() itself. An entity whose model
 * has clips and nothing to play idles ("idle", looping, out of phase with
 * its siblings).
 * Context: default_update(), for every entity
 */
void animator_update(animator_t *a, struct scene *s);

#endif /* __CLAP_ANIMATOR_H__ */
