/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANIMATOR_INTERNAL_H__
#define __CLAP_ANIMATOR_INTERNAL_H__

/*
 * The implementors' view of core/animator.h: what the opaque types hold.
 * Included by the sources in core/animator/ only, after they include
 * animator.h with IMPLEMENTOR defined.
 */
#include "animator.h"
#include "model.h"
#include "timeline.h"
#include "util.h"

struct anim_seq_elem {
    anim_t          ani;
    bool            repeat;
};

/**
 * struct anim_def - one library definition
 * @kind:   what it is
 * @clip:   ANIM_CLIP: the index into model3d::anis
 * @hold:   ANIM_HOLD: how long, seconds
 * @pose:   ANIM_POSE: the class and its configuration
 * @seq:    ANIM_SEQ: the elements, in order
 */
struct anim_def {
    union {
        struct { int index; }                                   clip;
        struct { double seconds; }                              hold;
        struct { const anim_pose_ops *ops; const void *cfg; }   pose;
        struct { darray(struct anim_seq_elem, elems); }         seq;
    };
    anim_kind       kind;
};

/**
 * struct anim_contact - one report from the physics step, queued for the update
 * @joint:  the limb's joint, -1 for the entity's own body
 * @other:  what it touched; referenced until the update drops it
 * @pos:    the contact point, world space
 * @normal: from @other toward the entity
 */
struct anim_contact {
    struct entity3d *other;
    vec3            pos;
    vec3            normal;
    int             joint;
};

/**
 * struct anim_binding - a pose class's instance on one entity
 * @ani:    the pose class
 * @priv:   its state on this entity, the binder's
 */
struct anim_binding {
    void            *priv;
    anim_t          ani;
};

/**
 * struct anim_transition - one entry of the state transition table
 * @to:         the state being entered
 * @from:       the state being left; meaningful unless @from_any
 * @from_any:   matches any @from, after the exact entries
 * @ani:        what to play
 * @fade:       cross-fade seconds, which animator_play_transition() applies
 */
struct anim_transition {
    double          fade;
    int             to;
    int             from;
    anim_t          ani;
    bool            from_any;
};

/**
 * struct anim_layer - one slot of playback
 * @active:         something plays here
 * @ended:          the last element ran out; the pose holds, nothing advances
 * @playing:        what plays; meaningful while @active
 * @element:        index into a sequence; 0 for a bare definition
 * @repeat:         a bare definition's repeat flag; a sequence's elements
 *                  carry their own
 * @pending:        animator_play() asked for this; the clock starts at the
 *                  next animator_update()
 * @phase:          the start phase the play asked for
 * @speed:          the rate, applied to the clock when it starts or changes
 * @speed_pending:  animator_set_speed() asked; applied at the next update
 * @now:            the clock as of the last update, for the accessors
 * @tl:             the current element's clock
 * @qa:             the clip record frame_fn callbacks see: the clip index
 *                  and the sfx state, reset when an element starts
 * @end:            the play's end callback, cleared once it fired
 * @end_priv:       its argument
 * @weight:         an overlay's blend weight as of the last update; 1 on
 *                  the base
 * @weight_to:      where the weight is going...
 * @weight_rate:    ...and how fast, per second; 0: it is there
 * @stop_at_zero:   animator_stop(fade): inactive once the weight is out
 * @mask:           the joints an overlay writes; size 0: all of them
 * @scratch:        nr_joints, allocated on first use: an overlay's
 *                  evaluation buffer, the base's outgoing pose in a fade
 * @fading:         the base's outgoing element still evaluates into
 *                  @scratch and blends with the incoming one
 * @fade_start:     the fade's clock; negative until the first update
 * @fade_dur:       how long it takes
 * @fading_ani:     the outgoing element: what it plays...
 * @fading_element: ...which element of it...
 * @fading_repeat:  ...whether that loops...
 * @fading_tl:      ...its clock...
 * @fading_ended:   ...and whether it had already run out, in which case
 *                  @scratch holds its last pose
 */
struct anim_layer {
    anim_t                  playing;
    unsigned int            element;
    double                  phase;
    double                  speed;
    double                  now;
    timeline                tl;
    struct queued_animation qa;
    anim_end_fn             end;
    void                    *end_priv;
    double                  weight;
    double                  weight_to;
    double                  weight_rate;
    struct bitmap           mask;
    struct joint            *scratch;
    double                  fade_start;
    double                  fade_dur;
    anim_t                  fading_ani;
    unsigned int            fading_element;
    timeline                fading_tl;
    bool                    active;
    bool                    ended;
    bool                    repeat;
    bool                    pending;
    bool                    speed_pending;
    bool                    stop_at_zero;
    bool                    fading;
    bool                    fading_repeat;
    bool                    fading_ended;
};

/* lib.c: a definition by handle, nullptr for a handle the library never gave out */
struct anim_def *anim_lib_def(anim_lib_t *lib, anim_t ani);

#endif /* __CLAP_ANIMATOR_INTERNAL_H__ */
