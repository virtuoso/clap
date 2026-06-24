// SPDX-License-Identifier: Apache-2.0
//
// Tests for the Umka embedding mechanisms the scripting subsystem relies on,
// exercised without the rest of the engine: two-layer module registration
// (umkaAddModule prototypes bound to umkaAddFunc externals), struct-handle
// marshalling, int arguments (ffi_int()), host context via metadata, and the
// frame callback list in script/frame.c that clap::on_frame() and
// Frame.cancel() are built on.

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "script/ffi.h"
#include "script/frame.h"
#include "test.h"
#include "umka_api.h"

struct script_test_ctx {
    struct list     frames;
    int             recorded[8];
    int             n;
};

static void t_record(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_test_ctx *tc = umkaGetMetadata(umkaGetInstance(result));
    int v = umkaGetParam(params, 0)->intVal;

    if (tc->n < (int)array_size(tc->recorded))  tc->recorded[tc->n++] = v;
}

/* record() through ffi_int(), the way the engine's FFI reads int arguments */
static void t_record_int(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_test_ctx *tc = umkaGetMetadata(umkaGetInstance(result));

    if (tc->n < (int)array_size(tc->recorded))  tc->recorded[tc->n++] = ffi_int(params, 0);
}

static void t_add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    int a = umkaGetParam(params, 0)->intVal;
    int b = umkaGetParam(params, 1)->intVal;

    umkaGetResult(params, result)->intVal = a + b;
}

/* Return a Thing (struct { _h: uint }) carrying an opaque handle */
static void t_make_thing(UmkaStackSlot *params, UmkaStackSlot *result)
{
    int id = umkaGetParam(params, 0)->intVal;

    *(uint64_t *)umkaGetResult(params, result)->ptrVal = (uint64_t)(1000 + id);
}

static void t_thing_id(UmkaStackSlot *params, UmkaStackSlot *result)
{
    uint64_t h = umkaGetParam(params, 0)->uintVal;

    umkaGetResult(params, result)->intVal = (int)(h - 1000);
}

/* clap::on_frame(): capture the closure onto the context's frame list */
static void t_on_frame(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    struct script_test_ctx *tc = umkaGetMetadata(umka);

    ffi_ret_ptr(params, result, script_frame_capture(umka, &tc->frames, params, 0));
}

/* Frame.cancel() */
static void t_frame_cancel(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_test_ctx *tc = umkaGetMetadata(umkaGetInstance(result));

    script_frame_cancel(&tc->frames, ffi_ptr(params, 0));
}

static const char *test_module =
    "type Thing* = struct { _h: uint }\n"
    "type Frame* = struct { _h: uint }\n"
    "fn record*(x: int)\n"
    "fn recordInt*(x: int)\n"
    "fn add*(a, b: int): int\n"
    "fn makeThing*(id: int): Thing\n"
    "fn thingId*(t: Thing): int\n"
    "fn onFrame*(cb: fn()): Frame\n"
    "fn frameCancel*(f: Frame)\n"
    "fn (t: ^Thing) id*(): int { return thingId(t^) }\n"
    "fn (f: ^Frame) cancel*() { frameCancel(f^) }\n";

static Umka *test_script_setup(struct script_test_ctx *tc, const char *prog)
{
    Umka *umka = umkaAlloc();
    if (!umka)  return NULL;

    if (!umkaInit(umka, "prog.um", prog, 1024 * 1024, NULL, 0, NULL, false, false, NULL)) {
        umkaFree(umka);
        return NULL;
    }

    list_init(&tc->frames);
    umkaSetMetadata(umka, tc);
    umkaAddFunc(umka, "record", t_record);
    umkaAddFunc(umka, "recordInt", t_record_int);
    umkaAddFunc(umka, "add", t_add);
    umkaAddFunc(umka, "makeThing", t_make_thing);
    umkaAddFunc(umka, "thingId", t_thing_id);
    umkaAddFunc(umka, "onFrame", t_on_frame);
    umkaAddFunc(umka, "frameCancel", t_frame_cancel);
    umkaAddModule(umka, "t.um", test_module);

    if (!umkaCompile(umka)) {
        umkaFree(umka);
        return NULL;
    }

    return umka;
}

static void test_script_teardown(struct script_test_ctx *tc, Umka *umka)
{
    script_frames_release(umka, &tc->frames);
    umkaFree(umka);
}

