// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "gpsm.h"
#include "common.h"
#include "logger.h"

/*
 * General-purpose state machine: a state enum, a static const transition
 * table indexed by target state, and per-state guard/enter/leave/tick
 * callbacks. gpsm_set() is the only way to change state; gpsm.h describes
 * what a transition goes through.
 */

/* Fatal only in debug; release falls through to logging + recovery. */
#ifndef CONFIG_FINAL
#define GPSM_FATAL()    enter_debugger()
#else
#define GPSM_FATAL()    do {} while (0)
#endif /* CONFIG_FINAL */

static inline const char *gpsm_id(const struct gpsm *sm)
{
    return sm->name ? sm->name : "?";
}

static inline bool state_in_range(const struct gpsm *sm, gpsm_state_t s)
{
    return s >= 0 && (unsigned int)s < sm->nr_states;
}

static inline bool mask_test(const struct gpsm_mask *m, gpsm_state_t s)
{
    return (m->bits[(unsigned int)s / GPSM_LONG_BITS] >> ((unsigned int)s % GPSM_LONG_BITS)) & 1;
}

static inline void mask_set(struct gpsm_mask *m, gpsm_state_t s)
{
    m->bits[(unsigned int)s / GPSM_LONG_BITS] |= (uint64_t)1 << ((unsigned int)s % GPSM_LONG_BITS);
}

/**
 * gpsm_bad_redirect() - handle a post-commit anomaly in the enter chain
 * @sm:     the state machine
 * @from:   state the transition started from
 * @cur:    last state whose enter() ran
 * @bad:    the redirect target that is illegal, out of range or already seen
 * @what:   what went wrong, for the log line
 *
 * leave() has already fired, so the machine always lands somewhere. With a
 * recover callback the user chooses; without one it's fatal in debug and
 * lands on @cur in release.
 *
 * Return: CERR_OK when recover() picked the landing state; otherwise the
 *         error recover() returned, or CERR_INVALID_ARGUMENTS for an
 *         out-of-range pick and CERR_INVALID_OPERATION without recover(),
 *         with @sm left on @cur
 */
static cerr gpsm_bad_redirect(struct gpsm *sm, gpsm_state_t from,
                              gpsm_state_t cur, gpsm_state_t bad, const char *what)
{
    err("gpsm(%s): %s: %d -> %d (transition from %d)\n", gpsm_id(sm), what, cur, bad, from);

    if (sm->recover) {
        cres(gpsm_state_t) r = sm->recover(sm, sm->priv, from, cur, bad);

        if (IS_CERR(r)) {
            sm->state = cur;            /* best-effort landing; caller decides */
            sm->target = cur;
            return cerr_error_cres(r);
        }
        if (!state_in_range(sm, r.val)) {
            err("gpsm(%s): recover returned out-of-range state %d\n", gpsm_id(sm), r.val);
            sm->state = cur;
            sm->target = cur;
            return CERR_INVALID_ARGUMENTS;
        }
        sm->state = r.val;              /* trusted; no valid_from re-check */
        sm->target = r.val;
        return CERR_OK;
    }

    GPSM_FATAL();                       /* debug: catch the bug here */
    sm->state = cur;                    /* release: land and limp on */
    sm->target = cur;
    return CERR_INVALID_OPERATION;
}

cerr gpsm_init(struct gpsm *sm, const struct gpsm_state *table,
               unsigned int nr_states, void *priv, gpsm_state_t initial)
{
    if (!sm || !table || !nr_states || nr_states > GPSM_MAX_STATES)  return CERR_INVALID_ARGUMENTS;
    if (initial < 0 || (unsigned int)initial >= nr_states)  return CERR_INVALID_ARGUMENTS;

    /* Only touch the fields we own; name/recover are the caller's to set. */
    sm->table     = table;
    sm->nr_states = nr_states;
    sm->priv      = priv;
    sm->state     = initial;
    sm->target    = initial;

    if (table[initial].enter) {
        auto got = table[initial].enter(sm, priv, GPSM_NO_STATE);

        /* The initial enter runs for side effects; it must land on initial. */
        if (got != initial) {
            err("gpsm(%s): initial enter redirected %d -> %d; ignored\n",
                gpsm_id(sm), initial, got);
            GPSM_FATAL();
        }
    }

    return CERR_OK;
}

gpsm_state_t gpsm_get(const struct gpsm *sm)
{
    return sm->state;
}

gpsm_state_t gpsm_get_target(const struct gpsm *sm)
{
    return sm->target;
}

