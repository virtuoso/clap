/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_H__
#define __CLAP_PIPELINE_H__

struct render_pass;
struct pipeline;

typedef struct pipeline_pass_config {
    struct render_pass  *source;
    const char          *shader;
    const char          *shader_override;
    const char          *name;
    texture_format      *color_format;
    float               scale;
    int                 nr_attachments;
    int                 blit_from;
    int                 pingpong;
    int                 cascade;
    bool                multisampled;
    bool                stop;
} pipeline_pass_config;

DEFINE_REFCLASS_INIT_OPTIONS(pipeline,
    const char      *name;
    struct scene    *scene;
);
DECLARE_REFCLASS(pipeline);

void pipeline_resize(struct pipeline *pl);
void pipeline_shadow_resize(struct pipeline *pl, int width);
void pipeline_set_resize_cb(struct pipeline *pl, bool (*cb)(fbo_t *, bool, int, int));
struct render_pass *_pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg);
#define pipeline_add_pass(_pl, args...) \
    _pipeline_add_pass((_pl), &(pipeline_pass_config){ args })
void pipeline_pass_set_name(struct render_pass *pass, const char *name);
void pipeline_pass_add_source(struct pipeline *pl, struct render_pass *pass, int to,
                              struct render_pass *src, int blit_src, texture_format color_format);
void pipeline_pass_repeat(struct render_pass *pass, struct render_pass *repeat, int count);
void pipeline_render(struct pipeline *pl, bool stop);
texture_t *pipeline_pass_get_texture(struct render_pass *pass, unsigned int idx);
#ifndef CONFIG_FINAL
void pipeline_debug(struct pipeline *pl);
#else
static inline void pipeline_debug(struct pipeline *pl) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_PIPELINE_H__ */
