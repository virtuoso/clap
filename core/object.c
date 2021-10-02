#include <stdarg.h>
#include "common.h"
#include "object.h"
#include "json.h"

static DECLARE_LIST(ref_classes);
static char ref_classes_string[4096];
static bool ref_classes_updated;

static void ref_classes_update(void)
{
    size_t size, total = 0;
    struct ref_class *rc;
    char buf[256];
    unsigned long counter = 0;

    list_for_each_entry(rc, &ref_classes, entry) {
        size = snprintf(&ref_classes_string[total], sizeof(ref_classes_string) - total,
                        " -> '%s': %lu\n", rc->name, rc->nr_active);
        if (total + size >= sizeof(ref_classes_string))
            break;
        total += size;
        counter++;
    }

    size = snprintf(&ref_classes_string[total], sizeof(ref_classes_string) - total,
                    " total: %lu", counter);
    ref_classes_string[total+size] = 0;
    ref_classes_updated = false;
}

const char *ref_classes_get_string(void)
{
    if (ref_classes_updated)
        ref_classes_update();
    return ref_classes_string;
}

static bool ref_class_needs_init(struct ref_class *rc)
{
    return list_empty(&rc->entry);
}

static void ref_class_init_lazy(struct ref_class *rc)
{
    list_append(&ref_classes, &rc->entry);
}

void ref_class_add(struct ref *ref)
{
    struct ref_class *rc = ref->refclass;

    bug_on(!ref->refclass);

    if (ref_class_needs_init(rc))
        ref_class_init_lazy(rc);

    rc->nr_active++;
    ref_classes_updated = true;
}

static void ref_class_unuse(struct ref *ref)
{
    /* not deleting the class itself */
    ref->refclass->nr_active--;
    ref_classes_updated = true;
}

void _ref_drop(struct ref *ref)
{
    ref_class_unuse(ref);
    ref->drop(ref);
}

/*
 * 0. Object can be anything that has type pointer at offset 0.
 * 1. Type can be struct class or anything that embeds it at offset 0.
 */

struct object {
    struct class    *type;
    char            *name;
};

void *make(void *type, ...)
{
    struct class *c = type;
    Object *o;
    va_list ap;

    CHECK(o = calloc(1, c->size));
    o->type = c;

    va_start(ap, type);
    o = c->make(o, &ap);
    va_end(ap);

    return o;
}

/*void *_make(int argc, void *type, ...)
{
    return 
}*/

void drop(void *object)
{
    Object *o = object;
    struct class *c;

    if (!o)
        return;

    c = o->type;
    bug_on(!c || !c->drop);

    o = c->drop(o);
    free(o);
}

int cmp(void *object, void *other)
{
    Object *self = object;
    Object *_other = other;

    bug_on(!self || !other || !self->type);
    if (_other->type != self->type)
        return 1;

    return self->type->cmp(object, other);
}

/*
 * Types of serialization:
 *  - complete: most internals, id
 *    -> deser results in a new object, identical to self
 *    -> can't be incremental
 *    -> useful for - ?
 *  - state: location, angle, stuff
 *    -> can be incremental: have "dirty" flag for state properties
 *    -> reference by id
 *    -> useful for network sync
 * 
 * ==============
 * ID -> base property:
 *  - how is it assigned (minding the network)?
 */
int serialize(void *object, JsonNode *root)
{
    Object *o = object;
    struct class *c = o->type;
    JsonNode *type;

    if (!c->serialize)
        return -1;

    type = json_mkstring(c->name);
    json_append_member(root, "type", type);

    return c->serialize(o, root);
}

/*
 * deserialize() can restore or construct
 * if @object == NULL, acts as a consturctor
 */
void *deserialize(void *object, JsonNode *root)
{
    Object *o;
    struct class *c;
    JsonNode *type;

    o = object;
    if (!o) {
        type = json_find_member(root, "type");
        if (type->tag != JSON_STRING)
            return NULL;
        c = class_find(type->string_);
        o = make(c, NULL);
    } else {
        c = o->type;
    }

    if (!c->deserialize)
        return NULL;

    return c->deserialize(o, root);
}

static void *class_make(void *self, va_list *args)
{
    Object *o = self;

    o->name = va_arg(*args, char *);
    //dbg(" => '%s'\n", o->name);

    return self;
}

static void *class_drop(void *self)
{
    return self;
}

void cleanup__Objectp(Object **object)
{
    drop(*object);
}

static void *class_clone(void *self)
{
    return NULL;
}

static int class_cmp(void *self, void *other)
{
    Object *_self = self, *_other = other;

    if (self == other)
        return 0;

    return strcmp(_self->name, _other->name);
}

static int class_serialize(void *self, JsonNode *root)
{
    Object *o = self;
    JsonNode *val = json_mkstring(o->name);

    json_append_member(root, "name", val);
    //msg("%s\n", json_encode(root));

    return 0;
}

static void *class_deserialize(void *self, JsonNode *root)
{
    Object *o = self;
    JsonNode *val;

    val = json_find_member(root, "name");
    o->name = val->string_;

    return o;
}

/* Base abstract class */
struct class class = {
    .name        = "class",
    .size        = sizeof(struct object),
    .make        = class_make,
    .drop        = class_drop,
    .clone       = class_clone,
    .cmp         = class_cmp,
    .serialize   = class_serialize,
    .deserialize = class_deserialize,
};
DECLARE_CLASS(class);

extern struct class *__start_ClassList;
extern struct class *__stop_ClassList;

struct class *__for_each_class(int (*fn)(struct class *, const void *), const void *data)
{
    struct class **c;

    if (fn(&class, data))
        return &class;
    return NULL;
    //dbg("class list: %p..%p\n", &__start_ClassList, &__stop_ClassList);
    for (c = &__start_ClassList; c < &__stop_ClassList; c++) {
        if (fn(*c, data))
            return *c;
    }
    //dbg("done\n");

    return NULL;
}

static int __class_print(struct class *class, const void *data)
{
    msg(" => class '%s'\n", class->name);
    return 0;
}

void print_each_class(void)
{
    __for_each_class(__class_print, NULL);
}

static int __class_match_name(struct class *class, const void *data)
{
    const char *name = data;

    return !strcmp(class->name, name);
}

struct class *class_find(const char *name)
{
    return __for_each_class(__class_match_name, name);
}

/*static void obj_drop(struct ref *ref)
{
    struct class *obj = container_of(ref, struct class, ref);

    free(obj);
}

struct class *__obj_new(const char *name, size_t size)
{
    struct class *obj = ref_new(struct class, ref, obj_drop);

    obj->name = name;
    obj->size = size;

    return obj;
}*/