/* Externs, struct handles passed by value and returned, methods, metadata */
static int script_test_ffi(void)
{
    struct script_test_ctx tc = {};
    Umka *umka = test_script_setup(&tc,
        "import \"t.um\"\n"
        "fn main() {\n"
        "    t::record(t::add(3, 4))\n"          /* -> 7 */
        "    t::record(t::makeThing(5).id())\n"  /* -> 5 via struct handle + method */
        "}\n");
    if (!umka)                                                      return EXIT_FAILURE;

    int rc = umkaRun(umka);
    test_script_teardown(&tc, umka);

    if (rc != 0)                                                    return EXIT_FAILURE;
    if (tc.n != 2 || tc.recorded[0] != 7 || tc.recorded[1] != 5)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* ffi_int(): Umka's 64-bit ints past either end of int's range read as that end */
static int script_test_ffi_int(void)
{
    struct script_test_ctx tc = {};
    Umka *umka = test_script_setup(&tc,
        "import \"t.um\"\n"
        "fn main() {\n"
        "    t::recordInt(7)\n"
        "    t::recordInt(-7)\n"
        "    t::recordInt(2147483647)\n"
        "    t::recordInt(-2147483648)\n"
        "    t::recordInt(2147483648)\n"
        "    t::recordInt(-2147483649)\n"
        "    t::recordInt(4294967297)\n"      /* 1 if truncated */
        "    t::recordInt(-4294967297)\n"     /* -1 if truncated */
        "}\n");
    if (!umka)                              return EXIT_FAILURE;

    int rc = umkaRun(umka);
    test_script_teardown(&tc, umka);
    if (rc != 0)                            return EXIT_FAILURE;

    static const int expected[] = { 7, -7, INT_MAX, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MIN };
    if (tc.n != (int)array_size(expected))  return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(expected); i++)
        if (tc.recorded[i] != expected[i])  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* Capture a closure in main() and call it every frame, as the frame loop does */
static int script_test_closure(void)
{
    struct script_test_ctx tc = {};
    Umka *umka = test_script_setup(&tc,
        "import \"t.um\"\n"
        "var counter: int\n"
        "fn main() {\n"
        "    t::onFrame(fn() {\n"
        "        counter += 1\n"
        "        t::record(counter)\n"
        "    })\n"
        "}\n");
    if (!umka)                                                                              return EXIT_FAILURE;

    int rc = umkaRun(umka);
    for (int frame = 0; !rc && frame < 3; frame++)
        if (!script_frames_run(umka, &tc.frames))   rc = -1;

    test_script_teardown(&tc, umka);
    if (rc)                                                                                 return EXIT_FAILURE;

    /* The counter must persist across calls: 1, 2, 3 */
    if (tc.n != 3 || tc.recorded[0] != 1 || tc.recorded[1] != 2 || tc.recorded[2] != 3)     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A callback may cancel itself or its neighbour from inside its own call */
static int script_test_frame_cancel(void)
{
    struct script_test_ctx tc = {};
    Umka *umka = test_script_setup(&tc,
        "import \"t.um\"\n"
        "var self: t::Frame\n"
        "var other: t::Frame\n"
        "var count: int\n"
        "fn main() {\n"
        "    self = t::onFrame(fn() {\n"        /* runs once, then cancels itself */
        "        count += 1\n"
        "        t::record(count)\n"
        "        self.cancel()\n"
        "    })\n"
        "    t::onFrame(fn() {\n"               /* cancels the callback after it */
        "        t::record(20)\n"
        "        other.cancel()\n"
        "    })\n"
        "    other = t::onFrame(fn() { t::record(30) })\n"
        "}\n");
    if (!umka)                              return EXIT_FAILURE;

    int rc = umkaRun(umka);
    for (int frame = 0; !rc && frame < 3; frame++)
        if (!script_frames_run(umka, &tc.frames))   rc = -1;

    test_script_teardown(&tc, umka);
    if (rc)                                 return EXIT_FAILURE;

    /* frame 1: 1 and 20, the third callback already cancelled; then 20 twice */
    static const int expected[] = { 1, 20, 20, 20 };
    if (tc.n != (int)array_size(expected))  return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(expected); i++)
        if (tc.recorded[i] != expected[i])          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "script ffi", .test = script_test_ffi },
    { .name = "script ffi int", .test = script_test_ffi_int },
    { .name = "script closure", .test = script_test_closure },
    { .name = "script frame cancel", .test = script_test_frame_cancel },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
