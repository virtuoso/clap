// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdio.h>
#include "error.h"
#include "memory.h"

const char *cerr_str(cerr_enum err)
{
    switch (err) {
        case _CERR_OK:                      return "no error";
        case _CERR_NOMEM:                   return "memory allocation error";
        case _CERR_INVALID_ARGUMENTS:       return "invalid arguments";
        case _CERR_NOT_SUPPORTED:           return "not supported";
        case _CERR_INVALID_TEXTURE_SIZE:    return "invalid texture size";
        case _CERR_TEXTURE_NOT_LOADED:      return "texture is not loaded";
        case _CERR_FRAMEBUFFER_INCOMPLETE:  return "incomplete framebuffer";
        case _CERR_PARSE_FAILED:            return "parse failed";
        case _CERR_ALREADY_LOADED:          return "object is already loaded";
        case _CERR_FONT_NOT_LOADED:         return "font is not loaded";
        case _CERR_INVALID_SHADER:          return "invalid shader";
        case _CERR_TOO_LARGE:               return "too large";
        case _CERR_INVALID_OPERATION:       return "invalid operation";
        case _CERR_INVALID_FORMAT:          return "invalid format";
        case _CERR_INITIALIZATION_FAILED:   return "initialization failed";
        case _CERR_SHADER_NOT_LOADED:       return "shader is not loaded";
        case _CERR_SOCK_ACCEPT_FAILED:      return "socket accept failed";
        case _CERR_SOCK_BIND_FAILED:        return "socket bind failed";
        case _CERR_SOCK_LISTEN_FAILED:      return "socket listen failed";
        case _CERR_SOUND_NOT_LOADED:        return "sound not loaded";
        case _CERR_BUFFER_OVERRUN:          return "buffer overrun";
        case _CERR_BUFFER_INCOMPLETE:       return "buffer incomplete";
        case _CERR_INVALID_INDEX:           return "invalid index";
        case _CERR_NOT_FOUND:               return "not found";
        case _CERR_LUT_NOT_LOADED:          return "LUT is not loaded";
        case _CERR_SCENE_NOT_LOADED:        return "scene is not loaded";
        case _CERR_OUT_OF_BOUNDS:           return "out of bounds";
        case _CERR_EOF:                     return "end of file";
        case _CERR_ACCESS_DENIED:           return "access denied";
        case _CERR_NOT_A_DIRECTORY:         return "not a directory";
        case _CERR_TOO_MANY_OPEN_FILES:     return "too many open files";
        case _CERR_NAME_TOO_LONG:           return "name too long";
        case _CERR_UNKNOWN_ERROR:           return "unknown error";
        case _CERR_PERMISSION_DENIED:       return "permission denied";
        default:                            break;
    }

    return "<unknown>";
}

int cerr_strbuf(char *buf, size_t size, void *_err)
{
    cerr *err = _err;
    const char *mod = "<unknown>";
    int line = -1;

#ifndef CONFIG_FINAL
    mod  = err->mod;
    line = err->line;
#endif /* CONFIG_FINAL */

    const char *basename = str_basename(mod);
    return snprintf(buf, size, "%s at %s:%d", cerr_str(err->err), basename, line);
}
