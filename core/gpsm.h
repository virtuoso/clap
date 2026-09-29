/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GPSM_H__
#define __CLAP_GPSM_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "error.h"

/*
 * General-purpose state machine.
 *
 * A small, embeddable, no-heap primitive: a state enum, a static transition
 * table indexed by target state, per-state guard/enter/leave/tick callbacks,
 * and a single setter that validates, dispatches and can redirect. Meant to be
 * embedded in a user struct with the table declared static const.
 */

typedef int gpsm_state_t;       /* signed so -1 can mean "no previous state" */
cres_ret(gpsm_state_t);         /* guard()/recover() return this: a state, or an error */

#define GPSM_NO_STATE   (-1)

/*
 * State cap. Kept to a single 64-bit mask word for now (uint64_t, so the cap
 * holds on LLP64 too, unlike unsigned long). Supporting more states means a
 * runtime mask builder in place of the compile-time GPSM_FROM(); left to
 * whoever first needs it. No current machine is close.
 */
#ifndef GPSM_MAX_STATES
#define GPSM_MAX_STATES 64
#endif
#define GPSM_LONG_BITS  64u
#define GPSM_MASK_LONGS ((GPSM_MAX_STATES + GPSM_LONG_BITS - 1) / GPSM_LONG_BITS)

/*
 * Value-type bitmask, inline so a whole transition table is static const with
 * masks written in place via GPSM_FROM(). Deliberately not struct bitmap
 * (core/util.h): that is pointer-backed and cannot be a static const field.
 */
struct gpsm_mask { uint64_t bits[GPSM_MASK_LONGS]; };

static_assert(GPSM_MASK_LONGS == 1,
              "GPSM_FROM() builds a single mask word; >64 states needs a runtime builder");

struct gpsm;

/**
 * gpsm_guard_fn() - entry gate for a state; runs *before* leave(), so it can
 *                   still refuse cleanly (nothing has been committed yet)
 * @sm:     the state machine
 * @priv:   the machine's user pointer
 * @from:   state we are transitioning out of
 *
 * Belongs to the target state. Complements the static valid_from mask: the mask
 * gates the structural "is this transition declared legal", the guard gates the
 * dynamic "is it legal *now*, given the object's state" (character's
 * `if (ch->airborne)`). Guards must be light and must NOT call gpsm_set().
 *
 * Return: cres(gpsm_state_t) holding either the state to actually enter (usually
 *         the target; may be a different state to redirect, which must satisfy
 *         valid_from from @from and whose own guard() is not run), or an error
 *         (CERR_GPSM_ABORT, optionally with a reason via CERR_GPSM_ABORT_REASON())
 *         to abort with the state unchanged.
 */
typedef cres(gpsm_state_t) (*gpsm_guard_fn)(struct gpsm *sm, void *priv,
                                            gpsm_state_t from);

/**
 * gpsm_enter_fn() - do the entry work for a state; runs *after* leave(), so the
 *                   machine is already committed to landing on a new state
 * @sm:     the state machine
 * @priv:   the machine's user pointer
 * @from:   previous state, or GPSM_NO_STATE on the initial enter from gpsm_init()
 *
 * No abort path: refusing is guard()'s job and guard() has already greenlit this.
 * enter() may still redirect for the rarer case where the right landing only
 * becomes clear mid-entry: return a different state and gpsm runs its enter()
 * next (the enter chain). enter() must NOT call gpsm_set(); returning a state is
 * the only redirect mechanism.
 *
 * Return: the state actually entered -- normally @from's target, i.e. the same
 *         state whose enter() this is. A different value redirects and must
 *         satisfy valid_from from @from (illegal or cyclic is fatal in debug).
 */
typedef gpsm_state_t (*gpsm_enter_fn)(struct gpsm *sm, void *priv,
                                      gpsm_state_t from);

/**
 * gpsm_leave_fn() - leave a state; the commit point of a transition
 * @to:     the state being entered next
 */
typedef void (*gpsm_leave_fn)(struct gpsm *sm, void *priv, gpsm_state_t to);

/**
 * gpsm_tick_fn() - per-frame work for the current state, called by gpsm_tick()
 */
