// SPDX-License-Identifier: Apache-2.0
#include "profiler.h"
#include "ui-debug.h"

void profiler_show(struct profile *first, unsigned long fps)
{
    static debug_plot frame_total = {
        .fmt        = "total avg: %f",
        .scale_max  = 1.0,
        .size       = { 200.0, 40.0 },
    };

    static debug_plot fps_plot = {
        .fmt        = "fps avg: %.02f",
        .scale_max  = 120.0,
        .size       = { 200.0, 60.0 },
    };

    debug_plot_push(&fps_plot, (float)fps);

    debug_module *dbgm = ui_igBegin(DEBUG_FRAME_PROFILER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        struct profile *prof, *last;
        for (prof = first->next; prof; prof = prof->next) {
            ui_igDebugPlotLines(prof->name, &prof->plot);
            last = prof;
        }

        struct timespec total;
        timespec_diff(&first->ts, &last->ts, &total);
        debug_plot_push(&frame_total, (float)total.tv_nsec / NSEC_PER_SEC);
        ui_igDebugPlotLines("##total", &frame_total);

        ui_igDebugPlotLines("##fps", &fps_plot);
    }

    ui_igEnd(DEBUG_FRAME_PROFILER);
}
