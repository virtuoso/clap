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

void key_event(struct message_source *src, unsigned int key_code, const char *key,
               unsigned int mods, unsigned int press);

#endif /* __CLAP_INPUT_KEYBOARD_H__ */