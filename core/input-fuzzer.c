// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "clap.h"
#include "messagebus.h"
#include "input.h"
#include "scene.h"

static struct message_source fuzzer_source = {
    .name = "fuzzer",
    .desc = "random input generator",
    .type = MST_FUZZER,
};

static unsigned long paused;
static bool enabled;

void fuzzer_input_step(struct clap_context *ctx)
{
    int mode, bit, off, count;
    struct message_input mi;
    unsigned long *mask;

    if (!enabled)
        return;

    if (paused > 0) {
        paused--;
        return;
    }

    mode = rand();
    if (mode & 1) {
        mode >>= 1;
        paused = mode & 0xf;
        return;
    }
    mode >>= 1;

    memset(&mi, 0, sizeof(mi));
    mask = (unsigned long *)&mi;

    count = (mode & 0xf);
    mode >>= 4;
    for (; count >= 0; count--) {
        if (!mode)
            mode = rand();
        bit  = (mode & 0x3f);
        mode >>= 6;
        off = bit / (sizeof(*mask) * 8);
        mask[off] |= 1ul << (bit - off * sizeof(*mask) * 8);
    }

    if (!mode)
        mode = rand();

    count = (mode & 0xf);
    mode >>= 4;
    for (; count >= 0; count--) {
        if (!mode)
            mode = rand();
        switch (mode & 0x7) {
            case 0:
                mi.delta_lx = drand48() * 2 - 1;
                break;
            case 1:
                mi.delta_ly = drand48() * 2 - 1;
                break;
            case 2:
                mi.delta_rx = drand48() * 2 - 1;
                break;
            case 3:
                mi.delta_rx = drand48() * 2 - 1;
                break;
            case 4:
                mi.trigger_l = drand48() * 2 - 1;
                break;
            case 5:
                mi.trigger_r = drand48() * 2 - 1;
                break;
            case 6:
                mi.x = rand();
                break;
            case 7:
                mi.x = rand();
                break;
        }
    }

    mi.focus_next = mi.focus_prev = mi.verboser = mi.volume_up = mi.resize = mi.fullscreen = mi.exit = 0;
    message_input_send(ctx, &mi, &fuzzer_source);
}

static int fuzzer_handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    if (m->cmd.toggle_fuzzer)
        enabled = true;
    return 0;
}

void fuzzer_input_init(struct clap_context *ctx)
{
    subscribe(ctx, MT_COMMAND, fuzzer_handle_command, NULL);
}

/*
 * Input motion setters: deterministic alternatives to the random fuzzer.
 * --run-to=<cardinal>   drives the left stick in a fixed direction
 * --run-circle=<radius> rotates the left stick direction over time
 */

enum motion_mode {
    MOTION_NONE,
    MOTION_RUN_TO,
    MOTION_RUN_CIRCLE,
};

static struct {
    enum motion_mode    mode;
    float               dx, dy;     /* run-to: fixed direction */
    float               radius;     /* run-circle: signed radius */
    double              angle;      /* run-circle: current angle */
    bool                jump;       /* always jump */
} motion_state;

static struct message_source motion_source = {
    .name = "motion",
    .desc = "deterministic input motion setter",
    .type = MST_FUZZER,
};

struct cardinal_dir {
    const char  *name;
    float       dx, dy;
};

static const struct cardinal_dir cardinal_dirs[] = {
    { "n",  0,           -1          },
    { "s",  0,            1          },
    { "e",  1,            0          },
    { "w", -1,            0          },
    { "ne", M_SQRT1_2,   -M_SQRT1_2 },
    { "nw", -M_SQRT1_2,  -M_SQRT1_2 },
    { "se", M_SQRT1_2,    M_SQRT1_2  },
    { "sw", -M_SQRT1_2,   M_SQRT1_2  },
};

cerr input_motion_set_run_to(const char *cardinal)
{
    for (size_t i = 0; i < array_size(cardinal_dirs); i++) {
        if (!strcasecmp(cardinal, cardinal_dirs[i].name)) {
            motion_state = (typeof(motion_state)) {
                .mode = MOTION_RUN_TO,
                .dx   = cardinal_dirs[i].dx,
                .dy   = cardinal_dirs[i].dy,
            };
            return CERR_OK;
        }
    }
    return CERR_INVALID_ARGUMENTS_REASON(
        .fmt  = "unknown cardinal direction '%s'",
        .arg0 = cardinal
    );
}

cerr input_motion_set_run_circle(float radius)
{
    motion_state = (typeof(motion_state)) {
        .mode   = MOTION_RUN_CIRCLE,
        .radius = radius,
        .angle  = 0.0,
    };
    return CERR_OK;
}

cerr input_motion_set_jump(void)
{
    motion_state.jump = true;
    return CERR_OK;
}

void input_motion_step(struct clap_context *ctx)
{
    if (motion_state.mode == MOTION_NONE || clap_is_paused(ctx))
        return;

    struct message_input mi;
    memset(&mi, 0, sizeof(mi));

    switch (motion_state.mode) {
    case MOTION_RUN_TO:
        mi.delta_lx = motion_state.dx;
        mi.delta_ly = motion_state.dy;
        break;

    case MOTION_RUN_CIRCLE: {
        struct scene *scene = clap_get_scene(ctx);
        if (!scene)
            return;

        double dt = clap_get_fps_delta(ctx).tv_nsec / (double)NSEC_PER_SEC;
        double omega = scene->lin_speed / fabs(motion_state.radius);

        if (motion_state.radius > 0)
            motion_state.angle -= omega * dt;
        else
            motion_state.angle += omega * dt;

        mi.delta_lx = cos(motion_state.angle);
        mi.delta_ly = sin(motion_state.angle);
        break;
    }

    default:
        return;
    }

    if (motion_state.jump)  mi.space = 1;
    message_input_send(ctx, &mi, &motion_source);
}
