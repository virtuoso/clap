/* SPDX-License-Identifier: Apache-2.0 */
/*
 * "Private" struct data: only IMPLEMENTOR can access struct data
 */
#undef TYPE
#ifdef IMPLEMENTOR
#define TYPE(__type, __def) \
    typedef struct __type { __def } __type ## _t;
#define TYPE_FORWARD(__type) \
    typedef struct __type __type ## _t;
#else
# ifdef CLAP_DEBUG
#define TYPE(__type, __def) \
    typedef struct __type ## _impl { __def } __type ## _t;
#define TYPE_FORWARD(__type) \
    typedef struct __type ## _impl __type ## _t;
# else
#define TYPE(__type, __def) \
    typedef struct __type { char __res[ sizeof(struct { __def }) ]; } __type ## _t; \
    struct __type ## _impl { __def }; \
    static_assert(sizeof(struct __type ## _impl) == sizeof(__type ## _t), \
                  # __type "_t doesn't match implementation");
#define TYPE_FORWARD(__type) \
    typedef struct __type __type ## _t;
# endif
#endif
