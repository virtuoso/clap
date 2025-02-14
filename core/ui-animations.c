// SPDX-License-Identifier: Apache-2.0
#include "interp.h"
#include "ui.h"
#include "ui-debug.h"

struct ui_animation {
    struct list         entry;
    struct ui_element   *uie;
    void                (*trans)(struct ui_animation *uia);
    void                *setter;
    void                (*iter)(struct ui_animation *uia);
    double              start_time;
    double              duration;
    int                 int0;
    int                 int1;
    float               float0;
    float               float_start;
    float               float_end;
    float               float_delta;
    float               float_shift;
};

static void ui_animation_done(struct ui_animation *uia)
{
    list_del(&uia->entry);
    mem_free(uia);
}

void ui_element_animations_done(struct ui_element *uie)
{
    struct ui_animation *ua, *itua;
    list_for_each_entry_iter (ua, itua, &uie->animation, entry) {
        ui_animation_done(ua);
    }
}

static void ui_animation_run(struct ui_animation *ua)
{
    if (ua->uie->ui->time >= ua->start_time)
        ua->trans(ua);
}

static int ui_animation_update(entity3d *e, void *data)
{
    struct ui_element   *uie = e->priv;
    struct ui_animation *ua;

    if (list_empty(&uie->animation)) {
        e->update = ui_element_update;
        goto out;
    }

    ua = list_first_entry(&uie->animation, struct ui_animation, entry);
    ui_animation_run(ua);

out:
    return ui_element_update(e, data);
}

static void ui_animation_next(struct ui_animation *ua)
{
    struct ui_animation *next;

    if (ua == list_last_entry(&ua->uie->animation, struct ui_animation, entry))
        return;

    next = list_next_entry(ua, entry);
    ui_animation_run(next);
}

static struct ui_animation *ui_animation(struct ui_element *uie)
{
    struct ui_animation *ua;

    ua = mem_alloc(sizeof(struct ui_animation), .zero = 1, .fatal_fail = 1);
    ua->uie = uie;
    list_append(&uie->animation, &ua->entry);
    uie->entity->update = ui_animation_update;

    return ua;
}

/* Time elapsed for this animation */
static double ua_elapsed(struct ui_animation *ua)
{
    struct ui *ui = ua->uie->ui;
    if (ui->time < ua->start_time)
        return 0;
    return ui->time - ua->start_time;
}

/* Progress of the animation in range [0..1] */
static double ua_progress(struct ui_animation *ua)
{
    return ua_elapsed(ua) / ua->duration;
}

/* If it's past its duration */
static bool ua_expired(struct ui_animation *ua)
{
    return ua_elapsed(ua) > ua->duration;
}

/* Get start time for a new animation */
static double start_time(struct ui_element *uie, bool wait)
{
    double time = uie->ui->time;

    if (!list_empty(&uie->animation)) {
        struct ui_animation *last = list_last_entry(&uie->animation, struct ui_animation, entry);
        time = last->start_time + (wait ? last->duration : 0);
    }

    return time;
}

/*
 * *_iter(): functions that update corresponding animated value
 */

static void __uia_lin_float_iter(struct ui_animation *ua)
{
    ua->float0 = linf_interp(ua->float_start, ua->float_end, ua_progress(ua));
}

static void __uia_cos_float_iter(struct ui_animation *ua)
{
    ua->float0 = cosf_interp(ua->float_start, ua->float_end,
                             ua->float_shift + ua->float_delta * ua_elapsed(ua));
}

/*
 * *_trans(): initialize the value, call ::iter(), check that this animation
 * should keep running, call ::setter to apply the updated value, call the
 * next animation in the list, delete itself when it's done
 */

static void __uia_float_trans(struct ui_animation *ua)
{
    void (*float_setter)(struct ui_element *, float) = ua->setter;
    bool done = false;

    if (!ua->int0) {
        /* first iteration: set up the initial animated value */
        ua->float0 = ua->float_start;
        ua->int0++;
    } else {
        /* the rest of the iterations: update the animated value */
        ua->iter(ua);
    }

    /* Check if the value has reached its target OR we ran out of time */
    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end) ||
        ua_expired(ua)) {
        done = true;
        /* clamp, in case we overshoot */
        ua->float0 = ua->float_end;
    }

    /* call the setter for the new value to take effect */
    (*float_setter)(ua->uie, ua->float0);
    /* apply the rest of the animations in the list */
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

