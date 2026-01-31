/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_JOYSTICK_H__
#define __CLAP_INPUT_JOYSTICK_H__

#define NR_JOYS 16

struct clap_context;

void joystick_name_update(int joy, const char *name);
void joystick_axes_update(int joy, const double *axes, int nr_axes);
void joystick_faxes_update(int joy, const float *axes, int nr_axes);
void joystick_buttons_update(int joy, const unsigned char *buttons, int nr_buttons);
void joystick_abuttons_update(int joy, const double *abtns, int nr_buttons);
void joysticks_poll(struct clap_context *ctx);

/*
 * Standard gamepad mapping (based on GLFW/Xbox 360)
 */
#define CLAP_JOY_AXIS_LX        0
#define CLAP_JOY_AXIS_LY        1
#define CLAP_JOY_AXIS_RX        2
#define CLAP_JOY_AXIS_RY        3
#define CLAP_JOY_AXIS_LT        4
#define CLAP_JOY_AXIS_RT        5

#define CLAP_JOY_BTN_A          0
#define CLAP_JOY_BTN_B          1
#define CLAP_JOY_BTN_X          2
#define CLAP_JOY_BTN_Y          3
#define CLAP_JOY_BTN_LB         4
#define CLAP_JOY_BTN_RB         5
#define CLAP_JOY_BTN_BACK       6
#define CLAP_JOY_BTN_START      7
#define CLAP_JOY_BTN_GUIDE      8
#define CLAP_JOY_BTN_LTHUMB     9
#define CLAP_JOY_BTN_RTHUMB     10
#define CLAP_JOY_BTN_DPAD_UP    11
#define CLAP_JOY_BTN_DPAD_RIGHT 12
#define CLAP_JOY_BTN_DPAD_DOWN  13
#define CLAP_JOY_BTN_DPAD_LEFT  14

/* Virtual buttons for triggers (mainly for browser support) */
#define CLAP_JOY_BTN_LT         15
#define CLAP_JOY_BTN_RT         16

#define CLAP_JOY_AXIS_COUNT     6
#define CLAP_JOY_BTN_COUNT      17

#endif /* __CLAP_INPUT_JOYSTICK_H__ */
