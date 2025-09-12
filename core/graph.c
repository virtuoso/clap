#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef GRAPH_STANDALONE
#define __CLAP_LOGGER_H__
#endif /* GRAPH_STANDALONE */
#include "util.h"

#include "graph.h"

struct node {
    char    *name;
    int     id;
};

struct edge {
    struct list     entry;
    unsigned int    left;
    unsigned int    right;
};

struct graph {
    struct node     *nodes;
    struct list     edges;
    struct edge     **adj;
    unsigned int    nr_nodes;
    unsigned int    nr_edges;
};

static void graph_adj_set(struct graph *g, unsigned int left, unsigned int right, struct edge *e)
{
    auto right_edge_idx = left * g->nr_nodes + right;
    auto left_edge_idx = right * g->nr_nodes + left;
    auto right_edge = g->adj[right_edge_idx];
    auto left_edge = g->adj[left_edge_idx];

    if (right_edge) {
        list_del(&right_edge->entry);
        free(right_edge);
    }

    if (left_edge && left_edge != right_edge) {
        list_del(&left_edge->entry);
        free(left_edge);
    }

    g->adj[right_edge_idx] = e;
    g->adj[left_edge_idx] = e;
}

static struct edge *graph_adj_get(struct graph *g, unsigned int left, unsigned int right)
{
    return g->adj[left * g->nr_nodes + right] ? : g->adj[right * g->nr_nodes + left];
}

static __unused void adjacency_print(struct edge **adj, size_t nr_nodes, int fd)
{
    int i, j;

    dprintf(fd, "adjacency %zdx%zd:\n", nr_nodes, nr_nodes);
    for (i = 0; i < nr_nodes; i++) {
        for (j = 0; j < nr_nodes; j++)
            dprintf(fd, "%-16lx ", (unsigned long)adj[i * nr_nodes + j]);
        dprintf(fd, "\n");
    }
}

static void graph_adj_grow(struct graph *g)
{
    struct edge **new = calloc(g->nr_nodes * g->nr_nodes, sizeof(void *));
    int i, j, old_nodes = g->nr_nodes - 1;

    if (!new)
        return;

    // memset(new, 0, sizeof(void *) * g->nr_nodes * g->nr_nodes);
    if (old_nodes) {
        for (i = 0; i < old_nodes; i++)
            for (j = i; j < old_nodes; j++) {
                new[i * g->nr_nodes + j] = g->adj[i * old_nodes + j];
                new[j * g->nr_nodes + i] = g->adj[j * old_nodes + i];
            }
        // adjacency_print(g->adj, old_nodes, 2);
        // adjacency_print(new, g->nr_nodes, 2);
    }

    free(g->adj);
    g->adj = new;
}

static int graph_node_find(struct graph *g, const char *name, int id)
{
    int i;

    for (i = 0; i < g->nr_nodes; i++)
        if (!strcmp(g->nodes[i].name, name) && g->nodes[i].id == id)
            return i;
    return -1;
}

static int graph_node_last_id(struct graph *g, const char *name)
{
    int i;

    /* XXX: with graph-from-string, there's no guarantee of id ordering */
    for (i = g->nr_nodes - 1; i >= 0; i--) {
        struct node *node = &g->nodes[i];
        if (!strcmp(node->name, name))
            return node->id;
    }
    return -1;
}

static int graph_node_push_id(struct graph *g, const char *name, unsigned int id)
{
    struct node *new;

    new = realloc(g->nodes, sizeof(struct node) * (g->nr_nodes + 1));
    if (!new)
        return -1;
    g->nodes = new;
    g->nodes[g->nr_nodes].name = strdup(name);
    g->nodes[g->nr_nodes].id = id;
    g->nr_nodes++;
    graph_adj_grow(g);

    return g->nr_nodes - 1;
}

static int graph_node_push(struct graph *g, const char *name)
{
    return graph_node_push_id(g, name, graph_node_last_id(g, name) + 1);
}

static void graph_edge_push(struct graph *g, unsigned int left, unsigned int right)
{
    struct edge *new;

    if (left == right)
        return;

    if (graph_adj_get(g, left, right))
        return;

    new = malloc(sizeof(struct edge));
    if (!new)
        return;
    new->left = left;
    new->right = right;
    list_append(&g->edges, &new->entry);
    graph_adj_set(g, left, right, new);
    // adjacency_print(g->adj, g->nr_nodes, 2);
    g->nr_edges++;
}

struct graph *graph_new(void)
{
    struct graph *g;

