// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <errno.h>
#include "common.h"
#include "messagebus.h"
#include "memory.h"

static struct list subscriber[MT_MAX];

cerr subscribe(enum message_type type, subscriber_fn fn, void *data)
{
    struct subscriber *s;

    s = mem_alloc(sizeof(*s));
    if (!s)
        return CERR_NOMEM;

    s->handle = fn;
    s->data = data;
    list_append(&subscriber[type], &s->entry);

    return CERR_OK;
}

cerr unsubscribe(enum message_type type, void *data)
{
    if (type >= MT_MAX) return CERR_INVALID_ARGUMENTS;

    struct subscriber *s, *it;
    list_for_each_entry_iter(s, it, &subscriber[type], entry)
        if (s->data == data) {
            list_del(&s->entry);
            mem_free(s);

            return CERR_OK;
        }

    return CERR_NOT_FOUND;
}

int message_send(struct message *m)
{
    struct subscriber *s;
    int ret = 0, res;

    list_for_each_entry(s, &subscriber[m->type], entry) {
        res = s->handle(m, s->data);
        if (res == MSG_STOP)
            break;
        ret |= res;
    }

    return ret;
}

cerr messagebus_init(void)
{
    int i;

    for (i = 0; i < MT_MAX; i++)
        list_init(&subscriber[i]);

    return CERR_OK;
}

void messagebus_done(void)
{
    int i;

    for (i = 0; i < MT_MAX; i++) {
        struct subscriber *s, *iter;

        list_for_each_entry_iter(s, iter, &subscriber[i], entry) {
            list_del(&s->entry);
            mem_free(s);
        }
    }
}
