/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_H__
#define __CLAP_PIPELINE_H__

struct pipeline {
    // darray(struct render_pass, pass);
    struct scene        *scene;
    struct ref          ref;
    struct list         passes; 
};

struct pipeline *pipeline_new(struct scene *s);
void pipeline_add_pass(struct pipeline *pl, const char *prog_name);
void pipeline_render(struct pipeline *pl);

#endif /* __CLAP_PIPELINE_H__ */
