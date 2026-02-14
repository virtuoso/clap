// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "messagebus.h"
#include "input.h"
#include "input-joystick.h"
#include "input-keyboard.h"
#include "ui-debug.h"
#include "ui-imgui-www.h"

static struct message_source keyboard_source = {
    .name   = "keyboard",
    .desc   = "keyboard and mouse",
    .type   = MST_KEYBOARD,
};

static struct clap_context *clap_ctx;

static inline const char *emscripten_event_type_to_string(int eventType)
{
    const char *events[] = {"(invalid)", "(none)", "keypress", "keydown", "keyup", "click", "mousedown", "mouseup", "dblclick", "mousemove", "wheel", "resize",
                            "scroll", "blur", "focus", "focusin", "focusout", "deviceorientation", "devicemotion", "orientationchange", "fullscreenchange", "pointerlockchange",
                            "visibilitychange", "touchstart", "touchend", "touchmove", "touchcancel", "gamepadconnected", "gamepaddisconnected", "beforeunload",
                            "batterychargingchange", "batterylevelchange", "webglcontextlost", "webglcontextrestored", "mouseenter", "mouseleave", "mouseover", "mouseout", "(invalid)"};
    ++eventType;
    if (eventType < 0)
        eventType = 0;
    if (eventType >= sizeof(events) / sizeof(events[0]))
        eventType = sizeof(events) / sizeof(events[0]) - 1;

    return events[eventType];
}

static EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData)
{
    struct message_input mi;
    int press, mods = 0;

    switch (eventType) {
        case EMSCRIPTEN_EVENT_KEYUP:
            press = KEY_RELEASE;
            break;
        case EMSCRIPTEN_EVENT_KEYPRESS:
            press = KEY_HOLD;
            break;
        case EMSCRIPTEN_EVENT_KEYDOWN:
            press = KEY_PRESS;
            break;
    }

    if (__ui_set_key(e->keyCode, e->key, press == KEY_PRESS || press == KEY_HOLD))
        return true;

    memset(&mi, 0, sizeof(mi));
    trace("%s, key: \"%s\", code: \"%s\", location: %u,%s%s%s%s repeat: %d, locale: \"%s\", char: \"%s\", charCode: %u, keyCode: %u, which: %u\n",
          emscripten_event_type_to_string(eventType), e->key, e->code, e->location,
          e->ctrlKey ? " CTRL" : "", e->shiftKey ? " SHIFT" : "", e->altKey ? " ALT" : "", e->metaKey ? " META" : "",
          e->repeat, e->locale, e->charValue, e->charCode, e->keyCode, e->which);
    mods |= (!!e->shiftKey) << 0;
    mods |= (!!e->ctrlKey)  << 1;
    mods |= (!!e->altKey)   << 2;

    switch (e->keyCode) {
    case 32:
        /*if (e->ctrlKey)
            mi.focus_cancel = 1;
        if (e->shiftKey)
            mi.focus_prev = 1;
        else
            mi.focus_next = 1;*/
        mi.space = 1;
        break;
    default:
        key_event(clap_ctx, &keyboard_source, e->keyCode, e->code, mods, press);
        /* don't send empty messages */
        return true;
    };

    message_input_send(clap_ctx, &mi, &keyboard_source);

    return true;
}

struct touchpoint {
    struct list entry;
    int         x, y;
    int         orig_x, orig_y;
    int         id;
    int         lifetime;
    bool        imgui;
    bool        pop;
    /* XXX: padding */
};

#define NR_TOUCHPOINTS 32
struct touch {
    struct touchpoint   pool[NR_TOUCHPOINTS];
    uint32_t            used_mask;
    struct list         head;
    int                 w, h;
};

static struct touchpoint *touch_alloc(struct touch *touch)
{
    if (touch->used_mask == ~0u)
        return NULL;

    int slot = __builtin_ctz(~touch->used_mask);
    touch->used_mask |= 1u << slot;
    struct touchpoint *pt = &touch->pool[slot];
    memset(pt, 0, sizeof(*pt));

    /* make sure used_mask store happens before the slot is used */
    barrier();
    list_append(&touch->head, &pt->entry);

    return pt;
}

