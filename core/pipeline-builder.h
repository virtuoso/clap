/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_BUILDER_H__
#define __CLAP_PIPELINE_BUILDER_H__

#include "pipeline.h"

typedef struct pipeline_builder_opts {
    pipeline_init_options   *pl_opts;
    struct mq               *mq;
    pipeline                *pl;
} pipeline_builder_opts;

cresp(pipeline) pipeline_build(pipeline_builder_opts *opts);

/**
 * pipeline_builder_done() - release the builder's process-wide resources
 *
 * Drops the film grain blue noise texture and the SSAO state that
 * pipeline_build() keeps at file scope. Called from clap_done().
 */
void pipeline_builder_done(void);

#endif /* __CLAP_PIPELINE_BUILDER_H__ */
