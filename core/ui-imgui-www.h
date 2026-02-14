#ifndef __CLAP_UI_IMGUI_WWW__
#define __CLAP_UI_IMGUI_WWW__

#include "clap.h"

struct ImGuiContext;
struct ImGuiIO;

#ifndef CONFIG_FINAL

bool __ui_set_mouse_click(unsigned int button, bool down);
bool __ui_set_mouse_position(unsigned int x, unsigned int y);
bool __ui_set_key(int key_code, const char *key, bool down);
bool __ui_mouse_event_wheel(double dx, double dy);

void ui_ig_new_frame(void);
void ui_ig_init_for_emscripten(struct clap_context *clap_ctx, struct ImGuiContext *igctx,
                               struct ImGuiIO *io);

#else

static inline bool __ui_set_mouse_click(unsigned int button, bool down) { return false; }
static inline bool __ui_set_mouse_position(unsigned int x, unsigned int y) { return false; }
static inline bool __ui_mouse_event_wheel(double dx, double dy) { return false; }
static inline bool __ui_set_key(int key_code, const char *key, bool down) { return false; }

static inline void ui_ig_new_frame(void) {}
static inline void ui_ig_init_for_emscripten(struct clap_context *ctx, struct ImGuiContext *igctx,
                                             struct ImGuiIO *io) {}

#endif /* CONFIG_FINAL */

#endif /* __CLAP_UI_IMGUI_WWW__ */
