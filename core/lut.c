#include "error.h"
#include "interp.h"
#include "librarian.h"
#include "linmath.h"
#include "lut.h"
#include "memory.h"
#include "pipeline.h"
#include "render.h"
#include "scene.h"
#include "ui-debug.h"
#include "util.h"

/* ------------------------------- LUT PRESETS -------------------------------
 * Instead of shipping .cube files for basic color grading LUTs, generate them
 * at startup time, to avoid wasting space, bandwidth and parsing an text
 * based float array.
 * --------------------------------------------------------------------------- */
static void __identity(vec3 color, vec3 out)
{
    mat3x3 mat;
    mat3x3_identity(mat);
    mat3x3_transpose(mat, mat);
    mat3x3_mul_vec3(out, mat, color);
}

static void __orange_blue_filmic(vec3 color, vec3 out)
{
    vec3 powed;
    vec3_pow_vec3(powed, color, (vec3){ 0.9f, 0.95f, 1.1f });
    vec3_dup(out, (vec3){
        clampf(powed[0] * 1.6f - powed[1] * 0.2f, 0.0, 1.0),
        clampf(powed[1], 0.0, 1.0),
        clampf(powed[2] * 1.3f - powed[0] * 0.3f, 0.0, 1.0)
    });
}

static void __comic_red(vec3 color, vec3 out)
{
    float luma = vec3_mul_inner(color, (vec3){ 0.3, 0.59, 0.11 });
    float gray = luma;

    float redness = powf(fmaxf(fminf(color[0] - color[1], color[0] - color[2]) - 0.125, 0.0), 0.75);
    vec3 grayscale = { gray, gray, gray };
    vec3 reds = { powf(color[0], 0.25), gray * 0.2, gray * 0.2 };
    vec3_interp(out, grayscale, reds, fminf(redness, 1.0));
}

static void __comic_green(vec3 color, vec3 out)
{
    float luma = vec3_mul_inner(color, (vec3){ 0.3, 0.59, 0.11 });
    float gray = luma;

    float greenness = powf(fmaxf(fminf(color[1] - color[0], color[1] - color[2]) - 0.125, 0.0), 0.75);
    vec3 grayscale = { gray, gray, gray };
    vec3 greens = { gray * 0.2, powf(color[1], 0.25), gray * 0.2 };
    vec3_interp(out, grayscale, greens, fminf(greenness, 1.0));
}

static void __comic_blue(vec3 color, vec3 out)
{
    float luma = vec3_mul_inner(color, (vec3){ 0.3, 0.59, 0.11 });
    float gray = luma;

    float blueness = powf(fmaxf(fminf(color[2] - color[0], color[2] - color[1]) - 0.125, 0.0), 0.75);
    vec3 grayscale = { gray, gray, gray };
    vec3 blues = { gray * 0.2, gray * 0.2, powf(color[2], 0.25) };
    vec3_interp(out, grayscale, blues, fminf(blueness, 1.0));
}

static void __sunset_warm(vec3 color, vec3 out)
{
    out[0] = clampf(color[0] * 1.15 + 0.05, 0.0, 1.0);
    out[1] = color[1];
    out[2] = color[2] * 0.85;
}

static void __hyper_sunset(vec3 color, vec3 out)
{
    out[0] = clampf(color[0] * 1.5, 0.0, 1.0);
    out[1] = clampf(color[1] * 1.2, 0.0, 1.0);
    out[2] = clampf(color[2] * 0.7, 0.0, 1.0);
    vec3_pow(out, out, 0.85);
}

static void __green_matrix(vec3 color, vec3 out)
{
    mat3x3 mat = {
        { 0.0, 0.5, 0.0 },  // output R = 0*R + 0.5*G + 0*B
        { 0.1, 1.0, 0.1 },  // output G = 0.1*R + 1.0*G + 0.1*B
        { 0.0, 0.4, 0.0 }   // output B = 0*R + 0.4*G + 0*B
    };
    mat3x3_transpose(mat, mat);
    mat3x3_mul_vec3(out, mat, color);
}

static void __scifi_bluegreen(vec3 in, vec3 out)
{
    out[0] = clampf(in[0] * 0.3f, 0.0, 1.0);
    out[1] = clampf(in[1] * 1.4f, 0.0, 1.0);
    out[2] = clampf(in[2] * 1.6f, 0.0, 1.0);
}

static void __scifi_neon(vec3 in, vec3 out)
{
    vec3_dup(out, in);
    vec3_sub(out, out, (vec3){ 0.5, 0.5, 0.5 });
    vec3_scale(out, out, 1.6);
    vec3_add(out, out, (vec3){ 0.5, 0.5, 0.5 });
    out[0] = clampf(out[0], 0.0, 1.0);
    out[1] = clampf(out[1], 0.0, 1.0);
    out[2] = clampf(out[2], 0.0, 1.0);
}

