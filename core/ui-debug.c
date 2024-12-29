// SPDX-License-Identifier: Apache-2.0
#include "ui-debug.h"
#include "settings.h"

static struct debug_module debug_enabled[DEBUG_MODULES_MAX] = {
    [DEBUG_PIPELINE_PASSES]     = { .name = "pipeline passes" },
    [DEBUG_PIPELINE_SELECTOR]   = { .name = "pipeline selector" },
    [DEBUG_SCENE_PARAMETERS]    = { .name = "scene parameters" },
    [DEBUG_FRUSTUM_VIEW]        = { .name = "frustum view" },
    [DEBUG_LIGHT]               = { .name = "light position" },
    [DEBUG_CHARACTERS]          = { .name = "characters" },
    [DEBUG_CHARACTER_MOTION]    = { .name = "character motion" },
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

    int i;
    for (i = 0; i < DEBUG_MODULES_MAX; i++)
        debug_enabled[i].display = settings_get_bool(settings, debug_group, debug_enabled[i].name);
}

void ui_debug_set_one(enum debug_modules mod)
{
    debug_module *dbgm = ui_debug_module(mod);

    if (!debug_group)
        return;

    settings_set_bool(settings, debug_group, dbgm->name, dbgm->open);
}

debug_module *ui_igBegin_name(enum debug_modules mod, ImGuiWindowFlags flags,
                              const char *fmt, ...)
{
    debug_module *dbgm = ui_debug_module(mod);

    if (!dbgm || !dbgm->display)
        return dbgm;

    char name[128];
    if (fmt) {
        va_list va;

        va_start(va, fmt);
        vsnprintf(name, sizeof(name), fmt, va);
        va_end(va);
    }

    dbgm->open = true;
    dbgm->unfolded = igBegin(fmt ? name : dbgm->name, &dbgm->open, ImGuiWindowFlags_AlwaysAutoResize);

    return dbgm;
}

void ui_igEnd(enum debug_modules mod)
{
    debug_module *dbgm = ui_debug_module(mod);

    if (!dbgm || !dbgm->display)
        return;

    igEnd();
    ui_debug_set_one(mod);
    dbgm->display = dbgm->open;
}

void ui_igCheckbox(bool *v, const char *label, ...)
{
    char buf[128];
    va_list va;

    va_start(va, label);
    vsnprintf(buf, sizeof(buf), label, va);
    va_end(va);

    igCheckbox(buf, v);
}

void ui_igSliderFloat(float *v, float min, float max, const char *fmt, ImGuiSliderFlags flags,
                      const char *label, ...)
{
    char buf[128];
    va_list va;

    va_start(va, label);
    vsnprintf(buf, sizeof(buf), label, va);
    va_end(va);

    igSliderFloat(buf, v, min, max, fmt, flags);
}
