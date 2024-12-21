// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "settings.h"
#include "ui-debug.h"

#ifdef __EMSCRIPTEN__
#include "ui-imgui-www.h"
#else
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#endif

#include "imgui_impl_opengl3.h"

static struct settings *settings;
static struct ImGuiContext *ctx;
static struct ImGuiIO *io;

void imgui_set_settings(struct settings *rs)
{
    settings = rs;

    JsonNode *debug_group = settings_find_get(rs, NULL, "debug", JSON_OBJECT);
    if (!debug_group)
        return;

    const char *ini = settings_get_str(settings, debug_group, "imgui_config");
    if (!ini)
        return;

    igLoadIniSettingsFromMemory(ini, strlen(ini));
}

void imgui_render_begin(int width, int height)
{
    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

    ImGui_ImplOpenGL3_NewFrame();
#ifdef __EMSCRIPTEN__
    ui_ig_new_frame();
#else
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

    if (io->WantSaveIniSettings && settings) {
        JsonNode *debug_group = settings_find_get(settings, NULL, "debug", JSON_OBJECT);
        if (!debug_group)
            return;

        settings_set_string(settings, debug_group, "imgui_config", igSaveIniSettingsToMemory(NULL));
        io->WantSaveIniSettings = false;
    }
}

void imgui_init(struct clap_context *clap_ctx, void *data, int width, int height)
{
    ctx = igCreateContext(NULL);
    io = igGetIO();

    io->IniFilename = NULL;
    io->LogFilename = NULL;
    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

#ifndef __EMSCRIPTEN__
    GLFWwindow *win = data;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    const char *glsl_version = "#version 410";
#else
    ui_ig_init_for_emscripten(clap_ctx);
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

bool ui_igVecTableHeader(const char *str_id, int n)
{
    if (n > 4)
        return false;

    if (!igBeginTable(str_id, n + 1, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0))
        return false;

    const char *labels[] = { "X", "Y", "Z", "W" };
    igTableSetupColumn("vector", ImGuiTableColumnFlags_WidthStretch, 0, 0);

    int i;
    for (i = 0; i < n; i++)
        igTableSetupColumn(labels[i], ImGuiTableColumnFlags_WidthFixed, 0, 0);

    return true;
}

void ui_igVecRow(float *v, int n, const char *fmt, ...)
{
    char buf[128];
    va_list va;

    if (n > 4)
        return;

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    igTableNextRow(0, 0);
    igTableNextColumn();
    igText(buf);

    int i;
    for (i = 0; i < n; i++) {
        igTableNextColumn();
        igText("%f", v[i]);
    }
}

bool ui_igMat4x4(mat4x4 m, const char *name)
{
    if (!igBeginTable(name, 4, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0))
        return false;

    igTableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 0, 0);

    int i, j;
    for (i = 0; i < 4; i++) {
        igTableNextRow(0, 0);
        for (j = 0; j < 4; j++) {
            igTableNextColumn();
            igText("%f", m[i][j]);
        }
    }
    igEndTable();
    return true;
}
