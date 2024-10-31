#include "common.h"
#include "display.h"
#include "ui-debug.h"

static struct backend_data {
    struct clap_context *ctx;
    double time;
} bd;

bool __ui_mouse_event_propagate(void)
{
    struct ImGuiIO *io = igGetIO();
    return io->WantCaptureMouse;
}

bool __ui_set_mouse_position(unsigned int x, unsigned int y)
{
    struct ImGuiIO *io = igGetIO();

    ImGuiIO_AddMousePosEvent(io, x, y);

    return false;
}

bool __ui_set_mouse_click(unsigned int button, bool down)
{
    struct ImGuiIO *io = igGetIO();

    ImGuiIO_AddMouseButtonEvent(io, button, down);

    return false;
}

void ui_ig_new_frame(void)
{
    struct ImGuiIO *io = igGetIO();
    double time = clap_get_current_time(bd.ctx);
    io->DeltaTime = bd.time > 0.0 ? time - bd.time : 1.0 / gl_refresh_rate();
    bd.time = time;
}

void ui_ig_init_for_emscripten(struct clap_context *ctx)
{
    bd.ctx = ctx;
}
