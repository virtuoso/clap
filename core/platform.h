/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PLATFORM_H__
#define __CLAP_PLATFORM_H__

#include "config.h"

typedef struct render_options render_options;

/**
 * struct clap_platform - per-platform performance/capability constraints
 * @name:                   platform name, as in CLAP_PLATFORM=<name>
 * @init_render_options:    render_options overrides applied on top of the
 *                          engine defaults, at the tail of
 *                          clap_init_render_options()
 * @shadow_passes_max:      upper bound for clap_reserve_shadow_passes();
 *                          0 means CASCADES_MAX
 *
 * Provided by platforms/<name>/core.c when the build carries one
 * (CLAP_PLATFORM=<name> in cmake); generic builds register nothing.
 *
 * Ordering: engine defaults -> platform overrides -> the game's own
 * graphics init. The platform owns the perf/capability knobs, the game
 * owns the look; fields the game sets win over the platform.
 */
typedef struct clap_platform {
    const char      *name;
    void            (*init_render_options)(render_options *ropts);
    unsigned int    shadow_passes_max;
} clap_platform;

/**
 * platform_register() - make the platform's constraints available to the engine
 * @platform:   the platform descriptor, which has to outlive the engine
 */
void platform_register(const clap_platform *platform);

/**
 * platform_get() - the registered platform descriptor
 *
 * Return: the platform's constraints, or NULL in a generic build.
 */
const clap_platform *platform_get(void);

/**
 * platform_init() - the platform's entry point, called once from clap_init()
 *
 * Defined in platforms/<CONFIG_PLATFORM>/core.c, calls
 * platform_register(). An explicit call rather than a constructor: the
 * platform object sits in a static library and the linker drops
 * unreferenced archive members, constructors included.
 */
#ifdef CONFIG_PLATFORM
void platform_init(void);
#else
static inline void platform_init(void) {}
#endif /* !CONFIG_PLATFORM */

#endif /* __CLAP_PLATFORM_H__ */
