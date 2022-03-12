/*
 * "Private" struct data: only IMPLEMENTOR can access struct data
 */
#undef TYPE
#ifdef IMPLEMENTOR
#define TYPE(__type, __def) \
    typedef struct __type { __def } __type ## _t;
#else
# ifdef DEBUG
#define TYPE(__type, __def) \
    typedef struct __type ## _impl { __def } __type ## _t;
# else
#define TYPE(__type, __def) \
    typedef struct __type { char __res[ sizeof(struct { __def }) ]; } __type ## _t; \
    struct __type ## _impl { __def }; \
    static_assert(sizeof(struct __type ## _impl) == sizeof(__type ## _t), \
                  # __type "_t doesn't match implementation");
# endif
#endif