    g = calloc(1, sizeof(*g));
    if (!g)
        return NULL;
    list_init(&g->edges);

    return g;
}

void graph_free(struct graph *g)
{
    struct edge *e, *it;
    int i;

    for (i = 0; i < g->nr_nodes; i++)
        free(g->nodes[i].name);
    list_for_each_entry_iter(e, it, &g->edges, entry) {
        list_del(&e->entry);
        free(e);
    }
    free(g->nodes);
    free(g->adj);
    free(g);
}

unsigned int graph_node_edge_count(struct graph *g, unsigned int node)
{
    unsigned int ret, i;

    /* XXX: bidirectional only at the moment */
    for (ret = 0, i = 0; i < g->nr_nodes; i++)
        if (i != node)  ret += !!graph_adj_get(g, i, node);

    return ret;
}

struct graph *graph_random(unsigned int nr_nodes, unsigned int nr_edges)
{
    struct graph *g = graph_new();
    unsigned int start, end, n0, n1;
    int i;

    // unsigned int max_edges = nr_edges / nr_nodes;

    start = graph_node_push(g, "start");
    end = graph_node_push(g, "end");
    for (i = 0; i < nr_nodes - 2; i++)
        graph_node_push(g, "room");

    for (i = 0; i < nr_edges; i++) {
        do {
            n0 = lrand48() % (g->nr_nodes);
            n1 = lrand48() % (g->nr_nodes);
        } while (
            n0 == n1/* ||
            graph_node_edge_count(g, n0) > max_edges ||
            graph_node_edge_count(g, n1) > max_edges*/
        );

        // if (n0 == n1)
        //     n1 = (n1 + 1) % (g->nr_nodes - end);
        graph_edge_push(g, n0, n1);
    }

    // if (graph_adj_get(g, start, end))
    /* no shortcuts directly from start to end */
    graph_adj_set(g, start, end, NULL);

    return g;
}

struct graph *graph_from_string(const char *string)
{
    const char *p, *start = string;
    struct graph *g = graph_new();
    size_t len = strlen(string);

    do {
        p = strchr(start, ' ');
        if (!p)
            p = string + len;

        char *token = strndup(start, p - start);
        // fprintf(stderr, "# token '%s'\n", token);

        char *right = strchr(token, '-');
        if (right) {
            *right++ = 0;
            char *rid = strchr(right, '_');
            char *lid = strchr(token, '_');
            int _rid = 0, _lid = 0;

            if (lid) {
                _lid = atoi(lid + 1);
                *lid = 0;
            }
            if (rid) {
                _rid = atoi(rid + 1);
                *rid = 0;
            }

            int _right = graph_node_find(g, right, _rid);
            int _left = graph_node_find(g, token, _lid);

            if (_left < 0)
                _left = graph_node_push_id(g, token, _lid);
            if (_right < 0)
                _right = graph_node_push_id(g, right, _rid);

            graph_edge_push(g, _left, _right);
            fprintf(stderr, "(%d) %s -- (%d) %s\n",
                    _left, g->nodes[_left].name, _right, g->nodes[_right].name);
        }
        free(token);

        start = p + 1;
    } while (p - string < len);

    return g;
}

// static int array_int_find(int *arr, size_t sz, int elem)
// {
//     int i;

//     for (i = 0; i < sz; i++)
//         if (arr[i] == elem)
//             return i;
//     return -1;
// }

static __unused char *prefix(int level)
{
    return &"                "[16 - level];
}

static struct edge **graph_subgraph(struct graph *g, struct graph *sg, int _start, int **idx)
{
    struct edge *e, **res = NULL;
    int i, j, nr_res = 0, *start = NULL;
    int *nodes = NULL, *noderef = NULL, *_idx = NULL;

    res = calloc(sg->nr_edges, sizeof(struct edge *));
    if (!res)
        return NULL;
    nodes = calloc(sg->nr_nodes, sizeof(int));
    if (!nodes)
        goto not_found;

    noderef = calloc(sg->nr_nodes, sizeof(int));
    if (!noderef)
        goto not_found;

    start = calloc(sg->nr_nodes, sizeof(int));
    if (!start)
            goto not_found;
    start[0] = _start;

    _idx = calloc(sg->nr_nodes, sizeof(int));
    if (!_idx)
            goto not_found;

    for (i = 0; i < sg->nr_nodes; i++) {
        nodes[i] = -1;
        _idx[i] = -1;
    }

