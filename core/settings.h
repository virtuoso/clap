/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_H_SETTINGS_H__
#define __CLAP_H_SETTINGS_H__

#include "json.h"

struct settings;
struct settings *settings_init(void *cb, void *data);
void settings_done(struct settings *settings);
void settings_set(struct settings *settings, const char *key, JsonNode *node);
void settings_set_num(struct settings *settings, const char *key, double num);
void settings_set_string(struct settings *settings, const char *key, const char *str);
JsonNode *settings_get(struct settings *settings, const char *key);
double settings_get_num(struct settings *settings, const char *key);
const char *settings_get_str(struct settings *settings, const char *key);

#endif /* __CLAP_H_SETTINGS_H__ */
