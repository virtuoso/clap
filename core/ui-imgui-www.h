#ifndef __CLAP_UI_IMGUI_WWW__
#define __CLAP_UI_IMGUI_WWW__

bool __ui_set_mouse_click(unsigned int button, bool down);
bool __ui_mouse_event_propagate(void);
bool __ui_set_mouse_position(unsigned int x, unsigned int y);

void ui_ig_new_frame(void);
void ui_ig_init_for_emscripten(struct clap_context *ctx);

#endif /* __CLAP_UI_IMGUI_WWW__ */