static void __mad_max_bleach(vec3 in, vec3 out)
{
    float luma = vec3_mul_inner(in, (vec3){ 0.3f, 0.59f, 0.11f });
    float harsh = fminf(1.0f, luma * 1.6f);
    out[0] = fmaxf(in[0], harsh);
    out[1] = fmaxf(in[1] * 0.9f, harsh * 0.8f);
    out[2] = fmaxf(in[2] * 0.6f, harsh * 0.6f);
}

static void __teal_orange(vec3 color, vec3 out)
{
    vec3 powed;
    vec3_pow_vec3(powed, color, (vec3){ 0.9f, 1.0f, 1.1f });

    // R: boost for skin tones, suppress blue spill
    out[0] = clampf(powed[0] * 1.3f - powed[2] * 0.2f, 0.0f, 1.0f);
    // G: slight lift
    out[1] = clampf(powed[1] * 1.0f + powed[2] * 0.05f, 0.0f, 1.0f);
    // B: boost mids, darken overall
    out[2] = clampf(powed[2] * 1.1f - powed[0] * 0.2f - powed[1] * 0.1f, 0.0f, 1.0f);
}

typedef void (*lut_fn)(vec3, vec3);

static struct {
    const char  *name;
    lut_fn      fn;
    float       exposure;
    float       contrast;
} lut_presets[LUT_MAX] = {
    [LUT_IDENTITY]              = {
        .name       = "identity",
        .fn         = __identity,
        .exposure   = 2.0,
        .contrast   = 0.25,
    },
    [LUT_ORANGE_BLUE_FILMIC]    = {
        .name       = "orange blue filmic",
        .fn         = __orange_blue_filmic,
        .exposure   = 2.2,
        .contrast   = 0.15,
    },
    [LUT_COMIC_RED]             = {
        .name       = "comic red",
        .fn         = __comic_red,
        .exposure   = 2.4,
        .contrast   = 0.3,
    },
    [LUT_COMIC_GREEN]           = {
        .name       = "comic green",
        .fn         = __comic_green,
        .exposure   = 2.4,
        .contrast   = 0.3,
    },
    [LUT_COMIC_BLUE]            = {
        .name       = "comic blue",
        .fn         = __comic_blue,
        .exposure   = 2.4,
        .contrast   = 0.3,
    },
    [LUT_SUNSET_WARM]           = {
        .name       = "sunset warm",
        .fn         = __sunset_warm,
        .exposure   = 2.2,
        .contrast   = 0.4,
    },
    [LUT_HYPER_SUNSET]          = {
        .name       = "hyper sunset",
        .fn         = __hyper_sunset,
        .exposure   = 1.8,
        .contrast   = 0.35,
    },
    [LUT_GREEN_MATRIX]          = {
        .name       = "green matrix",
        .fn         = __green_matrix,
        .exposure   = 2.6,
        .contrast   = 0.2,
    },
    [LUT_SCIFI_BLUEGREEN]       = {
        .name       = "scifi bluegreen",
        .fn         = __scifi_bluegreen,
        .exposure   = 2.5,
        .contrast   = 0.2,
    },
    [LUT_SCIFI_NEON]            = {
        .name       = "scifi neon",
        .fn         = __scifi_neon,
        .exposure   = 2.35,
        .contrast   = 0.15,
    },
    [LUT_MAD_MAX_BLEACH]        = {
        .name       = "mad max bleach",
        .fn         = __mad_max_bleach,
        .exposure   = 2.2,
        .contrast   = 0.15,
    },
    [LUT_TEAL_ORANGE]           = {
        .name       = "teal orange",
        .fn         = __teal_orange,
        .exposure   = 2.2,
        .contrast   = 0.15,
    },
};

lut_preset lut_presets_all[LUT_MAX + 1] = {
    LUT_IDENTITY,
    LUT_ORANGE_BLUE_FILMIC,
    LUT_COMIC_RED,
    LUT_COMIC_GREEN,
    LUT_COMIC_BLUE,
    LUT_SUNSET_WARM,
    LUT_HYPER_SUNSET,
    LUT_GREEN_MATRIX,
    LUT_SCIFI_BLUEGREEN,
    LUT_SCIFI_NEON,
    LUT_MAD_MAX_BLEACH,
    LUT_TEAL_ORANGE,
    LUT_MAX,
};

/* ------------------------------- LUT CORE ---------------------------------- */

typedef struct lut {
    texture_t       tex;
    const char      *name;
    struct list     entry;
    struct ref      ref;
    lut_fn          fn;
    float           exposure;
    float           contrast;
} lut;