    /* all target edges */
    list_for_each_entry(e, &sg->edges, entry) {
        struct node *left;
        struct node *right;

rollback:
        left = &sg->nodes[e->left];
        right = &sg->nodes[e->right];
        // fprintf(stderr, "%slooking for edge %d, start %s_%d nodes:", prefix(nr_res),
        //         nr_res, g->nodes[start[nr_res]].name, g->nodes[start[nr_res]].id);
        for (i = 0; i < sg->nr_nodes; i++)
            fprintf(stderr, " %d (%d)", nodes[i], noderef[i]);
        fprintf(stderr, "\n");

        for (i = start[nr_res]; i < g->nr_nodes; i++) {
            if (strcmp(g->nodes[i].name, left->name))
                continue;
            // fprintf(stderr, "%strying node %s_%d\n", prefix(nr_res),
            //         g->nodes[i].name, g->nodes[i].id);

            if (nodes[e->left] < 0) {
                nodes[e->left] = g->nodes[i].id;
                noderef[e->left] = 1;
            } else if (g->nodes[i].id != nodes[e->left]) {
                continue;
            } else {
                noderef[e->left]++;
            }

            // fprintf(stderr, "%sleft good %d (%d)\n", prefix(nr_res), nodes[e->left], noderef[e->left]);
            _idx[e->left] = i;
            for (j = 0; j < g->nr_nodes; j++) {
                struct edge *candidate = graph_adj_get(g, i, j);

                if (i == j)
                    continue;
                if (!candidate || strcmp(g->nodes[j].name, right->name))
                    continue;
                int k;

                /* check if we already have this edge */
                for (k = 0; k < nr_res; k++)
                    if (res[k] == candidate) {
                        // fprintf(stderr, "%salready have it\n", prefix(nr_res));
                        goto inner_next;
                    }
                // fprintf(stderr, "%shaven't seen this one yet\n", prefix(nr_res));

                if (nodes[e->right] < 0) {
                    nodes[e->right] = g->nodes[j].id;
                    noderef[e->right] = 1;
                } else if (g->nodes[j].id != nodes[e->right]) {
                    continue;
                } else {
                    noderef[e->right]++;
                }
                // fprintf(stderr, "%sright good %d (%d)\n", prefix(nr_res), nodes[e->right], noderef[e->right]);

                _idx[e->right] = j;
                res[nr_res] = candidate;
                // fprintf(stderr, "%smatching edge %d--%d: ", prefix(nr_res),
                //         res[nr_res]->left, res[nr_res]->right);
                // graph_print_edge(g, res[nr_res], 2);
                nr_res++;
                if (nr_res == sg->nr_edges)
                    goto done;
                goto next_edge;
inner_next:;
            }
            /*
             * at this point, we've checked all neighbors of i and should
             * uncache it from nodes[]
             */
            fprintf(stderr, "  ### e->left %d nodes[%d]=%d (%d)\n", e->left, e->left, nodes[e->left], noderef[e->left]);
            if (!--noderef[e->left])
                nodes[e->left] = -1;
            // nodes[e->right] = -1;
        }
        /* no matching edges found, roll back */
        // nodes[e->left] = -1;

        // fprintf(stderr, "no matches for edge %d--%d\n", e->left, e->right);
        if (!nr_res)
            goto not_found;
        nr_res--;
        // start = min(res[nr_res]->left, res[nr_res]->right) + 1;
        e = list_prev_entry(e, entry);
        if (&e->entry == &sg->edges)
            goto not_found;
        fprintf(stderr, "  ### e->left %d nodes[%d]=%d (%d)\n", e->left, e->left, nodes[e->left], noderef[e->left]);
        if (!--noderef[e->left])
            nodes[e->left] = -1;
        fprintf(stderr, "  ### e->right %d nodes[%d]=%d (%d)\n", e->right, e->right, nodes[e->right], noderef[e->right]);
        if (!--noderef[e->right])
            nodes[e->right] = -1;
        if (++start[nr_res] == g->nr_nodes)
            goto not_found;
        goto rollback;
next_edge:;
    }
done:
    // fprintf(stderr, "found subgraph starting at node %s_%d\n",
    //         g->nodes[res[0]->left].name, g->nodes[res[0]->left].id);
    free(noderef);
    free(nodes);
    free(start);
    *idx = _idx;
    return res;
not_found:
    free(noderef);
    free(start);
    free(nodes);
    free(res);
    free(_idx);
    return NULL;
}

