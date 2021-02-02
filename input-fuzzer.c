#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "messagebus.h"
#include "input.h"

static struct message_source fuzzer_source = {
    .name = "fuzzer",
    .desc = "random input generator",
    .type = MST_FUZZER,
};

static unsigned long paused;
static bool enabled;

void fuzzer_input_step(void)
{
    int mode, bit, off, count;
    struct message_input mi;
    unsigned long *mask;

    if (!enabled)
        return;

    if (paused > 0) {
        paused--;
        return;
    }

    mode = rand();
    if (mode & 1) {
        mode >>= 1;
        paused = mode & 0xf;
        return;
    }
    mode >>= 1;

    memset(&mi, 0, sizeof(mi));
    mask = (unsigned long *)&mi;

    count = (mode & 0xf);
    mode >>= 4;
    for (; count >= 0; count--) {
        if (!mode)
            mode = rand();
        bit  = (mode & 0x3f);
        mode >>= 6;
        off = bit / (sizeof(*mask) * 8);
        mask[off] |= 1ul << (bit - off * sizeof(*mask) * 8);
    }

    if (!mode)
        mode = rand();

    count = (mode & 0xf);
    mode >>= 4;
    for (; count >= 0; count--) {
        if (!mode)
            mode = rand();
        switch (mode & 0x7) {
            case 0:
                mi.delta_lx = drand48() * 2 - 1;
                break;
            case 1:
                mi.delta_ly = drand48() * 2 - 1;
                break;
            case 2:
                mi.delta_rx = drand48() * 2 - 1;
                break;
            case 3:
                mi.delta_rx = drand48() * 2 - 1;
                break;
            case 4:
                mi.trigger_l = drand48() * 2 - 1;
                break;
            case 5:
                mi.trigger_r = drand48() * 2 - 1;
                break;
            case 6:
                mi.x = rand();
                break;
            case 7:
                mi.x = rand();
                break;
        }
    }

    mi.focus_next = mi.focus_prev = mi.verboser = mi.volume_up = mi.resize = mi.fullscreen = mi.exit = 0;
    message_input_send(&mi, &fuzzer_source);
}

static int fuzzer_handle_command(struct message *m, void *data)
{
    if (m->cmd.toggle_fuzzer)
        enabled = true;
    return 0;
}

void fuzzer_input_init(void)
{
    subscribe(MT_COMMAND, fuzzer_handle_command, NULL);
}
