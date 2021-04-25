#include <stdbool.h>
#include <string.h>
#include "common.h"
#include "messagebus.h"
#include "input.h"
#include "input-joystick.h"

static struct message_source keyboard_source = {
    .name   = "keyboard",
    .desc   = "keyboard and mouse",
    .type   = MST_KEYBOARD,
};

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

    memset(&mi, 0, sizeof(mi));
    trace("%s, key: \"%s\", code: \"%s\", location: %lu,%s%s%s%s repeat: %d, locale: \"%s\", char: \"%s\", charCode: %lu, keyCode: %lu, which: %lu\n",
          emscripten_event_type_to_string(eventType), e->key, e->code, e->location,
          e->ctrlKey ? " CTRL" : "", e->shiftKey ? " SHIFT" : "", e->altKey ? " ALT" : "", e->metaKey ? " META" : "",
          e->repeat, e->locale, e->charValue, e->charCode, e->keyCode, e->which);
    if (eventType == EMSCRIPTEN_EVENT_KEYUP)
        return true;

    switch (e->keyCode) {
    case 9: /* Tab */
        mi.tab = 1;
        break;
    case 39: /* ArrowRight */
        if (e->shiftKey)
            mi.yaw_right = 1;
        else
            mi.right = 1;
        break;
    case 37: /* ArrowLeft */
        if (e->shiftKey)
            mi.yaw_left = 1;
        else
            mi.left = 1;
        break;
    case 40: /* ArrowDown */
        if (e->shiftKey)
            mi.pitch_down = 1;
        else
            mi.down = 1;
        break;
    case 38: /* ArrowUp */
        if (e->shiftKey)
            mi.pitch_up = 1;
        else
            mi.up = 1;
        break;
    case 32:
        /*if (e->ctrlKey)
            mi.focus_cancel = 1;
        if (e->shiftKey)
            mi.focus_prev = 1;
        else
            mi.focus_next = 1;*/
        mi.space = 1;
        break;
    case 77: /* m */
        mi.menu_toggle = 1;
        break;
    case 112: /* F1 */
        mi.fullscreen = 1;
        break;
    case 113: /* F2 */
        mi.volume_down = 1;
        break;
    case 114: /* F3 */
        mi.volume_up = 1;
        break;
    case 121: /* F10 */
        mi.autopilot = 1;
        break;
    case 123: /* F12 */
        mi.verboser = 1;
        break;
    default:
        /* don't send empty messages */
        return true;
    };
    message_input_send(&mi, &keyboard_source);

    return true;
}

static EM_BOOL gamepad_callback(int type, const EmscriptenGamepadEvent *e, void *data)
{
    //dbg("### GAMEPAD event: connected: %d index: %d nr_axes: %d nr_buttons: %d id: '%s' mapping: '%s'\n",
    //    e->connected, e->index, e->numAxes, e->numButtons, e->id, e->mapping);

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
}

void www_joysticks_poll(void)
{
    struct message_input mi;
    int i, nr_joys, ret;

    ret = emscripten_sample_gamepad_data();
    if (ret)
        return;

    nr_joys = min(emscripten_get_num_gamepads(), NR_JOYS);

    for (i = 0; i < nr_joys; i++) {
        EmscriptenGamepadEvent ge;
        char btn[64];
        int b;

        ret = emscripten_get_gamepad_status(i, &ge);
        if (ret)
            continue;

        for (b = 0; b < ge.numButtons; b++)
            btn[b] = !!ge.digitalButton[b];
        joystick_axes_update(i, ge.axis, ge.numAxes);
        joystick_buttons_update(i, btn, ge.numButtons);
        joystick_abuttons_update(i, ge.analogButton, ge.numButtons);
    }
}


static EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent *e, void *userData)
{
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
    message_input_send(&mi, &keyboard_source);

    return true;
}

static EM_BOOL click_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
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
    message_input_send(&mi, &keyboard_source);

    return true;
}

static EM_BOOL mousemove_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
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
    message_input_send(&mi, &keyboard_source);

    return true;
}

static EM_BOOL resize_callback(int eventType, const EmscriptenUiEvent *e, void *userData)
{
    struct message_input mi;
    memset(&mi, 0, sizeof(mi));
    mi.resize = 1;
    mi.x      = e->windowInnerWidth;
    mi.y      = e->windowInnerHeight;

    message_input_send(&mi, &keyboard_source); /* XXX: source */

    return 0;
}

int platform_input_init(void)
{
    CHECK0(emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback));
    CHECK0(emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback));
    CHECK0(emscripten_set_gamepadconnected_callback(NULL, 1, gamepad_callback));
    CHECK0(emscripten_set_gamepaddisconnected_callback(NULL, 1, gamepad_callback));
    CHECK0(emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, wheel_callback));
    CHECK0(emscripten_set_click_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, click_callback));
    CHECK0(emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, mousemove_callback));
    CHECK0(emscripten_set_resize_callback("#canvas", 0, 1, resize_callback));
    return 0;
}
