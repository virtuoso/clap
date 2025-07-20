/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_DEBUG_H__
#define __CLAP_PIPELINE_DEBUG_H__

#include "pipeline.h"
#include "render.h"

#ifndef CONFIG_FINAL
typedef struct pipeline_dropdown {
    char        name[128];
    texture_t   *tex;
    render_pass *pass;
} pipeline_dropdown;

void pipeline_debug_init(pipeline *pl);
void pipeline_debug_done(pipeline *pl);
void pipeline_dropdown_push(pipeline *pl, render_pass *pass);

void pipeline_debug_begin(struct pipeline *pl);
void pipeline_debug_end(struct pipeline *pl);
void pipeline_pass_debug_begin(struct pipeline *pl, struct render_pass *pass, int srcidx);
void pipeline_pass_debug_end(struct pipeline *pl, unsigned long count, unsigned long culled);

#define PIPELINE_DEBUG_DATA darray(pipeline_dropdown, dropdown)
#else
#define PIPELINE_DEBUG_DATA

static inline void pipeline_debug_init(pipeline *pl) {}
static inline void pipeline_debug_done(pipeline *pl) {}
static inline void pipeline_dropdown_push(pipeline *pl, render_pass *pass) {}
static inline void pipeline_debug_begin(struct pipeline *pl) {}
static inline void pipeline_debug_end(struct pipeline *pl) {}
static inline void pipeline_pass_debug_begin(struct pipeline *pl, struct render_pass *pass, int srcidx) {}
static inline void pipeline_pass_debug_end(struct pipeline *pl, unsigned long count, unsigned long culled) {}

#endif /* CONFIG_FINAL */

#endif /* __CLAP_PIPELINE_DEBUG_H__ */
