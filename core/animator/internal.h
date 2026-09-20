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
 * @fade:       cross-fade seconds, stored with the transition
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
    bool                    active;
    bool                    ended;
    bool                    repeat;
    bool                    pending;
    bool                    speed_pending;
};

/* lib.c: a definition by handle, nullptr for a handle the library never gave out */
struct anim_def *anim_lib_def(anim_lib_t *lib, anim_t ani);

#endif /* __CLAP_ANIMATOR_INTERNAL_H__ */
