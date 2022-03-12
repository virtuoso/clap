#ifndef __CLAP_PNGLOADER_H__
#define __CLAP_PNGLOADER_H__

#if defined(CONFIG_BROWSER) && defined(USE_PRELOAD_PLUGINS)
#error "This is broken"
#include <limits.h>
static inline unsigned char *fetch_png(const char *name, int *width, int *height)
{
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/asset/%s", name);
    //emscripten_run_preload_plugins(path, NULL, NULL);
    return (unsigned char *)emscripten_get_preloaded_image_data(path, width, height);
}
#else
unsigned char *fetch_png(const char *file_name, int *width, int *height, int *has_alpha);
unsigned char *decode_png(void *buf, size_t length, int *width, int *height, int *has_alpha);
#endif

#endif /* __CLAP_PNGLOADER_H__ */
