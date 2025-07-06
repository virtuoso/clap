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

#ifdef CONFIG_RENDERER_OPENGL
#include "imgui_impl_opengl3.h"
#elif defined(CONFIG_RENDERER_METAL)
#include "ui-imgui-metal.h"
#else
#error "Unsupported renderer"
#endif

static struct settings *settings;
static struct ImGuiContext *ctx;
static struct ImGuiIO *io;
static imgui_style imstyle;

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

    JsonNode *jimstyle = settings_find_get(rs, debug_group, "imgui_style", JSON_NUMBER);
    if (jimstyle && jimstyle->tag == JSON_NUMBER)
        imgui_set_style((enum imgui_style)jimstyle->tag);

    const char *ini = settings_get_str(settings, debug_group, "imgui_config");
    if (!ini)
        return;

    igLoadIniSettingsFromMemory(ini, strlen(ini));
}

static void debug_debugger(void)
{
    debug_module *dbgm = ui_igBegin_name(DEBUG_DEBUGGER, ImGuiWindowFlags_AlwaysAutoResize, "debugger");

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        static bool show_demo;

        igCheckbox("ImGui Demo Window", &show_demo);
        if (show_demo)
            igShowDemoWindow(NULL);
    }

    ui_igEnd(DEBUG_DEBUGGER);
}

void imgui_render_begin(int width, int height)
{
    io->DisplaySize.x = width;
    io->DisplaySize.y = height;

#ifdef CONFIG_RENDERER_OPENGL
    ImGui_ImplOpenGL3_NewFrame();
#elif defined(CONFIG_RENDERER_METAL)
    ui_imgui_metal_new_frame();
#endif

#ifdef __EMSCRIPTEN__
    ui_ig_new_frame();
#else
    ImGui_ImplGlfw_NewFrame();
#endif
    igNewFrame();
}

