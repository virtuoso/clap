/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANIMATOR_TIMELINE_H__
#define __CLAP_ANIMATOR_TIMELINE_H__

#include <stdbool.h>

/**
 * struct timeline - the clock of one playing element
 * @t0:     when it started, on the animation clock
 * @speed:  playback rate: elapsed time runs at @speed times the clock
 *
 * Pure arithmetic over the animation clock, so the sequencing rules can be
 * unit-tested without an entity; the animator owns one per layer.
 */
typedef struct timeline {
    double  t0;
    double  speed;
} timeline;

/**
 * timeline_start() - start an element
 * @tl:         the clock; fully overwritten
 * @now:        the animation clock
 * @duration:   the element's length in seconds at speed 1
 * @speed:      playback rate; 0 means 1.0
 * @phase:      start this far into the element, [0, 1) of @duration
 */
void timeline_start(timeline *tl, double now, double duration, double speed, double phase);

/* seconds into the element, at its rate */
double timeline_elapsed(const timeline *tl, double now);

/* has an element of @duration run out */
bool timeline_ended(const timeline *tl, double now, double duration);

/* how far through an element of @duration (> 0), 0..1 and beyond once ended */
double timeline_progress(const timeline *tl, double now, double duration);

/**
 * timeline_set_speed() - change the rate mid-element
 * @tl:         the clock
 * @now:        the animation clock
 * @duration:   the element's length
 * @speed:      the new rate; 0 means 1.0
 *
 * The elapsed time is not rescaled: it jumps to (now - t0) * speed. An
 * element that lands past its end restarts at @now.
 * Return: true if it restarted, so the caller resets per-element state
 */
bool timeline_set_speed(timeline *tl, double now, double duration, double speed);

#endif /* __CLAP_ANIMATOR_TIMELINE_H__ */
