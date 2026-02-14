#include <emscripten/key_codes.h>
#include "common.h"
#include "ui-debug.h"

static struct backend_data {
    struct clap_context *ctx;
    double time;
    struct ImGuiContext *igctx;
    struct ImGuiIO *io;
} bd;

bool __ui_set_mouse_position(unsigned int x, unsigned int y)
{
    ImGuiIO_AddMousePosEvent(bd.io, x, y);

    return false;
}

bool __ui_set_mouse_click(unsigned int button, bool down)
{
    ImGuiIO_AddMouseButtonEvent(bd.io, button, down);

    return false;
}

bool __ui_mouse_event_wheel(double dx, double dy)
{
    if (!__ui_mouse_event_propagate())  return false;

    ImGuiIO_AddMouseWheelEvent(bd.io, (float)dx, (float)dy);

    return true;
}

static ImGuiKey keycode_to_imgui_key(int keycode)
{
    switch (keycode) {
        case DOM_VK_TAB:           return ImGuiKey_Tab;
        case DOM_VK_LEFT:          return ImGuiKey_LeftArrow;
        case DOM_VK_RIGHT:         return ImGuiKey_RightArrow;
        case DOM_VK_UP:            return ImGuiKey_UpArrow;
        case DOM_VK_DOWN:          return ImGuiKey_DownArrow;
        case DOM_VK_PAGE_UP:       return ImGuiKey_PageUp;
        case DOM_VK_PAGE_DOWN:     return ImGuiKey_PageDown;
        case DOM_VK_HOME:          return ImGuiKey_Home;
        case DOM_VK_END:           return ImGuiKey_End;
        case DOM_VK_INSERT:        return ImGuiKey_Insert;
        case DOM_VK_DELETE:        return ImGuiKey_Delete;
        case DOM_VK_BACK_SPACE:    return ImGuiKey_Backspace;
        case DOM_VK_SPACE:         return ImGuiKey_Space;
        case DOM_VK_ENTER:         return ImGuiKey_Enter;
        case DOM_VK_ESCAPE:        return ImGuiKey_Escape;
        case DOM_VK_QUOTE:         return ImGuiKey_Apostrophe;
        case DOM_VK_COMMA:         return ImGuiKey_Comma;
        case DOM_VK_HYPHEN_MINUS:  return ImGuiKey_Minus;
        case DOM_VK_PERIOD:        return ImGuiKey_Period;
        case DOM_VK_SLASH:         return ImGuiKey_Slash;
        case DOM_VK_SEMICOLON:     return ImGuiKey_Semicolon;
        case DOM_VK_EQUALS:        return ImGuiKey_Equal;
        case DOM_VK_OPEN_BRACKET:  return ImGuiKey_LeftBracket;
        case DOM_VK_BACK_SLASH:    return ImGuiKey_Backslash;
        case DOM_VK_CLOSE_BRACKET: return ImGuiKey_RightBracket;
        case DOM_VK_BACK_QUOTE:    return ImGuiKey_GraveAccent;
        case DOM_VK_CAPS_LOCK:     return ImGuiKey_CapsLock;
        case DOM_VK_SCROLL_LOCK:   return ImGuiKey_ScrollLock;
        case DOM_VK_NUM_LOCK:      return ImGuiKey_NumLock;
        case DOM_VK_PRINTSCREEN:   return ImGuiKey_PrintScreen;
        case DOM_VK_PAUSE:         return ImGuiKey_Pause;
        case DOM_VK_NUMPAD0:       return ImGuiKey_Keypad0;
        case DOM_VK_NUMPAD1:       return ImGuiKey_Keypad1;
        case DOM_VK_NUMPAD2:       return ImGuiKey_Keypad2;
        case DOM_VK_NUMPAD3:       return ImGuiKey_Keypad3;
        case DOM_VK_NUMPAD4:       return ImGuiKey_Keypad4;
        case DOM_VK_NUMPAD5:       return ImGuiKey_Keypad5;
        case DOM_VK_NUMPAD6:       return ImGuiKey_Keypad6;
        case DOM_VK_NUMPAD7:       return ImGuiKey_Keypad7;
        case DOM_VK_NUMPAD8:       return ImGuiKey_Keypad8;
        case DOM_VK_NUMPAD9:       return ImGuiKey_Keypad9;
        case DOM_VK_DECIMAL:       return ImGuiKey_KeypadDecimal;
        case DOM_VK_DIVIDE:        return ImGuiKey_KeypadDivide;
        case DOM_VK_MULTIPLY:      return ImGuiKey_KeypadMultiply;
        case DOM_VK_SUBTRACT:      return ImGuiKey_KeypadSubtract;
        case DOM_VK_ADD:           return ImGuiKey_KeypadAdd;
        case DOM_VK_SHIFT:         return ImGuiKey_LeftShift;
        case DOM_VK_CONTROL:       return ImGuiKey_LeftCtrl;
        case DOM_VK_ALT:           return ImGuiKey_LeftAlt;
        case DOM_VK_WIN:           return ImGuiKey_LeftSuper;
        case DOM_VK_CONTEXT_MENU:  return ImGuiKey_Menu;
        case DOM_VK_0:             return ImGuiKey_0;
        case DOM_VK_1:             return ImGuiKey_1;
        case DOM_VK_2:             return ImGuiKey_2;
        case DOM_VK_3:             return ImGuiKey_3;
        case DOM_VK_4:             return ImGuiKey_4;
        case DOM_VK_5:             return ImGuiKey_5;
        case DOM_VK_6:             return ImGuiKey_6;
        case DOM_VK_7:             return ImGuiKey_7;
        case DOM_VK_8:             return ImGuiKey_8;
        case DOM_VK_9:             return ImGuiKey_9;
        case DOM_VK_A:             return ImGuiKey_A;
        case DOM_VK_B:             return ImGuiKey_B;
        case DOM_VK_C:             return ImGuiKey_C;
        case DOM_VK_D:             return ImGuiKey_D;
        case DOM_VK_E:             return ImGuiKey_E;
        case DOM_VK_F:             return ImGuiKey_F;
        case DOM_VK_G:             return ImGuiKey_G;
        case DOM_VK_H:             return ImGuiKey_H;
        case DOM_VK_I:             return ImGuiKey_I;
        case DOM_VK_J:             return ImGuiKey_J;
        case DOM_VK_K:             return ImGuiKey_K;
        case DOM_VK_L:             return ImGuiKey_L;
        case DOM_VK_M:             return ImGuiKey_M;
        case DOM_VK_N:             return ImGuiKey_N;
        case DOM_VK_O:             return ImGuiKey_O;
        case DOM_VK_P:             return ImGuiKey_P;
        case DOM_VK_Q:             return ImGuiKey_Q;
        case DOM_VK_R:             return ImGuiKey_R;
        case DOM_VK_S:             return ImGuiKey_S;
        case DOM_VK_T:             return ImGuiKey_T;
        case DOM_VK_U:             return ImGuiKey_U;
        case DOM_VK_V:             return ImGuiKey_V;
        case DOM_VK_W:             return ImGuiKey_W;
        case DOM_VK_X:             return ImGuiKey_X;
        case DOM_VK_Y:             return ImGuiKey_Y;
        case DOM_VK_Z:             return ImGuiKey_Z;
        case DOM_VK_F1:            return ImGuiKey_F1;
        case DOM_VK_F2:            return ImGuiKey_F2;
        case DOM_VK_F3:            return ImGuiKey_F3;
        case DOM_VK_F4:            return ImGuiKey_F4;
        case DOM_VK_F5:            return ImGuiKey_F5;
        case DOM_VK_F6:            return ImGuiKey_F6;
        case DOM_VK_F7:            return ImGuiKey_F7;
        case DOM_VK_F8:            return ImGuiKey_F8;
        case DOM_VK_F9:            return ImGuiKey_F9;
        case DOM_VK_F10:           return ImGuiKey_F10;
        case DOM_VK_F11:           return ImGuiKey_F11;
        case DOM_VK_F12:           return ImGuiKey_F12;
        default:                   return ImGuiKey_None;
    }
}

bool __ui_set_key(int key_code, const char *key, bool down)
{
    if (!__ui_mouse_event_propagate()) return false;

    ImGuiKey igkey = keycode_to_imgui_key(key_code);
    if (igkey != ImGuiKey_None)
        ImGuiIO_AddKeyEvent(bd.io, igkey, down);

    if (down && key[0] && !key[1])
        ImGuiIO_AddInputCharacter(bd.io, key[0]);

    return true;
}

void ui_ig_new_frame(void)
{
    struct timespec delta = clap_get_fps_delta(bd.ctx);

    double dt = delta.tv_sec + (delta.tv_nsec / (double)NSEC_PER_SEC);
    bd.io->DeltaTime = dt;
}

void ui_ig_init_for_emscripten(struct clap_context *clap_ctx, struct ImGuiContext *igctx,
                               struct ImGuiIO *io)
{
    bd.ctx = clap_ctx;
    bd.igctx = igctx;
    bd.io = io;
}
