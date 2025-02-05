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

#endif /* __CLAP_ERROR_H__ */
