// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <errno.h>
#include "common.h"
#include "messagebus.h"
#include "memory.h"

static struct list subscriber[MT_MAX];

int subscribe(enum message_type type, subscriber_fn fn, void *data)
{
    struct subscriber *s;

    s = mem_alloc(sizeof(*s));
    if (!s)
        return -ENOMEM;

    s->handle = fn;
    s->data = data;
    list_append(&subscriber[type], &s->entry);

    return 0;
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

int messagebus_init(void)
{
    int i;

    for (i = 0; i < MT_MAX; i++)
        list_init(&subscriber[i]);

    return 0;
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
