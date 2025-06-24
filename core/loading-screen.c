// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "font.h"
#include "object.h"
#include "loading-screen.h"
#include "ui.h"

extern model3dtx *ui_quadtx;

loading_screen *loading_screen_init(struct ui *ui)
{
    cresp(ui_widget) res = ui_progress_bar_new(ui,
        .width          = ui->width / 3,
        .height         = 21,
        .border         = 1,
        .y_off          = 100,
        .affinity       = UI_AF_BOTTOM | UI_AF_HCENTER,
        .bar_color      = (vec4){ 0, 0, 1, 1 },
        .border_color   = (vec4){ 0.7, 0.7, 0.7, 1 },
    );

    if (IS_CERR(res))
        err_cerr(res, "error creating progress bar\n");

    loading_screen *ret = mem_alloc(sizeof(*ret), .zero = 1);
    ret->progress = res.val;

    model3d *bg_m = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
    model3dtx *bg_txm = ref_new(model3dtx, .model = ref_pass(bg_m), .texture_file_name = "background.png");
    if (bg_txm) {
        ui_add_model_tail(ui, bg_txm);
        ret->background = ref_new(ui_element,
            .ui         = ui,
            .txmodel    = bg_txm,
            .affinity   = UI_AF_BOTTOM | UI_AF_LEFT,
            .width      = ui->width,
            .height     = ui->height,
        );
    }

    struct clap_config *cfg = clap_get_config(ui->clap_ctx);

    struct font *font = ref_new(font,
        .ctx    = clap_get_font(ui->clap_ctx),
        .name   = cfg->default_font_name,
        .size   = 48,
    );

    ret->uie = ref_new(ui_element,
    .ui             = ui,
        .txmodel    = ui_quadtx,
        .affinity   = UI_AF_CENTER,
        .y_off      = 0,
        .width      = 300,
        .height     = 100);

    const char *title = clap_get_config(ui->clap_ctx)->title;
    ret->uit = ui_printf(ui, font, ret->uie, (vec4){ 0.7, 0.7, 0.7, 1.0 }, UI_AF_CENTER, "%s", title);

    ref_put(font);

    ret->ui = ui;

    return ret;
}

void loading_screen_done(loading_screen *ls)
{
    ref_put(ls->uit);
    ref_put(ls->uie);
    ref_put(ls->progress);
    if (ls->background)
        ref_put(ls->background);
    mem_free(ls);
}

void loading_screen_progress(loading_screen *ls, float progress)
{
#ifndef CONFIG_BROWSER
    renderer_t *r = clap_get_renderer(ls->ui->clap_ctx);
    renderer_viewport(r, 0, 0, ls->ui->width, ls->ui->height);
    renderer_clear(r, true, false, false);
    ui_progress_bar_set_progress(ls->progress, progress);

    ui_update(ls->ui);
    models_render(ls->ui->renderer, &ls->ui->mq);
    display_swap_buffers();
#endif /* CONFIG_BROWSER */
}