void imgui_render(void)
{
    debug_debugger();

    igRender();
#ifdef CONFIG_RENDERER_OPENGL
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
#elif defined(CONFIG_RENDERER_METAL)
    ui_imgui_metal_render_draw_data(igGetDrawData());
#endif

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

static void imgui_style_solarized(ImGuiStyle *style)
{
    style->Alpha = 1.0;
    style->ChildRounding = 3;
    style->WindowRounding = 3;
    style->GrabRounding = 1;
    style->GrabMinSize = 20;
    style->FrameRounding = 3;

    ImVec4 bg_dark          = { 0.00f, 0.17f, 0.21f, 1.00f };
    ImVec4 bg_light         = { 0.03f, 0.21f, 0.26f, 1.00f };
    ImVec4 fg_base          = { 0.61f, 0.68f, 0.69f, 1.00f };
    ImVec4 fg_dim           = { 0.40f, 0.47f, 0.48f, 1.00f };
    ImVec4 accent_blue      = { 0.12f, 0.42f, 0.65f, 1.00f };
    ImVec4 accent_cyan      = { 0.103f, 0.425f, 0.05f, 1.00f };
    ImVec4 accent_red       = { 0.86f, 0.20f, 0.18f, 1.00f };
    ImVec4 accent_green     = { 0.44f, 0.50f, 0.00f, 1.00f };
    ImVec4 grab             = { 0.03f, 0.21f, 0.26f, 0.74f };
    ImVec4 grab_hovered     = { 0.00f, 0.40f, 0.50f, 0.74f };
    ImVec4 grab_active      = { 0.00f, 0.40f, 0.50f, 1.00f };

    style->Colors[ImGuiCol_Text]            = fg_base;
    style->Colors[ImGuiCol_TextSelectedBg]  = accent_green;
    style->Colors[ImGuiCol_TextDisabled]    = fg_dim;
    style->Colors[ImGuiCol_WindowBg]         = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.70f };
    style->Colors[ImGuiCol_ChildBg]         = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_PopupBg]         = bg_light;
    style->Colors[ImGuiCol_Border]          = (ImVec4) {0.20f, 0.28f, 0.30f, 0.60f };
    style->Colors[ImGuiCol_BorderShadow]    = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_FrameBg]         = (ImVec4) {0.05f, 0.25f, 0.30f, 0.60f };
    style->Colors[ImGuiCol_FrameBgHovered]  = bg_dark;
    style->Colors[ImGuiCol_FrameBgActive]   = accent_cyan;
    style->Colors[ImGuiCol_TitleBg]         = bg_light;
    style->Colors[ImGuiCol_TitleBgActive]   = accent_blue;
    style->Colors[ImGuiCol_Button]          = (ImVec4) {0.10f, 0.30f, 0.34f, 0.70f };
    style->Colors[ImGuiCol_ButtonHovered]   = bg_dark;
    style->Colors[ImGuiCol_ButtonActive]    = accent_cyan;
    style->Colors[ImGuiCol_Header]          = (ImVec4) {0.10f, 0.30f, 0.34f, 0.70f };
    style->Colors[ImGuiCol_HeaderHovered]   = bg_dark;
    style->Colors[ImGuiCol_HeaderActive]    = accent_cyan;
    style->Colors[ImGuiCol_CheckMark]       = accent_blue;
    style->Colors[ImGuiCol_SliderGrab]      = accent_blue;
    style->Colors[ImGuiCol_SliderGrabActive] = grab_active;
    style->Colors[ImGuiCol_Separator]       = (ImVec4) {0.20f, 0.28f, 0.30f, 0.60f };
    style->Colors[ImGuiCol_ResizeGrip]      = bg_dark;
    style->Colors[ImGuiCol_ResizeGripHovered] = accent_blue;
    style->Colors[ImGuiCol_ResizeGripActive] = accent_red;
    style->Colors[ImGuiCol_Tab]             = bg_light;
    style->Colors[ImGuiCol_TabHovered]      = bg_dark;
    style->Colors[ImGuiCol_TabSelected]     = accent_blue;
    style->Colors[ImGuiCol_TableHeaderBg]   = accent_blue;
    style->Colors[ImGuiCol_ScrollbarBg]             = (ImVec4) { 0.20f, 0.20f, 0.30f, 0.71f };
    style->Colors[ImGuiCol_ScrollbarGrabHovered]    = grab_hovered;
    style->Colors[ImGuiCol_ScrollbarGrab]           = grab;
    style->Colors[ImGuiCol_ScrollbarGrabActive]     = grab_active;
}

