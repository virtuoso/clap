// SPDX-License-Identifier: Apache-2.0
#include "graph.h"
#include "memory.h"

struct graph_node {
    char    *name;
    void    *payload;
    int     id;
};

struct graph_edge {
    struct list     entry;
    unsigned int    left;
    unsigned int    right;
    void            *payload;
};

struct graph {
    graph_params        params;
    darray(struct graph_node, nodes);
    struct list         edges;
    struct graph_edge   **adj;
    unsigned int        nr_nodes;
    unsigned int        nr_edges;
};

static struct graph_node *graph_node(const graph *g, unsigned int node)
{
    return darray_get(((graph *)g)->nodes, node);
}

static struct graph_edge *graph_adj_get(const graph *g, unsigned int left, unsigned int right)
{
    /* graph_adj_set() mirrors undirected edges into both cells */
    return g->adj[left * g->nr_nodes + right];
}

static void graph_adj_set(graph *g, unsigned int left, unsigned int right, struct graph_edge *e)
{
    g->adj[left * g->nr_nodes + right] = e;
    if (!g->params.directed)    g->adj[right * g->nr_nodes + left] = e;
}

static cerr graph_adj_grow(graph *g, unsigned int new_nr)
{
    struct graph_edge **adj = mem_alloc(sizeof(*adj), .nr = (size_t)new_nr * new_nr, .zero = 1);

    if (!adj)   return CERR_NOMEM;

    for (unsigned int i = 0; i < g->nr_nodes; i++)
        for (unsigned int j = 0; j < g->nr_nodes; j++)
            adj[i * new_nr + j] = g->adj[i * g->nr_nodes + j];

    mem_free(g->adj);
    g->adj = adj;

    return CERR_OK;
}

cresp(graph) _graph_new(const graph_params *params)
{
    graph *g = mem_alloc(sizeof(*g), .zero = 1);

    if (!g)     return cresp_error(graph, CERR_NOMEM);

    g->params = *params;
    list_init(&g->edges);
    darray_init(g->nodes);

    return cresp_val(graph, g);
}

void graph_free(graph *g)
{
    if (!g)     return;

    struct graph_node *node;
    darray_for_each(node, g->nodes) {
        if (g->params.node_drop && node->payload)   g->params.node_drop(node->payload);
        mem_free(node->name);
    }

    struct graph_edge *e, *it;
    list_for_each_entry_iter(e, it, &g->edges, entry) {
        list_del(&e->entry);
        if (g->params.edge_drop && e->payload)      g->params.edge_drop(e->payload);
        mem_free(e);
    }

    darray_clearout(g->nodes);
    mem_free(g->adj);
    mem_free(g);
}

DEFINE_CLEANUP(graph, if (*p) graph_free(*p))

/* the highest id among @name nodes, -1 when there are none */
static int graph_node_max_id(const graph *g, const char *name)
{
    struct graph_node *node;
    int max_id = -1;

    darray_for_each(node, ((graph *)g)->nodes)
        if (!strcmp(node->name, name))  max_id = max(max_id, node->id);

    return max_id;
}

cres(int) graph_node_find(const graph *g, const char *name, int id)
{
    if (!g || !name)    return cres_error(int, CERR_INVALID_ARGUMENTS);

    for (unsigned int i = 0; i < g->nr_nodes; i++) {
        struct graph_node *node = graph_node(g, i);

        if (!strcmp(node->name, name) && node->id == id)    return cres_val(int, i);
    }

    return cres_error(int, CERR_NOT_FOUND);
}

cres(int) graph_node_add_id(graph *g, const char *name, int id, void *payload)
{
    if (!g || !name || !*name || id < 0)    return cres_error(int, CERR_INVALID_ARGUMENTS);

    cres(int) found = graph_node_find(g, name, id);
    if (!IS_CERR(found))
        return cres_error(int, CERR_ALREADY_LOADED,
                          .fmt = "node %s_%d exists", .arg0 = name, .arg1 = (void *)(intptr_t)id);

    char *name_copy = memdup(name, strlen(name) + 1);
    if (!name_copy)                         return cres_error(int, CERR_NOMEM);

    struct graph_node *node = darray_add(g->nodes);
    if (!node) {
        mem_free(name_copy);
        return cres_error(int, CERR_NOMEM);
    }

    CERR_RET(graph_adj_grow(g, g->nr_nodes + 1), {
        darray_delete(g->nodes, g->nr_nodes);
        mem_free(name_copy);
        return cres_error_cerr(int, __cerr);
    });

    node->name = name_copy;
    node->id = id;
    node->payload = payload;

    return cres_val(int, g->nr_nodes++);
}

cres(int) graph_node_add(graph *g, const char *name, void *payload)
{
    if (!g || !name)    return cres_error(int, CERR_INVALID_ARGUMENTS);

    return graph_node_add_id(g, name, graph_node_max_id(g, name) + 1, payload);
}

