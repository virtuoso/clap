#include "object.h"

static void obj_drop(struct ref *ref)
{
    struct obj *obj = container_of(ref, struct obj, ref);

    free(obj);
}

struct obj *__obj_new(const char *name, size_t size)
{
    struct obj *obj = ref_new(struct obj, ref, obj_drop);

    obj->name = name;
    obj->size = size;

    return obj;
}

