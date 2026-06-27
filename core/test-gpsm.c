// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "gpsm.h"
#include "test.h"
#include "util.h"

enum { TS_A, TS_B, TS_C, TS_D, TS_NR };

struct ts {
    int          enter_n[TS_NR];
    int          leave_n[TS_NR];
    int          tick_n[TS_NR];
    gpsm_state_t enter_from[TS_NR];
    gpsm_state_t leave_to[TS_NR];
    int          guard_n;
    /* knobs */
    bool         b_guard_abort;
    gpsm_state_t b_guard_redirect;          /* GPSM_NO_STATE => none */
    gpsm_state_t enter_redir[TS_NR];        /* GPSM_NO_STATE => land on self */
    /* recover */
    int          recover_n;
    gpsm_state_t recover_ret;               /* < 0 => return CERR_REQUEST_EXIT */
    gpsm_state_t recover_from, recover_cur, recover_bad;
};

static gpsm_state_t ts_do_enter(struct ts *t, gpsm_state_t id, gpsm_state_t from)
{
    t->enter_n[id]++;
    t->enter_from[id] = from;
    if (t->enter_redir[id] != GPSM_NO_STATE)  return t->enter_redir[id];
    return id;
}

static void ts_do_leave(struct ts *t, gpsm_state_t id, gpsm_state_t to)
{
    t->leave_n[id]++;
    t->leave_to[id] = to;
}

static void ts_do_tick(struct ts *t, gpsm_state_t id)
{
    t->tick_n[id]++;
}

#define TS_CB(ID) \
    static gpsm_state_t ts_enter_##ID(struct gpsm *sm, void *priv, gpsm_state_t from) \
    { return ts_do_enter((struct ts *)priv, ID, from); } \
    static void ts_leave_##ID(struct gpsm *sm, void *priv, gpsm_state_t to) \
    { ts_do_leave((struct ts *)priv, ID, to); } \
    static void ts_tick_##ID(struct gpsm *sm, void *priv) \
    { ts_do_tick((struct ts *)priv, ID); }

TS_CB(TS_A)
TS_CB(TS_B)
TS_CB(TS_C)
TS_CB(TS_D)

static cres(gpsm_state_t) ts_guard_B(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct ts *t = priv;

    t->guard_n++;
    if (t->b_guard_abort)  return cres_error(gpsm_state_t, CERR_GPSM_ABORT);
    if (t->b_guard_redirect != GPSM_NO_STATE)  return cres_val(gpsm_state_t, t->b_guard_redirect);
    return cres_val(gpsm_state_t, TS_B);
}

static cres(gpsm_state_t) ts_recover(struct gpsm *sm, void *priv, gpsm_state_t from,
                                     gpsm_state_t cur, gpsm_state_t bad)
{
    struct ts *t = priv;

    t->recover_n++;
    t->recover_from = from;
    t->recover_cur  = cur;
    t->recover_bad  = bad;
    if (t->recover_ret == GPSM_NO_STATE)  return cres_error(gpsm_state_t, CERR_REQUEST_EXIT);
    return cres_val(gpsm_state_t, t->recover_ret);
}

