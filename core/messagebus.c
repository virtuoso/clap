// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <errno.h>
#include "common.h"
#include "messagebus.h"
#include "memory.h"
#include "clap.h"

cerr subscribe(struct clap_context *ctx, enum message_type type, subscriber_fn fn, void *data)
{
    struct subscriber *s;
    struct messagebus *mb = clap_get_messagebus(ctx);

    s = mem_alloc(sizeof(*s));
    if (!s)
        return CERR_NOMEM;

    s->handle = fn;
    s->data = data;
    list_append(&mb->subscriber[type], &s->entry);

    return CERR_OK;
}

cerr unsubscribe(struct clap_context *ctx, enum message_type type, void *data)
{
    if (type >= MT_MAX) return CERR_INVALID_ARGUMENTS;

    struct subscriber *s, *it;
    struct messagebus *mb = clap_get_messagebus(ctx);

    list_for_each_entry_iter(s, it, &mb->subscriber[type], entry)
        if (s->data == data) {
            list_del(&s->entry);
            mem_free(s);

            return CERR_OK;
        }

    return CERR_NOT_FOUND;
}

int message_send(struct clap_context *ctx, struct message *m)
{
    struct subscriber *s;
    int ret = 0, res;
    struct messagebus *mb = clap_get_messagebus(ctx);

    list_for_each_entry(s, &mb->subscriber[m->type], entry) {
        res = s->handle(ctx, m, s->data);
        if (res == MSG_STOP)
            break;
        ret |= res;
    }

    return ret;
}

cerr messagebus_init(struct clap_context *ctx)
{
    int i;
    struct messagebus *mb = clap_get_messagebus(ctx);

    for (i = 0; i < MT_MAX; i++)
        list_init(&mb->subscriber[i]);

    return CERR_OK;
}

void messagebus_done(struct clap_context *ctx)
{
    int i;
    struct messagebus *mb = clap_get_messagebus(ctx);

    for (i = 0; i < MT_MAX; i++) {
        struct subscriber *s, *iter;

        list_for_each_entry_iter(s, iter, &mb->subscriber[i], entry) {
            list_del(&s->entry);
            mem_free(s);
        }
    }
}
