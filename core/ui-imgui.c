// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "common.h"
#include "settings.h"
#include "memory.h"
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

bool __ui_mouse_event_propagate(void)
{
    struct ImGuiIO *io = igGetIO_ContextPtr(ctx);
    return io->WantCaptureMouse;
}

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

static void *imgui_alloc(size_t size, __unused void *user_data)
{
    return mem_alloc(size);
}

static void imgui_free(void *ptr, __unused void *user_data)
{
    mem_free(ptr);
}

void imgui_init(struct clap_context *clap_ctx, void *data, int width, int height)
{
    igSetAllocatorFunctions(imgui_alloc, imgui_free, NULL);
    ctx = igCreateContext(NULL);
    io = igGetIO_ContextPtr(ctx);

    io->IniFilename = NULL;
    io->LogFilename = NULL;
    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

    /* XXX: -> clap config callback */
    ImGuiStyle *style = igGetStyle();

    // Corners
    style->WindowRounding = 4.0f;
    style->ChildRounding = 4.0f;
    style->FrameRounding = 3.0f;
    style->PopupRounding = 3.0f;
    style->ScrollbarRounding = 3.0f;
    style->GrabRounding = 3.0f;
    style->TabRounding = 3.0f;

    // Colors
    style->Colors[ImGuiCol_Text]                   = (ImVec4){ 0.90f, 0.90f, 0.90f, 1.00f };
    style->Colors[ImGuiCol_TextDisabled]           = (ImVec4){ 0.60f, 0.60f, 0.60f, 1.00f };
    style->Colors[ImGuiCol_WindowBg]               = (ImVec4){ 0.07f, 0.02f, 0.02f, 0.85f };
    style->Colors[ImGuiCol_ChildBg]                = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_PopupBg]                = (ImVec4){ 0.14f, 0.11f, 0.11f, 0.92f };
    style->Colors[ImGuiCol_Border]                 = (ImVec4){ 0.50f, 0.50f, 0.50f, 0.50f };
    style->Colors[ImGuiCol_BorderShadow]           = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_FrameBg]                = (ImVec4){ 0.43f, 0.43f, 0.43f, 0.39f };
    style->Colors[ImGuiCol_FrameBgHovered]         = (ImVec4){ 0.70f, 0.41f, 0.41f, 0.40f };
    style->Colors[ImGuiCol_FrameBgActive]          = (ImVec4){ 0.75f, 0.48f, 0.48f, 0.69f };
    style->Colors[ImGuiCol_TitleBg]                = (ImVec4){ 0.48f, 0.18f, 0.18f, 0.80f };
    style->Colors[ImGuiCol_TitleBgActive]          = (ImVec4){ 0.52f, 0.12f, 0.12f, 0.90f };
    style->Colors[ImGuiCol_TitleBgCollapsed]       = (ImVec4){ 0.80f, 0.40f, 0.40f, 0.20f };
    style->Colors[ImGuiCol_MenuBarBg]              = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.80f };
    style->Colors[ImGuiCol_ScrollbarBg]            = (ImVec4){ 0.30f, 0.20f, 0.20f, 0.60f };
    style->Colors[ImGuiCol_ScrollbarGrab]          = (ImVec4){ 0.96f, 0.17f, 0.17f, 0.30f };
    style->Colors[ImGuiCol_ScrollbarGrabHovered]   = (ImVec4){ 1.00f, 0.07f, 0.07f, 0.40f };
    style->Colors[ImGuiCol_ScrollbarGrabActive]    = (ImVec4){ 1.00f, 0.36f, 0.36f, 0.60f };
    style->Colors[ImGuiCol_CheckMark]              = (ImVec4){ 0.90f, 0.90f, 0.90f, 0.50f };
    style->Colors[ImGuiCol_SliderGrab]             = (ImVec4){ 1.00f, 1.00f, 1.00f, 0.30f };
    style->Colors[ImGuiCol_SliderGrabActive]       = (ImVec4){ 0.80f, 0.39f, 0.39f, 0.60f };
    style->Colors[ImGuiCol_Button]                 = (ImVec4){ 0.71f, 0.18f, 0.18f, 0.62f };
    style->Colors[ImGuiCol_ButtonHovered]          = (ImVec4){ 0.71f, 0.27f, 0.27f, 0.79f };
    style->Colors[ImGuiCol_ButtonActive]           = (ImVec4){ 0.80f, 0.46f, 0.46f, 1.00f };
    style->Colors[ImGuiCol_Header]                 = (ImVec4){ 0.56f, 0.16f, 0.16f, 0.45f };
    style->Colors[ImGuiCol_HeaderHovered]          = (ImVec4){ 0.53f, 0.11f, 0.11f, 1.00f };
    style->Colors[ImGuiCol_HeaderActive]           = (ImVec4){ 0.87f, 0.53f, 0.53f, 0.80f };
    style->Colors[ImGuiCol_Separator]              = (ImVec4){ 0.50f, 0.50f, 0.50f, 0.60f };
    style->Colors[ImGuiCol_SeparatorHovered]       = (ImVec4){ 0.60f, 0.60f, 0.70f, 1.00f };
    style->Colors[ImGuiCol_SeparatorActive]        = (ImVec4){ 0.70f, 0.70f, 0.90f, 1.00f };
    style->Colors[ImGuiCol_ResizeGrip]             = (ImVec4){ 1.00f, 1.00f, 1.00f, 0.10f };
    style->Colors[ImGuiCol_ResizeGripHovered]      = (ImVec4){ 0.78f, 0.82f, 1.00f, 0.60f };
    style->Colors[ImGuiCol_ResizeGripActive]       = (ImVec4){ 0.78f, 0.82f, 1.00f, 0.90f };
    style->Colors[ImGuiCol_TabHovered]             = (ImVec4){ 0.68f, 0.21f, 0.21f, 0.80f };
    style->Colors[ImGuiCol_Tab]                    = (ImVec4){ 0.47f, 0.12f, 0.12f, 0.79f };
    style->Colors[ImGuiCol_TabSelected]            = (ImVec4){ 0.68f, 0.21f, 0.21f, 1.00f };
    style->Colors[ImGuiCol_TabSelectedOverline]    = (ImVec4){ 0.95f, 0.84f, 0.84f, 0.40f };
    style->Colors[ImGuiCol_TabDimmed]              = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.83f };
    style->Colors[ImGuiCol_TabDimmedSelected]      = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.83f };
    style->Colors[ImGuiCol_TabDimmedSelectedOverline] = (ImVec4){ 0.55f, 0.23f, 0.23f, 1.00f };
    // style->Colors[ImGuiCol_DockingPreview]         = (ImVec4){ 0.90f, 0.40f, 0.40f, 0.31f });
    // style->Colors[ImGuiCol_DockingEmptyBg]         = (ImVec4){ 0.20f, 0.20f, 0.20f, 1.00f });
    style->Colors[ImGuiCol_PlotLines]              = (ImVec4){ 1.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_PlotLinesHovered]       = (ImVec4){ 0.90f, 0.70f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogram]          = (ImVec4){ 0.90f, 0.70f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogramHovered]   = (ImVec4){ 1.00f, 0.60f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_TableHeaderBg]          = (ImVec4){ 0.56f, 0.16f, 0.16f, 0.45f };
    style->Colors[ImGuiCol_TableBorderStrong]      = (ImVec4){ 0.68f, 0.21f, 0.21f, 0.80f };
    style->Colors[ImGuiCol_TableBorderLight]       = (ImVec4){ 0.26f, 0.26f, 0.28f, 1.00f };
    style->Colors[ImGuiCol_TableRowBg]             = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_TableRowBgAlt]          = (ImVec4){ 1.00f, 1.00f, 1.00f, 0.07f };
    style->Colors[ImGuiCol_TextSelectedBg]         = (ImVec4){ 1.00f, 0.00f, 0.00f, 0.35f };
    style->Colors[ImGuiCol_DragDropTarget]         = (ImVec4){ 1.00f, 1.00f, 0.00f, 0.90f };
    // style->Colors[ImGuiCol_NavHighlight]           = (ImVec4){ 0.45f, 0.45f, 0.90f, 0.80f });
    style->Colors[ImGuiCol_NavWindowingHighlight]  = (ImVec4){ 1.00f, 1.00f, 1.00f, 0.70f };
    style->Colors[ImGuiCol_NavWindowingDimBg]      = (ImVec4){ 0.80f, 0.80f, 0.80f, 0.20f };
    style->Colors[ImGuiCol_ModalWindowDimBg]       = (ImVec4){ 0.20f, 0.20f, 0.20f, 0.35f };