static void graph_edges_remove(struct graph *g, struct edge **edges, size_t nr_edges)
{
    struct edge *e, *it;
    int i;

    list_for_each_entry_iter(e, it, &g->edges, entry) {
        for (i = 0; i < nr_edges; i++)
            if (e == edges[i]) {
                graph_adj_set(g, e->left, e->right, NULL);
                // list_del(&e->entry);
                // free(e);
                g->nr_edges--;
            }
    }
}

static void graph_edges_replace(struct graph *g, struct graph *sg, int *nodes)
{
    struct edge *e;

    list_for_each_entry(e, &sg->edges, entry) {
        graph_edge_push(g, nodes[e->left], nodes[e->right]);
    }
}

struct graph *graph_linear(unsigned int nr_nodes)
{
    struct graph *g = graph_new();
    int i;

    graph_node_push(g, "start");
    for (i = 0; i < nr_nodes - 2; i++)
        graph_node_push(g, "room");
    graph_node_push(g, "end");

    for (i = 0; i < nr_nodes - 1; i++)
        graph_edge_push(g, i, i + 1);

    return g;
}

bool graph_search_replace(struct graph *g, struct graph *find, struct graph *replace, int start)
{
    struct edge **sub_edges;
    int *nodes, i;

    sub_edges = graph_subgraph(g, find, start, &nodes);
    if (!sub_edges)
        return false;

    // dprintf(1, "subgraph cluster_%d {\n", start);
    // for (i = 0; i < find->nr_edges; i++)
    //     graph_print_edge(g, sub_edges[i], 2);
    //// dprintf(1, "}\n");
    //// break;
    for (i = 0; i < find->nr_nodes; i++)
        fprintf(stderr, " [%s_%d]", g->nodes[nodes[i]].name, g->nodes[nodes[i]].id);
    fprintf(stderr, "\n");
    graph_edges_remove(g, sub_edges, find->nr_edges);
    graph_edges_replace(g, replace, nodes);
    free(sub_edges);
    free(nodes);

    return true;
}

#ifdef GRAPH_STANDALONE
static void graph_print_edge(struct graph *g, struct edge *e, int fd)
{
    struct node *n[2] = { &g->nodes[e->left], &g->nodes[e->right] };
    int j;

    for (j = 0; j < 2; j++) {
        if (n[j]->id == graph_node_last_id(g, n[j]->name) && !n[j]->id)
            dprintf(fd, "%s", n[j]->name);
        else
            dprintf(fd, "%s_%d", n[j]->name, n[j]->id);
        dprintf(fd, "%s", !j ? " -- " : ";\n");
    }
}

static void graph_print_gv_begin(struct graph *g, const char *name, int fd)
{
    dprintf(fd, "graph %s {\n", name);
}

static void graph_print_gv_end(struct graph *g, int fd)
{
    dprintf(fd, "}\n");
}

static void graph_print_gv(struct graph *g, int fd)
{
    struct edge *e;

    list_for_each_entry(e, &g->edges, entry) {
        graph_print_edge(g, e, fd);
    }
}

struct rule {
    const char  *find;
    const char  *replace;
} rules[] = {
    {
        .find       = "room-room_1 room_1-room_2 room-room_2",
        .replace    = "room-room_1 room-room_2",
    },
    {
        .find       = "start-room start-room_1",
        .replace    = "start-room room-room_1",
    },
    {
        .find       = "start-room room-end start-end",
        .replace    = "start-room room-end",
    },
    // {
    //     .find       = "room-room_1 room_1-room_2 room_2-room_3 room-room_3",
    //     .replace    = "room-room_1 room-room_2 room-room_3",
    // },
    {
        .find       = "start-end",
        .replace    = "",
    },
};

int main()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    srand48(ts.tv_nsec);

    struct graph *g = graph_random(10, 20);
    // struct graph *sg = graph_from_string("room-room room_1-room_2 room-room_2");
    // struct graph *sg, *rep;
    // int start, try, i;

    graph_print_gv_begin(g, "G", 1);
    // for (i = 0; i < array_size(rules); i++) {
    //     sg = graph_from_string(rules[i].find);
    //     rep = graph_from_string(rules[i].replace);

    //     for (try = 0; try < 100; try++) {
    //         start = 0;//lrand48() % (g->nr_nodes - sg->nr_nodes);
    //         if (!graph_search_replace(g, sg, rep, start))
    //             break;
    //     }

    //     graph_free(rep);
    //     graph_free(sg);
    //     break;
    // }

    graph_print_gv(g, 1);
    graph_print_gv_end(g, 1);
    graph_free(g);
    return EXIT_SUCCESS;
}
#endif /* GRAPH_STANDALONE */
