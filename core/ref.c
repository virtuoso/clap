#include "logger.h"
#include "object.h"

void cleanup__ref(struct ref **ref)
{
    ref_put_ref(*ref);
}