typedef void (*gpsm_tick_fn)(struct gpsm *sm, void *priv);

/**
 * gpsm_recover_fn() - optional last-chance handler for an internal transition
 *                     error (an illegal or cyclic redirect from an enter())
 * @from:   state the transition started from (invariant across the chain)
 * @cur:    last state whose enter() ran (where the chain stalled)
 * @bad:    the redirect target that was illegal or already seen (cyclic)
 *
 * When set it is consulted (in every build) instead of the default "fatal in
 * debug, log-and-land in release"; lets the user report state and choose an
 * outcome. Return cres_val(gpsm_state_t, s) to land on @s and return CERR_OK, or
 * an error (e.g. CERR_REQUEST_EXIT) to propagate it -- sm->state is left at @cur.
 */
typedef cres(gpsm_state_t) (*gpsm_recover_fn)(struct gpsm *sm, void *priv,
                                              gpsm_state_t from, gpsm_state_t cur,
                                              gpsm_state_t bad);

struct gpsm_state {
    const char       *name;         /* optional, used by debug overlay & traces */
    struct gpsm_mask  valid_from;    /* bit s set => transitions from s are legal */
    gpsm_guard_fn     guard;         /* optional */
    gpsm_enter_fn     enter;         /* optional */
    gpsm_leave_fn     leave;         /* optional */
    gpsm_tick_fn      tick;          /* optional, called from gpsm_tick() */
};

struct gpsm {
    gpsm_state_t              state;
    gpsm_state_t              target;      /* the state being entered; see gpsm_get_target() */
    unsigned int              nr_states;
    const struct gpsm_state  *table;       /* [nr_states] */
    void                     *priv;
    const char               *name;        /* optional, names the machine in log lines and gpsm_debug() */
    gpsm_recover_fn           recover;     /* optional; set before/after gpsm_init() */
};

/*
 * Designated-initializer-friendly mask builder. Usage:
 *   .valid_from = GPSM_FROM(CS_IDLE, CS_FALLING, CS_JUMPING)
 * Builds a struct gpsm_mask compound literal with those bits set; works in
 * static const tables. Up to 16 states per list; a state with no legal
 * from-states just leaves .valid_from zero-initialised.
 */
#define GPSM_BIT(s)          ((uint64_t)1 << (unsigned int)(s))
#define GPSM_OR1(a)          GPSM_BIT(a)
#define GPSM_OR2(a, ...)     (GPSM_BIT(a) | GPSM_OR1(__VA_ARGS__))
#define GPSM_OR3(a, ...)     (GPSM_BIT(a) | GPSM_OR2(__VA_ARGS__))
#define GPSM_OR4(a, ...)     (GPSM_BIT(a) | GPSM_OR3(__VA_ARGS__))
#define GPSM_OR5(a, ...)     (GPSM_BIT(a) | GPSM_OR4(__VA_ARGS__))
#define GPSM_OR6(a, ...)     (GPSM_BIT(a) | GPSM_OR5(__VA_ARGS__))
#define GPSM_OR7(a, ...)     (GPSM_BIT(a) | GPSM_OR6(__VA_ARGS__))
#define GPSM_OR8(a, ...)     (GPSM_BIT(a) | GPSM_OR7(__VA_ARGS__))
#define GPSM_OR9(a, ...)     (GPSM_BIT(a) | GPSM_OR8(__VA_ARGS__))
#define GPSM_OR10(a, ...)    (GPSM_BIT(a) | GPSM_OR9(__VA_ARGS__))
#define GPSM_OR11(a, ...)    (GPSM_BIT(a) | GPSM_OR10(__VA_ARGS__))
#define GPSM_OR12(a, ...)    (GPSM_BIT(a) | GPSM_OR11(__VA_ARGS__))
#define GPSM_OR13(a, ...)    (GPSM_BIT(a) | GPSM_OR12(__VA_ARGS__))
#define GPSM_OR14(a, ...)    (GPSM_BIT(a) | GPSM_OR13(__VA_ARGS__))
#define GPSM_OR15(a, ...)    (GPSM_BIT(a) | GPSM_OR14(__VA_ARGS__))
#define GPSM_OR16(a, ...)    (GPSM_BIT(a) | GPSM_OR15(__VA_ARGS__))
#define GPSM_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define GPSM_NARG(...)       GPSM_NARG_(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define GPSM_CAT_(a, b)      a ## b
#define GPSM_CAT(a, b)       GPSM_CAT_(a, b)
#define GPSM_OR(...)         GPSM_CAT(GPSM_OR, GPSM_NARG(__VA_ARGS__))(__VA_ARGS__)
#define GPSM_FROM(...)       ((struct gpsm_mask){ { GPSM_OR(__VA_ARGS__) } })
#define GPSM_FROM_ANY        ((struct gpsm_mask){ { ~(uint64_t)0 } })