static void imgui_style_enemymouse(ImGuiStyle *style)
{
    style->Alpha = 1.0;
    style->ChildRounding = 3;
    style->WindowRounding = 3;
    style->GrabRounding = 1;
    style->GrabMinSize = 20;
    style->FrameRounding = 3;


    style->Colors[ImGuiCol_Text]                    = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_TextDisabled]            = (ImVec4) { 0.00f, 0.40f, 0.41f, 1.00f };
    style->Colors[ImGuiCol_WindowBg]                = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.70f };
    style->Colors[ImGuiCol_ChildBg]                 = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_Border]                  = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.65f };
    style->Colors[ImGuiCol_BorderShadow]            = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_FrameBg]                 = (ImVec4) { 0.44f, 0.80f, 0.80f, 0.18f };
    style->Colors[ImGuiCol_FrameBgHovered]          = (ImVec4) { 0.44f, 0.80f, 0.80f, 0.27f };
    style->Colors[ImGuiCol_FrameBgActive]           = (ImVec4) { 0.44f, 0.81f, 0.86f, 0.66f };
    style->Colors[ImGuiCol_TitleBg]                 = (ImVec4) { 0.14f, 0.28f, 0.31f, 0.80f };
    style->Colors[ImGuiCol_TitleBgCollapsed]        = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.54f };
    style->Colors[ImGuiCol_TitleBgActive]           = (ImVec4) { 0.00f, 0.45f, 0.45f, 0.70f };
    style->Colors[ImGuiCol_MenuBarBg]               = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.20f };
    style->Colors[ImGuiCol_ScrollbarBg]             = (ImVec4) { 0.22f, 0.29f, 0.30f, 0.71f };
    style->Colors[ImGuiCol_ScrollbarGrab]           = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.44f };
    style->Colors[ImGuiCol_ScrollbarGrabHovered]    = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.74f };
    style->Colors[ImGuiCol_ScrollbarGrabActive]     = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    // style->Colors[ImGuiCol_ComboBg]                 = (ImVec4) { 0.16f, 0.24f, 0.22f, 0.60f };
    style->Colors[ImGuiCol_CheckMark]               = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.68f };
    style->Colors[ImGuiCol_SliderGrab]              = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.36f };
    style->Colors[ImGuiCol_SliderGrabActive]        = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.76f };
    style->Colors[ImGuiCol_Button]                  = (ImVec4) { 0.00f, 0.65f, 0.65f, 0.46f };
    style->Colors[ImGuiCol_ButtonHovered]           = (ImVec4) { 0.01f, 1.00f, 1.00f, 0.43f };
    style->Colors[ImGuiCol_ButtonActive]            = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.62f };
    style->Colors[ImGuiCol_Header]                  = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.33f };
    style->Colors[ImGuiCol_HeaderHovered]           = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.42f };
    style->Colors[ImGuiCol_HeaderActive]            = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.54f };

    style->Colors[ImGuiCol_Separator]               = (ImVec4){ 0.00f, 0.10f, 0.10f, 0.80f };
    style->Colors[ImGuiCol_SeparatorActive]         = (ImVec4){ 0.00f, 0.10f, 0.10f, 1.00f };
    style->Colors[ImGuiCol_SeparatorHovered]        = (ImVec4){ 0.00f, 0.10f, 0.10f, 0.90f };

    style->Colors[ImGuiCol_TableHeaderBg]           = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.40f };
    style->Colors[ImGuiCol_TableBorderStrong]       = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.80f };
    style->Colors[ImGuiCol_TableBorderLight]        = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.60f };
    style->Colors[ImGuiCol_TableRowBg]              = (ImVec4) { 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_TableRowBgAlt]           = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.07f };

    style->Colors[ImGuiCol_ResizeGrip]              = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.54f };
    style->Colors[ImGuiCol_ResizeGripHovered]       = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.74f };
    style->Colors[ImGuiCol_ResizeGripActive]        = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_PlotLines]               = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_PlotLinesHovered]        = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogram]           = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogramHovered]    = (ImVec4) { 0.00f, 1.00f, 1.00f, 1.00f };
    style->Colors[ImGuiCol_TextSelectedBg]          = (ImVec4) { 0.00f, 1.00f, 1.00f, 0.22f };
}

