/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_H__
#define __CLAP_INPUT_H__

#include "messagebus.h"

int platform_input_init(void);
int message_input_send(struct message_input *mi, struct message_source *src);

int input_init(void);
void fuzzer_input_step(void);
void fuzzer_input_init(void);

#ifdef CONFIG_BROWSER
extern void touch_input_set_size(int, int);
extern void input_events_dispatch(void);
#else
static inline void touch_input_set_size(int w, int h) {}
static inline void input_events_dispatch(void) {}
#endif

#endif /* __CLAP_INPUT_H__ */
