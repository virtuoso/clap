// SPDX-License-Identifier: Apache-2.0
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include "common.h"
#include "memory.h"
#include "websocket.h"

const uint8_t *http_headers_end(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return buf + i + 4;

    return NULL;
}

const char *http_header_find(const char *buf, const char *end, const char *name, size_t *vlen)
{
    size_t     namelen = strlen(name);
    const char *p, *c;

    for (p = buf; p < end; p = c) {
        /* find the start of the next line, staying inside the buffer */
        for (c = p; c < end && *c != '\n'; c++)
            ;
        if (c < end)                                                c++;

        /* a name the buffer truncates cannot match */
        if ((size_t)(end - p) < namelen + 1)                        continue;
        if (strncasecmp(p, name, namelen) || p[namelen] != ':')     continue;

        const char *v = p + namelen + 1;
        while (v < end && (*v == ' ' || *v == '\t'))                v++;

        const char *vend = v;
        while (vend < end && *vend != '\r' && *vend != '\n')        vend++;

        *vlen = vend - v;
        return v;
    }

    return NULL;
}

cres(size_t) ws_decode(uint8_t *input, size_t size, uint8_t **poutput, size_t *poutsz, int *popcode,
                       bool require_mask)
{
    uint8_t  *mask = NULL;
    size_t   off = 2;
    uint8_t  *output;
    uint64_t len;

    /* the shortest frame is the two mandatory header bytes */
    if (size < 2)   return cres_error(size_t, CERR_BUFFER_INCOMPLETE);

    unsigned int fin    = input[0] >> 7;
    unsigned int rsv    = (input[0] >> 4) & 0x7;
    unsigned int opcode = input[0] & 0xf;
    unsigned int masked = input[1] >> 7;

    len = input[1] & 0x7f;
    dbg("ws_header: fin=%u opcode=%u mask=%u length=%" PRIu64 " (%zu left)\n",
        fin, opcode, masked, len, size);

    /* no extensions are negotiated and fragmentation isn't supported */
    if (rsv || !fin || opcode == WSOP_CONT)     return cres_error(size_t, CERR_INVALID_FORMAT);

    /* RFC 6455 5.5: control frames must be short and whole */
    if (opcode >= WSOP_CLOSE && len > 125)      return cres_error(size_t, CERR_INVALID_FORMAT);

    if (require_mask && !masked)                return cres_error(size_t, CERR_INVALID_FORMAT);

    if (len == 126) {
        if (size < off + 2)     return cres_error(size_t, CERR_BUFFER_INCOMPLETE);

        len = (uint64_t)input[2] << 8 | input[3];
        off += 2;
    } else if (len == 127) {
        if (size < off + 8)     return cres_error(size_t, CERR_BUFFER_INCOMPLETE);

        len = 0;
        for (int i = 0; i < 8; i++)     len = len << 8 | input[2 + i];
        off += 8;
    }

    if (masked) {
        if (size < off + 4)     return cres_error(size_t, CERR_BUFFER_INCOMPLETE);

        mask = &input[off];
        off += 4;
    }

    if (len > size - off) {
        dbg("## packet larger than input buffer: %" PRIu64 "/%zu\n", len, size);
        return cres_error(size_t, CERR_BUFFER_INCOMPLETE);
    }

    output = mem_alloc(len + 1, .zero = 1, .fatal_fail = 1);
    if (!masked) {
        memcpy(output, &input[off], len);
    } else {
        for (size_t i = 0; i < len; i++)    output[i] = input[off + i] ^ mask[i % 4];
    }
    output[len] = 0;

    *poutput = output;
    *poutsz  = len;
    *popcode = opcode;

    return cres_val(size_t, off + len);
}

void ws_encode(uint8_t *input, size_t inputsz, uint8_t **poutput, size_t *poutsz, int opcode)
{
    size_t            length = inputsz + 2, off = 2;
    uint8_t           *output = NULL;
    int               lensz;

    if (inputsz <= 125)             lensz = 0;
    else if (inputsz <= UINT16_MAX) lensz = 2;
    else                            lensz = 8;

    length += lensz;
    off += lensz;

    output = mem_alloc(length, .zero = 1, .fatal_fail = 1);
    output[0] = 0x80 | (opcode & 0xf);      /* FIN, no fragmentation */
    switch (lensz) {
    case 0:
        output[1] = (uint8_t)inputsz;
        break;
    case 2:
        output[1] = 126;
        output[2] = (uint8_t)(inputsz >> 8);
        output[3] = (uint8_t)inputsz;
        break;
    case 8:
        output[1] = 127;
        for (int i = 0; i < 8; i++)     output[2 + i] = (uint8_t)((uint64_t)inputsz >> (56 - 8 * i));
        break;
    }
    memcpy(&output[off], input, inputsz);

    *poutput = output;
    *poutsz  = length;
}
