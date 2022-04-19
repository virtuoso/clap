/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANICTL_H__
#define __CLAP_ANICTL_H__

#include <stdbool.h>

struct anictl {
    unsigned int    state;
    void            *object;
};

static inline bool anictl_set_state(struct anictl *anictl, unsigned int state)
{
    if (anictl->state == state)
        return false;
    
    anictl->state = state;
    return true;
}

#endif /* __CLAP_ANICTL_H__ */
