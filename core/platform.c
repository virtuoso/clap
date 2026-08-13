// SPDX-License-Identifier: Apache-2.0
#include "platform.h"

static const clap_platform *platform;

void platform_register(const clap_platform *p)
{
    platform = p;
}

const clap_platform *platform_get(void)
{
    return platform;
}