static void touch_free(struct touch *touch, struct touchpoint *pt)
{
    list_del(&pt->entry);

    /* make sure slot is unused before used_mask store */
    barrier();

    int slot = pt - touch->pool;
    if (slot >= array_size(touch->pool)) {
        err("touchpoint out of bounds: %d vs %zu\n", slot, array_size(touch->pool));
        return;
    }

    touch->used_mask &= ~(1u << slot);
}

static struct touchpoint *touch_find(struct touch *touch, long id)
{
    struct touchpoint *pt;

    list_for_each_entry(pt, &touch->head, entry)
        if (id == pt->id)
            return pt;

    return NULL;
}

static void touch_push(struct touch *touch, int id, int x, int y)
{
    struct touchpoint *pt = touch_find(touch, id);

    if (pt) {
        pt->x = x;
        pt->y = y;
        return;
    }

    pt = touch_alloc(touch);
    if (!pt)
        return;

    pt->id = id;
    pt->x = x;
    pt->y = y;
    pt->orig_x = x;
    pt->orig_y = y;
}

static EM_BOOL touchstart_callback(int type, const EmscriptenTouchEvent *e, void *userData)
{
    struct touch *touch = userData;
    int i;

    // dbg("touchstart_callback: %d: '%s' num_touches: %d\n", type, emscripten_event_type_to_string(type), e->numTouches);
    for (i = 0; i < e->numTouches; ++i) {
        const EmscriptenTouchPoint *t = &e->touches[i];

        // dbg("  %d: screen: (%d,%d), client: (%d,%d), page: (%d,%d), isChanged: %d, onTarget: %d, canvas: (%d, %d)\n",
        //     t->identifier, t->screenX, t->screenY, t->clientX, t->clientY, t->pageX, t->pageY, t->isChanged, t->onTarget, t->canvasX, t->canvasY);
        touch_push(touch, t->identifier, t->pageX, t->pageY);

        /*
         * assume that only the first (preferably single) touch is used to
         * interact with ImGui
         */
        if (!i) {
            __ui_set_mouse_position(t->pageX, t->pageY);
            struct touchpoint *pt = touch_find(touch, t->identifier);
            if (pt)
                pt->imgui = true;
        }
    }

    return true;
}

static EM_BOOL touchend_callback(int type, const EmscriptenTouchEvent *e, void *userData)
{
    struct touch *touch = userData;
    int i;

    for (i = 0; i < e->numTouches; ++i) {
        const EmscriptenTouchPoint *t = &e->touches[i];

        struct touchpoint *pt = touch_find(touch, t->identifier);
        if (!pt)
            continue;

        if (pt->imgui) {
            __ui_set_mouse_click(0, false);
        } else if (pt->x == pt->orig_x && pt->y == pt->orig_y && pt->lifetime < 10) {
            /* a short tap without movement -> mouse click */
            struct message_input mi = {};
            mi.mouse_click = 1;
            mi.x = pt->x;
            mi.y = pt->y;
            message_input_send(clap_ctx, &mi, &keyboard_source);
        }

        pt->pop = true;
        // dbg("  %d: screen: (%d,%d), client: (%d,%d), page: (%d,%d), isChanged: %d, onTarget: %d, canvas: (%d, %d)\n",
        //     t->identifier, t->screenX, t->screenY, t->clientX, t->clientY, t->pageX, t->pageY, t->isChanged, t->onTarget, t->canvasX, t->canvasY);
    }

    return true;
}

static EM_BOOL touch_callback(int type, const EmscriptenTouchEvent *e, void *userData)
{
    struct touch *touch = userData;
    int i;

    for (i = 0; i < e->numTouches; ++i) {
        const EmscriptenTouchPoint *t = &e->touches[i];

        struct touchpoint *pt = touch_find(touch, t->identifier);

        if (!pt)
            continue;

        if (!i && pt->imgui)
            __ui_set_mouse_position(t->pageX, t->pageY);

        // dbg("  %d: screen: (%d,%d), client: (%d,%d), page: (%d,%d), isChanged: %d, onTarget: %d, canvas: (%d, %d)\n",
        //     t->identifier, t->screenX, t->screenY, t->clientX, t->clientY, t->pageX, t->pageY, t->isChanged, t->onTarget, t->canvasX, t->canvasY);
        pt->x = t->pageX;
        pt->y = t->pageY;
    }

    return true;
}

