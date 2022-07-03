// SPDX-License-Identifier: Apache-2.0
#include <GLFW/glfw3.h>
#include "messagebus.h"
#include "input.h"
#include "input-keyboard.h"
#include "logger.h"

struct key_map {
    const char  *name;
    int         key;
    int         offset;
    unsigned int (*setter)(unsigned int press);
};

static inline unsigned int is_release(unsigned int press)
{
    return press == KEY_RELEASE;
}

static inline unsigned int is_press(unsigned int press)
{
    return press == KEY_PRESS;
}

static inline unsigned int is_press_hold(unsigned int press)
{
    return press == KEY_PRESS || press == KEY_HOLD;
}

static inline unsigned int to_press_release(unsigned int press)
{
    if (is_press_hold(press))
        return 1;
    if (is_release(press))
        return 2;
    return 0;
}

#define KEY_NAME(_name, _setter, _field) \
    { .name = _name, .setter = _setter, .offset = offsetof(struct message_input, _field) }
#define KEY_VAL(_key, _setter, _field) \
    { .key = _key, .setter = _setter, .offset = offsetof(struct message_input, _field) }

#ifdef CONFIG_BROWSER
static struct key_map key_map_wasd[] = {
    KEY_NAME("KeyA",       to_press_release, left),
    KEY_NAME("KeyD",       to_press_release, right),
    KEY_NAME("KeyW",       to_press_release, up),
    KEY_NAME("KeyS",       to_press_release, down),
    KEY_NAME("ShiftLeft",  is_press,         dash),
    KEY_NAME("ArrowUp",    to_press_release, pitch_up),
    KEY_NAME("ArrowDown",  to_press_release, pitch_down),
    KEY_NAME("ArrowLeft",  to_press_release, yaw_left),
    KEY_NAME("ArrowRight", to_press_release, yaw_right),
    KEY_NAME("KeyQ",       is_press,         inv_toggle),
    KEY_NAME("KeyE",       is_press,         pad_y),
};
#else
static struct key_map key_map_wasd[] = {
    KEY_VAL(GLFW_KEY_A, to_press_release, left),
    KEY_VAL(GLFW_KEY_D, to_press_release, right),
    KEY_VAL(GLFW_KEY_W, to_press_release, up),
    KEY_VAL(GLFW_KEY_S, to_press_release, down),
    KEY_VAL(GLFW_KEY_LEFT_SHIFT, is_press,         dash),
    KEY_VAL(GLFW_KEY_UP,    to_press_release, pitch_up),
    KEY_VAL(GLFW_KEY_DOWN,  to_press_release, pitch_down),
    KEY_VAL(GLFW_KEY_LEFT,  to_press_release, yaw_left),
    KEY_VAL(GLFW_KEY_RIGHT, to_press_release, yaw_right),
    KEY_VAL(GLFW_KEY_Q,     is_press,         inv_toggle),
    KEY_VAL(GLFW_KEY_E,     is_press,         pad_y),
    KEY_VAL(GLFW_KEY_P,     is_press,         debug_action),
};
#endif

static struct key_map *key_map = key_map_wasd; 
static size_t key_map_nr = array_size(key_map_wasd);

void key_event(struct message_source *src, unsigned int key_code, const char *key,
               unsigned int mods, unsigned int press)
{
    struct message_input mi;
    int i;

    for (i = 0; i < key_map_nr; i++)
        if (key) {
            if (!strcmp(key, key_map[i].name))
                goto found;
        } else if (key_code == key_map[i].key) {
            goto found;
        }
    return;
found:
    memset(&mi, 0, sizeof(mi));

    unsigned char *val = (void *)&mi + key_map[i].offset;
    *val = key_map[i].setter(press);
    message_input_send(&mi, src);
}

