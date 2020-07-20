#ifndef __CLAP_DISPLAY_H__
#define __CLAP_DISPLAY_H__

#ifdef CONFIG_BROWSER
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#endif

typedef void (*display_update)(void *data);
typedef void (*display_resize)(int w, int h);
void gl_init(const char *title, int width, int height, display_update update_fn, void *update_fn_data, display_resize resize_fn);
void gl_main_loop(void);
void gl_done(void);
void gl_swap_buffers(void);
void gl_get_sizes(int *widthp, int *heightp);
void gl_title(const char *fmt, ...);
void gl_request_exit(void);
void gl_resize(int w, int h);
void gl_enter_fullscreen(void);
void gl_leave_fullscreen(void);

#endif /* __CLAP_DISPLAY_H__ */