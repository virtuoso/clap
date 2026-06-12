// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "websocket.h"

struct ws_header {
    unsigned opcode : 4;
    unsigned rsv    : 3;
    unsigned fin    : 1;
    unsigned length : 7;
    unsigned mask   : 1;
};

size_t ws_decode(uint8_t *input, size_t size, uint8_t **poutput, size_t *poutsz, int *popcode)
{
    struct ws_header *h = (void *)input;
    uint8_t          *mask = NULL;
    size_t           len, off = 2;
    uint8_t          *output;
    int              op;

    if (size < sizeof(*h))
        return -2;
    dbg("ws_header: fin=%d opcode=%d mask=%d length=%d (%zu left)\n",
        h->fin, h->opcode, h->mask, h->length, size);
    len = h->length;
    op  = h->opcode;

    if (len <= 125) {
        off = 2;
    } else if (len == 126) {
        len = input[off++] << 8;
        len |= input[off++];
    } else if (len == 127) {
        len  = input[off++] << 24;
        len |= input[off++] << 16;
        len |= input[off++] << 8;
        len |= input[off++];
    }
    if (h->mask) {
        mask = &input[off];
        off += 4;
    }

    if (len + off > size) {
        dbg("## packet larger than input buffer: %zu/%zu\n", len, size);
        return -2;
    }

    output = mem_alloc(len + 1, .zero = 1, .fatal_fail = 1);
    if (!h->mask) {
        memcpy(output, &input[off], len);
    } else {
        int i;

        for (i = 0; i < len; i++)
            output[i] = input[off + i] ^ mask[i % 4];
    }
    output[len] = 0;

    *poutput = output;
    *poutsz  = len;
    *popcode = op;

    return off + len;
}

int ws_encode(uint8_t *input, size_t inputsz, uint8_t **poutput, size_t *poutsz)
{
    size_t            length = inputsz + 2, off = 2;
    uint8_t           *output = NULL;
    struct ws_header  *h;
    uint16_t          len16;
    uint64_t          len64;
    int               lensz;

    if (inputsz < 125) {
        lensz = 0;
        //length += 2;
        //dbg("header: %02x\n", output[0]);
    } else if (inputsz < UINT16_MAX) {
        lensz = 2;
        len16 = (uint16_t)inputsz;
    } else {
        lensz = 8;
        len64 = (uint64_t)inputsz;
    }
    length += lensz;
    off += lensz;

    output = mem_alloc(length, .zero = 1, .fatal_fail = 1);
    h         = (void *)output;
    h->fin    = 1;
    h->opcode = WSOP_BIN;
    switch (lensz) {
    case 0:
        h->length = (uint8_t)inputsz;
        break;
    case 2:
        h->length = 126;
        memcpy(&output[2], &len16, lensz);
        break;
    case 8:
        h->length = 127;
        memcpy(&output[2], &len64, lensz);
        break;
    }
    memcpy(&output[off], input, inputsz);

    *poutput = output;
    *poutsz  = length;
    return 0;
}
