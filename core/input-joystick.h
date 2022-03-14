/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_JOYSTICK_H__
#define __CLAP_INPUT_JOYSTICK_H__

#define NR_JOYS 16

void joystick_name_update(int joy, const char *name);
void joystick_axes_update(int joy, const double *axes, int nr_axes);
void joystick_faxes_update(int joy, const float *axes, int nr_axes);
void joystick_buttons_update(int joy, const char *buttons, int nr_buttons);
void joystick_abuttons_update(int joy, const double *abtns, int nr_buttons);
void joysticks_poll(void);

#endif /* __CLAP_INPUT_JOYSTICK_H__ */