#define TS_ROW(ID, FROM_MASK, GUARD) \
    [ID] = { .name = #ID, .valid_from = FROM_MASK, .guard = GUARD, \
             .enter = ts_enter_##ID, .leave = ts_leave_##ID, .tick = ts_tick_##ID }

static const struct gpsm_state ts_table[TS_NR] = {
    TS_ROW(TS_A, GPSM_FROM(TS_B, TS_C, TS_D),       NULL),
    TS_ROW(TS_B, GPSM_FROM(TS_A, TS_C),             ts_guard_B),
    TS_ROW(TS_C, GPSM_FROM(TS_A, TS_B, TS_C),       NULL),
    TS_ROW(TS_D, GPSM_FROM(TS_A, TS_B),             NULL),
};

/* Alternate table: TS_A additionally self-loops (for gpsm_swap_table). */
static const struct gpsm_state ts_table_alt[TS_NR] = {
    TS_ROW(TS_A, GPSM_FROM(TS_A, TS_B, TS_C, TS_D), NULL),
    TS_ROW(TS_B, GPSM_FROM(TS_A, TS_C),             ts_guard_B),
    TS_ROW(TS_C, GPSM_FROM(TS_A, TS_B, TS_C),       NULL),
    TS_ROW(TS_D, GPSM_FROM(TS_A, TS_B),             NULL),
};

static void ts_setup(struct ts *t)
{
    *t = (struct ts){};
    for (int i = 0; i < TS_NR; i++) {
        t->enter_redir[i] = GPSM_NO_STATE;
        t->enter_from[i]  = GPSM_NO_STATE;
        t->leave_to[i]    = GPSM_NO_STATE;
    }
    t->b_guard_redirect = GPSM_NO_STATE;
    t->recover_ret      = GPSM_NO_STATE;
}

/* Reset recordings between phases of a test; leaves the knobs alone. */
static void ts_reset(struct ts *t)
{
    for (int i = 0; i < TS_NR; i++) {
        t->enter_n[i] = t->leave_n[i] = t->tick_n[i] = 0;
        t->enter_from[i] = t->leave_to[i] = GPSM_NO_STATE;
    }
    t->guard_n = t->recover_n = 0;
    t->recover_from = t->recover_cur = t->recover_bad = GPSM_NO_STATE;
}

/* IS_CERR()/CERR_CODE() take the address of their arg, so a call result must be
 * bound first; these helpers do that so the assertions can stay terse. */
static bool ts_failed(cerr e)     { return IS_CERR(e); }
static cerr_enum ts_code(cerr e)  { return CERR_CODE(e); }

static int gpsm_test_basic(void)
{
    struct ts t;
    ts_setup(&t);
    struct gpsm sm = { .name = "ts" };

    if (ts_failed(gpsm_init(&sm, ts_table, TS_NR, &t, TS_A)))  return EXIT_FAILURE;
    /* initial enter ran with from == GPSM_NO_STATE */
    if (t.enter_n[TS_A] != 1 || t.enter_from[TS_A] != GPSM_NO_STATE)  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;

    /* valid-from rejection: TS_A is not in its own valid_from */
    if (ts_code(gpsm_set(&sm, TS_A)) != _CERR_INVALID_OPERATION)  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;

    /* legal transition A -> C */
    if (ts_failed(gpsm_set(&sm, TS_C)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_C)  return EXIT_FAILURE;
    if (t.leave_n[TS_A] != 1 || t.leave_to[TS_A] != TS_C)  return EXIT_FAILURE;
    if (t.enter_n[TS_C] != 1 || t.enter_from[TS_C] != TS_A)  return EXIT_FAILURE;

    /* self-loop C -> C (declared): full leave + enter re-run */
    if (ts_failed(gpsm_set(&sm, TS_C)))  return EXIT_FAILURE;
    if (t.leave_n[TS_C] != 1 || t.leave_to[TS_C] != TS_C)  return EXIT_FAILURE;
    if (t.enter_n[TS_C] != 2 || t.enter_from[TS_C] != TS_C)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int gpsm_test_guard(void)
{
    struct ts t;
    ts_setup(&t);
    struct gpsm sm = { .name = "ts" };

    gpsm_init(&sm, ts_table, TS_NR, &t, TS_A);
    ts_reset(&t);

    /* guard abort: state unchanged, no commit */
    t.b_guard_abort = true;
    if (ts_code(gpsm_set(&sm, TS_B)) != _CERR_GPSM_ABORT)  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;
    if (t.leave_n[TS_A] != 0 || t.enter_n[TS_B] != 0)  return EXIT_FAILURE;

    /* guard redirect (pre-leave): asked B, land C */
    t.b_guard_abort = false;
    t.b_guard_redirect = TS_C;
    if (ts_failed(gpsm_set(&sm, TS_B)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_C)  return EXIT_FAILURE;
    if (t.enter_n[TS_B] != 0 || t.enter_n[TS_C] != 1)  return EXIT_FAILURE;
    if (t.leave_n[TS_A] != 1 || t.enter_from[TS_C] != TS_A)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int gpsm_test_enter_chain(void)
{
    struct ts t;
    ts_setup(&t);
    struct gpsm sm = { .name = "ts" };

    gpsm_init(&sm, ts_table, TS_NR, &t, TS_A);
    ts_reset(&t);

    /* D.enter -> C, C.enter -> B, B lands; from == A for every enter */
    t.enter_redir[TS_D] = TS_C;
    t.enter_redir[TS_C] = TS_B;
    if (ts_failed(gpsm_set(&sm, TS_D)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_B)  return EXIT_FAILURE;
    if (t.leave_n[TS_A] != 1)  return EXIT_FAILURE;
    if (t.enter_n[TS_D] != 1 || t.enter_n[TS_C] != 1 || t.enter_n[TS_B] != 1)  return EXIT_FAILURE;
    if (t.enter_from[TS_D] != TS_A || t.enter_from[TS_C] != TS_A || t.enter_from[TS_B] != TS_A)  return EXIT_FAILURE;
    /* guard is NOT re-run on a redirect into B */
    if (t.guard_n != 0)  return EXIT_FAILURE;
    /* intermediates were never the state: no leave for D or C */
    if (t.leave_n[TS_D] != 0 || t.leave_n[TS_C] != 0)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int gpsm_test_recover(void)
{
    struct ts t;
    ts_setup(&t);
    struct gpsm sm = { .name = "ts", .recover = ts_recover };

    gpsm_init(&sm, ts_table, TS_NR, &t, TS_A);
    ts_reset(&t);

    /* cycle: D -> C -> D; recover lands us on A */
    t.enter_redir[TS_D] = TS_C;
    t.enter_redir[TS_C] = TS_D;
    t.recover_ret = TS_A;
    if (ts_failed(gpsm_set(&sm, TS_D)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;
    if (t.recover_n != 1 || t.recover_from != TS_A)  return EXIT_FAILURE;
    if (t.recover_cur != TS_C || t.recover_bad != TS_D)  return EXIT_FAILURE;

    /* illegal redirect: D.enter -> A, but A is not enterable from A; recover -> C */
    ts_reset(&t);
    t.enter_redir[TS_D] = TS_A;
    t.enter_redir[TS_C] = GPSM_NO_STATE;
    t.recover_ret = TS_C;
    if (ts_failed(gpsm_set(&sm, TS_D)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_C)  return EXIT_FAILURE;
    if (t.recover_n != 1 || t.recover_cur != TS_D || t.recover_bad != TS_A)  return EXIT_FAILURE;

    /* recover requests exit: error propagates, state left at the stalled cur (D) */
    if (ts_failed(gpsm_set(&sm, TS_A)))  return EXIT_FAILURE;  /* C -> A */
    ts_reset(&t);
    t.enter_redir[TS_D] = TS_A;
    t.recover_ret = GPSM_NO_STATE;
    if (ts_code(gpsm_set(&sm, TS_D)) != _CERR_REQUEST_EXIT)  return EXIT_FAILURE;
    if (t.recover_n != 1 || gpsm_get(&sm) != TS_D)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int gpsm_test_swap_tick(void)
{
    struct ts t;
    ts_setup(&t);
    struct gpsm sm = { .name = "ts" };

    gpsm_init(&sm, ts_table, TS_NR, &t, TS_A);
    ts_reset(&t);

    /* tick dispatches to the current state */
    gpsm_tick(&sm);
    if (t.tick_n[TS_A] != 1)  return EXIT_FAILURE;

    /* A -> A illegal in the main table */
    if (ts_code(gpsm_set(&sm, TS_A)) != _CERR_INVALID_OPERATION)  return EXIT_FAILURE;

    /* swap to a table where A self-loops; state index is preserved */
    gpsm_swap_table(&sm, ts_table_alt, TS_NR);
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;
    if (ts_failed(gpsm_set(&sm, TS_A)))  return EXIT_FAILURE;
    if (gpsm_get(&sm) != TS_A)  return EXIT_FAILURE;

    /* a state with no callbacks still transitions and ticks safely */
    static const struct gpsm_state null_table[2] = {
        [0] = { .name = "n0", .valid_from = GPSM_FROM(0, 1) },
        [1] = { .name = "n1", .valid_from = GPSM_FROM(0, 1) },
    };
    struct gpsm nsm = { .name = "null" };
    if (ts_failed(gpsm_init(&nsm, null_table, 2, NULL, 0)))  return EXIT_FAILURE;
    gpsm_tick(&nsm);
    if (ts_failed(gpsm_set(&nsm, 1)) || gpsm_get(&nsm) != 1)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "gpsm basic/valid-from/self-loop", .test = gpsm_test_basic },
    { .name = "gpsm guard abort/redirect", .test = gpsm_test_guard },
    { .name = "gpsm enter redirect chain", .test = gpsm_test_enter_chain },
    { .name = "gpsm recover cycle/illegal/exit", .test = gpsm_test_recover },
    { .name = "gpsm swap/tick/null", .test = gpsm_test_swap_tick },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
