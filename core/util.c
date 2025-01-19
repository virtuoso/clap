// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "memory.h"
#include "util.h"

void cleanup__fd(int *fd)
{
    close(*fd);
}

void cleanup__FILEp(FILE **f)
{
    if (*f)
        fclose(*f);
}

void cleanup__malloc(void **x)
{
    mem_free(*x);
}

void cleanup__charp(char **s)
{
    mem_free(*s);
}

void cleanup__ucharp(uchar **s)
{
    mem_free(*s);
}

void *memdup(const void *x, size_t size)
{
    void *r = mem_alloc(size);
    if (r) {
        memcpy(r, x, size);
    }
    return r;
}

void *_darray_resize(struct darray *da, size_t nr_el)
{
    void *new;

    /*
     * XXX: be smarter. For now, just avoid realloc() on deletion, unless it's
     *      the last element (see below)
     *  - the array is likely to get repopulated (game.c)
     *  - or, filled in once and then cleared out (gltf.c)
     *
     * Possible reasons to shrink an array:
     *  - the element size is large
     *  - the ratio of additions to deletions is higher than a certain threshold
     */
    if (nr_el < da->nr_el)
        goto out;

    new = mem_realloc_array(da->array, nr_el, da->elsz);

    if (!new)
        return NULL;

    da->array = new;
    if (nr_el > da->nr_el)
        memset(new + da->nr_el * da->elsz, 0, (nr_el - da->nr_el) * da->elsz);

out:
    da->nr_el = nr_el;

    /*
     * XXX: See above about being smarter, but at the bare minimum, we need to
     * free the array when the last element is gone
     */
    if (!da->nr_el)
        _darray_clearout(da);

    return da->array;
}

void *_darray_add(struct darray *da)
{
    void *new = _darray_resize(da, da->nr_el + 1);

    if (!new)
        return NULL;

    new = _darray_get(da, da->nr_el - 1);
    memset(new, 0, da->elsz);

    return new;
}

void *_darray_insert(struct darray *da, size_t idx)
{
    void *new = _darray_resize(da, da->nr_el + 1);

    if (!new)
        return NULL;

    memmove(new + (idx + 1) * da->elsz, new + idx * da->elsz,
            (da->nr_el - idx - 1) * da->elsz);
    new = _darray_get(da, idx);
    memset(new, 0, da->elsz);

    return new;
}

void _darray_delete(struct darray *da, size_t idx)
{
    if (!da->nr_el)
        return;

    if (idx < 0)
        idx = da->nr_el - 1;

    if (idx < da->nr_el - 1)
        memmove(da->array + idx * da->elsz, da->array + (idx + 1) * da->elsz,
                (da->nr_el - idx - 1) * da->elsz);

    (void)_darray_resize(da, da->nr_el - 1);
}

void _darray_clearout(struct darray *da)
{
    mem_free(da->array);
    da->array = NULL;
    da->nr_el = 0;
}

unsigned int fletcher32(const unsigned short *data, size_t len)
{
	unsigned int c0, c1;
	len = (len + 1) & ~1;      /* Round up len to words */

	/* We similarly solve for n > 0 and n * (n+1) / 2 * (2^16-1) < (2^32-1) here. */
	/* On modern computers, using a 64-bit c0/c1 could allow a group size of 23726746. */
	for (c0 = c1 = 0; len > 0; ) {
		size_t blocklen = len;
		if (blocklen > 360*2) {
			blocklen = 360*2;
		}
		len -= blocklen;
		do {
			c0 = c0 + *data++;
			c1 = c1 + c0;
		} while ((blocklen -= 2));
		c0 = c0 % 65535;
		c1 = c1 % 65535;
	}
	return (c1 << 16 | c0);
}

static unsigned long hash_simple(struct hashmap *hm, unsigned int key)
{
    return key & (hm->nr_buckets - 1);
}

int hashmap_init(struct hashmap *hm, size_t nr_buckets)
{
    int i;

    if (nr_buckets & (nr_buckets - 1))
        return -1;

    hm->buckets = mem_alloc(sizeof(struct list), .nr = nr_buckets);
    hm->nr_buckets = nr_buckets;
    hm->hash = hash_simple;
    list_init(&hm->list);

    for (i = 0; i < nr_buckets; i++)
        list_init(&hm->buckets[i]);

    return 0;
}