cerr graph_edge_add(graph *g, unsigned int left, unsigned int right, void *payload)
{
    if (!g || left >= g->nr_nodes || right >= g->nr_nodes)  return CERR_INVALID_ARGUMENTS;

    if (left == right)
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "self loop on node %u",
            .arg0   = (void *)(uintptr_t)left
        );

    if (graph_adj_get(g, left, right))                      return CERR_ALREADY_LOADED;

    struct graph_edge *e = mem_alloc(sizeof(*e), .zero = 1);
    if (!e)                                                 return CERR_NOMEM;

    e->left = left;
    e->right = right;
    e->payload = payload;
    list_append(&g->edges, &e->entry);
    graph_adj_set(g, left, right, e);
    g->nr_edges++;

    return CERR_OK;
}

cerr graph_edge_del(graph *g, unsigned int left, unsigned int right)
{
    if (!g || left >= g->nr_nodes || right >= g->nr_nodes)  return CERR_INVALID_ARGUMENTS;

    struct graph_edge *e = graph_adj_get(g, left, right);
    if (!e)                                                 return CERR_NOT_FOUND;

    graph_adj_set(g, e->left, e->right, NULL);
    list_del(&e->entry);
    if (g->params.edge_drop && e->payload)                  g->params.edge_drop(e->payload);
    mem_free(e);
    g->nr_edges--;

    return CERR_OK;
}

unsigned int graph_nr_nodes(const graph *g)
{
    return g ? g->nr_nodes : 0;
}

unsigned int graph_nr_edges(const graph *g)
{
    return g ? g->nr_edges : 0;
}

bool graph_has_edge(const graph *g, unsigned int left, unsigned int right)
{
    if (!g || left >= g->nr_nodes || right >= g->nr_nodes)  return false;

    return !!graph_adj_get(g, left, right);
}

cresp(const_char) graph_node_name(const graph *g, unsigned int node)
{
    struct graph_node *n = g ? graph_node(g, node) : NULL;

    if (!n)     return cresp_error(const_char, CERR_INVALID_INDEX);

    return cresp_val(const_char, n->name);
}

cres(int) graph_node_id(const graph *g, unsigned int node)
{
    struct graph_node *n = g ? graph_node(g, node) : NULL;

    if (!n)     return cres_error(int, CERR_INVALID_INDEX);

    return cres_val(int, n->id);
}

cres(int) graph_node_edge_count(const graph *g, unsigned int node)
{
    if (!g || node >= g->nr_nodes)  return cres_error(int, CERR_INVALID_INDEX);

    int count = 0;
    for (unsigned int i = 0; i < g->nr_nodes; i++) {
        if (i == node)              continue;

        count += !!g->adj[node * g->nr_nodes + i];
        if (g->params.directed)     count += !!g->adj[i * g->nr_nodes + node];
    }

    return cres_val(int, count);
}

cresp(void) graph_node_payload(const graph *g, unsigned int node)
{
    struct graph_node *n = g ? graph_node(g, node) : NULL;

    if (!n)     return cresp_error(void, CERR_INVALID_INDEX);

    return cresp_val(void, n->payload);
}

cerr graph_node_payload_set(graph *g, unsigned int node, void *payload)
{
    struct graph_node *n = g ? graph_node(g, node) : NULL;

    if (!n)                                                             return CERR_INVALID_INDEX;

    if (g->params.node_drop && n->payload && n->payload != payload)     g->params.node_drop(n->payload);
    n->payload = payload;

    return CERR_OK;
}

cresp(void) graph_edge_payload(const graph *g, unsigned int left, unsigned int right)
{
    if (!g || left >= g->nr_nodes || right >= g->nr_nodes)  return cresp_error(void, CERR_INVALID_ARGUMENTS);

    struct graph_edge *e = graph_adj_get(g, left, right);
    if (!e)                                                 return cresp_error(void, CERR_NOT_FOUND);

    return cresp_val(void, e->payload);
}

cerr graph_edge_payload_set(graph *g, unsigned int left, unsigned int right, void *payload)
{
    if (!g || left >= g->nr_nodes || right >= g->nr_nodes)              return CERR_INVALID_ARGUMENTS;

    struct graph_edge *e = graph_adj_get(g, left, right);
    if (!e)                                                             return CERR_NOT_FOUND;

    if (g->params.edge_drop && e->payload && e->payload != payload)     g->params.edge_drop(e->payload);
    e->payload = payload;

    return CERR_OK;
}

