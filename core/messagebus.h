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
    unsigned char   autopilot;
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
                    toggle_modality : 1,
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
