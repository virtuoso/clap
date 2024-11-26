// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include "common.h"
#include "json.h"
#include "librarian.h"

#define SETTINGS_DEFAULT \
    "{"                  \
    " \"music_volume\": 0" \
    "}"

static char *settings_file;

#define SETTINGS_FILE "clap.json"

struct settings {
    JsonNode *root;
    void     (*on_ready)(struct settings *rs, void *data);
    void     *on_ready_data;
    bool     ready;
    bool     dirty;
} _settings;

#ifdef CONFIG_BROWSER
static void settings_sync(void)
{
    EM_ASM(
        if (!Module.fs_syncing) {
            Module.fs_syncing = true;
            FS.syncfs(false, function(err) {
                assert(!err);
                Module.fs_syncing = false;
            });
        }
    );
}
#else
static void settings_sync(void) {}
#endif /* CONFIG_BROWSER */

static int settings_store(struct settings *settings);

static void settings_default(struct settings *settings)
{
    err_on(settings->dirty);
    settings->root = json_decode(SETTINGS_DEFAULT);
    err_on(!settings->root, "couldn't parse default settings\n");
    settings->dirty = true;
    settings_store(settings);
}

static int settings_store(struct settings *settings)
{
    LOCAL(char, buf);
    FILE *f;

    if (!settings->dirty)
        return 0;
    if (!settings->root)
        settings_default(settings);

    CHECK(buf = json_encode(settings->root));
    f   = fopen(settings_file, "w+");
    if (!f)
        return -1;

    fwrite(buf, strlen(buf) + 1, 1, f);
    trace("wrote '%s' settings\n", buf);
    fclose(f);
    settings_sync();

    return 0;
}

static int settings_load(struct settings *settings)
{
    LOCAL(char, buf);
    LOCAL(FILE, f);
    struct stat st;

    if (stat(settings_file, &st) == -1) {
        if (errno != ENOENT)
            return -1;

        settings_default(settings);
        return 0;
    }

    f = fopen(settings_file, "r");
    if (!f)
        return -1;

    if (settings->root) {
        if (settings->dirty)
            return -1;
        json_delete(settings->root);
    }

    CHECK(buf = calloc(1, st.st_size + 1));
    if (fread(buf, st.st_size, 1, f) != 1) {
        free(buf);
        return -1;
    }

    settings->root = json_decode(buf);
    if (!settings->root) {
        warn("couldn't parse %s, restoring defaults\n", settings_file);
        settings_default(settings);
    }
    trace("read '%s' from settings\n", buf);
    settings->ready = true;

    return 0;
}

JsonNode *settings_get(struct settings *settings, const char *key)
{
    if (!settings->ready)
        return NULL;

    return json_find_member(settings->root, key);
}

double settings_get_num(struct settings *settings, const char *key)
{
    JsonNode *node;

    if (!settings->ready)
        return 0.0;

    node = settings_get(settings, key);
    if (!node || node->tag != JSON_NUMBER)
        return 0.0;

    return node->number_;
}

const char *settings_get_str(struct settings *settings, const char *key)
{
    JsonNode *node;

    if (!settings->ready)
        return NULL;

    node = settings_get(settings, key);
    if (!node || node->tag != JSON_STRING)
        return NULL;

    return node->string_;
}

void settings_set(struct settings *settings, const char *key, JsonNode *node)
{
    JsonNode *old;

    /*
     * We could store these in a temorary tree and merge them into ->root
     * when @settings becomes ready. But not urgently so.
     */
    if (!settings->ready)
        return;

    err_on(!settings->root);
    old = json_find_member(settings->root, key);
    if (old)
        json_delete(old);

    json_append_member(settings->root, key, node);
    settings->dirty = true;
    settings_store(settings);
}

void settings_set_num(struct settings *settings, const char *key, double num)
{
    JsonNode *new;
   
    if (!settings->ready)
        return;

    CHECK(new = json_mknumber(num));
    settings_set(settings, key, new);
}

void settings_set_string(struct settings *settings, const char *key, const char *str)
{
    JsonNode *new;

    if (!settings->ready)
        return;

    CHECK(new = json_mkstring(str));
    settings_set(settings, key, new);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void settings_ready(void)
{
    settings_load(&_settings);
    _settings.ready = true;
    _settings.on_ready(&_settings, _settings.on_ready_data);
}
#endif /* __EMCRIPTEN__ */

struct settings *settings_init(void *cb, void *data)
{
    unused const char *home;

    _settings.on_ready      = cb;
    _settings.on_ready_data = data;
    settings_file = lib_figure_uri(RES_STATE, SETTINGS_FILE);
    if (!settings_file)
        return NULL;

#ifdef __EMSCRIPTEN__
    EM_ASM(
        FS.mkdir("/settings");
        FS.mount(IDBFS, {}, "/settings");
        FS.syncfs(true, function(err) {
            assert(!err);
            ccall("settings_ready");
        });
        console.log("requested settings\n");
    );
#else
    settings_load(&_settings);
    _settings.on_ready(&_settings, data);
#endif /* !__EMSCRIPTEN__ */
    return &_settings;
}

void settings_done(struct settings *settings)
{
    settings_store(settings);
#ifndef __EMSCRIPTEN__
    free(settings_file);
#endif /* !__EMSCRIPTEN__ */
    json_delete(settings->root);
    settings->ready = false;
}
