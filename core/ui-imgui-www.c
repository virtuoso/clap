#include <string.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/key_codes.h>
#include "common.h"
#include "memory.h"
#include "ui-debug.h"

static struct backend_data {
    struct clap_context *ctx;
    double time;
    struct ImGuiContext *igctx;
    struct ImGuiIO *io;
    char *clipboard;    /* text last pasted from or copied to the browser */
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
        case DOM_VK_RETURN:        return ImGuiKey_Enter;
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

/*
 * ImGui derives io.KeyCtrl & co from the ImGuiMod_* keys. Every key event
 * carries the current modifier flags, so they are refreshed from those rather
 * than from the modifier keys' own presses, which never arrive while ImGui is
 * not listening: a modifier held down since before it started would go unseen.
 */
static void set_key_mods(const EmscriptenKeyboardEvent *e)
{
    ImGuiIO_AddKeyEvent(bd.io, ImGuiMod_Shift, e->shiftKey);
    ImGuiIO_AddKeyEvent(bd.io, ImGuiMod_Ctrl, e->ctrlKey);
    ImGuiIO_AddKeyEvent(bd.io, ImGuiMod_Alt, e->altKey);
    ImGuiIO_AddKeyEvent(bd.io, ImGuiMod_Super, e->metaKey);
}

bool __ui_set_key(const EmscriptenKeyboardEvent *e, bool down)
{
    bool capture = __ui_keyboard_event_propagate();

    /*
     * Releases go to ImGui even when it isn't listening: a key can end the
     * capture with its press, as the keys that leave a text field do, and a
     * release lost then leaves the key down, repeating into the next field
     * to take the keyboard. The game still gets them.
     */
    if (!capture && down)   return false;

    set_key_mods(e);

    ImGuiKey igkey = keycode_to_imgui_key(e->keyCode);
    if (igkey != ImGuiKey_None)
        ImGuiIO_AddKeyEvent(bd.io, igkey, down);

    if (down && e->key[0] && !e->key[1])
        ImGuiIO_AddInputCharacter(bd.io, e->key[0]);

    return capture;
}

/*
 * Clipboard: the browser only hands its contents out through a paste event,
 * which it dispatches right after a Ctrl+V keydown that kept its default
 * action and before the next frame renders. The hook stashes that text, so
 * ImGui's own Ctrl+V handling reads it back a moment later via the platform
 * getter; the setter mirrors ImGui's copies into the browser clipboard.
 */
EM_JS(void, www_clipboard_hook, (void), {
    window.addEventListener("paste", function(e) {
        var text = e.clipboardData ? e.clipboardData.getData("text/plain") : "";
        if (!text)  return;

        var ptr = stringToNewUTF8(text);
        ccall("www_ui_clipboard_paste", null, ["number"], [ptr]);
        _free(ptr);
        e.preventDefault();
    });
});

EM_JS(void, www_clipboard_write, (const char *text), {
    if (navigator.clipboard)
        navigator.clipboard.writeText(UTF8ToString(text)).catch(function() {});
});

static void clipboard_set(const char *text)
{
    mem_free(bd.clipboard);
    bd.clipboard = memdup(text, strlen(text) + 1);
}

EMSCRIPTEN_KEEPALIVE void www_ui_clipboard_paste(const char *text)
{
    clipboard_set(text);
}

static const char *www_get_clipboard_text(struct ImGuiContext *igctx)
{
    return bd.clipboard;
}

static void www_set_clipboard_text(struct ImGuiContext *igctx, const char *text)
{
    clipboard_set(text);
    www_clipboard_write(text);
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

    /* Cmd, not Ctrl, is the clipboard modifier in browsers on a Mac */
    if (clap_get_os(clap_ctx)->os == CLAP_OS_MACOS)     io->ConfigMacOSXBehaviors = true;

    struct ImGuiPlatformIO *pio = igGetPlatformIO_ContextPtr(igctx);
    pio->Platform_GetClipboardTextFn = www_get_clipboard_text;
    pio->Platform_SetClipboardTextFn = www_set_clipboard_text;
    www_clipboard_hook();
}

void ui_ig_done_for_emscripten(void)
{
    mem_free(bd.clipboard);
    bd.clipboard = NULL;
}