static void __uia_float_move_trans(struct ui_animation *ua)
{
    bool done = false;

    if (!ua->int0) {
        /* first iteration: setup initial animated value */
        ua->float0 = ua->float_start;
        ua->int0++;
    } else {
        /* the rest of the iterations: update the animated value */
        ua->iter(ua);
    }

    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end) ||
        ua_expired(ua)) {
        done       = true;
        /* clamp, in case we overshoot */
        ua->float0 = ua->float_end;
    }

    /*
     * ua::int1 is the direction
     * XXX: should this just be a setter, then this function is identical to
     * __uia_float_trans(), except setters only take one float as a parameter,
     * but this one will also need a direction.
     */
    ua->uie->movable[ua->int1] = ua->float0;
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

/*
 * Do nothing for the given period of time, then call the next animation
 * and delete itself
 */
static void __uia_skip_duration_trans(struct ui_animation *ua)
{
    if (!ua_expired(ua))
        return;

    ui_animation_next(ua);
    ui_animation_done(ua);
}

/* Do a thing once and delete itself */
static void __uia_action_trans(struct ui_animation *ua)
{
    ua->iter(ua);

    ui_animation_next(ua);
    ui_animation_done(ua);
}

/*
 * Set element's visibility, call next animation, delete itself immediately
 */
static void __uia_set_visible_trans(struct ui_animation *ua)
{
    ui_element_set_visibility(ua->uie, ua->int0);

    ui_animation_next(ua);
    ui_animation_done(ua);
}

/* ------------------------------- ANIMATIONS --------------------------------
 * These are basically about changing one parameter of a UI element from one
 * value to another over time (or at once). They are added to the ::animation
 * list of a ui_element and are played in the order in whic they were added.
 *
 * Calling one of these will append its animation to the list of UI element's
 * animations. Some of them are one-shot, others take a duration in seconds,
 * indicated by the @duration parameter, over which the animation takes place.
 * The @wait parameter, if true, means don't start this animation until the
 * previous one has played out, otherwise run it in parallel with the previous
 * one on the list, so multiple things can happen at once.
 * --------------------------------------------------------------------------- */

void uia_skip_duration(struct ui_element *uie, double duration)
{
    double _start_time = start_time(uie, true);
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->duration    = duration;
    uia->trans       = __uia_skip_duration_trans;
}

void uia_action(struct ui_element *uie, void (*callback)(struct ui_animation *))
{
    double _start_time = start_time(uie, true);
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->trans       = __uia_action_trans;
    uia->iter        = callback;
}

void uia_set_visible(struct ui_element *uie, int visible)
{
    double _start_time = start_time(uie, true);
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->int0        = visible;
    uia->trans       = __uia_set_visible_trans;
}

void uia_lin_float(struct ui_element *uie, void *setter, float start, float end, bool wait,
                   double duration)
{
    double _start_time = start_time(uie, wait);
    struct ui_animation *uia;
    // float len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->float_start = start;
    uia->float_end   = end;
    // uia->float_delta = len / duration; /* XXX: not really used */
    uia->duration    = duration;
    uia->setter      = setter;
    uia->iter        = __uia_lin_float_iter;
    uia->trans       = __uia_float_trans;
}

void uia_cos_float(struct ui_element *uie, void *setter, float start, float end, bool wait,
                   double duration, float phase, float shift)
{
    double _start_time = start_time(uie, wait);
    float len   = fabsf(start - end);
    float delta = len / duration;
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->duration    = duration;
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = (delta / len) * phase;
    uia->float_shift = delta * shift;
    uia->setter      = setter;
    uia->iter        = __uia_cos_float_iter;
    uia->trans       = __uia_float_trans;
}

void uia_lin_move(struct ui_element *uie, enum uie_mv mv, float start, float end, bool wait,
                  double duration)
{
    double _start_time = start_time(uie, wait);
    struct ui_animation *uia;
    // float len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time;
    uia->duration    = duration;
    uia->float_start = start;
    uia->float_end   = end;
    // uia->float_delta = len / duration; /* XXX: not really used */
    uia->int1        = mv;
    uia->trans       = __uia_float_move_trans;
    uia->iter        = __uia_lin_float_iter;
}

void uia_cos_move(struct ui_element *uie, enum uie_mv mv, float start, float end, bool wait,
                  double duration, float phase, float shift)
{
    double _start_time = start_time(uie, wait);
    float len   = fabsf(start - end);
    float delta = len / duration;
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_time  = _start_time ;
    uia->duration    = duration;
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = (delta / len) * phase;
    uia->float_shift = delta * shift;
    uia->int1        = mv;
    uia->trans       = __uia_float_move_trans;
    uia->iter        = __uia_cos_float_iter;
}
