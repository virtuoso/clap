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

static int graph_subgraph_test(void)
{
    LOCAL_SET(graph, g) = CRES_RET(graph_from_string("start-a a-b b-end"), return EXIT_FAILURE);
    LOCAL_SET(graph, pat) = CRES_RET(graph_from_string("a-b end"), return EXIT_FAILURE);
    graph_match match = {};

    /* one edge, one isolated pattern node; ids checked on the host side */
    CERR_RET(graph_subgraph(g, pat, &match), return EXIT_FAILURE);
    if (match.nr_nodes != 3 || match.nr_edges != 1)                                         return EXIT_FAILURE;

    int pa = CRES_RET(graph_node_find(pat, "a", 0), return EXIT_FAILURE);
    int pb = CRES_RET(graph_node_find(pat, "b", 0), return EXIT_FAILURE);
    int pe = CRES_RET(graph_node_find(pat, "end", 0), return EXIT_FAILURE);
    int ga = CRES_RET(graph_node_find(g, "a", 0), return EXIT_FAILURE);
    int gb = CRES_RET(graph_node_find(g, "b", 0), return EXIT_FAILURE);
    int ge = CRES_RET(graph_node_find(g, "end", 0), return EXIT_FAILURE);

    if (match.node_map[pa] != (unsigned int)ga)                                             return EXIT_FAILURE;
    if (match.node_map[pb] != (unsigned int)gb)                                             return EXIT_FAILURE;
    if (match.node_map[pe] != (unsigned int)ge)                                             return EXIT_FAILURE;
    if (match.edges[0][0] != (unsigned int)ga || match.edges[0][1] != (unsigned int)gb)     return EXIT_FAILURE;
    graph_match_release(&match);
    graph_match_release(&match);

    /* same-name pattern variables bind distinct connected hosts */
    LOCAL_SET(graph, rooms) = CRES_RET(graph_from_string("room-room_1 room_1-room_2"),
                                       return EXIT_FAILURE);
    LOCAL_SET(graph, pair) = CRES_RET(graph_from_string("room-room_1"), return EXIT_FAILURE);
    CERR_RET(graph_subgraph(rooms, pair, &match), return EXIT_FAILURE);
    if (match.node_map[0] == match.node_map[1])                                             return EXIT_FAILURE;
    if (!graph_has_edge(rooms, match.node_map[0], match.node_map[1]))                       return EXIT_FAILURE;
    graph_match_release(&match);

    /* injectivity: two pattern variables need two host nodes */
    LOCAL_SET(graph, single) = CRES_RET(graph_from_string("a-b"), return EXIT_FAILURE);
    LOCAL_SET(graph, twoas) = CRES_RET(graph_from_string("a-a_1"), return EXIT_FAILURE);
    cerr err = graph_subgraph(single, twoas, &match);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))                                                 return EXIT_FAILURE;

    /* names that the host doesn't have don't match */
    LOCAL_SET(graph, noz) = CRES_RET(graph_from_string("a-z"), return EXIT_FAILURE);
    err = graph_subgraph(g, noz, &match);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))                                                 return EXIT_FAILURE;

    /*
     * Backtracking: the scan meets a-b first (a, b at indices 0, 1), but
     * only a_1-b_1 b_1-a extends to the a-b b-a_1 path
     */
    LOCAL_SET(graph, maze) = CRES_RET(graph_from_string("a-b a_1-b_1 b_1-a"),
                                      return EXIT_FAILURE);
    LOCAL_SET(graph, path) = CRES_RET(graph_from_string("a-b b-a_1"), return EXIT_FAILURE);
    CERR_RET(graph_subgraph(maze, path, &match), return EXIT_FAILURE);

    int fa = CRES_RET(graph_node_find(path, "a", 0), return EXIT_FAILURE);
    int fb = CRES_RET(graph_node_find(path, "b", 0), return EXIT_FAILURE);
    int fa1 = CRES_RET(graph_node_find(path, "a", 1), return EXIT_FAILURE);
    if (match.node_map[fa] == match.node_map[fa1])                                          return EXIT_FAILURE;
    if (!graph_has_edge(maze, match.node_map[fa], match.node_map[fb]))                      return EXIT_FAILURE;
    if (!graph_has_edge(maze, match.node_map[fb], match.node_map[fa1]))                     return EXIT_FAILURE;
    graph_match_release(&match);

    /* directed hosts only match the pattern's orientation */
    LOCAL_SET(graph, dg) = CRES_RET(graph_new(.directed = true), return EXIT_FAILURE);
    int da = CRES_RET(graph_node_add(dg, "a", NULL), return EXIT_FAILURE);
    int db = CRES_RET(graph_node_add(dg, "b", NULL), return EXIT_FAILURE);
    CERR_RET(graph_edge_add(dg, da, db, NULL), return EXIT_FAILURE);

    LOCAL_SET(graph, fwd) = CRES_RET(graph_from_string("a-b"), return EXIT_FAILURE);
    LOCAL_SET(graph, rev) = CRES_RET(graph_from_string("b-a"), return EXIT_FAILURE);
    CERR_RET(graph_subgraph(dg, fwd, &match), return EXIT_FAILURE);
    if (match.edges[0][0] != (unsigned int)da || match.edges[0][1] != (unsigned int)db)     return EXIT_FAILURE;
    graph_match_release(&match);
    err = graph_subgraph(dg, rev, &match);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))                                                 return EXIT_FAILURE;

    /* an empty pattern matches nothing meaningfully */
    LOCAL_SET(graph, emptyp) = CRES_RET(graph_new(), return EXIT_FAILURE);
    err = graph_subgraph(g, emptyp, &match);
    if (!IS_CERR(err) || IS_CERR_CODE(err, CERR_NOT_FOUND))                                 return EXIT_FAILURE;
    err = graph_subgraph(g, pat, NULL);
    if (!IS_CERR(err))                                                                      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int graph_rewrite_test(void)
{
    /* the canonical splice: start-a becomes start-b b-a */
    LOCAL_SET(graph, g) = CRES_RET(graph_from_string("start-a a-end"), return EXIT_FAILURE);
    LOCAL_SET(graph, find) = CRES_RET(graph_from_string("start-a"), return EXIT_FAILURE);
    LOCAL_SET(graph, repl) = CRES_RET(graph_from_string("start-b b-a"), return EXIT_FAILURE);

    int gstart = CRES_RET(graph_node_find(g, "start", 0), return EXIT_FAILURE);
    int ga = CRES_RET(graph_node_find(g, "a", 0), return EXIT_FAILURE);
    int gend = CRES_RET(graph_node_find(g, "end", 0), return EXIT_FAILURE);
    int marker = 0;
    CERR_RET(graph_node_payload_set(g, ga, &marker), return EXIT_FAILURE);

    CERR_RET(graph_rewrite_once(g, find, repl, NULL), return EXIT_FAILURE);

    if (graph_nr_nodes(g) != 4 || graph_nr_edges(g) != 3)                       return EXIT_FAILURE;
    int gb = CRES_RET(graph_node_find(g, "b", 0), return EXIT_FAILURE);
    if (graph_has_edge(g, gstart, ga))                                          return EXIT_FAILURE;
    if (!graph_has_edge(g, gstart, gb) || !graph_has_edge(g, gb, ga))           return EXIT_FAILURE;
    if (!graph_has_edge(g, ga, gend))                                           return EXIT_FAILURE;
    /* matched nodes stay put, payload and all */
    if (CRES_RET(graph_node_payload(g, ga), return EXIT_FAILURE) != &marker)    return EXIT_FAILURE;

    /* nothing left to match, nothing changes */
    cerr err = graph_rewrite_once(g, find, repl, NULL);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))                                     return EXIT_FAILURE;
    if (graph_nr_nodes(g) != 4 || graph_nr_edges(g) != 3)                       return EXIT_FAILURE;

    /* a seeded rewrite splices a mid into some pair of connected rooms */
    LOCAL_SET(graph, corr) = CRES_RET(graph_linear(6), return EXIT_FAILURE);
    LOCAL_SET(graph, pair) = CRES_RET(graph_from_string("room-room_1"), return EXIT_FAILURE);
    LOCAL_SET(graph, splice) = CRES_RET(graph_from_string("room-mid mid-room_1"),
                                        return EXIT_FAILURE);
    LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 5), return EXIT_FAILURE);

    CERR_RET(graph_rewrite_once(corr, pair, splice, rng), return EXIT_FAILURE);
    if (graph_nr_nodes(corr) != 7 || graph_nr_edges(corr) != 6)                 return EXIT_FAILURE;
    int mid = CRES_RET(graph_node_find(corr, "mid", 0), return EXIT_FAILURE);
    if (CRES_RET(graph_node_edge_count(corr, mid), return EXIT_FAILURE) != 2)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int graph_rewrite_all_test(void)
{
    /* one generation: disjoint room pairs each get a mid spliced in */
    LOCAL_SET(graph, g) = CRES_RET(graph_linear(6), return EXIT_FAILURE);
    LOCAL_SET(graph, pair) = CRES_RET(graph_from_string("room-room_1"), return EXIT_FAILURE);
    LOCAL_SET(graph, splice) = CRES_RET(graph_from_string("room-mid mid-room_1"),
                                        return EXIT_FAILURE);

    int n = CRES_RET(graph_rewrite_all(g, pair, splice, NULL), return EXIT_FAILURE);
    if (n != 2)                                                                     return EXIT_FAILURE;
    if (graph_nr_nodes(g) != 8 || graph_nr_edges(g) != 7)                           return EXIT_FAILURE;
    /* each application drew its own fresh mid */
    int m0 = CRES_RET(graph_node_find(g, "mid", 0), return EXIT_FAILURE);
    int m1 = CRES_RET(graph_node_find(g, "mid", 1), return EXIT_FAILURE);
    if (CRES_RET(graph_node_edge_count(g, m0), return EXIT_FAILURE) != 2)           return EXIT_FAILURE;
    if (CRES_RET(graph_node_edge_count(g, m1), return EXIT_FAILURE) != 2)           return EXIT_FAILURE;
    /* rooms 1-2 and 3-4 were split up, the 2-3 link between pairs survives */
    if (graph_has_edge(g, 1, 2) || graph_has_edge(g, 3, 4))                         return EXIT_FAILURE;
    if (!graph_has_edge(g, 2, 3))                                                   return EXIT_FAILURE;

    /* a growing rule applies once per room per generation and terminates */
    LOCAL_SET(graph, dungeon) = CRES_RET(graph_linear(5), return EXIT_FAILURE);
    LOCAL_SET(graph, room) = CRES_RET(graph_from_string("room"), return EXIT_FAILURE);
    LOCAL_SET(graph, loot) = CRES_RET(graph_from_string("room-loot"), return EXIT_FAILURE);

    n = CRES_RET(graph_rewrite_all(dungeon, room, loot, NULL), return EXIT_FAILURE);
    if (n != 3 || graph_nr_nodes(dungeon) != 8)                                     return EXIT_FAILURE;
    n = CRES_RET(graph_rewrite_all(dungeon, room, loot, NULL), return EXIT_FAILURE);
    if (n != 3 || graph_nr_nodes(dungeon) != 11)                                    return EXIT_FAILURE;
    int l5 = CRES_RET(graph_node_find(dungeon, "loot", 5), return EXIT_FAILURE);
    if (CRES_RET(graph_node_edge_count(dungeon, l5), return EXIT_FAILURE) != 1)     return EXIT_FAILURE;

    /* no match is a zero count, not an error */
    LOCAL_SET(graph, vault) = CRES_RET(graph_from_string("vault"), return EXIT_FAILURE);
    n = CRES_RET(graph_rewrite_all(dungeon, vault, loot, NULL), return EXIT_FAILURE);
    if (n)                                                                          return EXIT_FAILURE;

    /* directed hosts rewrite with orientation intact */
    LOCAL_SET(graph, dg) = CRES_RET(graph_new(.directed = true), return EXIT_FAILURE);
    int da = CRES_RET(graph_node_add(dg, "a", NULL), return EXIT_FAILURE);
    int de = CRES_RET(graph_node_add(dg, "end", NULL), return EXIT_FAILURE);
    CERR_RET(graph_edge_add(dg, da, de, NULL), return EXIT_FAILURE);

    LOCAL_SET(graph, dfind) = CRES_RET(graph_from_string("a-end"), return EXIT_FAILURE);
    LOCAL_SET(graph, drepl) = CRES_RET(graph_from_string("a-c c-end"), return EXIT_FAILURE);
    CERR_RET(graph_rewrite_once(dg, dfind, drepl, NULL), return EXIT_FAILURE);
    int dc = CRES_RET(graph_node_find(dg, "c", 0), return EXIT_FAILURE);
    if (graph_has_edge(dg, da, de))                                                 return EXIT_FAILURE;
    if (!graph_has_edge(dg, da, dc) || graph_has_edge(dg, dc, da))                  return EXIT_FAILURE;
    if (!graph_has_edge(dg, dc, de))                                                return EXIT_FAILURE;

    /* empty patterns are refused */
    LOCAL_SET(graph, none) = CRES_RET(graph_new(), return EXIT_FAILURE);
    cerr err = graph_rewrite_once(dg, none, drepl, NULL);
    if (!IS_CERR(err) || IS_CERR_CODE(err, CERR_NOT_FOUND))                         return EXIT_FAILURE;
    cres(int) bad = graph_rewrite_all(dg, none, drepl, NULL);
    if (!IS_CERR(bad))                                                              return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "graph basic", .test = graph_basic_test },
    { .name = "graph directed", .test = graph_directed_test },
    { .name = "graph from string", .test = graph_from_string_test },
    { .name = "graph generators", .test = graph_generators_test },
    { .name = "graph subgraph", .test = graph_subgraph_test },
    { .name = "graph rewrite once", .test = graph_rewrite_test },
    { .name = "graph rewrite all", .test = graph_rewrite_all_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
