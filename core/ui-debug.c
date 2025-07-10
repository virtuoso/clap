// SPDX-License-Identifier: Apache-2.0
#include "font.h"
#include "ui.h"
#include "ui-debug.h"
#include "settings.h"

/*************************************************************************
 * ImGui based debug UI
 *************************************************************************/
static struct debug_module debug_enabled[DEBUG_MODULES_MAX] = {
    [DEBUG_ENTITY_INSPECTOR]    = { .name = "entity inspector" },
    [DEBUG_PIPELINE_PASSES]     = { .name = "pipeline passes" },
    [DEBUG_PIPELINE_SELECTOR]   = { .name = "pipeline selector" },
    [DEBUG_SCENE_PARAMETERS]    = { .name = "scene parameters" },
    [DEBUG_FRUSTUM_VIEW]        = { .name = "frustum view" },
    [DEBUG_LIGHT]               = { .name = "light position" },
    [DEBUG_CHARACTERS]          = { .name = "characters" },
    [DEBUG_CHARACTER_MOTION]    = { .name = "character motion" },
    [DEBUG_INPUT]               = { .name = "input" },
    [DEBUG_FRAME_PROFILER]      = { .name = "frame profiler" },
    [DEBUG_RENDERER]            = { .name = "renderer" },
    [DEBUG_DEBUGGER]            = { .name = "debugger" },
    [DEBUG_MEMORY]              = { .name = "memory" },
};

debug_module *ui_debug_module(enum debug_modules mod)
{
    if (mod >= DEBUG_MODULES_MAX)
        return NULL;

    return &debug_enabled[mod];
}

static bool debug_selector;
static bool debug_selector_ui_open;
static JsonNode *debug_group;
static struct settings *settings;

void ui_toggle_debug_selector(void)
{
    debug_selector = !debug_selector;

    if (debug_group)
        settings_set_bool(settings, debug_group, "debug_selector", debug_selector);
}

void ui_debug_selector(void)
{
    if (!debug_selector)
        return;

    debug_selector_ui_open = igBegin("Debug controls", &debug_selector, ImGuiWindowFlags_AlwaysAutoResize);
    if (!debug_selector_ui_open) {
        igEnd();
        return;
    }

    int i;
    for (i = 0; i < DEBUG_MODULES_MAX; i++)
        igCheckbox(debug_enabled[i].name, &debug_enabled[i].display);

    imgui_style_switcher();
    igEnd();
}

void ui_debug_set_settings(struct settings *rs)
{
    imgui_set_settings(rs);

    settings = rs;

    debug_group = settings_find_get(rs, NULL, "debug", JSON_OBJECT);
    if (!debug_group)
        return;

    debug_selector = settings_get_bool(settings, debug_group, "debug_selector");

    imgui_set_style(settings_get_num(settings, debug_group, "imgui_style"));

    int i;
    for (i = 0; i < DEBUG_MODULES_MAX; i++) {
        debug_enabled[i].display = settings_get_bool(settings, debug_group, debug_enabled[i].name);
        debug_enabled[i].prev = debug_enabled[i].display;
    }
}

void ui_debug_set_one(enum debug_modules mod)
{
    debug_module *dbgm = ui_debug_module(mod);

    if (!dbgm || !debug_group)
        return;

    if (!dbgm->prev != dbgm->display)
        return;

    dbgm->prev = dbgm->display;
    settings_set_bool(settings, debug_group, dbgm->name, dbgm->display);
}

debug_module *ui_igBegin_name(enum debug_modules mod, ImGuiWindowFlags flags,
                              const char *fmt, ...)
{
    debug_module *dbgm = ui_debug_module(mod);

    ui_debug_set_one(mod);

    if (!dbgm || !dbgm->display)
        return dbgm;

    char name[128];
    if (fmt) {
        va_list va;

        va_start(va, fmt);
        int pos = vsnprintf(name, sizeof(name), fmt, va);
        va_end(va);

        if (pos < sizeof(name) - 1) {
            pos += snprintf(name + pos, sizeof(name) - pos, "###%s", dbgm->name);
            if (pos == sizeof(name))
                name[sizeof(name) - 1] = 0;
        }
    }

    dbgm->open = true;
    dbgm->unfolded = igBegin(fmt ? name : dbgm->name, &dbgm->open, flags);

    return dbgm;
}

void ui_igEnd(enum debug_modules mod)
{
    debug_module *dbgm = ui_debug_module(mod);

    if (!dbgm || !dbgm->display)
        return;

    igEnd();
    dbgm->display = dbgm->open;
}