static void imgui_style_matrix(ImGuiStyle *style)
{
    // Corners
    style->WindowRounding = 4.0f;
    style->ChildRounding = 4.0f;
    style->FrameRounding = 3.0f;
    style->PopupRounding = 3.0f;
    style->ScrollbarRounding = 3.0f;
    style->GrabRounding = 3.0f;
    style->TabRounding = 3.0f;

    style->Colors[ImGuiCol_Text]                   = (ImVec4){ 0.30f, 1.00f, 0.30f, 1.00f };
    style->Colors[ImGuiCol_TextDisabled]           = (ImVec4){ 0.10f, 0.50f, 0.10f, 1.00f };
    style->Colors[ImGuiCol_WindowBg]               = (ImVec4){ 0.00f, 0.05f, 0.00f, 0.85f };
    style->Colors[ImGuiCol_ChildBg]                = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_PopupBg]                = (ImVec4){ 0.00f, 0.10f, 0.00f, 0.92f };
    style->Colors[ImGuiCol_Border]                 = (ImVec4){ 0.20f, 0.80f, 0.20f, 0.50f };
    style->Colors[ImGuiCol_BorderShadow]           = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_FrameBg]                = (ImVec4){ 0.00f, 0.30f, 0.00f, 0.39f };
    style->Colors[ImGuiCol_FrameBgHovered]         = (ImVec4){ 0.00f, 0.60f, 0.00f, 0.40f };
    style->Colors[ImGuiCol_FrameBgActive]          = (ImVec4){ 0.00f, 0.80f, 0.00f, 0.69f };
    style->Colors[ImGuiCol_TitleBg]                = (ImVec4){ 0.00f, 0.20f, 0.00f, 0.80f };
    style->Colors[ImGuiCol_TitleBgActive]          = (ImVec4){ 0.00f, 0.40f, 0.00f, 0.90f };
    style->Colors[ImGuiCol_TitleBgCollapsed]       = (ImVec4){ 0.00f, 0.30f, 0.00f, 0.20f };
    style->Colors[ImGuiCol_MenuBarBg]              = (ImVec4){ 0.00f, 0.10f, 0.00f, 0.80f };
    style->Colors[ImGuiCol_ScrollbarBg]            = (ImVec4){ 0.00f, 0.10f, 0.00f, 0.60f };
    style->Colors[ImGuiCol_ScrollbarGrab]          = (ImVec4){ 0.00f, 0.80f, 0.00f, 0.30f };
    style->Colors[ImGuiCol_ScrollbarGrabHovered]   = (ImVec4){ 0.20f, 1.00f, 0.20f, 0.40f };
    style->Colors[ImGuiCol_ScrollbarGrabActive]    = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.60f };
    style->Colors[ImGuiCol_CheckMark]              = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.90f };
    style->Colors[ImGuiCol_SliderGrab]             = (ImVec4){ 0.20f, 1.00f, 0.20f, 0.40f };
    style->Colors[ImGuiCol_SliderGrabActive]       = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.70f };
    style->Colors[ImGuiCol_Button]                 = (ImVec4){ 0.00f, 0.40f, 0.00f, 0.62f };
    style->Colors[ImGuiCol_ButtonHovered]          = (ImVec4){ 0.00f, 0.60f, 0.00f, 0.79f };
    style->Colors[ImGuiCol_ButtonActive]           = (ImVec4){ 0.00f, 0.80f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_Header]                 = (ImVec4){ 0.00f, 0.30f, 0.00f, 0.45f };
    style->Colors[ImGuiCol_HeaderHovered]          = (ImVec4){ 0.00f, 0.50f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_HeaderActive]           = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.80f };
    style->Colors[ImGuiCol_Separator]              = (ImVec4){ 0.20f, 0.60f, 0.20f, 0.60f };
    style->Colors[ImGuiCol_SeparatorHovered]       = (ImVec4){ 0.40f, 1.00f, 0.40f, 1.00f };
    style->Colors[ImGuiCol_SeparatorActive]        = (ImVec4){ 0.50f, 1.00f, 0.50f, 1.00f };
    style->Colors[ImGuiCol_ResizeGrip]             = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.20f };
    style->Colors[ImGuiCol_ResizeGripHovered]      = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.70f };
    style->Colors[ImGuiCol_ResizeGripActive]       = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.90f };
    style->Colors[ImGuiCol_Tab]                    = (ImVec4){ 0.00f, 0.30f, 0.00f, 0.79f };
    style->Colors[ImGuiCol_TabHovered]             = (ImVec4){ 0.00f, 0.60f, 0.00f, 0.80f };
    style->Colors[ImGuiCol_TabSelected]            = (ImVec4){ 0.00f, 0.80f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_TabSelectedOverline]    = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.40f };
    style->Colors[ImGuiCol_TabDimmed]              = (ImVec4){ 0.00f, 0.10f, 0.00f, 0.83f };
    style->Colors[ImGuiCol_TabDimmedSelected]      = (ImVec4){ 0.00f, 0.10f, 0.00f, 0.83f };
    style->Colors[ImGuiCol_TabDimmedSelectedOverline] = (ImVec4){ 0.30f, 1.00f, 0.30f, 1.00f };
    style->Colors[ImGuiCol_PlotLines]              = (ImVec4){ 0.00f, 1.00f, 0.00f, 1.00f };
    style->Colors[ImGuiCol_PlotLinesHovered]       = (ImVec4){ 0.50f, 1.00f, 0.50f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogram]          = (ImVec4){ 0.40f, 1.00f, 0.40f, 1.00f };
    style->Colors[ImGuiCol_PlotHistogramHovered]   = (ImVec4){ 0.60f, 1.00f, 0.60f, 1.00f };
    style->Colors[ImGuiCol_TableHeaderBg]          = (ImVec4){ 0.00f, 0.30f, 0.00f, 0.45f };
    style->Colors[ImGuiCol_TableBorderStrong]      = (ImVec4){ 0.00f, 0.80f, 0.00f, 0.80f };
    style->Colors[ImGuiCol_TableBorderLight]       = (ImVec4){ 0.10f, 0.30f, 0.10f, 1.00f };
    style->Colors[ImGuiCol_TableRowBg]             = (ImVec4){ 0.00f, 0.00f, 0.00f, 0.00f };
    style->Colors[ImGuiCol_TableRowBgAlt]          = (ImVec4){ 0.00f, 1.00f, 0.00f, 0.05f };
    style->Colors[ImGuiCol_TextSelectedBg]         = (ImVec4){ 0.00f, 1.00f, 0.00f, 0.35f };
    style->Colors[ImGuiCol_DragDropTarget]         = (ImVec4){ 1.00f, 1.00f, 0.00f, 0.90f };
    style->Colors[ImGuiCol_NavWindowingHighlight]  = (ImVec4){ 0.30f, 1.00f, 0.30f, 0.70f };
    style->Colors[ImGuiCol_NavWindowingDimBg]      = (ImVec4){ 0.00f, 0.20f, 0.00f, 0.20f };
    style->Colors[ImGuiCol_ModalWindowDimBg]       = (ImVec4){ 0.00f, 0.20f, 0.00f, 0.35f };
}

