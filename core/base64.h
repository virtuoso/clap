/* Licensed under BSD-MIT - see LICENSE file for details */
#ifndef CCAN_BASE64_H
#define CCAN_BASE64_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/**
 * typedef base64_maps_t - structure to hold maps for encode/decode
 * @encode_map: encode map
 * @decode_map: decode map
 */
typedef struct {
	char encode_map[64];
	signed char decode_map[256];
} base64_maps_t;

/**
 * base64_encoded_length() - Calculate encode buffer length
 * @srclen:	the size of the data to be encoded
 *
 * add 1 to this to get null-termination
 * Return: Buffer length required for encode
 */
size_t base64_encoded_length(size_t srclen);

/**
 * base64_decoded_length() - Calculate decode buffer length
 * @srclen:	Length of the data to be decoded
 *
 * This does not return the size of the decoded data!  see base64_decode
 * Return: Minimum buffer length for safe decode
 */
size_t base64_decoded_length(size_t srclen);

/**
 * base64_init_maps() - populate a base64_maps_t based on a supplied alphabet
 * @dest:	A base64 maps object
 * @src:	Alphabet to populate the maps from (e.g. base64_alphabet_rfc4648)
 */
void base64_init_maps(base64_maps_t *dest, const char src[64]);


/**
 * base64_encode_triplet_using_maps() - encode 3 bytes into base64 using a specific alphabet
 * @maps:	Maps to use for encoding (see base64_init_maps)
 * @dest:	Buffer containing 3 bytes
 * @src:	Buffer containing 4 characters
 */
void base64_encode_triplet_using_maps(const base64_maps_t *maps,
				      char dest[4], const char src[3]);

/**
 * base64_encode_tail_using_maps() - encode the final bytes of a source using a specific alphabet
 * @maps:	Maps to use for encoding (see base64_init_maps)
 * @dest:	Buffer containing 4 bytes
 * @src:	Buffer containing srclen bytes
 * @srclen:	Number of bytes (<= 3) to encode in src
 */
void base64_encode_tail_using_maps(const base64_maps_t *maps, char dest[4],
				   const char *src, size_t srclen);

/**
 * base64_encode_using_maps() - encode a buffer into base64 using a specific alphabet
 * @maps:		Maps to use for encoding (see base64_init_maps)
 * @dest:		Buffer to encode into
 * @destlen:	Length of dest
 * @src:		Buffer to encode
 * @srclen:		Length of the data to encode
 *
 * Notes:
 * * dest will be nul-padded to destlen (past any required padding)
 * * sets errno = EOVERFLOW if destlen is too small
 * Return: Number of encoded bytes set in dest. -1 on error (and errno set)
 */
ssize_t base64_encode_using_maps(const base64_maps_t *maps,
				 char *dest, size_t destlen,
				 const char *src, size_t srclen);

/*
 * base64_char_in_alphabet() - returns true if character can be part of an encoded string
 * @maps:		A base64 maps object (see base64_init_maps)
 * @b64char:	Character to check
 */
bool base64_char_in_alphabet(const base64_maps_t *maps, char b64char);

/**
 * base64_decode_using_maps() - decode a base64-encoded string using a specific alphabet
 * @maps:		A base64 maps object (see base64_init_maps)
 * @dest:		Buffer to decode into
 * @destlen:	length of dest
 * @src:		the buffer to decode
 * @srclen:		the length of the data to decode
 *
 * Notes:
 * * dest will be nul-padded to destlen
 * * sets errno = EOVERFLOW if destlen is too small
 * * sets errno = EDOM if src contains invalid characters
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
ssize_t base64_decode_using_maps(const base64_maps_t *maps,
				 char *dest, size_t destlen,
				 const char *src, size_t srclen);

/**
 * base64_decode_quartet_using_maps() - decode 4 bytes from base64 using a specific alphabet
 * @maps:	A base64 maps object (see base64_init_maps)
 * @dest:	Buffer containing 3 bytes
 * @src:	Buffer containing 4 bytes
 *
 * Note: sets errno = EDOM if src contains invalid characters
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
int base64_decode_quartet_using_maps(const base64_maps_t *maps,
				     char dest[3], const char src[4]);

/**
 * base64_decode_tail_using_maps() - decode the final bytes of a base64 string using a specific alphabet
 * @maps:	A base64 maps object (see base64_init_maps)
 * @dest:	Buffer containing 3 bytes
 * @src:	Buffer containing 4 bytes - padded with '=' as required
 * @srclen:	Number of bytes to decode in src
 *
 * Notes:
 * * sets errno = EDOM if src contains invalid characters
 * * sets errno = EINVAL if src is an invalid base64 tail
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
int base64_decode_tail_using_maps(const base64_maps_t *maps, char *dest,
				  const char *src, size_t srclen);


/* the rfc4648 functions: */

