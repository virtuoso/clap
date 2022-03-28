/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MESSAGEBUS_H__
#define __CLAP_MESSAGEBUS_H__

#include <time.h>
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
    unsigned long   left        : 2, /* 0 */
                    right       : 2,
                    down        : 2,
                    up          : 2, // for the arrow keys, 1 means "press", 2 means "release".
                    pad_a       : 1,
                    pad_b       : 1,
                    pad_x       : 1, /* 10 */
                    pad_y       : 1,
                    stick_l     : 1,
                    stick_r     : 1,
                    pad_lb      : 1,
                    pad_rb      : 1,
                    pad_lt      : 1,
                    pad_rt      : 1,
                    pad_min     : 1,
                    pad_plus    : 1,
                    pad_home    : 1,
                    tab         : 1, /* 20 */
                    enter       : 1,
                    space       : 1,
                    back        : 1,
                    zoom        : 1,
                    pitch_up    : 2,
                    pitch_down  : 2,
                    yaw_left    : 2, /* 30 */
                    yaw_right   : 2,
                    focus_next  : 1,
                    focus_prev  : 1,
                    focus_cancel: 1,
                    verboser    : 1,
                    autopilot   : 1,
                    fullscreen  : 1,
                    resize      : 1, /* 40 */
                    volume_up   : 1,
                    volume_down : 1,
                    menu_toggle : 1,
                    mouse_move  : 1,
                    mouse_click : 1, /* 45 */
                    exit        : 1; /* 46 */
    float           delta_lx;
    float           delta_ly;
    float           delta_rx;
    float           delta_ry;
    float           trigger_l;
    float           trigger_r;
    unsigned int    x, y;
};

struct message_command {
    unsigned int    menu_enter  : 1,
                    menu_exit   : 1,
                    global_exit : 1,
                    status      : 1,
                    connect     : 1,
                    restart     : 1,
                    log_follows : 1,
                    toggle_fuzzer : 1,
                    toggle_autopilot : 1,
                    toggle_noise: 1;
    unsigned int    fps, sys_seconds, world_seconds;
    struct timespec64 time;
};

struct message_log {
    struct timespec64   ts;
    unsigned int        length;
    char                msg[0];
};

struct message_source {
    enum message_source_type    type;
    char                        *name;
    const char                  *desc;
};

struct message {
    enum message_type           type;
    struct message_source       *source; /* for input: keyboard, joystick */

    union {
        struct message_input    input;
        struct message_command  cmd;
        struct message_log      log;
    };
};

typedef int (*subscriber_fn)(struct message *m, void *data);

struct subscriber {
    subscriber_fn       handle;
    void                *data;
    struct subscriber   *next;
};

int subscribe(enum message_type type, subscriber_fn fn, void *data);
int message_send(struct message *m);
int messagebus_init(void);

#endif /* __CLAP_MESSAGEBUS_H__ */