static void imgui_style_maroon(ImGuiStyle *style)
{
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
}

void imgui_set_style(imgui_style style)
{
    ImGuiStyle *igstyle = igGetStyle();

    switch (style) {
        case IMSTYLE_MATRIX:
            imgui_style_matrix(igstyle);
            imstyle = style;
            break;
        case IMSTYLE_TEAL:
            imgui_style_enemymouse(igstyle);
            imstyle = style;
            break;
        case IMSTYLE_SOLARIZED:
            imgui_style_solarized(igstyle);
            imstyle = style;
            break;
        default:
        case IMSTYLE_MAROON:
            imgui_style_maroon(igstyle);
            imstyle = IMSTYLE_MAROON;
            break;
    }
}

void imgui_style_switcher(void)
{
    igSeparatorText("ImGui style");

    if (igRadioButton_IntPtr("Maroon",      (int *)&imstyle, IMSTYLE_MAROON) ||
        igRadioButton_IntPtr("Matrix",      (int *)&imstyle, IMSTYLE_MATRIX) ||
        igRadioButton_IntPtr("Teal",        (int *)&imstyle, IMSTYLE_TEAL)   ||
        igRadioButton_IntPtr("Solarized",   (int *)&imstyle, IMSTYLE_SOLARIZED)) {
        imgui_set_style(imstyle);

        if (!settings)
            return;

        JsonNode *debug_group = settings_find_get(settings, NULL, "debug", JSON_OBJECT);
        if (debug_group)
            settings_set_num(settings, debug_group, "imgui_style", (double)imstyle);
    }
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

    imgui_set_style(IMSTYLE_MAROON);

#ifdef __APPLE__
    io->ConfigMacOSXBehaviors = true;
#endif /* __APPLE__ */

#ifndef __EMSCRIPTEN__
    GLFWwindow *win = data;
# ifdef CONFIG_RENDERER_OPENGL
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    const char *glsl_version = "#version 410";
# elif defined(CONFIG_RENDERER_METAL)
    ImGui_ImplGlfw_InitForOther(win, true);
# endif
#else
    ui_ig_init_for_emscripten(clap_ctx, ctx, io);
    const char *glsl_version = "#version 300 es";
#endif

#ifdef CONFIG_RENDERER_OPENGL
    ImGui_ImplOpenGL3_Init(glsl_version);
#elif defined(CONFIG_RENDERER_METAL)
    ui_imgui_metal_init(clap_ctx);
#endif

    igStyleColorsDark(NULL);
}

