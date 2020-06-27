#include <stdbool.h>
#include <string.h>
#include "common.h"
#include "messagebus.h"
#include "input.h"

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
        if (e->ctrlKey)
            mi.focus_cancel = 1;
        if (e->shiftKey)
            mi.focus_prev = 1;
        else
            mi.focus_next = 1;
        break;
    case 112: /* F1 */
        mi.fullscreen = 1;
        break;
    case 121: /* F10 */
        mi.autopilot = 1;
        break;
    case 123: /* F12 */
        mi.verboser = 1;
        break;
    };
    message_input_send(&mi, &keyboard_source);

    return true;
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
    mi.delta_x = e->deltaX;
    mi.delta_y = e->deltaY;
    mi.delta_z = e->deltaZ;
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
    int ret;

    ret = emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback);
    ret = emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, key_callback);
    ret = emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, wheel_callback);
    ret = emscripten_set_resize_callback("#canvas", 0, 1, resize_callback);
    return ret;
}
