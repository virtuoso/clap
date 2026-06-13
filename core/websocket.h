/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_WEBSOCKET_H__
#define __CLAP_WEBSOCKET_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "error.h"

/*
 * RFC 6455 frame opcodes
 */
enum {
    WSOP_CONT = 0x0,
    WSOP_TEXT,
    WSOP_BIN,
    WSOP_CLOSE = 0x8,
    WSOP_PING,
    WSOP_PONG,
};

/**
 * http_headers_end() - find the end of an HTTP header block
 * @buf:        request bytes
 * @len:        number of bytes at @buf
 *
 * Return: pointer just past the blank line that terminates the headers, or
 * NULL while the block is still incomplete.
 */
const uint8_t *http_headers_end(const uint8_t *buf, size_t len);

/**
 * http_header_find() - find an HTTP header value in a request buffer
 * @buf:        start of the request text
 * @end:        end of the request text
 * @name:       header name, matched case-insensitively, without the colon
 * @vlen:       filled with the length of the returned value
 *
 * Return: pointer into @buf at the header's value, leading whitespace and
 * the line ending trimmed, or NULL if the header isn't there.
 */
const char *http_header_find(const char *buf, const char *end, const char *name, size_t *vlen);

/**
 * ws_decode() - decode one websocket frame from a byte buffer
 * @input:          raw frame bytes as received from the peer
 * @size:           number of bytes available at @input
 * @poutput:        filled with a freshly allocated, unmasked, NUL-terminated payload
 * @poutsz:         filled with the payload length
 * @popcode:        filled with the frame opcode (WSOP_*)
 * @require_mask:   reject unmasked frames (RFC 6455 obliges servers to)
 *
 * Return: the number of bytes consumed from @input; CERR_BUFFER_INCOMPLETE
 * when @input does not yet hold a complete frame and more data is needed;
 * CERR_INVALID_FORMAT on protocol violations: reserved bits, fragmentation
 * (unsupported), an oversized control frame, or an unmasked frame when
 * @require_mask says so.
 */
cres(size_t) ws_decode(uint8_t *input, size_t size, uint8_t **poutput, size_t *poutsz, int *popcode,
                       bool require_mask);

/**
 * ws_encode() - wrap a payload in a single unmasked websocket frame
 * @input:      payload bytes
 * @inputsz:    payload length
 * @poutput:    filled with a freshly allocated frame (unmasked, server-to-client)
 * @poutsz:     filled with the frame length
 * @opcode:     frame opcode (WSOP_*)
 *
 * Return: 0.
 */
int ws_encode(uint8_t *input, size_t inputsz, uint8_t **poutput, size_t *poutsz, int opcode);

#endif /* __CLAP_WEBSOCKET_H__ */