static EM_BOOL gamepad_callback(int type, const EmscriptenGamepadEvent *e, void *data)
{
    dbg("### GAMEPAD event: connected: %d index: %d nr_axes: %d nr_buttons: %d id: '%s' mapping: '%s'\n",
       e->connected, e->index, e->numAxes, e->numButtons, e->id, e->mapping);

    joystick_name_update(e->index, e->connected ? e->id : NULL);
    if (e->connected) {
        EmscriptenGamepadEvent ge;
        int ret;

        ret = emscripten_get_gamepad_status(e->index, &ge);
        if (!ret)
            joystick_axes_update(e->index, ge.axis, ge.numAxes);
        else
            joystick_axes_update(e->index, e->axis, e->numAxes);
    }

    return true;
}

static struct touch touch;

#ifndef CONFIG_FINAL
void input_debug(void)
{
    debug_module *dbgm = ui_igBegin(DEBUG_INPUT, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        struct touchpoint *pt;
        igText("list_empty: %d used_mask: %08x", list_empty(&touch.head), touch.used_mask);
        list_for_each_entry(pt, &touch.head, entry) {
            igText("pt%d: %d,%d - %d,%d lifetime: %d imgui: %d pop: %d",
                pt->id, pt->x, pt->y, pt->orig_x, pt->orig_y, pt->lifetime, pt->imgui, pt->pop
            );
        }

        for (int i = 0; i < NR_TOUCHPOINTS; i++) {
            if (!(touch.used_mask & (1u << i)))
                continue;

            igText("%d: lifetime: %d", i, touch.pool[i].lifetime);
        }
    }

    ui_igEnd(DEBUG_INPUT);
}
#endif /* CONFIG_FINAL */

void input_events_dispatch(void)
{
    struct message_input mi = {};
    struct touchpoint *pt, *ptit;

    list_for_each_entry_iter(pt, ptit, &touch.head, entry) {

        if (pt->pop) {
            touch_free(&touch, pt);
            continue;
        }

        if (pt->imgui) {
            if (__ui_mouse_event_propagate()) {
                __ui_set_mouse_click(0, true);
                continue;
            } else {
                pt->imgui = false;
            }
        }

        pt->lifetime++;
        if (pt->orig_x < touch.w / 2) {
            /* left stick */
            mi.delta_lx = (float)(pt->x - pt->orig_x) / touch.w * 8;
            mi.delta_ly = (float)(pt->y - pt->orig_y) / touch.h * 8;
        } else {
            /* right stick */
            mi.delta_rx = (float)(pt->x - pt->orig_x) / touch.w * 4;
            mi.delta_ry = (float)(pt->y - pt->orig_y) / touch.h * 4;
        }
    }
    message_input_send(clap_ctx, &mi, &keyboard_source);
}

