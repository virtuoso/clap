// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "linmath.h"
#include "memory.h"
#include "util.h"

void cleanup__fd(int *fd)
{
    close(*fd);
}

DEFINE_CLEANUP(FILE, if (*p) fclose(*p))
DEFINE_CLEANUP(void, mem_free(*p))
DEFINE_CLEANUP(char, mem_free(*p))
DEFINE_CLEANUP(uchar, mem_free(*p))

void aabb_center(float *aabb, float *center)
{
    vec3 minv = { aabb[0], aabb[2], aabb[4] };
    vec3 maxv = { aabb[1], aabb[3], aabb[5] };

    vec3_sub(center, maxv, minv);
    vec3_scale(center, center, 0.5);
    vec3_add(center, center, minv);
}

void vertex_array_aabb_calc(float *aabb, float *vx, size_t vxsz)
{
    int i;

    vxsz /= sizeof(float);
    aabb[0] = aabb[2] = aabb[4] = INFINITY;
    aabb[1] = aabb[3] = aabb[5] = -INFINITY;
    for (i = 0; i < vxsz; i += 3) {
        aabb[0] = min(vx[i + 0], aabb[0]);
        aabb[1] = max(vx[i + 0], aabb[1]);
        aabb[2] = min(vx[i + 1], aabb[2]);
        aabb[3] = max(vx[i + 1], aabb[3]);
        aabb[4] = min(vx[i + 2], aabb[4]);
        aabb[5] = max(vx[i + 2], aabb[5]);
    }
}

void vertex_array_fix_origin(float *vx, size_t vxsz, float *aabb)
{
    vec3 center;
    aabb_center(aabb, center);
    center[1] = aabb[2];

    for (int v = 0; v < vxsz / sizeof(float); v += 3)
        vec3_sub(&vx[v], &vx[v], center);

    vertex_array_aabb_calc(aabb, vx, vxsz);
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
    if (nr_el <= da->nr_el)
        goto out;

    err_on(da->array && !da->nr_el, "array: %p nr_el: %zu\n", da->array, da->nr_el);
    new = mem_realloc_array(da->array, nr_el, da->elsz, .mod = da->mod);

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

cerr hashmap_insert(struct hashmap *hm, unsigned int key, void *value)
{
    unsigned long hash;
    struct hashmap_entry *e;

    if (_hashmap_find(hm, key, &hash))
        return CERR_ALREADY_LOADED;

    e = mem_alloc(sizeof(*e), .zero = 1);
    if (!e)
        return CERR_NOMEM;

    e->value = value;
    e->key = key;
    list_append(&hm->buckets[hash], &e->entry);
    list_append(&hm->list, &e->list_entry);

    return CERR_OK;
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

notrace cerr exit_cleanup(exit_handler_fn fn)
{
    struct exit_handler *eh;

    eh = mem_alloc(sizeof(*eh), .zero = 1);
    if (!eh)
        return CERR_NOMEM;

    eh->fn = fn;

    list_append(&ehs_list, &eh->entry);
    return CERR_OK;
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
