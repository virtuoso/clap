#include "common.h"
#include "ui-debug.h"

static struct backend_data {
    struct clap_context *ctx;
    double time;
} bd;

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
    struct timespec delta = clap_get_fps_delta(bd.ctx);

    double dt = delta.tv_sec + (delta.tv_nsec / (double)NSEC_PER_SEC);
    io->DeltaTime = dt;
}

void ui_ig_init_for_emscripten(struct clap_context *ctx)
{
    bd.ctx = ctx;
}
