/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_H__
#define __CLAP_PIPELINE_H__

struct render_pass;
struct pipeline;

struct pipeline *pipeline_new(struct scene *s, const char *name);
void pipeline_put(struct pipeline *pl);
void pipeline_resize(struct pipeline *pl);
void pipeline_shadow_resize(struct pipeline *pl, int width);
void pipeline_set_resize_cb(struct pipeline *pl, void (*cb)(struct fbo *, bool, int, int));
struct render_pass *pipeline_add_pass(struct pipeline *pl, struct render_pass *src, const char *shader,
                                      const char *shader_override, bool ms, int nr_targets, int target);
void pipeline_pass_set_name(struct render_pass *pass, const char *name);
void pipeline_pass_add_source(struct pipeline *pl, struct render_pass *pass, int to, struct render_pass *src, int blit_src);
void pipeline_pass_repeat(struct render_pass *pass, struct render_pass *repeat, int count);
void pipeline_render(struct pipeline *pl);
texture_t *pipeline_pass_get_texture(struct render_pass *pass, unsigned int idx);
#ifndef CONFIG_FINAL
void pipeline_debug(struct pipeline *pl);
#else
static inline void pipeline_debug(struct pipeline *pl) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_PIPELINE_H__ */
