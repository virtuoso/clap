// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <string.h>
#include "common.h"
#include "messagebus.h"
#include "input.h"

int message_input_send(struct message_input *mi, struct message_source *src)
{
    struct message m = {
        .type   = MT_INPUT,
        .source = src,
    };

    memcpy(&m.input, mi, sizeof(m.input));
    message_send(&m);

    return true;
}

int input_init(void)
{
    fuzzer_input_init();
    return platform_input_init();
}