cresp(graph) graph_random(prng *rng, unsigned int nr_nodes, unsigned int nr_edges)
{
    if (!rng || nr_nodes < 2)   return cresp_error(graph, CERR_INVALID_ARGUMENTS);

    /* distinct pairs, minus the forbidden direct start-end edge */
    unsigned long long max_edges = (unsigned long long)nr_nodes * (nr_nodes - 1) / 2 - 1;
    if (nr_edges > max_edges)
        return cresp_error(graph, CERR_INVALID_ARGUMENTS,
                           .fmt = "%u nodes fit at most %u edges",
                           .arg0 = (void *)(uintptr_t)nr_nodes,
                           .arg1 = (void *)(uintptr_t)max_edges);

    LOCAL_SET(graph, g) = CRES_RET_T(graph_new(), graph);
    unsigned int start = CRES_RET_T(graph_node_add(g, "start", NULL), graph);
    unsigned int end = CRES_RET_T(graph_node_add(g, "end", NULL), graph);
    for (unsigned int i = 0; i < nr_nodes - 2; i++)
        CRES_RET_T(graph_node_add(g, "room", NULL), graph);

    while (g->nr_edges < nr_edges) {
        unsigned int n0 = CRES_RET_T(prng_range(rng, 0, nr_nodes), graph);
        unsigned int n1 = CRES_RET_T(prng_range(rng, 0, nr_nodes), graph);

        if (n0 == n1)                                                   continue;
        /* no shortcuts directly from start to end */
        if ((n0 == start && n1 == end) || (n0 == end && n1 == start))   continue;

        cerr err = graph_edge_add(g, n0, n1, NULL);
        if (IS_CERR(err) && !IS_CERR_CODE(err, CERR_ALREADY_LOADED))    return cresp_error_cerr(graph, err);
    }

    return cresp_val(graph, NOCU(g));
}

cresp(graph) graph_linear(unsigned int nr_nodes)
{
    if (nr_nodes < 2)   return cresp_error(graph, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(graph, g) = CRES_RET_T(graph_new(), graph);
    CRES_RET_T(graph_node_add(g, "start", NULL), graph);
    for (unsigned int i = 0; i < nr_nodes - 2; i++)
        CRES_RET_T(graph_node_add(g, "room", NULL), graph);
    CRES_RET_T(graph_node_add(g, "end", NULL), graph);

    for (unsigned int i = 0; i < nr_nodes - 1; i++)
        CERR_RET_T(graph_edge_add(g, i, i + 1, NULL), graph);

    return cresp_val(graph, NOCU(g));
}

/*
 * Split a "name_N" label into a name and an id: the suffix counts only
 * when everything after the last underscore is digits; a bare name means
 * id 0
 */
static void graph_parse_label(char *label, int *id)
{
    *id = 0;

    char *us = strrchr(label, '_');
    if (!us || !us[1])  return;

    for (char *d = us + 1; *d; d++)
        if (!isdigit((unsigned char)*d))    return;

    *id = atoi(us + 1);
    *us = 0;
}

/* find-or-create the node a label names */
static cres(int) graph_node_from_label(graph *g, const char *label, size_t len)
{
    LOCAL(char, name);

    name = mem_alloc(len + 1);
    if (!name)              return cres_error(int, CERR_NOMEM);
    memcpy(name, label, len);
    name[len] = 0;

    int id;
    graph_parse_label(name, &id);

    cres(int) found = graph_node_find(g, name, id);
    if (!IS_CERR(found))    return found;

    return graph_node_add_id(g, name, id, NULL);
}

cresp(graph) graph_from_string(const char *str)
{
    if (!str)   return cresp_error(graph, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(graph, g) = CRES_RET_T(graph_new(), graph);

    const char *pos = str;
    while (*(pos = skip_space(pos))) {
        const char *tok_end = skip_nonspace(pos);
        int prev = -1;

        /* a token is a chain of dash-separated labels */
        while (pos < tok_end) {
            const char *dash = memchr(pos, '-', tok_end - pos);
            const char *label_end = dash ? : tok_end;

            int node = CRES_RET_T(graph_node_from_label(g, pos, label_end - pos), graph);
            if (prev >= 0)
                CERR_RET(graph_edge_add(g, prev, node, NULL), {
                    if (!IS_CERR_CODE(__cerr, CERR_ALREADY_LOADED))     return cresp_error_cerr(graph, __cerr);
                });

            prev = node;
            pos = label_end + !!dash;
        }
    }

    return cresp_val(graph, NOCU(g));
}

static void graph_gv_label(const graph *g, const struct graph_node *node, char *buf, size_t size)
{
    if (node->id || graph_node_max_id(g, node->name) > 0)
        snprintf(buf, size, "%s_%d", node->name, node->id);
    else
        snprintf(buf, size, "%s", node->name);
}

void graph_dump_gv(const graph *g, const char *name)
{
    if (!g)     return;

    char left[128], right[128];

    dbg("%s %s {\n", g->params.directed ? "digraph" : "graph", name ? : "G");

    for (unsigned int i = 0; i < g->nr_nodes; i++) {
        cres(int) count = graph_node_edge_count(g, i);

        if (!IS_CERR(count) && !count.val) {
            graph_gv_label(g, graph_node(g, i), left, sizeof(left));
            dbg("    %s;\n", left);
        }
    }

    struct graph_edge *e;
    list_for_each_entry(e, &g->edges, entry) {
        graph_gv_label(g, graph_node(g, e->left), left, sizeof(left));
        graph_gv_label(g, graph_node(g, e->right), right, sizeof(right));
        dbg("    %s %s %s;\n", left, g->params.directed ? "->" : "--", right);
    }

    dbg("}\n");
}
