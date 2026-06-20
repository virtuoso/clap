// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "graph.h"
#include "prng.h"
#include "test.h"
#include "util.h"

static void graph_test_drop(void *payload)
{
    (*(int *)payload)++;
}

static int graph_basic_test(void)
{
    int node_drops = 0, edge_drops = 0;

    LOCAL_SET(graph, g) = CRES_RET(graph_new(.node_drop = graph_test_drop,
                                             .edge_drop = graph_test_drop),
                                   return EXIT_FAILURE);

    int a  = CRES_RET(graph_node_add(g, "a", &node_drops), return EXIT_FAILURE);
    int a1 = CRES_RET(graph_node_add(g, "a", NULL), return EXIT_FAILURE);
    int b  = CRES_RET(graph_node_add(g, "b", NULL), return EXIT_FAILURE);

    if (graph_nr_nodes(g) != 3)                                                     return EXIT_FAILURE;
    if (CRES_RET(graph_node_id(g, a), return EXIT_FAILURE) != 0)                    return EXIT_FAILURE;
    if (CRES_RET(graph_node_id(g, a1), return EXIT_FAILURE) != 1)                   return EXIT_FAILURE;
    if (CRES_RET(graph_node_id(g, b), return EXIT_FAILURE) != 0)                    return EXIT_FAILURE;
    if (strcmp(CRES_RET(graph_node_name(g, a1), return EXIT_FAILURE), "a"))         return EXIT_FAILURE;

    if (CRES_RET(graph_node_find(g, "a", 1), return EXIT_FAILURE) != a1)            return EXIT_FAILURE;
    cres(int) missing = graph_node_find(g, "a", 2);
    if (!IS_CERR_CODE(missing, CERR_NOT_FOUND))                                     return EXIT_FAILURE;

    cres(int) dup = graph_node_add_id(g, "a", 0, NULL);
    if (!IS_CERR_CODE(dup, CERR_ALREADY_LOADED))                                    return EXIT_FAILURE;

    /* automatic ids pick up after the highest existing one */
    int c = CRES_RET(graph_node_add_id(g, "c", 7, NULL), return EXIT_FAILURE);
    int c8 = CRES_RET(graph_node_add(g, "c", NULL), return EXIT_FAILURE);
    if (CRES_RET(graph_node_id(g, c8), return EXIT_FAILURE) != 8)                   return EXIT_FAILURE;

    CERR_RET(graph_edge_add(g, a, b, NULL), return EXIT_FAILURE);
    if (!graph_has_edge(g, a, b) || !graph_has_edge(g, b, a))                       return EXIT_FAILURE;

    cerr err = graph_edge_add(g, b, a, NULL);
    if (!IS_CERR_CODE(err, CERR_ALREADY_LOADED))                                    return EXIT_FAILURE;
    err = graph_edge_add(g, a, a, NULL);
    if (!IS_CERR(err))                                                              return EXIT_FAILURE;
    err = graph_edge_add(g, a, 100, NULL);
    if (!IS_CERR(err))                                                              return EXIT_FAILURE;

    CERR_RET(graph_edge_add(g, a1, b, NULL), return EXIT_FAILURE);
    if (graph_nr_edges(g) != 2)                                                     return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, b), return EXIT_FAILURE) != 2)            return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, a), return EXIT_FAILURE) != 1)            return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, c), return EXIT_FAILURE) != 0)            return EXIT_FAILURE;

    if (CRES_RET(graph_node_payload(g, a), return EXIT_FAILURE) != &node_drops)     return EXIT_FAILURE;
    /* replacing a payload drops the previous one */
    CERR_RET(graph_node_payload_set(g, a, NULL), return EXIT_FAILURE);
    if (node_drops != 1)                                                            return EXIT_FAILURE;

    CERR_RET(graph_edge_payload_set(g, a, b, &edge_drops), return EXIT_FAILURE);
    /* an undirected edge is reachable from both ends */
    if (CRES_RET(graph_edge_payload(g, b, a), return EXIT_FAILURE) != &edge_drops)  return EXIT_FAILURE;
    cresp(void) nope = graph_edge_payload(g, a, c);
    if (!IS_CERR_CODE(nope, CERR_NOT_FOUND))                                        return EXIT_FAILURE;

    /* deleting an edge drops its payload */
    CERR_RET(graph_edge_del(g, b, a), return EXIT_FAILURE);
    if (edge_drops != 1)                                                            return EXIT_FAILURE;
    if (graph_has_edge(g, a, b) || graph_nr_edges(g) != 1)                          return EXIT_FAILURE;
    err = graph_edge_del(g, a, b);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))                                         return EXIT_FAILURE;

    /* freeing the graph drops what's left */
    CERR_RET(graph_node_payload_set(g, b, &node_drops), return EXIT_FAILURE);
    CERR_RET(graph_edge_payload_set(g, a1, b, &edge_drops), return EXIT_FAILURE);
    graph_free(NOCU(g));
    if (node_drops != 2 || edge_drops != 2)                                         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int graph_directed_test(void)
{
    LOCAL_SET(graph, g) = CRES_RET(graph_new(.directed = true), return EXIT_FAILURE);

    int a = CRES_RET(graph_node_add(g, "a", NULL), return EXIT_FAILURE);
    int b = CRES_RET(graph_node_add(g, "b", NULL), return EXIT_FAILURE);
    int c = CRES_RET(graph_node_add(g, "c", NULL), return EXIT_FAILURE);

    CERR_RET(graph_edge_add(g, a, b, NULL), return EXIT_FAILURE);
    if (!graph_has_edge(g, a, b) || graph_has_edge(g, b, a))                return EXIT_FAILURE;

    /* the reverse direction is a separate edge with its own payload */
    CERR_RET(graph_edge_add(g, b, a, NULL), return EXIT_FAILURE);
    if (graph_nr_edges(g) != 2)                                             return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, a), return EXIT_FAILURE) != 2)    return EXIT_FAILURE;
    CERR_RET(graph_edge_payload_set(g, a, b, &a), return EXIT_FAILURE);
    if (CRES_RET(graph_edge_payload(g, b, a), return EXIT_FAILURE))         return EXIT_FAILURE;

    CERR_RET(graph_edge_del(g, a, b), return EXIT_FAILURE);
    if (graph_has_edge(g, a, b) || !graph_has_edge(g, b, a))                return EXIT_FAILURE;

    CERR_RET(graph_edge_add(g, b, c, NULL), return EXIT_FAILURE);
    if (CRES_RET(graph_node_edge_count(g, b), return EXIT_FAILURE) != 2)    return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, a), return EXIT_FAILURE) != 1)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int graph_from_string_test(void)
{
    LOCAL_SET(graph, g) = CRES_RET(graph_from_string("start-a a-b b-end"), return EXIT_FAILURE);
    if (graph_nr_nodes(g) != 4 || graph_nr_edges(g) != 3)                       return EXIT_FAILURE;

    int start = CRES_RET(graph_node_find(g, "start", 0), return EXIT_FAILURE);
    int aa = CRES_RET(graph_node_find(g, "a", 0), return EXIT_FAILURE);
    int bb = CRES_RET(graph_node_find(g, "b", 0), return EXIT_FAILURE);
    if (!graph_has_edge(g, start, aa) || !graph_has_edge(g, aa, bb))            return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, start), return EXIT_FAILURE) != 1)    return EXIT_FAILURE;

    /* dash chains and isolated nodes */
    LOCAL_SET(graph, chain) = CRES_RET(graph_from_string("x-y-z w"), return EXIT_FAILURE);
    if (graph_nr_nodes(chain) != 4 || graph_nr_edges(chain) != 2)               return EXIT_FAILURE;
    int x = CRES_RET(graph_node_find(chain, "x", 0), return EXIT_FAILURE);
    int y = CRES_RET(graph_node_find(chain, "y", 0), return EXIT_FAILURE);
    int z = CRES_RET(graph_node_find(chain, "z", 0), return EXIT_FAILURE);
    int w = CRES_RET(graph_node_find(chain, "w", 0), return EXIT_FAILURE);
    if (!graph_has_edge(chain, x, y) || !graph_has_edge(chain, y, z))           return EXIT_FAILURE;
    if (graph_has_edge(chain, x, z))                                            return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(chain, w), return EXIT_FAILURE) != 0)    return EXIT_FAILURE;

    /* _N suffixes address same-name nodes, other underscores stay in names */
    LOCAL_SET(graph, ids) = CRES_RET(graph_from_string("a-a_1 a_1-a_2 big_room_2-a"),
                                     return EXIT_FAILURE);
    if (graph_nr_nodes(ids) != 4 || graph_nr_edges(ids) != 3)                   return EXIT_FAILURE;
    int a2 = CRES_RET(graph_node_find(ids, "a", 2), return EXIT_FAILURE);
    int br = CRES_RET(graph_node_find(ids, "big_room", 2), return EXIT_FAILURE);
    int a0 = CRES_RET(graph_node_find(ids, "a", 0), return EXIT_FAILURE);
    if (!graph_has_edge(ids, br, a0) || graph_has_edge(ids, br, a2))            return EXIT_FAILURE;
    /* automatic ids continue above the ones the string spelled out */
    int a3 = CRES_RET(graph_node_add(ids, "a", NULL), return EXIT_FAILURE);
    if (CRES_RET(graph_node_id(ids, a3), return EXIT_FAILURE) != 3)             return EXIT_FAILURE;

    /* repeated edges collapse */
    LOCAL_SET(graph, dupg) = CRES_RET(graph_from_string("a-b b-a a-b"), return EXIT_FAILURE);
    if (graph_nr_nodes(dupg) != 2 || graph_nr_edges(dupg) != 1)                 return EXIT_FAILURE;

    cresp(graph) bad = graph_from_string("a-a");
    if (!IS_CERR(bad))                                                          return EXIT_FAILURE;
    bad = graph_from_string(NULL);
    if (!IS_CERR(bad))                                                          return EXIT_FAILURE;

    LOCAL_SET(graph, empty) = CRES_RET(graph_from_string("  "), return EXIT_FAILURE);
    if (graph_nr_nodes(empty))                                                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int graph_generators_test(void)
{
    LOCAL_SET(graph, lin) = CRES_RET(graph_linear(5), return EXIT_FAILURE);
    if (graph_nr_nodes(lin) != 5 || graph_nr_edges(lin) != 4)                   return EXIT_FAILURE;
    if (CRES_RET(graph_node_find(lin, "start", 0), return EXIT_FAILURE) != 0)   return EXIT_FAILURE;
    if (CRES_RET(graph_node_find(lin, "end", 0), return EXIT_FAILURE) != 4)     return EXIT_FAILURE;
    for (unsigned int i = 0; i < 5; i++) {
        int expected = !i || i == 4 ? 1 : 2;

        if (CRES_RET(graph_node_edge_count(lin, i), return EXIT_FAILURE) != expected)   return EXIT_FAILURE;
        if (i && !graph_has_edge(lin, i - 1, i))                                        return EXIT_FAILURE;
    }

    cresp(graph) badg = graph_linear(1);
    if (!IS_CERR(badg))                                                         return EXIT_FAILURE;

    LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 3), return EXIT_FAILURE);
    LOCAL_SET(graph, r1) = CRES_RET(graph_random(rng, 8, 12), return EXIT_FAILURE);
    if (graph_nr_nodes(r1) != 8 || graph_nr_edges(r1) != 12)                    return EXIT_FAILURE;
    /* no direct start-end shortcut */
    if (graph_has_edge(r1, 0, 1))                                               return EXIT_FAILURE;

    /* deterministic given the seed */
    LOCAL_SET(prng, rng2) = CRES_RET(prng_new(PRNG_DEFAULT, 3), return EXIT_FAILURE);
    LOCAL_SET(graph, r2) = CRES_RET(graph_random(rng2, 8, 12), return EXIT_FAILURE);
    for (unsigned int i = 0; i < 8; i++)
        for (unsigned int j = 0; j < 8; j++)
            if (graph_has_edge(r1, i, j) != graph_has_edge(r2, i, j))   return EXIT_FAILURE;

    /* saturation: every pair of 4 nodes except the start-end one */
    LOCAL_SET(graph, full) = CRES_RET(graph_random(rng, 4, 5), return EXIT_FAILURE);
    if (graph_nr_edges(full) != 5 || graph_has_edge(full, 0, 1))                return EXIT_FAILURE;

    badg = graph_random(rng, 4, 6);
    if (!IS_CERR(badg))                                                         return EXIT_FAILURE;
    badg = graph_random(NULL, 4, 2);
    if (!IS_CERR(badg))                                                         return EXIT_FAILURE;
    badg = graph_random(rng, 1, 0);
    if (!IS_CERR(badg))                                                         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "graph basic", .test = graph_basic_test },
    { .name = "graph directed", .test = graph_directed_test },
    { .name = "graph from string", .test = graph_from_string_test },
    { .name = "graph generators", .test = graph_generators_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
