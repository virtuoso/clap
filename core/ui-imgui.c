// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "ui-debug.h"

#ifndef __EMSCRIPTEN__
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#endif

#include "imgui_impl_opengl3.h"

static struct ImGuiContext *ctx;
static struct ImGuiIO *io;

void imgui_render_begin(int width, int height)
{
    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

    ImGui_ImplOpenGL3_NewFrame();
#ifndef __EMSCRIPTEN__
    ImGui_ImplGlfw_NewFrame();
#endif
    igNewFrame();

    // igSetNextWindowPos((struct ImVec2){0,0}, ImGuiCond_FirstUseEver,(struct ImVec2){0,0} );
    // igShowDemoWindow(NULL);
}

void imgui_render(void)
{
    igRender();
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
}

void imgui_init(void *data, int width, int height)
{
    ctx = igCreateContext(NULL);
    io = igGetIO();

    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

#ifndef __EMSCRIPTEN__
    GLFWwindow *win = data;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    const char *glsl_version = "#version 410";
#else
    const char *glsl_version = "#version 300 es";
#endif

    ImGui_ImplOpenGL3_Init(glsl_version);

    igStyleColorsDark(NULL);
}

void imgui_done(void)
{
    ImGui_ImplOpenGL3_Shutdown();
#ifndef __EMSCRIPTEN__
    ImGui_ImplGlfw_Shutdown();
#endif
    igDestroyContext(ctx);
}