void www_joysticks_poll(void)
{
    int i, nr_joys, ret;

    ret = emscripten_sample_gamepad_data();
    if (ret)
        return;

    nr_joys = min(emscripten_get_num_gamepads(), NR_JOYS);

    for (i = 0; i < nr_joys; i++) {
        EmscriptenGamepadEvent ge;
        double axes[CLAP_JOY_AXIS_COUNT];
        unsigned char buttons[CLAP_JOY_BTN_COUNT];
        int ret;

        ret = emscripten_get_gamepad_status(i, &ge);
        if (ret)
            continue;

        memset(axes, 0, sizeof(axes));
        memset(buttons, 0, sizeof(buttons));

        /* Standard mapping assumed for browser Gamepad API */
        /* TODO: Check ge.mapping (usually "standard") */

        if (ge.numAxes > 0) axes[CLAP_JOY_AXIS_LX] = ge.axis[0];
        if (ge.numAxes > 1) axes[CLAP_JOY_AXIS_LY] = ge.axis[1];
        if (ge.numAxes > 2) axes[CLAP_JOY_AXIS_RX] = ge.axis[2];
        if (ge.numAxes > 3) axes[CLAP_JOY_AXIS_RY] = ge.axis[3];
        /*
         * Emscripten/Browser doesn't usually have axes for triggers in the
         * axis array for standard mapping, they are often buttons 6 and 7
         * (analog)
         */

        /*
         * standard mapping buttons:
         * 0: A
         * 1: B
         * 2: X
         * 3: Y
         * 4: LB
         * 5: RB
         * 6: LT (analog)
         * 7: RT (analog)
         * 8: Back
         * 9: Start
         * 10: LThumb
         * 11: RThumb
         * 12: Dpad Up
         * 13: Dpad Down
         * 14: Dpad Left
         * 15: Dpad Right
         * 16: Home
         */

        if (ge.numButtons > 0) buttons[CLAP_JOY_BTN_A]          = !!ge.digitalButton[0];
        if (ge.numButtons > 1) buttons[CLAP_JOY_BTN_B]          = !!ge.digitalButton[1];
        if (ge.numButtons > 2) buttons[CLAP_JOY_BTN_X]          = !!ge.digitalButton[2];
        if (ge.numButtons > 3) buttons[CLAP_JOY_BTN_Y]          = !!ge.digitalButton[3];
        if (ge.numButtons > 4) buttons[CLAP_JOY_BTN_LB]         = !!ge.digitalButton[4];
        if (ge.numButtons > 5) buttons[CLAP_JOY_BTN_RB]         = !!ge.digitalButton[5];

        /* Triggers as axes */
        if (ge.numButtons > 6) axes[CLAP_JOY_AXIS_LT] = ge.analogButton[6];
        if (ge.numButtons > 7) axes[CLAP_JOY_AXIS_RT] = ge.analogButton[7];

        /* Triggers as buttons */
        if (ge.numButtons > 6) buttons[CLAP_JOY_BTN_LT] = !!ge.digitalButton[6];
        if (ge.numButtons > 7) buttons[CLAP_JOY_BTN_RT] = !!ge.digitalButton[7];

        if (ge.numButtons > 8) buttons[CLAP_JOY_BTN_BACK]       = !!ge.digitalButton[8];
        if (ge.numButtons > 9) buttons[CLAP_JOY_BTN_START]      = !!ge.digitalButton[9];
        if (ge.numButtons > 10) buttons[CLAP_JOY_BTN_LTHUMB]    = !!ge.digitalButton[10];
        if (ge.numButtons > 11) buttons[CLAP_JOY_BTN_RTHUMB]    = !!ge.digitalButton[11];
        if (ge.numButtons > 12) buttons[CLAP_JOY_BTN_DPAD_UP]   = !!ge.digitalButton[12];
        if (ge.numButtons > 13) buttons[CLAP_JOY_BTN_DPAD_DOWN] = !!ge.digitalButton[13];
        if (ge.numButtons > 14) buttons[CLAP_JOY_BTN_DPAD_LEFT] = !!ge.digitalButton[14];
        if (ge.numButtons > 15) buttons[CLAP_JOY_BTN_DPAD_RIGHT] = !!ge.digitalButton[15];
        if (ge.numButtons > 16) buttons[CLAP_JOY_BTN_GUIDE]     = !!ge.digitalButton[16];

        joystick_axes_update(i, axes, CLAP_JOY_AXIS_COUNT);
        joystick_buttons_update(i, buttons, CLAP_JOY_BTN_COUNT);
        /* joystick_abuttons_update not really needed if we map triggers to axes */
    }
}


static __unused EM_BOOL scroll_callback(int eventType, const EmscriptenUiEvent *e, void *userData)
{
    return true;
}

static EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent *e, void *userData)
{
    if (__ui_mouse_event_wheel(e->deltaX, -e->deltaY))
        return true;

    struct message_input mi;

    memset(&mi, 0, sizeof(mi));
    /*trace("%s, screen: (%ld,%ld), client: (%ld,%ld),%s%s%s%s button: %hu, buttons: %hu, canvas: (%ld,%ld), delta:(%g,%g,%g), deltaMode:%lu\n",
          emscripten_event_type_to_string(eventType), e->mouse.screenX, e->mouse.screenY, e->mouse.clientX, e->mouse.clientY,
          e->mouse.ctrlKey ? " CTRL" : "", e->mouse.shiftKey ? " SHIFT" : "", e->mouse.altKey ? " ALT" : "", e->mouse.metaKey ? " META" : "",
          e->mouse.button, e->mouse.buttons, e->mouse.canvasX, e->mouse.canvasY,
          (float)e->deltaX, (float)e->deltaY, (float)e->deltaZ, e->deltaMode);*/
    if (e->mouse.shiftKey) {
        mi.delta_rx = e->deltaX / 10;
        mi.delta_ry = e->deltaY;
    } else if (e->mouse.altKey || e->mouse.metaKey) {
        mi.delta_ry = e->deltaY;
    } else {
        mi.delta_lx = e->deltaX;
        mi.delta_ly = e->deltaY;
    }
    message_input_send(clap_ctx, &mi, &keyboard_source);

    return true;
}

