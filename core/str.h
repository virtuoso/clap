/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_STR_H__
#define __CLAP_STR_H__

typedef struct string_view {
    char    *data;
    size_t  start;
    size_t  end;
    size_t  cap;
} string_view;

#define type_as_expr(...) (*(typeof(__VA_ARGS__) *)nullptr)

#define str_is_ptr(x) _Generic(type_as_expr(&(x)), \
    const char **:  true, \
    char **:        true, \
    default:        false \
)

#define _str_is_const(x) _Generic(&(x), \
    const char **:      true, \
    const char (*)[]:   true, \
    default:            false \
)

#define str_is_const(x) (__builtin_constant_p((x)) ? true : _str_is_const((x)))

#define sv(_str)    (_str ## _sv)

// Note: in order for this to be usable on constant strings, we need to strip
// away const qualifier, so that string_view::data can still be set. We
// compensate for this by setting capacity to 0, so sv_append() won't try to
// modify the underlying string. However, this is aliasing, and since
// string_view::data is visible, it can be used to write to constant strings,
// or at least attempt to.
// TODO: make string_view opaque, so only dedicated functions can operate on
// it.
#define declare_sv_from(_sv, _str) \
    string_view sv(_sv) = { \
        .data   = (char *)(_str), \
        .cap    = str_is_const((_str)) ? 0 : (str_is_ptr((_str)) ? strlen((_str)) + 1 : sizeof((_str))), \
        .end    = str_is_ptr((_str)) ? strlen((_str)) : sizeof((_str)) - 1, \
    }

#define declare_sv(_str)    declare_sv_from(_str, _str)

#include <stdarg.h>
#include "common.h"
#include "error.h"

static inline size_t sv_len(string_view *sv)    { return sv->end - sv->start; }

static inline void sv_reset(string_view *sv)    { sv->end = 0; }

static inline cres(size_t) sv_append(string_view *sv, const char *fmt, ...)
{
    if (!sv->cap)
        return cres_error(
            size_t,
            CERR_INVALID_OPERATION,
            .fmt = "writing to a const string"
        );

    if (sv->end >= sv->cap - 1) {
        sv->end = sv->cap - 1;
        return cres_error(size_t, CERR_TOO_LARGE);
    }

    size_t room = sv->cap - sv->end;
    va_list ap;

    va_start(ap, fmt);
    int len = vsnprintf(sv->data + sv->end, room, fmt, ap);
    va_end(ap);

    if  (len < 0) {
        sv->data[sv->end] = 0;
        return cres_error(size_t, CERR_TOO_LARGE);
    } else if (len >= room) {
        sv->end = sv->cap - 1;
        return cres_error(size_t, CERR_TOO_LARGE);
    }

    sv->end += len;

    return cres_val(size_t, sv->end);
}

#endif /* __CLAP_STR_H__ */