void imgui_done(void)
{
#ifdef CONFIG_RENDERER_OPENGL
    ImGui_ImplOpenGL3_Shutdown();
#elif defined(CONFIG_RENDERER_METAL)
    ui_imgui_metal_shutdown();
#endif
#ifndef __EMSCRIPTEN__
    ImGui_ImplGlfw_Shutdown();
#endif
    igDestroyContext(ctx);
}

/*
 * Set up a table header with column labels
 * @str_id:     table ID
 * @labels:     array of column name strings, must contain at least @n elements
 * @n:          number of columns
 */
bool ui_igTableHeader(const char *str_id, const char **labels, int n)
{
    if (!igBeginTable(str_id, n, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0))
        return false;

    igTableSetupColumn(labels[0], ImGuiTableColumnFlags_WidthStretch, 0, 0);

    for (int i = 1; i < n; i++)
        igTableSetupColumn(labels[i], ImGuiTableColumnFlags_WidthFixed, 0, 0);

    igTableHeadersRow();

    return true;
}

/*
 * Set up a vector table header
 * @str_id:     table ID
 * @n:          number vector components (3 for vec3 etc)
 */
bool ui_igVecTableHeader(const char *str_id, int n)
{
    if (n > 4)
        return false;

    const char *labels[] = { str_id, "X", "Y", "Z", "W" };

    return ui_igTableHeader(str_id, labels, n + 1);
}

void ui_igTableCell(bool new_row, const char *fmt, ...)
{
    if (new_row)
        igTableNextRow(0, 0);

    va_list va;

    va_start(va, fmt);
    igTableNextColumn();
    igTextV(fmt, va);
    va_end(va);
}

void ui_igTableRow(const char *key, const char *fmt, ...)
{
    igTableNextRow(0, 0);
    igTableNextColumn();
    igTextUnformatted(key, NULL);

    va_list va;

    va_start(va, fmt);
    igTableNextColumn();
    igTextV(fmt, va);
    va_end(va);
}

void ui_igVecRow(const float *v, int n, const char *fmt, ...)
{
    va_list va;

    if (n > 4)
        return;

    igTableNextRow(0, 0);
    igTableNextColumn();

    va_start(va, fmt);
    igTextV(fmt, va);
    va_end(va);


    int i;
    for (i = 0; i < n; i++) {
        igTableNextColumn();
        igText("%f", v[i]);
    }
}

bool ui_igMat4x4(const mat4x4 m, const char *name)
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

void ui_igDebugPlotLines(const char *label, debug_plot *plot)
{
    float avg = 0.0;
    for (int i = 0; i < array_size(plot->history); i++)
        avg += plot->history[i];
    avg /= array_size(plot->history);

    char text[128];
    snprintf(text, sizeof(text), plot->fmt, avg);

    igPlotLines_FloatPtr(
        label, plot->history, array_size(plot->history), plot->off, text,
        plot->scale_min, plot->scale_max,
        (ImVec2){ plot->size[0], plot->size[1] },
        sizeof(float)
    );
}

static const float left_padding = 4;

void ui_igTooltip(const char *fmt, ...)
{
    if (!igBeginItemTooltip())
        return;

    igPushTextWrapPos(igGetFontSize() * 35.0f);

    va_list va;
    va_start(va, fmt);
    igTextV(fmt, va);
    va_end(va);

    igPopTextWrapPos();
    igEndTooltip();
}