static EM_BOOL mouseup_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    __ui_set_mouse_click(e->button, false);
    return true;
}

static EM_BOOL mousedown_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    __ui_set_mouse_click(e->button, true);
    return true;
}

static EM_BOOL click_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    if (__ui_mouse_event_propagate())
        return true;

    struct message_input mi;

    memset(&mi, 0, sizeof(mi));
    /*trace("%s, screen: (%ld,%ld), client: (%ld,%ld),%s%s%s%s button: %hu, buttons: %hu, canvas: (%ld,%ld), delta:(%g,%g,%g), deltaMode:%lu\n",
          emscripten_event_type_to_string(eventType), e->mouse.screenX, e->mouse.screenY, e->mouse.clientX, e->mouse.clientY,
          e->mouse.ctrlKey ? " CTRL" : "", e->mouse.shiftKey ? " SHIFT" : "", e->mouse.altKey ? " ALT" : "", e->mouse.metaKey ? " META" : "",
          e->mouse.button, e->mouse.buttons, e->mouse.canvasX, e->mouse.canvasY,
          (float)e->deltaX, (float)e->deltaY, (float)e->deltaZ, e->deltaMode);*/
    //dbg("### button: %hu buttons: %hu click: %d,%d\n", e->button, e->buttons, e->clientX, e->clientY);
    if (e->button == 0)
        mi.mouse_click = 1;
    else if (e->button == 1)
        mi.zoom = 1;
    mi.x = e->targetX;
    mi.y = e->targetY;
    message_input_send(clap_ctx, &mi, &keyboard_source);

    return true;
}

static EM_BOOL mousemove_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    if (__ui_set_mouse_position(e->targetX, e->targetY))
        return true;

    struct message_input mi;

    memset(&mi, 0, sizeof(mi));
    /*dbg("%s, screen: (%ld,%ld), client: (%ld,%ld),%s%s%s%s button: %hu, buttons: %hu, canvas: (%ld,%ld), delta:(%g,%g,%g), deltaMode:%lu\n",
          emscripten_event_type_to_string(eventType), e->screenX, e->screenY, e->clientX, e->clientY,
          e->ctrlKey ? " CTRL" : "", e->shiftKey ? " SHIFT" : "", e->altKey ? " ALT" : "", e->metaKey ? " META" : "",
          e->button, e->buttons, e->canvasX, e->canvasY);*/
    //dbg("### mousemove: %d,%d\n", e->clientX, e->clientY);
    mi.mouse_move = 1;
    mi.x = e->targetX;
    mi.y = e->targetY;
    message_input_send(clap_ctx, &mi, &keyboard_source);

    return true;
}

extern void www_call_resize(int w, int h);

static EM_BOOL resize_callback(int eventType, const EmscriptenUiEvent *e, void *userData)
{
    www_call_resize(e->windowInnerWidth, e->windowInnerHeight);

    return 0;
}

void touch_input_set_size(int width, int height)
{
    touch.w = width;
    touch.h = height;
}

int platform_input_init(struct clap_context *ctx)
{
    clap_ctx = ctx;
    list_init(&touch.head);
    touch.used_mask = 0;
    CHECK0(emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback));
    CHECK0(emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback));
    CHECK0(emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &touch, 1, touchstart_callback));
    CHECK0(emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &touch, 1, touchend_callback));
    CHECK0(emscripten_set_touchmove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &touch, 1, touch_callback));
    CHECK0(emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &touch, 1, touchend_callback));
    CHECK0(emscripten_set_gamepadconnected_callback(NULL, 1, gamepad_callback));
    CHECK0(emscripten_set_gamepaddisconnected_callback(NULL, 1, gamepad_callback));
    CHECK0(emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, wheel_callback));
    // CHECK0(emscripten_set_scroll_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, scroll_callback));
    CHECK0(emscripten_set_click_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, click_callback));
    CHECK0(emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, mouseup_callback));
    CHECK0(emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, mousedown_callback));
    CHECK0(emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, mousemove_callback));
    CHECK0(emscripten_set_resize_callback("#canvas", 0, 1, resize_callback));
    return 0;
}
