#ifndef __CLAP_MESSAGEBUS_H__
#define __CLAP_MESSAGEBUS_H__

enum message_type {
    MT_RENDER   = 0,
    MT_INPUT,
    MT_COMMAND,
    /*---*/
    MT_MAX,
};

enum message_source_type {
    MST_KEYBOARD, /* XXX: not really */
};

struct message_input {
    unsigned int    left        : 1,
                    right       : 1,
                    down        : 1,
                    up          : 1,
                    zoom        : 1,
                    pitch_up    : 1,
                    pitch_down  : 1,
                    yaw_left    : 1,
                    yaw_right   : 1,
                    focus_next  : 1,
                    focus_prev  : 1,
                    focus_cancel: 1,
                    verboser    : 1,
                    autopilot   : 1,
                    fullscreen  : 1,
                    resize      : 1,
                    exit        : 1;
    float           delta_x;
    float           delta_y;
    float           delta_z;
    unsigned int    x, y;
};

struct message_command {
    unsigned int    menu_enter  : 1,
                    menu_exit   : 1,
                    global_exit : 1,
                    toggle_noise: 1;
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