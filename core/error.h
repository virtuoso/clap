#ifndef __CLAP_ERROR_H__
#define __CLAP_ERROR_H__

typedef enum cerr {
    CERR_OK                     = 0,
    CERR_NOMEM                  = -1,
    CERR_INVALID_ARGUMENTS      = -2,
    CERR_NOT_SUPPORTED          = -3,
    CERR_INVALID_TEXTURE_SIZE   = -4,
    CERR_TEXTURE_NOT_LOADED     = -5,
    CERR_FRAMEBUFFER_INCOMPLETE = -6,
    CERR_PARSE_FAILED           = -7,
    CERR_ALREADY_LOADED         = -8,
    CERR_FONT_NOT_LOADED        = -9,
    CERR_INVALID_SHADER         = -10,
    CERR_TOO_LARGE              = -11,
    CERR_INVALID_OPERATION      = -12,
    CERR_INVALID_FORMAT         = -13,
    CERR_INITIALIZATION_FAILED  = -14,
} cerr;

#define must_check __attribute__((warn_unused_result))
#define cerr_check cerr must_check

/*
 * Returning errors std::optional<T> style
 */

/* Name of a cres type with value type __type */
#define cres(__type) cres_ ## __type
/* Name of a cres type with value type pointer to __type */
#define cresp(__type) cres_ ## __type ## p

/* Declare a cres with value type __type */
#define cres_ret(__type) \
    typedef struct cres(__type) { \
        cerr    err; \
        __type  val; \
    } cres(__type)

/* Declare a cres with value type pointer to __type */
#define cresp_ret(__type) \
    typedef struct cresp(__type) { \
        cerr    err; \
        __type  *val; \
    } cresp(__type)

/* Declare a cres with value type pointer to struct __struct */
#define cresp_struct_ret(__struct) \
    typedef struct cresp(__struct) { \
        cerr            err; \
        struct __struct *val; \
    } cresp(__struct)

#define cres_check(__type) cres(__type) must_check
#define cresp_check(__type) cresp(__type) must_check

/* Declare cres with int value */
cres_ret(int);

/* Is cerr or cres an error */
#define IS_CERR(__x) ( \
    _Generic(__x, \
        cerr: (__x), \
        default: (__x).err \
    ) != CERR_OK \
)

/* Return a error */
#define cres_error(__type, __err) ({ \
    cres(__type) __res = { \
        .err = (__err), \
    }; \
    __res; \
})

/* Return a error */
#define cresp_error(__type, __err) ({ \
    cresp(__type) __res = { \
        .err = (__err), \
    }; \
    __res; \
})

/* Return a value */
#define cres_val(__type, __val) ({ \
    cres(__type) __res = { \
        .err = CERR_OK, \
        .val = (__val), \
    }; \
    __res; \
})

/* Return a value */
#define cresp_val(__type, __val) ({ \
    cresp(__type) __res = { \
        .err = CERR_OK, \
        .val = (__val), \
    }; \
    __res; \
})

#endif /* __CLAP_ERROR_H__ */
