/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MESSAGEBUS_H__
#define __CLAP_MESSAGEBUS_H__

#include <time.h>
#include <stdint.h>
#include "error.h"
#include "linmath.h"
#include "util.h"

enum message_result {
    MSG_HANDLED = 0,
    MSG_STOP = -1,
};

enum message_type {
    MT_RENDER   = 0,
    MT_INPUT,
    MT_COMMAND,
    MT_LOG,
    MT_DEBUG_DRAW,
    /*---*/
    MT_MAX,
};

enum message_source_type {
    MST_KEYBOARD, /* XXX: not really */
    MST_CLIENT,
    MST_SERVER,
    MST_FUZZER,
};

struct message_input {
    unsigned char   left;
    unsigned char   right;
    unsigned char   down;
    unsigned char   up;
    unsigned char   pad_a;
    unsigned char   pad_b;
    unsigned char   pad_x;
    unsigned char   pad_y;
    unsigned char   stick_l;
    unsigned char   stick_r;
    unsigned char   pad_lb;
    unsigned char   pad_rb;
    unsigned char   pad_lt;
    unsigned char   pad_rt;
    unsigned char   pad_min;
    unsigned char   pad_plus;
    unsigned char   pad_home;
    unsigned char   tab;
    unsigned char   enter;
    unsigned char   space;
    unsigned char   back;
    unsigned char   zoom;
    unsigned char   pitch_up;
    unsigned char   pitch_down;
    unsigned char   yaw_left;
    unsigned char   yaw_right;
    unsigned char   focus_next;
    unsigned char   focus_prev;
    unsigned char   focus_cancel;
    unsigned char   verboser;
    unsigned char   fullscreen;
    unsigned char   resize;
    unsigned char   volume_up;
    unsigned char   volume_down;
    unsigned char   menu_toggle;
    unsigned char   inv_toggle;
    unsigned char   mouse_move;
    unsigned char   mouse_click;
    unsigned char   exit;
    unsigned char   dash;
    unsigned char   debug_action;
    unsigned char   keyboard;
    unsigned char   reserved0[3];
    float           delta_lx;
    float           delta_ly;
    float           delta_rx;
    float           delta_ry;
    float           trigger_l;
    float           trigger_r;
    unsigned int    x, y;
};

struct message_command {
    unsigned int    toggle_modality : 1,
                    global_exit : 1,
                    status      : 1,
                    connect     : 1,
                    restart     : 1,
                    log_follows : 1,
                    toggle_fuzzer : 1,
                    toggle_noise: 1,
                    sound_ready : 1,
                    reserved0   : 21;
    unsigned int    fps, sys_seconds, world_seconds;
    struct timespec64 time;
};

struct message_log {
    struct timespec64   ts;
    unsigned int        length;
    char                msg[0];
};

typedef enum debug_draw_shape {
    DEBUG_DRAW_LINE,
    DEBUG_DRAW_AABB,
    DEBUG_DRAW_CIRCLE,
    DEBUG_DRAW_TEXT,
    DEBUG_DRAW_DISC,
    DEBUG_DRAW_GRID,
} debug_draw_shape;

struct message_debug_draw {
    vec3                v0;
    vec3                v1;
    debug_draw_shape    shape;
    vec4                color;
    float               thickness;
    union {
        unsigned int    cell;
        float           radius;
        char            *text;
    };
};

struct message_source {
    char                        *name;
    const char                  *desc;
    enum message_source_type    type;
};

struct message {
    struct message_source       *source; /* for input: keyboard, joystick */
    enum message_type           type;

    union {
        struct message_input        input;
        struct message_command      cmd;
        struct message_log          log;
        struct message_debug_draw   debug_draw;
    };
};

typedef int (*subscriber_fn)(struct message *m, void *data);

struct subscriber {
    subscriber_fn       handle;
    void                *data;
    struct list         entry;
};

cerr subscribe(enum message_type type, subscriber_fn fn, void *data);
int message_send(struct message *m);
cerr_check messagebus_init(void);
void messagebus_done(void);

#endif /* __CLAP_MESSAGEBUS_H__ */