/*
 * Add a "(?)" mark with the given tooltip text
 * Lifted from imgui_demo.cpp::HelpMarker()
 */
void ui_igHelpTooltip(const char *text)
{
    igSameLine(0.0, left_padding);
    igTextDisabled("(?)");
    ui_igTooltip(text);
}

bool ui_igControlTableHeader(const char *str_id_fmt, const char *longest_label, ...)
{
    char buf[128];
    va_list va;

    va_start(va, longest_label);
    vsnprintf(buf, sizeof(buf), str_id_fmt, va);
    va_end(va);

    igSeparatorText(buf);
    if (!igBeginTable(buf, 2, ImGuiTableFlags_SizingFixedFit, (ImVec2){0,0}, 0))
        return false;

    ImVec2 size;
    igCalcTextSize(&size, longest_label, NULL, true, 0);

    igPushID_Str(buf);
    igTableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, size.x + left_padding, 0);
    igTableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0, 0);
    igPopID();

    return true;
}

bool ui_igCheckbox(const char *label, bool *v)
{
    igTableNextRow(0, 0);
    igTableNextColumn();
    igTableNextColumn();

    return igCheckbox(label, v);
}

void ui_igLabel(const char *label)
{
    igTableNextRow(0, 0);
    igTableNextColumn();

    ImVec2 size;
    igCalcTextSize(&size, label, NULL, true, 0);
    igSetCursorPosX(igGetCursorPosX() + fabsf(igGetColumnWidth(0) - size.x - left_padding));
    igTextUnformatted(label, NULL);
}

bool ui_igSliderFloat(const char *label, float *v, float min, float max, const char *fmt,
                      ImGuiSliderFlags flags)
{
    ui_igLabel(label);

    char buf[128];
    snprintf(buf, sizeof(buf), "##%s", label);

    igTableNextColumn();
    igPushItemWidth(-1.0);
    bool ret = igSliderFloat(buf, v, min, max, fmt, flags);
    igPopItemWidth();

    return ret;
}

bool ui_igSliderInt(const char *label, int *v, int min, int max, const char *fmt,
                    ImGuiSliderFlags flags)
{
    ui_igLabel(label);

    char buf[128];
    snprintf(buf, sizeof(buf), "##%s", label);

    igTableNextColumn();
    igPushItemWidth(-1.0);
    bool ret = igSliderInt(buf, v, min, max, fmt, flags);
    igPopItemWidth();

    return ret;
}

bool ui_igSliderFloat3(const char *label, float *v, float min, float max, const char *fmt,
                       ImGuiSliderFlags flags)
{
    ui_igLabel(label);

    char buf[128];
    snprintf(buf, sizeof(buf), "##%s", label);

    igTableNextColumn();
    igPushItemWidth(-1.0);
    bool ret = igSliderFloat3(buf, v, min, max, fmt, flags);
    igPopItemWidth();

    return ret;
}

bool ui_igBeginCombo(const char *label, const char *preview_value, ImGuiComboFlags flags)
{
    ui_igLabel(label);

    char buf[128];
    snprintf(buf, sizeof(buf), "##%s", label);

    igTableNextColumn();
    igPushItemWidth(-1.0);
    bool ret = igBeginCombo(buf, preview_value, flags);
    if (!ret)
        igPopItemWidth();

    return ret;
}

void ui_igEndCombo(void)
{
    igEndCombo();
    igPopItemWidth();
}

bool ui_igColorEdit3(const char *label, float *color, ImGuiColorEditFlags flags)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "##%s", label);

    igTableNextRow(0, 0);
    igTableNextColumn();

    bool ret = igColorEdit3(buf, color, flags);

    igTableNextColumn();

    igText("RGB: #%02x%02x%02x (%.02f,%.02f,%.02f)",
        (unsigned int)(color[0] * 255.0),
        (unsigned int)(color[1] * 255.0),
        (unsigned int)(color[2] * 255.0),
        color[0], color[1], color[2]
    );

    return ret;
}
