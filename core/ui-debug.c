// SPDX-License-Identifier: Apache-2.0
#include "ui-debug.h"

static struct debug_module debug_enabled[DEBUG_MODULES_MAX] = {
    [DEBUG_PIPELINE_PASSES]     = { .name = "pipeline passes" },
    [DEBUG_PIPELINE_SELECTOR]   = { .name = "pipeline selector" },
    [DEBUG_SCENE_PARAMETERS]    = { .name = "scene parameters" },
};

debug_module *ui_debug_module(enum debug_modules mod)
{
    if (mod >= DEBUG_MODULES_MAX)
        return NULL;

    return &debug_enabled[mod];
}

static bool debug_selector;
static bool debug_selector_ui_open;

void ui_toggle_debug_selector(void)
{
    debug_selector = !debug_selector;
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