const char *gpsm_name(const struct gpsm *sm, gpsm_state_t s)
{
    if (!state_in_range(sm, s))  return "<invalid>";
    return sm->table[s].name ? sm->table[s].name : "<unnamed>";
}

cerr gpsm_set(struct gpsm *sm, gpsm_state_t requested)
{
    gpsm_state_t from = sm->state;

    if (!state_in_range(sm, requested))  return CERR_INVALID_ARGUMENTS;

    auto req_desc = &sm->table[requested];

    /* 1. structural check: is this transition declared legal? */
    if (!mask_test(&req_desc->valid_from, from))  return CERR_INVALID_OPERATION;

    /* 2. guard (pre-commit): may redirect the target, or abort cleanly. */
    gpsm_state_t target = requested;
    if (req_desc->guard) {
        cres(gpsm_state_t) g = req_desc->guard(sm, sm->priv, from);

        if (IS_CERR(g))  return cerr_error_cres(g);     /* abort; state unchanged */
        if (!state_in_range(sm, g.val)) {
            err("gpsm(%s): guard returned out-of-range state %d\n", gpsm_id(sm), g.val);
            GPSM_FATAL();
            return CERR_INVALID_ARGUMENTS;
        }
        target = g.val;
    }

    /* 3a. a guard redirect must itself be legal from @from (still pre-commit). */
    if (target != requested && !mask_test(&sm->table[target].valid_from, from)) {
        err("gpsm(%s): guard redirected %d -> %d, illegal from %d\n",
            gpsm_id(sm), requested, target, from);
        GPSM_FATAL();
        return CERR_INVALID_OPERATION;          /* state unchanged */
    }

    /* 3b. commit: from here we are guaranteed to land on a new state. */
    if (sm->table[from].leave)  sm->table[from].leave(sm, sm->priv, target);

    /* 4. enter chain. enter() receives the original @from throughout. */
    struct gpsm_mask seen = {};
    mask_set(&seen, target);
    gpsm_state_t cur = target;

    for (;;) {
        auto cur_desc = &sm->table[cur];

        if (!cur_desc->enter)  break;           /* no enter: land on cur */

        sm->target = cur;
        gpsm_state_t next = cur_desc->enter(sm, sm->priv, from);

        if (next == cur)  break;                /* enter landed */

        /* enter redirected ("this enter did not land"); guard is not re-run. */
        if (!state_in_range(sm, next))
            return gpsm_bad_redirect(sm, from, cur, next, "enter redirect out of range");
        if (mask_test(&seen, next))
            return gpsm_bad_redirect(sm, from, cur, next, "enter redirect cycle");
        if (!mask_test(&sm->table[next].valid_from, from))
            return gpsm_bad_redirect(sm, from, cur, next, "illegal enter redirect");

        mask_set(&seen, next);
        cur = next;
    }

    sm->state = cur;
    sm->target = cur;
    return CERR_OK;
}

void gpsm_tick(struct gpsm *sm)
{
    auto st = &sm->table[sm->state];

    if (st->tick)  st->tick(sm, sm->priv);
}

void gpsm_swap_table(struct gpsm *sm, const struct gpsm_state *table,
                     unsigned int nr_states)
{
    sm->table     = table;
    sm->nr_states = nr_states;
    /* state index preserved; caller ensures table[state] is meaningful */
}

#if !defined(CONFIG_FINAL) && !defined(CLAP_TESTS)
#include "ui-debug.h"

void gpsm_debug(struct gpsm *sm, bool readonly)
{
    gpsm_state_t cur = sm->state;

    /* state names repeat across machines sharing a window, so scope the IDs */
    igPushID_Ptr(sm);
    igText("state %s (%d)", gpsm_name(sm, cur), cur);

    if (readonly)  igBeginDisabled(true);

    bool first = true;
    for (unsigned int s = 0; s < sm->nr_states; s++) {
        if (!mask_test(&sm->table[s].valid_from, cur))  continue;

        if (!first)  igSameLine(0.0, -1.0);
        first = false;

        if (igSmallButton(gpsm_name(sm, s)))
            CERR_RET(gpsm_set(sm, s),
                     err_cerr(__cerr, "gpsm(%s): %s -> %s refused\n",
                              gpsm_id(sm), gpsm_name(sm, cur), gpsm_name(sm, s)));
    }

    if (readonly)  igEndDisabled();
    igPopID();
}
#endif /* !CONFIG_FINAL && !CLAP_TESTS */
