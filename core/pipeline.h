/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_H__
#define __CLAP_PIPELINE_H__

struct render_pass;

struct pipeline {
    // darray(struct render_pass, pass);
    struct scene        *scene;
    struct ref          ref;
    struct list         passes; 
};

struct pipeline *pipeline_new(struct scene *s);
struct render_pass *pipeline_add_pass(struct pipeline *pl, struct render_pass *src, const char *prog_name,
                                      bool ms, int nr_targets, int target);
void pipeline_pass_add_source(struct pipeline *pl, struct render_pass *pass, int to, struct render_pass *src, int blit_src);
void pipeline_pass_repeat(struct render_pass *pass, struct render_pass *repeat, int count);
void pipeline_render(struct pipeline *pl);

#endif /* __CLAP_PIPELINE_H__ */
