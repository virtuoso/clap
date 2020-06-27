#ifndef __CLAP_COMMON_H__
#define __CLAP_COMMON_H__

#include "config.h"
#ifdef CONFIG_BROWSER
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#define EM_ASM(x)
#endif
#include "util.h"
#include "logger.h"
#include "clap.h"

#endif /* __CLAP_COMMON_H__*/
