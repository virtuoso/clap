#include "common.h"
#include "ui-debug.h"

static struct backend_data {
    struct clap_context *ctx;
    double time;
    struct ImGuiContext *igctx;
    struct ImGuiIO *io;
} bd;

bool __ui_set_mouse_position(unsigned int x, unsigned int y)
{
    ImGuiIO_AddMousePosEvent(bd.io, x, y);

    return false;
}

bool __ui_set_mouse_click(unsigned int button, bool down)
{
    ImGuiIO_AddMouseButtonEvent(bd.io, button, down);

    return false;
}

void ui_ig_new_frame(void)
{
    struct timespec delta = clap_get_fps_delta(bd.ctx);

    double dt = delta.tv_sec + (delta.tv_nsec / (double)NSEC_PER_SEC);
    bd.io->DeltaTime = dt;
}

void ui_ig_init_for_emscripten(struct clap_context *clap_ctx, struct ImGuiContext *igctx,
                               struct ImGuiIO *io)
{
    bd.ctx = clap_ctx;
    bd.igctx = igctx;
    bd.io = io;
}
