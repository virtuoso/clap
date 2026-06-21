// SPDX-License-Identifier: Apache-2.0
#include "memory.h"
#include "script/frame.h"

struct script_frame {
    struct list     entry;
    UmkaFuncContext fnctx;
    UmkaAny         upvalue;
    bool            cancelled;
};

struct script_frame *script_frame_capture(Umka *vm, struct list *frames, UmkaStackSlot *params,
                                          int idx)
{
    const UmkaClosure *cb = (const UmkaClosure *)umkaGetParam(params, idx);
    const UmkaType *cb_type = umkaGetParamType(params, idx);

    struct script_frame *frame = mem_alloc(sizeof(*frame), .zero = 1);
    if (!frame)             return NULL;

    umkaMakeFuncContext(vm, cb_type, cb->entryOffset, &frame->fnctx);
    *umkaGetUpvalue(frame->fnctx.params) = cb->upvalue;
    frame->upvalue = cb->upvalue;

    /* Pin the closure's captured state so it survives past this call */
    if (cb->upvalue.data)   umkaIncRef(vm, cb->upvalue.data);

    list_append(frames, &frame->entry);

    return frame;
}

/* Unlink and free a callback, dropping the captured state it pinned */
static void script_frame_free(Umka *vm, struct script_frame *frame)
{
    list_del(&frame->entry);
    if (umkaAlive(vm) && frame->upvalue.data)   umkaDecRef(vm, frame->upvalue.data);
    mem_free(frame);
}

bool script_frame_cancel(struct list *frames, struct script_frame *target)
{
    struct script_frame *frame;

    list_for_each_entry(frame, frames, entry)
        if (frame == target) {
            /*
             * Only marked: a callback may cancel itself or its neighbour
             * while script_frames_run() walks the list, so nothing is
             * unlinked until no callback is mid-call
             */
            frame->cancelled = true;
            return true;
        }

    return false;
}

/* Reap the cancelled callbacks; called only when no callback is mid-call */
static void script_frames_reap(Umka *vm, struct list *frames)
{
    struct script_frame *frame, *iter;

    list_for_each_entry_iter(frame, iter, frames, entry)
        if (frame->cancelled)   script_frame_free(vm, frame);
}

bool script_frames_run(Umka *vm, struct list *frames)
{
    struct script_frame *frame;

    script_frames_reap(vm, frames);
    if (!umkaAlive(vm))     return false;

    list_for_each_entry(frame, frames, entry) {
        if (frame->cancelled)       continue;

        /* Each call consumes one reference to the captured state */
        if (frame->upvalue.data)    umkaIncRef(vm, frame->upvalue.data);

        umkaCall(vm, &frame->fnctx);
        if (!umkaAlive(vm))         return false;
    }

    return true;
}

void script_frames_release(Umka *vm, struct list *frames)
{
    struct script_frame *frame, *iter;

    list_for_each_entry_iter(frame, iter, frames, entry)
        script_frame_free(vm, frame);
}