#ifndef __EMSCRIPTEN__
    GLFWwindow *win = data;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    const char *glsl_version = "#version 410";
#else
    ui_ig_init_for_emscripten(clap_ctx, ctx, io);
    const char *glsl_version = "#version 300 es";
#endif

    ImGui_ImplOpenGL3_Init(glsl_version);
}

void imgui_done(void)
{
    ImGui_ImplOpenGL3_Shutdown();
#ifndef __EMSCRIPTEN__
    ImGui_ImplGlfw_Shutdown();
#endif
    igDestroyContext(ctx);
}

bool ui_igTableHeader(const char *str_id, const char **labels, int n)
{
    if (!igBeginTable(str_id, n + 1, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0))
        return false;

    igTableSetupColumn(str_id, ImGuiTableColumnFlags_WidthStretch, 0, 0);

    int i;
    for (i = 0; i < n; i++)
        igTableSetupColumn(labels[i], ImGuiTableColumnFlags_WidthFixed, 0, 0);

    return true;
}

bool ui_igVecTableHeader(const char *str_id, int n)
{
    if (n > 4)
        return false;

    const char *labels[] = { "X", "Y", "Z", "W" };

    return ui_igTableHeader(str_id, labels, n);
}

void ui_igTableRow(const char *key, const char *fmt, ...)
{
    igTableNextRow(0, 0);
    igTableNextColumn();
    igText(key);

    char buf[128];
    va_list va;

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    igTableNextColumn();
    igText(buf);
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
