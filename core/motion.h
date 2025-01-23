/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MOTION_H__
#define __CLAP_MOTION_H__

#include <stdbool.h>

struct scene;

struct motionctl {
    float   ls_left;
    float   ls_right;
    float   ls_up;
    float   ls_down;
    float   ls_dx;
    float   ls_dy;
    float   rs_left;
    float   rs_right;
    float   rs_up;
    float   rs_down;
    float   rs_dx;
    float   rs_dy;
    bool    rs_height;
};

void motion_parse_input(struct motionctl *mctl, struct message *m);
void motion_compute(struct motionctl *mctl);
void motion_reset(struct motionctl *mctl, struct scene *s);

#endif /* __CLAP_MOTION_H__ */
