/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_BUILDER_H__
#define __CLAP_PIPELINE_BUILDER_H__

#include "pipeline.h"

typedef struct pipeline_builder_opts {
    pipeline_init_options   *pl_opts;
    struct mq               *mq;
} pipeline_builder_opts;

pipeline *pipeline_build(pipeline_builder_opts *opts);

#endif /* __CLAP_PIPELINE_BUILDER_H__ */