static cerr lut_make(struct ref *ref, void *_opts)
{
    rc_init_opts(lut) *opts = _opts;

    if (!opts->name || !opts->list || !opts->list->prev || !opts->list->next)
        return CERR_INVALID_ARGUMENTS;

    lut *lut = container_of(ref, struct lut, ref);
    lut->name = opts->name;
    lut->exposure = 1.0;
    lut->contrast = 0.15;
    lut->fn = __identity;
    list_append(opts->list, &lut->entry);

    return CERR_OK;
}

static void lut_drop(struct ref *ref)
{
    lut *lut = container_of(ref, struct lut, ref);
    list_del(&lut->entry);
    texture_deinit(&lut->tex);
}
DEFINE_REFCLASS2(lut);

static cerr lut_setup(lut *lut, uchar *arr, int side)
{
    CERR_RET(
        texture_init(
            &lut->tex,
            .type       = TEX_3D,
            .format     = TEX_FMT_RGB8,
            .layers     = side,
            .min_filter = TEX_FLT_LINEAR,
            .mag_filter = TEX_FLT_LINEAR,
            .wrap       = TEX_CLAMP_TO_EDGE,
        ),
        return CERR_TEXTURE_NOT_LOADED
    );

    CERR_RET(
        texture_load(&lut->tex, TEX_FMT_RGB8, side, side, arr),
        return CERR_TEXTURE_NOT_LOADED
    );

    return CERR_OK;
}

static inline void arr_set(uchar *arr, int sz, int x, int y, int z, vec3 rgb)
{
    arr[z * sz * sz * 3 + y * sz * 3 + x * 3 + 0] = (unsigned char)(clampf(rgb[0], 0.0, 1.0) * 255.0);
    arr[z * sz * sz * 3 + y * sz * 3 + x * 3 + 1] = (unsigned char)(clampf(rgb[1], 0.0, 1.0) * 255.0);
    arr[z * sz * sz * 3 + y * sz * 3 + x * 3 + 2] = (unsigned char)(clampf(rgb[2], 0.0, 1.0) * 255.0);
}

cresp(lut) lut_generate(struct list *list, lut_preset preset, int sz)
{
    if (preset >= LUT_MAX)
        return cresp_error_cerr(lut, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(uchar, arr) = mem_alloc(1, .nr = sz * sz * sz * 3);

    lut *lut = CRES_RET_T(ref_new_checked(lut, .name = lut_presets[preset].name, .list = list), lut);
    lut->exposure = lut_presets[preset].exposure;
    lut->contrast = lut_presets[preset].contrast;
    lut->fn = lut_presets[preset].fn;

    for (int z = 0; z < sz; z++)
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++) {
                vec3 color_in = { (float)x / (sz - 1), (float)y / (sz - 1), (float)z / (sz - 1) };
                vec3 color_out;

                lut_presets[preset].fn(color_in, color_out);
                arr_set(arr, sz, x, y, z, color_out);
            }

    CERR_RET(lut_setup(lut, arr, sz), { ref_put_last(lut); return cresp_error_cerr(lut, __cerr); });

    return cresp_val(lut, lut);
}

static cerr cube_parse(lut *lut, void *buf, size_t size)
{
    /* A horrible format gets a horrible parser */
    LOCAL(uchar, arr);
    const char *p = buf;
    float r, g, b;
    int x = 0, y = 0, z = 0, sz = 0;
    do {
        p = skip_space(p);
        if (!*p) {
            break;
        } else if (!strncmp("LUT_1D_SIZE", p, 11)) {
            return CERR_NOT_SUPPORTED;
        } else if (!strncmp("LUT_3D_SIZE", p, 11)) {
            p = skip_space(p + 11);
            if (sscanf(p, "%d", &sz) != 1 || sz < 32)
                return CERR_NOT_SUPPORTED;
            arr = mem_alloc(1, .nr = sz * sz * sz * 3);
            if (!arr)
                return CERR_NOMEM;
        } else if ((isdigit(*p) || *p == '-') && sscanf(p, "%f %f %f", &r, &g, &b) == 3) {
            if (!sz || !arr)
                return CERR_INVALID_FORMAT;

            arr_set(arr, sz, x, y, z, (vec3){ r, g, b });
            if (++x == sz) {
                x = 0;
                if (++y == sz) {
                    x = y = 0;
                    if (++z == sz)
                        break;
                }
            }
        }
        p = skip_to_new_line(p);
    } while (p < (const char *)buf + size);

    if (x != 0 || y != 0 || z != sz)
        return CERR_INVALID_FORMAT;

    CERR_RET(lut_setup(lut, arr, sz), return __cerr);

    return CERR_OK;
}

