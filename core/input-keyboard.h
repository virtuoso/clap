/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_KEYBOARD_H__
#define __CLAP_INPUT_KEYBOARD_H__

#include "messagebus.h"

enum {
    KEY_NONE = 0,
    KEY_PRESS,
    KEY_HOLD,
    KEY_RELEASE,
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

/**
 * to_press_release() - Encode a key event for a message_input field
 * @press:  KEY_PRESS, KEY_HOLD or KEY_RELEASE
 *
 * Return: 1 on press or hold, 2 on release, 0 otherwise
 */
static inline unsigned int to_press_release(unsigned int press)
{
    if (is_press_hold(press))   return 1;
    if (is_release(press))      return 2;
    return 0;
}

void key_event(struct clap_context *ctx, struct message_source *src, unsigned int key_code, const char *key,
               unsigned int mods, unsigned int press);

#endif /* __CLAP_INPUT_KEYBOARD_H__ */