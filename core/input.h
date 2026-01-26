/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_INPUT_H__
#define __CLAP_INPUT_H__

#include "messagebus.h"

int platform_input_init(struct clap_context *ctx);
int message_input_send(struct clap_context *ctx, struct message_input *mi, struct message_source *src);

int input_init(struct clap_context *ctx);
void fuzzer_input_step(struct clap_context *ctx);
void fuzzer_input_init(struct clap_context *ctx);

#ifndef CONFIG_FINAL
void controllers_debug(void);
#else
static inline void controllers_debug(void) {}
#endif /* CONFIG_FINAL */

#if !defined(CONFIG_FINAL) && defined(CONFIG_BROWSER)
void input_debug(void);
#else
static inline void input_debug(void) {}
#endif /* !CONFIG_FINAL && CONFIG_BROWSER */

#ifdef CONFIG_BROWSER
extern void touch_input_set_size(int, int);
extern void input_events_dispatch(void);
#else
static inline void touch_input_set_size(int w, int h) {}
static inline void input_events_dispatch(void) {}
#endif

#endif /* __CLAP_INPUT_H__ */
