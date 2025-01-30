// SPDX-License-Identifier: Apache-2.0
#include "profiler.h"
#include "ui-debug.h"

void profiler_show(struct profile *first)
{
    debug_module *dbgm = ui_igBegin(DEBUG_FRAME_PROFILER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        ui_igVecTableHeader("profile", 1);

        struct profile *prof, *last;
        for (prof = first->next; prof; prof = prof->next) {
            igTableNextRow(0, 0);
            igTableNextColumn();
            igText(prof->name);

            igTableNextColumn();
            igText("%" PRItvsec ".%09lu", prof->diff.tv_sec, prof->diff.tv_nsec);
            last = prof;
        }
        struct timespec total;
        timespec_diff(&first->ts, &last->ts, &total);
        igTableNextRow(0, 0);
        igTableNextColumn();
        igText("total");

        igTableNextColumn();
        igText("%" PRItvsec ".%09lu", total.tv_sec, total.tv_nsec);
        igEndTable();
    }

    ui_igEnd(DEBUG_FRAME_PROFILER);
}
