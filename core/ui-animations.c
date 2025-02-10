// SPDX-License-Identifier: Apache-2.0
#include "interp.h"
#include "ui.h"

struct ui_animation {
    struct list entry;
    struct ui_element *uie;
    void (*trans)(struct ui_animation *uia);
    void *setter;
    void (*iter)(struct ui_animation *uia);
    unsigned long start_frame;
    unsigned long nr_frames;
    unsigned long sound_frame;
    int           int0;
    int           int1;
    float         float0;
    float         float_start;
    float         float_end;
    float         float_delta;
    float         float_shift;
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

static int ui_animation_update(entity3d *e, void *data)
{
    struct ui_element   *uie = e->priv;
    struct ui_animation *ua;

    if (list_empty(&uie->animation)) {
        e->update = ui_element_update;
        goto out;
    }

    ua = list_first_entry(&uie->animation, struct ui_animation, entry);
    ua->trans(ua);

out:
    return ui_element_update(e, data);
}

static void ui_animation_next(struct ui_animation *ua)
{
    struct ui_animation *next;

    if (ua == list_last_entry(&ua->uie->animation, struct ui_animation, entry))
        return;

    next = list_next_entry(ua, entry);
    next->trans(next);
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

/* ------------------------------ ANIMATIONS ------------------------------- */
static void __uia_skip_frames(struct ui_animation *ua)
{
    if (ua->uie->ui->frames_total < ua->start_frame)
        return;

    ui_animation_next(ua);
    ui_animation_done(ua);
}

void uia_skip_frames(struct ui_element *uie, unsigned long frames)
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_frame = uie->ui->frames_total + frames;
    uia->trans       = __uia_skip_frames;
}

static void __uia_action(struct ui_animation *ua)
{
    struct ui_element *uie  = ua->uie;
    bool              done = false;

    if (ua == list_first_entry(&uie->animation, struct ui_animation, entry)) {
        done = true;
        ua->iter(ua);
    }

    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_action(struct ui_element *uie, void (*callback)(struct ui_animation *))
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->trans = __uia_action;
    uia->iter  = callback;
}

static void __uia_set_visible(struct ui_animation *ua)
{
    ui_element_set_visibility(ua->uie, ua->int0);

    ui_animation_next(ua);
    ui_animation_done(ua);
}

void uia_set_visible(struct ui_element *uie, int visible)
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->int0  = visible;
    uia->trans = __uia_set_visible;
}

static unsigned long ua_frames(struct ui_animation *ua)
{
    struct ui *ui = ua->uie->ui;
    return ui->frames_total - ua->start_frame;
}

static bool ua_frames_done(struct ui_animation *ua)
{
    return ua_frames(ua) > ua->nr_frames;
}

/*
 * Updaters
 */
static void __uia_lin_float(struct ui_animation *ua)
{
    ua->float0 += ua->float_delta * ua_frames(ua);
}

static void __uia_quad_float(struct ui_animation *ua)
{
    unsigned long frame;

    for (frame = 0; frame < ua_frames(ua); frame++) {
        ua->float0 += ua->float_delta;
        ua->float_delta += ua->float_delta;
    }
}

static void __uia_float(struct ui_animation *ua)
{
    void (*float_setter)(struct ui_element *, float) = ua->setter;
    bool done = false;

    if (!ua->int0) {
        ua->float0 = ua->float_start;
        ua->int0++;
    } else {
        ua->iter(ua);
    }

    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end) ||
        ua_frames_done(ua)) {
        done = true;
        /* clamp, in case we overshoot */
        ua->float0 = ua->float_end;
    }

    (*float_setter)(ua->uie, ua->float0);
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_lin_float(struct ui_element *uie, void *setter, float start, float end, unsigned long frames)
{
    struct ui_animation *uia;
    float               len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = len / frames;
    uia->setter      = setter;
    uia->iter        = __uia_lin_float;
    uia->trans       = __uia_float;
}

void uia_quad_float(struct ui_element *uie, void *setter, float start, float end, float accel)
{
    struct ui_animation *uia;

    if ((start > end && accel >= 0) || (start < end && accel <= 0)) {
        warn("end %f unreachable from start %f via %f\n", end, start, accel);
        return;
    }

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = accel;
    uia->setter      = setter;
    uia->iter        = __uia_quad_float;
    uia->trans       = __uia_float;
}

static void __uia_float_move(struct ui_animation *ua)
{
    bool done = false;

    if (!ua->int0) {
        ua->float0      = ua->float_start;
        ua->start_frame = ua->uie->ui->frames_total;
        ua->int0++;
    } else {
        ua->iter(ua);
    }

    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end) ||
        ua_frames_done(ua)) {
        done       = true;
        ua->float0 = ua->float_end;
    }

    ua->uie->movable[ua->int1] = ua->float0;
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_lin_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames)
{
    struct ui_animation *uia;
    float               len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->nr_frames   = frames;
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = len / frames;
    uia->int1        = mv;
    uia->trans       = __uia_float_move;
    uia->iter        = __uia_lin_float;
}

static void __uia_cos_float(struct ui_animation *ua)
{
    ua->float0 = cos_interp(ua->float_start, ua->float_end,
                            ua->float_shift + ua->float_delta * ua_frames(ua));
}

void uia_cos_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames, float phase,
                  float shift)
{
    struct ui_animation *uia;
    float               len   = fabsf(start - end);
    float               delta = len / frames;

    CHECK(uia = ui_animation(uie));
    uia->nr_frames   = frames;
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = (delta / len) * phase;
    uia->float_shift = delta * shift;
    uia->int1        = mv;
    uia->trans       = __uia_float_move;
    uia->iter        = __uia_cos_float;
}
