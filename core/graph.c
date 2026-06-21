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

/*
 * Backtracking subgraph search state. gmap pins the host node bound to
 * each pattern node (-1 while unbound); bindings are injective via used[]
 * and can be masked off with excluded[] (node-disjoint rewriting). rot
 * rotates the candidate scan so a caller can randomize which match comes
 * up first.
 */
struct graph_matcher {
    const graph             *g;
    const graph             *find;
    const struct graph_edge **fedges;
    int                     *gmap;
    bool                    *used;
    const bool              *excluded;
    unsigned int            (*medges)[2];
    unsigned int            nr_fn;
    unsigned int            nr_fe;
    unsigned int            rot;
};

static bool graph_matcher_bindable(const struct graph_matcher *m, unsigned int fnode,
                                   unsigned int gnode)
{
    if (m->used[gnode] || (m->excluded && m->excluded[gnode]))  return false;

    return !strcmp(graph_node(m->find, fnode)->name, graph_node(m->g, gnode)->name);
}

/* second phase: bind pattern nodes that no edge covers */
static bool graph_match_nodes(struct graph_matcher *m, unsigned int fnode)
{
    for (; fnode < m->nr_fn; fnode++)
        if (m->gmap[fnode] < 0)                         break;

    if (fnode == m->nr_fn)  return true;

    for (unsigned int k = 0; k < m->g->nr_nodes; k++) {
        unsigned int cand = (k + m->rot) % m->g->nr_nodes;

        if (!graph_matcher_bindable(m, fnode, cand))    continue;

        m->gmap[fnode] = cand;
        m->used[cand] = true;
        if (graph_match_nodes(m, fnode + 1))            return true;
        m->gmap[fnode] = -1;
        m->used[cand] = false;
    }

    return false;
}

/* first phase: bind pattern edges to host edges, one per recursion level */
static bool graph_match_edges(struct graph_matcher *m, unsigned int edge)
{
    if (edge == m->nr_fe)   return graph_match_nodes(m, 0);

    const struct graph_edge *fe = m->fedges[edge];
    unsigned int nr = m->g->nr_nodes;
    int bl = m->gmap[fe->left];

    for (unsigned int k = 0; k < nr; k++) {
        unsigned int gl = (k + m->rot) % nr;
        bool bound_l = bl >= 0;

        if (bound_l) {
            if (k)                                  break;
            gl = bl;
        } else if (!graph_matcher_bindable(m, fe->left, gl)) {
            continue;
        }

        if (!bound_l) {
            m->gmap[fe->left] = gl;
            m->used[gl] = true;
        }

        int br = m->gmap[fe->right];
        for (unsigned int k2 = 0; k2 < nr; k2++) {
            unsigned int gr = (k2 + m->rot) % nr;
            bool bound_r = br >= 0;

            if (bound_r) {
                if (k2)     break;
                gr = br;
            } else if (!graph_matcher_bindable(m, fe->right, gr)) {
                continue;
            }

            if (!graph_has_edge(m->g, gl, gr))      continue;

            /* distinct pattern edges must land on distinct host edges */
            unsigned int p;
            for (p = 0; p < edge; p++)
                if ((m->medges[p][0] == gl && m->medges[p][1] == gr) ||
                    (!m->g->params.directed && m->medges[p][0] == gr && m->medges[p][1] == gl))
                    break;
            if (p < edge)                           continue;

            if (!bound_r) {
                m->gmap[fe->right] = gr;
                m->used[gr] = true;
            }
            m->medges[edge][0] = gl;
            m->medges[edge][1] = gr;

            if (graph_match_edges(m, edge + 1))     return true;

            if (!bound_r) {
                m->gmap[fe->right] = -1;
                m->used[gr] = false;
            }
        }

        if (!bound_l) {
            m->gmap[fe->left] = -1;
            m->used[gl] = false;
        }
    }

    return false;
}

static cerr graph_match_first(const graph *g, const graph *find, unsigned int rot,
                              const bool *excluded, graph_match *match)
{
    if (find->nr_nodes > g->nr_nodes)   return CERR_NOT_FOUND;

    struct graph_matcher m = {
        .g          = g,
        .find       = find,
        .excluded   = excluded,
        .nr_fn      = find->nr_nodes,
        .nr_fe      = find->nr_edges,
        .rot        = rot % g->nr_nodes,
    };

    LOCAL(void, fedges_buf);
    LOCAL(void, gmap_buf);
    LOCAL(void, used_buf);
    LOCAL(void, medges_buf);

    gmap_buf = mem_alloc(sizeof(*m.gmap), .nr = m.nr_fn);
    used_buf = mem_alloc(sizeof(*m.used), .nr = g->nr_nodes, .zero = 1);
    if (!gmap_buf || !used_buf)         return CERR_NOMEM;

    if (m.nr_fe) {
        fedges_buf = mem_alloc(sizeof(*m.fedges), .nr = m.nr_fe);
        medges_buf = mem_alloc(sizeof(*m.medges), .nr = m.nr_fe);
        if (!fedges_buf || !medges_buf)     return CERR_NOMEM;
    }

    m.fedges = fedges_buf;
    m.gmap = gmap_buf;
    m.used = used_buf;
    m.medges = medges_buf;

    for (unsigned int i = 0; i < m.nr_fn; i++)
        m.gmap[i] = -1;

    unsigned int i = 0;
    struct graph_edge *e;
    list_for_each_entry(e, &((graph *)find)->edges, entry)
        m.fedges[i++] = e;

    if (!graph_match_edges(&m, 0))      return CERR_NOT_FOUND;

    match->node_map = mem_alloc(sizeof(*match->node_map), .nr = m.nr_fn);
    match->edges = m.nr_fe ? mem_alloc(sizeof(*match->edges), .nr = m.nr_fe) : NULL;
    if (!match->node_map || (m.nr_fe && !match->edges)) {
        graph_match_release(match);
        return CERR_NOMEM;
    }

    for (i = 0; i < m.nr_fn; i++)
        match->node_map[i] = m.gmap[i];
    if (m.nr_fe)                        memcpy(match->edges, m.medges, sizeof(*match->edges) * m.nr_fe);
    match->nr_nodes = m.nr_fn;
    match->nr_edges = m.nr_fe;

    return CERR_OK;
}

