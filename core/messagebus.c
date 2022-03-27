// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <errno.h>
#include "common.h"
#include "messagebus.h"

static struct subscriber *subscriber[MT_MAX];

int subscribe(enum message_type type, subscriber_fn fn, void *data)
{
    struct subscriber *s, **lastp;

    s = malloc(sizeof(*s));
    if (!s)
        return -ENOMEM;

    s->handle = fn;
    s->data = data;
    s->next = NULL;

    for (lastp = &subscriber[type]; *lastp; lastp = &((*lastp)->next))
        ;

    *lastp = s;

    return 0;
}

int message_send(struct message *m)
{
    struct subscriber *s;
    int ret = 0, res;

    for (s = subscriber[m->type]; s; s = s->next) {
        res = s->handle(m, s->data);
        if (res == MSG_STOP)
            break;
        ret |= res;
    }

    return ret;
}

int messagebus_init(void)
{
    return 0;
}