/**
 * gpsm_init() - initialise a machine and enter its initial state
 * @table:      transition table, [@nr_states] entries, typically static const
 * @nr_states:  number of states, <= GPSM_MAX_STATES
 * @priv:       user pointer passed to every callback
 * @initial:    starting state; its enter() (if any) runs with from=GPSM_NO_STATE
 *
 * The initial enter() runs for side effects only: no guard, no leave, no chain.
 * Set sm->name / sm->recover before this call if you want them active during it.
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS.
 */
cerr  gpsm_init(struct gpsm *sm, const struct gpsm_state *table,
                unsigned int nr_states, void *priv, gpsm_state_t initial);

/** gpsm_get() - current state */
gpsm_state_t gpsm_get(const struct gpsm *sm);

/**
 * gpsm_get_target() - the state whose enter() is running
 * @sm:     the state machine
 *
 * Valid inside an enter chain, where gpsm_get() still says the state being
 * left: the state of the enter() that is running, so one enter() can serve
 * every state of a machine that plays the transition into whatever it
 * enters (see animator_play_transition()). Between transitions it is the
 * current state.
 * Return: the state being entered, or the current one
 */
gpsm_state_t gpsm_get_target(const struct gpsm *sm);

/** gpsm_name() - name of state @s, or a placeholder if unnamed / out of range */
const char  *gpsm_name(const struct gpsm *sm, gpsm_state_t s);

/**
 * gpsm_set() - attempt a transition to @requested
 * @sm:         the state machine
 * @requested:  the state asked for
 *
 * In order: @requested's valid_from must admit the current state; @requested's
 * guard(), if any, may abort or redirect, still with nothing changed; the
 * current state's leave() commits the transition; then the enter() chain runs
 * from the target (@requested, or the guard's redirect), each enter() able to
 * redirect once more, until one lands.
 *
 * Return: CERR_OK on a completed transition; CERR_INVALID_OPERATION if rejected
 *         by valid_from; a guard's abort error (e.g. CERR_GPSM_ABORT); or
 *         CERR_INVALID_ARGUMENTS for an out-of-range @requested.
 */
cerr  gpsm_set(struct gpsm *sm, gpsm_state_t requested);

/** gpsm_tick() - run the current state's tick() callback, if any */
void  gpsm_tick(struct gpsm *sm);

/**
 * gpsm_swap_table() - swap in a different transition table
 *
 * The state index is preserved; the caller ensures @table has a meaningful entry
 * at that index.
 */
void  gpsm_swap_table(struct gpsm *sm, const struct gpsm_state *table,
                      unsigned int nr_states);

#if !defined(CONFIG_FINAL) && !defined(CLAP_TESTS)
/**
 * gpsm_debug() - ImGui block for a machine, embedded in its owner's debug window
 * @sm:         the state machine
 * @readonly:   show the legal targets, but don't offer to transition
 *
 * Shows the current state and one button per state whose valid_from admits it;
 * a click attempts gpsm_set() to that state and a refusal is logged. No window
 * of its own: the caller is inside ui_igBegin()/ui_igEnd() of its own module.
 */
void  gpsm_debug(struct gpsm *sm, bool readonly);
#else
static inline void gpsm_debug(struct gpsm *sm, bool readonly) {}
#endif /* CONFIG_FINAL || CLAP_TESTS */

#endif /* __CLAP_GPSM_H__ */
