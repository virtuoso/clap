// SPDX-License-Identifier: Apache-2.0
#include "font.h"
#include "ui.h"
#include "ui-debug.h"
#include "settings.h"

/*************************************************************************
 * ui.c based debug widgets
 *************************************************************************/
static const char **ui_debug_mods = NULL;
static unsigned int nr_ui_debug_mods;
static unsigned int ui_debug_current;
static char **ui_debug_strs;
static struct ui_element *debug_uit;
static struct ui_element *debug_element;
static struct font *debug_font;

static char **ui_debug_mod_str(const char *mod)
{
    int i;

    mod = str_basename(mod);
    for (i = 0; i < nr_ui_debug_mods; i++)
        if (!strcmp(mod, ui_debug_mods[i]))
            goto found;

    CHECK(ui_debug_mods = realloc(ui_debug_mods, sizeof(char *) * (i + 1)));
    CHECK(ui_debug_strs = realloc(ui_debug_strs, sizeof(char *) * (i + 1)));
    nr_ui_debug_mods++;

    ui_debug_mods[i] = mod;
    ui_debug_strs[i] = NULL;
found:
    return &ui_debug_strs[i];
}

void ui_debug_update(struct ui *ui)
{
    struct font *font;
    float color[] = { 0.9, 0.1, 0.2, 1.0 };
    char *str;

    if (!ui_debug_strs || !nr_ui_debug_mods)
        return;

    str = ui_debug_strs[ui_debug_current];

    if (debug_uit) {
        ref_put_last(debug_uit);
        debug_uit = NULL;
    } else if (str) {
        debug_element = ref_new(ui_element,
                                .ui         = ui,
                                .txmodel    = ui_quadtx_get(),
                                .affinity   = UI_AF_BOTTOM | UI_AF_LEFT,
                                .x_off      = 0.01,
                                .y_off      = 50,
                                .width      = 400,
                                .height     = 150);
    }
    if (str) {
        font = font_get(debug_font);
        debug_uit = ui_printf(ui, font, debug_element, color, UI_AF_LEFT, "%s", str);
        font_put(font);
    }
}

void __ui_debug_printf(const char *mod, const char *fmt, ...)
{
    char **pstr = ui_debug_mod_str(mod);
    char *old = *pstr;
    va_list va;

    va_start(va, fmt);
    cres(int) res = mem_vasprintf(pstr, fmt, va);
    va_end(va);

    if (IS_CERR(res))
        *pstr = NULL;
    else
        mem_free(old);
}

void ui_show_debug(const char *debug_name)
{
    int i;

    for (i = 0; i < nr_ui_debug_mods; i++) {
        if (!strcmp(debug_name, ui_debug_mods[i]))
            goto found;
    }

    return;
found:
    ui_debug_current = i;
}

struct ui_widget *ui_debug_menu(struct ui *ui)
{
    return ui_menu_new(ui, ui_debug_mods, nr_ui_debug_mods);
}

cerr ui_debug_init(struct ui *ui)
{
    ui_debug_mod_str("off");
    debug_font = ref_new(font, .ctx = clap_get_font(ui->clap_ctx), .name = "ProggyTiny.ttf", .size = 28);
    return debug_font ? CERR_OK : CERR_FONT_NOT_LOADED;
}

void ui_debug_done(struct ui *ui)
{
    if (debug_uit) {
        ref_put(debug_element);
        ref_put_last(debug_uit);
    }

    if (debug_font)
        font_put(debug_font);

    int i;

    for (i = 0; i < nr_ui_debug_mods; i++)
        mem_free(ui_debug_strs[i]);

    mem_free(ui_debug_mods);
    mem_free(ui_debug_strs);
}

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
    [DEBUG_FRAME_PROFILER]      = { .name = "frame profiler" },
    [DEBUG_RENDERER]            = { .name = "renderer" },
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
