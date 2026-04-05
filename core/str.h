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
    char **:    true, \
    default:    false \
)

#define sv(_str)    (_str ## _sv)

#define declare_sv_from(_sv, _str) \
    string_view sv(_sv) = { \
        .data   = (_str), \
        .cap    = str_is_ptr((_str)) ? 0 : sizeof((_str)), \
        .end    = str_is_ptr((_str)) ? strlen((_str)) : sizeof((_str)) - 1, \
    }

#define declare_sv(_str)    declare_sv_from(_str, _str)

#include <stdarg.h>
#include "common.h"
#include "error.h"

static inline size_t sv_len(string_view *sv)    { return sv->end - sv->start; }

static inline cres(size_t) sv_append(string_view *sv, const char *fmt, ...)
{
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
