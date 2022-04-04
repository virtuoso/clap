// SPDX-License-Identifier: Apache-2.0
#include <GLFW/glfw3.h>
#include "messagebus.h"
#include "input.h"
#include "logger.h"

enum {
    LEFT = 0,
    RIGHT,
    UP,
    DOWN,
    DASH,
    PITCH_UP,
    PITCH_DOWN,
    YAW_LEFT,
    YAW_RIGHT,
    INVENTORY,
    USE,
};

struct key_map {
    const char  *name;
    int         key;
    int         map_to;
};

#ifdef CONFIG_BROWSER
static struct key_map key_map_wasd[] = {
    { .name = "KeyA",       .map_to = LEFT },
    { .name = "KeyD",       .map_to = RIGHT },
    { .name = "KeyW",       .map_to = UP },
    { .name = "KeyS",       .map_to = DOWN },
    { .name = "ShiftLeft",  .map_to = DASH },
    { .name = "ArrowUp",    .map_to = PITCH_UP },
    { .name = "ArrowDown",  .map_to = PITCH_DOWN },
    { .name = "ArrowLeft",  .map_to = YAW_LEFT },
    { .name = "ArrowRight", .map_to = YAW_RIGHT },
    { .name = "KeyQ",       .map_to = INVENTORY },
    { .name = "KeyE",       .map_to = USE },
};
#else
static struct key_map key_map_wasd[] = {
    { .key = GLFW_KEY_A, .map_to = LEFT },
    { .key = GLFW_KEY_D, .map_to = RIGHT },
    { .key = GLFW_KEY_W, .map_to = UP },
    { .key = GLFW_KEY_S, .map_to = DOWN },
    { .key = GLFW_KEY_LEFT_SHIFT, .map_to = DASH },
    { .key = GLFW_KEY_UP,    .map_to = PITCH_UP },
    { .key = GLFW_KEY_DOWN,  .map_to = PITCH_DOWN },
    { .key = GLFW_KEY_LEFT,  .map_to = YAW_LEFT },
    { .key = GLFW_KEY_RIGHT, .map_to = YAW_RIGHT },
    { .key = GLFW_KEY_Q, .map_to = INVENTORY },
    { .key = GLFW_KEY_E, .map_to = USE },
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

    switch (key_map[i].map_to) {
    case RIGHT:
        mi.right = press;
        break;
    case LEFT:
        mi.left = press;
        break;
    case DOWN:
        mi.down = press;
        break;
    case UP:
        mi.up = press;
        break;
    case DASH:
        mi.dash = press == 1;
        break;
    case YAW_RIGHT:
        mi.yaw_right = press;
        break;
    case YAW_LEFT:
        mi.yaw_left = press;
        break;
    case PITCH_DOWN:
        mi.pitch_down = press;
        break;
    case PITCH_UP:
        mi.pitch_up = press;
        break;
    case INVENTORY:
        if (press == 1)
            mi.inv_toggle = 1;
        break;
    case USE:
        if (press == 1)
            mi.pad_y = 1;
        break;
    default:
        return;
    }
    message_input_send(&mi, src);
}