cerr graph_subgraph(const graph *g, const graph *find, graph_match *match)
{
    if (!g || !find || !match || !find->nr_nodes)   return CERR_INVALID_ARGUMENTS;

    return graph_match_first(g, find, 0, NULL, match);
}

void graph_match_release(graph_match *match)
{
    if (!match)     return;

    mem_free(match->node_map);
    mem_free(match->edges);
    memset(match, 0, sizeof(*match));
}

static cerr graph_rewrite_apply(graph *g, const graph *find, const graph *replace,
                                const graph_match *match)
{
    /* the matched edges go away, payloads and all */
    for (unsigned int i = 0; i < match->nr_edges; i++)
        CERR_RET_CERR(graph_edge_del(g, match->edges[i][0], match->edges[i][1]));

    if (!replace->nr_nodes)     return CERR_OK;

    /*
     * Map replacement nodes into the host: labels shared with the
     * pattern reuse the match's bindings, the rest come into being
     */
    LOCAL(void, rmap_buf);
    rmap_buf = mem_alloc(sizeof(unsigned int), .nr = replace->nr_nodes);
    if (!rmap_buf)              return CERR_NOMEM;
    unsigned int *rmap = rmap_buf;

    for (unsigned int i = 0; i < replace->nr_nodes; i++) {
        struct graph_node *rn = graph_node(replace, i);
        cres(int) fi = graph_node_find(find, rn->name, rn->id);

        if (!IS_CERR(fi))
            rmap[i] = match->node_map[fi.val];
        else
            rmap[i] = CRES_RET(graph_node_add(g, rn->name, NULL),
                               return cerr_error_cres(__resp));
    }

    struct graph_edge *re;
    list_for_each_entry(re, &((graph *)replace)->edges, entry)
        CERR_RET(graph_edge_add(g, rmap[re->left], rmap[re->right], NULL), {
            if (!IS_CERR_CODE(__cerr, CERR_ALREADY_LOADED))     return __cerr;
        });

    return CERR_OK;
}

cerr graph_rewrite_once(graph *g, const graph *find, const graph *replace, prng *rng)
{
    if (!g || !find || !replace || !find->nr_nodes)
        return CERR_INVALID_ARGUMENTS;

    unsigned int rot = 0;
    if (rng && g->nr_nodes)     rot = CRES_RET(prng_range(rng, 0, g->nr_nodes), return cerr_error_cres(__resp));

    graph_match match = {};
    CERR_RET_CERR(graph_match_first(g, find, rot, NULL, &match));

    cerr err = graph_rewrite_apply(g, find, replace, &match);
    graph_match_release(&match);

    return err;
}

cres(int) graph_rewrite_all(graph *g, const graph *find, const graph *replace, prng *rng)
{
    if (!g || !find || !replace || !find->nr_nodes)     return cres_error(int, CERR_INVALID_ARGUMENTS);

    /*
     * Collect node-disjoint matches against the graph as it stands, only
     * then rewrite: one generation per call, and rules producing more of
     * their own pattern can't chase their tail
     */
    LOCAL(void, excluded_buf);
    bool *excluded = NULL;
    if (g->nr_nodes) {
        excluded_buf = mem_alloc(sizeof(*excluded), .nr = g->nr_nodes, .zero = 1);
        if (!excluded_buf)                          return cres_error(int, CERR_NOMEM);
        excluded = excluded_buf;
    }

    darray(graph_match, matches);
    darray_init(matches);

    cerr err = CERR_OK;
    int count = 0;
    graph_match *m;

    for (;;) {
        unsigned int rot = 0;
        if (rng && g->nr_nodes)
            rot = CRES_RET(prng_range(rng, 0, g->nr_nodes), { err = cerr_error_cres(__resp); goto out; });

        graph_match match = {};
        cerr found = graph_match_first(g, find, rot, excluded, &match);
        if (IS_CERR_CODE(found, CERR_NOT_FOUND))    break;
        if (IS_CERR(found)) {
            err = found;
            goto out;
        }

        for (unsigned int i = 0; i < match.nr_nodes; i++)
            excluded[match.node_map[i]] = true;

        graph_match *slot = darray_add(matches);
        if (!slot) {
            graph_match_release(&match);
            err = CERR_NOMEM;
            goto out;
        }
        *slot = match;
    }

    darray_for_each(m, matches) {
        err = graph_rewrite_apply(g, find, replace, m);
        if (IS_CERR(err))                           break;
        count++;
    }

out:
    darray_for_each(m, matches)
        graph_match_release(m);
    darray_clearout(matches);

    if (IS_CERR(err))                                   return cres_error_cerr(int, err);

    return cres_val(int, count);
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