void hashmap_done(struct hashmap *hm)
{
    struct hashmap_entry *e, *it;

    list_for_each_entry_iter(e, it, &hm->list, list_entry) {
        list_del(&e->list_entry);
        mem_free(e);
    }
    mem_free(hm->buckets);
    hm->nr_buckets = 0;
}

static struct hashmap_entry *
_hashmap_find(struct hashmap *hm, unsigned int key, unsigned long *phash)
{
    struct hashmap_entry *e;

    *phash = hm->hash(hm, key);
    list_for_each_entry(e, &hm->buckets[*phash], entry) {
        if (e->key == key)
            return e;
    }

    return NULL;
}

void *hashmap_find(struct hashmap *hm, unsigned int key)
{
    unsigned long hash;
    struct hashmap_entry *e = _hashmap_find(hm, key, &hash);

    return e ? e->value : NULL;
}

void hashmap_delete(struct hashmap *hm, unsigned int key)
{
    unsigned long hash;
    struct hashmap_entry *e = _hashmap_find(hm, key, &hash);

    if (!e)
        return;

    list_del(&e->entry);
    list_del(&e->list_entry);
    free(e);
}

int hashmap_insert(struct hashmap *hm, unsigned int key, void *value)
{
    unsigned long hash;
    struct hashmap_entry *e;

    if (_hashmap_find(hm, key, &hash))
        return -EBUSY;

    e = mem_alloc(sizeof(*e), .zero = 1);
    if (!e)
        return -ENOMEM;

    e->value = value;
    e->key = key;
    list_append(&hm->buckets[hash], &e->entry);
    list_append(&hm->list, &e->list_entry);

    return 0;
}

void hashmap_for_each(struct hashmap *hm, void (*cb)(void *value, void *data), void *data)
{
    struct hashmap_entry *e;

    list_for_each_entry(e, &hm->list, list_entry) {
        cb(e->value, data);
    }
}

void bitmap_init(struct bitmap *b, size_t bits)
{
    size_t size = bits / BITS_PER_LONG;

    size += !!(bits % BITS_PER_LONG);
    b->mask = mem_alloc(sizeof(unsigned long), .nr = size);
    if (!b->mask)
        return;

    b->size = size;
}

void bitmap_done(struct bitmap *b)
{
    free(b->mask);
    b->size = 0;
}

void bitmap_set(struct bitmap *b, unsigned int bit)
{
    unsigned int idx = bit / BITS_PER_LONG;
    b->mask[idx] |= 1ul << (bit % BITS_PER_LONG);
}

bool bitmap_is_set(struct bitmap *b, unsigned int bit)
{
    unsigned int idx = bit / BITS_PER_LONG;
    return !!(b->mask[idx] & (1ul << (bit % BITS_PER_LONG)));
}

bool bitmap_includes(struct bitmap *b, struct bitmap *subset)
{
    int i;

    if (subset->size > b->size)
        for (i = b->size; i < subset->size; i++)
            if (subset->mask[i])
                return false;

    for (i = 0; i < b->size; i++)
        if ((b->mask[i] & subset->mask[i]) != subset->mask[i])
            return false;

    return true;
}

struct exit_handler {
    exit_handler_fn     fn;
    struct list         entry;
};

static DECLARE_LIST(ehs_list);

notrace int exit_cleanup(exit_handler_fn fn)
{
    struct exit_handler *eh;

    eh = mem_alloc(sizeof(*eh), .zero = 1);
    if (!eh)
        return -ENOMEM;

    eh->fn = fn;

    list_append(&ehs_list, &eh->entry);
    return 0;
}

void exit_cleanup_run(int status)
{
    struct exit_handler *eh, *iter;

    list_for_each_entry_iter(eh, iter, &ehs_list, entry) {
        eh->fn(status);
        list_del(&eh->entry);
        mem_free(eh);
    }
}

static void __attribute__((destructor)) do_exit(void)
{
    exit_cleanup_run(0);
}