static void lut_onload(struct lib_handle *h, void *data)
{
    dbg("loading '%s'\n", h->name);

    if (h->state == RES_ERROR) {
        warn("couldn't load '%s'\n", h->name);
        goto out;
    }

    if (!str_endswith(h->name, ".cube")) {
        warn("LUT format not supported: %s\n", h->name);
        h->state = RES_ERROR;
        goto out;
    }

    CERR_RET(cube_parse(data, h->buf, h->size), { h->state = RES_ERROR; });
out:
    ref_put(h);
}

#ifndef CONFIG_FINAL
static void lut_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    /*
     * 1 second to fade in, 2 seconds to stay, 1 second to fade out,
     */
    uia_set_visible(uie, 1);
    uia_lin_float(uie, ui_element_set_alpha, 0, 1, true, 1.0);
    uia_skip_duration(uie, 2.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 1.0);
    uia_set_visible(uie, 0);
}
#endif /* CONFIG_FINAL */

void lut_apply(struct scene *scene, lut *lut)
{
    render_pass *pass = CRES_RET(pipeline_find_pass(scene->pl, "combine"), return);
    render_options *ropts = clap_get_render_options(scene->clap_ctx);

    ropts->lighting_lut = lut;
    ropts->lighting_exposure = lut->exposure;
    ropts->contrast = lut->contrast;
    render_pass_plug_texture(pass, UNIFORM_LUT_TEX, &lut->tex);

#ifndef CONFIG_FINAL
    vec3 osd_color_in = { 0.8, 0.6, 0.0 }, osd_color;
    lut->fn(osd_color_in, osd_color);

    struct ui *ui = clap_get_ui(scene->clap_ctx);
    ui_osd_new(ui, &(struct ui_widget_builder) {
            .affinity       = UI_AF_TOP | UI_AF_HCENTER,
            .el_affinity    = UI_AF_CENTER,
            .w              = 500,
            .h              = 0.3,
            .el_cb          = lut_osd_element_cb,
            .text_color     = { osd_color[0], osd_color[1], osd_color[2], 1.0 },
        },
        (const char *[]){ lut->name }, 1
    );
#endif
}

#ifndef CONFIG_FINAL
void luts_debug(struct scene *scene)
{
    render_options *ropts = clap_get_render_options(scene->clap_ctx);
    struct list *luts = clap_lut_list(scene->clap_ctx);
    const char *preview_value = ropts->lighting_lut ? ropts->lighting_lut->name : NULL;

    ui_igControlTableHeader("color grading", "LUT");
    if (ui_igBeginCombo("LUT", preview_value, ImGuiComboFlags_HeightLargest)) {
        lut *lut;
        list_for_each_entry(lut, luts, entry) {
            bool selected = lut == ropts->lighting_lut;
            if (igSelectable_Bool(lut->name, selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){})) {
                igSetItemDefaultFocus();
                lut_apply(scene, lut);
            }
        }
        ui_igEndCombo();
    }
    igEndTable();
}
#endif /* CONFIG_FINAL */

cresp(lut) lut_first(struct list *list)
{
    if (!list || list_empty(list))
        return cresp_error(lut, CERR_INVALID_ARGUMENTS);

    return cresp_val(lut, list_first_entry(list, lut, entry));
}

cresp(lut) lut_next(struct list *list, lut *lut)
{
    if (!list || list_empty(list))
        return cresp_error(lut, CERR_INVALID_ARGUMENTS);

    if (!lut || lut == list_last_entry(list, struct lut, entry))
        return lut_first(list);

    return cresp_val(lut, list_next_entry(lut, entry));
}

cresp(lut) lut_find(struct list *list, const char *name)
{
    lut *lut;

    list_for_each_entry(lut, list, entry)
        if (!strcmp(lut->name, name))
            return cresp_val(lut, lut);

    return cresp_error(lut, CERR_NOT_FOUND);
}

cresp(lut) lut_load(struct list *list, const char *name)
{
    if (!name)
        return cresp_error(lut, CERR_INVALID_ARGUMENTS);

    lut *lut = CRES_RET_T(ref_new_checked(lut, .name = name, .list = list), lut);

    struct lib_handle *lh;
    enum res_state state;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "lut/%s.cube", name);

    lh = lib_request(RES_ASSET, path, lut_onload, lut);
    state = lh->state;
    ref_put(lh);

    if (state == RES_ERROR) {
        ref_put_last(lut);
        return cresp_error(lut, CERR_LUT_NOT_LOADED);
    }

    return cresp_val(lut, lut);
}

void luts_done(struct list *list)
{
    lut *lut, *it;

    list_for_each_entry_iter(lut, it, list, entry)
        ref_put_last(lut);
}

texture_t *lut_tex(lut *lut)
{
    return &lut->tex;
}