extern const base64_maps_t base64_maps_rfc4648;

/**
 * base64_encode() - Encode a buffer into base64 according to rfc4648
 * @dest:		Buffer to encode into
 * @destlen:	Length of the destination buffer
 * @src:		Buffer to encode
 * @srclen:		Length of the data to encode
 *
 * Notes:
 * * dest will be nul-padded to destlen (past any required padding)
 * * sets errno = EOVERFLOW if destlen is too small
 *
 * This function encodes src according to http://tools.ietf.org/html/rfc4648
 *
 * Example:
 *	size_t encoded_length;
 *	char dest[100];
 *	const char *src = "This string gets encoded";
 *	encoded_length = base64_encode(dest, sizeof(dest), src, strlen(src));
 *	printf("Returned data of length %zd @%p\n", encoded_length, &dest);
 * Return: Number of encoded bytes set in dest. -1 on error (and errno set)
 */
static inline
ssize_t base64_encode(char *dest, size_t destlen,
		      const char *src, size_t srclen)
{
	return base64_encode_using_maps(&base64_maps_rfc4648,
					dest, destlen, src, srclen);
}

/**
 * base64_encode_triplet() - encode 3 bytes into base64 according to rfc4648
 * @dest:	Buffer containing 4 bytes
 * @src:	Buffer containing 3 bytes
 */
static inline
void base64_encode_triplet(char dest[4], const char src[3])
{
	base64_encode_triplet_using_maps(&base64_maps_rfc4648, dest, src);
}

/**
 * base64_encode_tail() - encode the final bytes of a source according to rfc4648
 * @dest:	Buffer containing 4 bytes
 * @src:	Buffer containing srclen bytes
 * @srclen:	Number of bytes (<= 3) to encode in src
 */
static inline
void base64_encode_tail(char dest[4], const char *src, size_t srclen)
{
	base64_encode_tail_using_maps(&base64_maps_rfc4648, dest, src, srclen);
}


/**
 * base64_decode() - decode An rfc4648 base64-encoded string
 * @dest:		Buffer to decode into
 * @destlen:	Length of the destination buffer
 * @src:		Buffer to decode
 * @srclen:		Length of the data to decode
 *
 * Notes:
 * * dest will be nul-padded to destlen
 * * sets errno = EOVERFLOW if destlen is too small
 * * sets errno = EDOM if src contains invalid characters
 *
 * This function decodes the buffer according to
 * http://tools.ietf.org/html/rfc4648
 *
 * Example:
 *	size_t decoded_length;
 *	char ret[100];
 *	const char *src = "Zm9vYmFyYmF6";
 *	decoded_length = base64_decode(ret, sizeof(ret), src, strlen(src));
 *	printf("Returned data of length %zd @%p\n", decoded_length, &ret);
 *
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
static inline
ssize_t base64_decode(char *dest, size_t destlen,
		      const char *src, size_t srclen)
{
	return base64_decode_using_maps(&base64_maps_rfc4648,
					dest, destlen, src, srclen);
}

/**
 * base64_decode_quartet() - decode the first 4 characters in src into dest
 * @dest:	Buffer containing 3 bytes
 * @src:	Buffer containing 4 characters
 *
 * Note: sets errno = EDOM if src contains invalid characters
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
static inline
int base64_decode_quartet(char dest[3], const char src[4])
{
	return base64_decode_quartet_using_maps(&base64_maps_rfc4648,
						dest, src);
}

/**
 * base64_decode_tail() - decode the final bytes of a base64 string from src into dest
 * @dest:	Buffer containing 3 bytes
 * @src:	Buffer containing 4 bytes - padded with '=' as required
 * @srclen:	Number of bytes to decode in src
 *
 * Notes:
 * * sets errno = EDOM if src contains invalid characters
 * * sets errno = EINVAL if src is an invalid base64 tail
 * Return: Number of decoded bytes set in dest. -1 on error (and errno set)
 */
static inline
ssize_t base64_decode_tail(char dest[3], const char *src, size_t srclen)
{
	return base64_decode_tail_using_maps(&base64_maps_rfc4648,
					     dest, src, srclen);
}

/* end rfc4648 functions */



#endif /* CCAN_BASE64_H */
