#ifndef __CLAP_INPUT_H__
#define __CLAP_INPUT_H__

#include "messagebus.h"

int platform_input_init(void);
int message_input_send(struct message_input *mi, struct message_source *src);

int input_init(void);

#endif /* __CLAP_INPUT_H__ */
