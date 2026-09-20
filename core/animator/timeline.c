// SPDX-License-Identifier: Apache-2.0
#include "timeline.h"

void timeline_start(timeline *tl, double now, double duration, double speed, double phase)
{
    tl->speed = speed > 0.0 ? speed : 1.0;
    tl->t0 = now - phase * duration / tl->speed;
}

double timeline_elapsed(const timeline *tl, double now)
{
    return (now - tl->t0) * tl->speed;
}

bool timeline_ended(const timeline *tl, double now, double duration)
{
    return timeline_elapsed(tl, now) >= duration;
}

double timeline_progress(const timeline *tl, double now, double duration)
{
    return timeline_elapsed(tl, now) / duration;
}

bool timeline_set_speed(timeline *tl, double now, double duration, double speed)
{
    tl->speed = speed > 0.0 ? speed : 1.0;
    if (!timeline_ended(tl, now, duration))
        return false;

    tl->t0 = now;
    return true;
}
