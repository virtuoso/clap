/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GRAPH_H__
#define __CLAP_GRAPH_H__

#include "error.h"
#include "prng.h"
#include "util.h"

/*
 * A small labeled graph for PCG layout work: nodes are (name, id) pairs
 * ("room" 0, "room" 1, ...), edges live in both an edge list and an
 * adjacency matrix, and both can carry a caller-owned payload. Node
 * indices are dense and stable: nodes are never removed.
 */

typedef struct graph graph;

/**
 * struct graph_params - graph_new() parameters
 * @node_drop:  called on each node's non-NULL payload when it is released
 * @edge_drop:  called on each edge's non-NULL payload when it is released
 * @directed:   edges are one-way (left to right); default is undirected
 */
typedef struct graph_params {
    void    (*node_drop)(void *payload);
    void    (*edge_drop)(void *payload);
    bool    directed;
} graph_params;

cresp_ret(graph);
DECLARE_CLEANUP(graph);

/**
 * graph_new() - create an empty graph
 * @args:   graph_params initializers
 *
 * Return: the new graph to be graph_free()d by the caller, or CERR_NOMEM.
 */
#define graph_new(args...) _graph_new(&(graph_params){ args })
cresp_check(graph) _graph_new(const graph_params *params);

/**
 * graph_free() - free a graph
 * @g:  the graph, may be NULL
 *
 * Runs the graph_params drop hooks on every non-NULL payload.
 */
void graph_free(graph *g);

/**
 * graph_node_add() - add a node with an automatic id
 * @g:          the graph
 * @name:       node name, copied
 * @payload:    optional caller-owned payload
 *
 * The node gets the smallest id greater than every existing @name node's
 * id, so (name, id) stays unique.
 *
 * Return: the new node's index, or CERR_NOMEM / CERR_INVALID_ARGUMENTS.
 */
cres_check(int) graph_node_add(graph *g, const char *name, void *payload);

/**
 * graph_node_add_id() - add a node with an explicit id
 * @g:          the graph
 * @name:       node name, copied
 * @id:         node id, distinguishes same-name nodes
 * @payload:    optional caller-owned payload
 *
 * Return: the new node's index, CERR_ALREADY_LOADED if (@name, @id)
 * exists, or CERR_NOMEM / CERR_INVALID_ARGUMENTS.
 */
cres_check(int) graph_node_add_id(graph *g, const char *name, int id, void *payload);

/**
 * graph_edge_add() - connect two nodes
 * @g:          the graph
 * @left:       node index; the edge tail when the graph is directed
 * @right:      node index; the edge head when the graph is directed
 * @payload:    optional caller-owned payload
 *
 * Return: CERR_OK, CERR_ALREADY_LOADED if the edge exists (its payload is
 * left alone), or CERR_NOMEM / CERR_INVALID_ARGUMENTS (bad indices, self
 * loop).
 */
cerr_check graph_edge_add(graph *g, unsigned int left, unsigned int right, void *payload);

/**
 * graph_edge_del() - disconnect two nodes
 * @g:      the graph
 * @left:   node index; the edge tail when the graph is directed
 * @right:  node index; the edge head when the graph is directed
 *
 * Runs graph_params::edge_drop on the edge's non-NULL payload.
 *
 * Return: CERR_OK, CERR_NOT_FOUND if there is no such edge, or
 * CERR_INVALID_ARGUMENTS.
 */
cerr_check graph_edge_del(graph *g, unsigned int left, unsigned int right);

/**
 * graph_nr_nodes() - number of nodes
 * @g:  the graph
 *
 * Return: the number of nodes; node indices are 0 .. count - 1.
 */
unsigned int graph_nr_nodes(const graph *g);

/**
 * graph_nr_edges() - number of edges
 * @g:  the graph
 *
 * Return: the number of edges.
 */
unsigned int graph_nr_edges(const graph *g);

/**
 * graph_has_edge() - check whether two nodes are connected
 * @g:      the graph
 * @left:   node index; the edge tail when the graph is directed
 * @right:  node index; the edge head when the graph is directed
 *
 * Return: true if the edge exists; false otherwise, including for
 * out-of-range indices.
 */
bool graph_has_edge(const graph *g, unsigned int left, unsigned int right);

/**
 * graph_node_find() - look a node up by its label
 * @g:      the graph
 * @name:   node name
 * @id:     node id
 *
 * Return: the node's index, or CERR_NOT_FOUND / CERR_INVALID_ARGUMENTS.
 */
cres_check(int) graph_node_find(const graph *g, const char *name, int id);

/**
 * graph_node_name() - a node's name
 * @g:      the graph
 * @node:   node index
 *
 * Return: the graph-owned name, or CERR_INVALID_INDEX.
 */
cresp_check(const_char) graph_node_name(const graph *g, unsigned int node);

/**
 * graph_node_id() - a node's id
 * @g:      the graph
 * @node:   node index
 *
 * Return: the node's id, or CERR_INVALID_INDEX.
 */
cres_check(int) graph_node_id(const graph *g, unsigned int node);

/**
 * graph_node_edge_count() - number of edges incident to a node
 * @g:      the graph
 * @node:   node index
 *
 * Return: the number of edges touching @node, counting both directions on
 * a directed graph, or CERR_INVALID_INDEX.
 */
cres_check(int) graph_node_edge_count(const graph *g, unsigned int node);

/**
 * graph_node_payload() - a node's payload
 * @g:      the graph
 * @node:   node index
 *
 * Return: the payload (NULL when none was set), or CERR_INVALID_INDEX.
 */
cresp_check(void) graph_node_payload(const graph *g, unsigned int node);

/**
 * graph_node_payload_set() - attach a payload to a node
 * @g:          the graph
 * @node:       node index
 * @payload:    caller-owned payload, replaces the previous one
 *
 * The previous payload is handed to graph_params::node_drop.
 *
 * Return: CERR_OK or CERR_INVALID_INDEX.
 */
cerr_check graph_node_payload_set(graph *g, unsigned int node, void *payload);

/**
 * graph_edge_payload() - an edge's payload
 * @g:      the graph
 * @left:   node index; the edge tail when the graph is directed
 * @right:  node index; the edge head when the graph is directed
 *
 * Return: the payload (NULL when none was set), or CERR_NOT_FOUND /
 * CERR_INVALID_ARGUMENTS.
 */
cresp_check(void) graph_edge_payload(const graph *g, unsigned int left, unsigned int right);

/**
 * graph_edge_payload_set() - attach a payload to an edge
 * @g:          the graph
 * @left:       node index; the edge tail when the graph is directed
 * @right:      node index; the edge head when the graph is directed
 * @payload:    caller-owned payload, replaces the previous one
 *
 * The previous payload is handed to graph_params::edge_drop.
 *
 * Return: CERR_OK, CERR_NOT_FOUND or CERR_INVALID_ARGUMENTS.
 */
cerr_check graph_edge_payload_set(graph *g, unsigned int left, unsigned int right, void *payload);

/**
 * graph_random() - generate a random undirected room graph
 * @rng:        random stream, caller retains ownership
 * @nr_nodes:   total node count, at least 2: "start", "end" and
 *              @nr_nodes - 2 "room" nodes
 * @nr_edges:   exact edge count; edges connect uniformly random distinct
 *              pairs, except that "start" and "end" are never connected
 *              directly
 *
 * Return: the new graph to be graph_free()d by the caller, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS (too few nodes, more edges than distinct pairs).
 */
cresp_check(graph) graph_random(prng *rng, unsigned int nr_nodes, unsigned int nr_edges);

/**
 * graph_linear() - generate a start-to-end chain
 * @nr_nodes:   total node count, at least 2: "start", @nr_nodes - 2 "room"
 *              nodes and "end", connected in a line
 *
 * Return: the new graph to be graph_free()d by the caller, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS.
 */
cresp_check(graph) graph_linear(unsigned int nr_nodes);

/**
 * graph_from_string() - build a graph from a text description
 * @str:    space-separated chains of dash-separated node labels:
 *          "start-a a-b-end c" makes edges start-a, a-b, b-end and the
 *          isolated node c. A label is a name with an optional _N id
 *          suffix ("room_2"); without one the id is 0, so "a" and "a_0"
 *          are the same node
 *
 * Return: the new graph to be graph_free()d by the caller, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS (NULL string, self loop).
 */
cresp_check(graph) graph_from_string(const char *str);

/**
 * struct graph_match - one match of a pattern graph inside a host graph
 * @node_map:   host node index bound to each pattern node index
 * @edges:      host (left, right) node indices matched to each pattern
 *              edge, in the pattern's edge order
 * @nr_nodes:   pattern node count
 * @nr_edges:   pattern edge count
 *
 * Produced by graph_subgraph(), released with graph_match_release().
 */
typedef struct graph_match {
    unsigned int    *node_map;
    unsigned int    (*edges)[2];
    unsigned int    nr_nodes;
    unsigned int    nr_edges;
} graph_match;

/**
 * graph_subgraph() - find a pattern graph inside a host graph
 * @g:      the host graph
 * @find:   the pattern: names must match host node names, while ids only
 *          tell same-name pattern nodes apart, so "room-room_1" matches
 *          any two connected host rooms
 * @match:  the first match found, in host node index order
 *
 * Backtracking subgraph search. Bindings are injective (distinct pattern
 * nodes bind distinct host nodes) and pattern edges match distinct host
 * edges; on a directed host, a pattern edge only matches its own
 * orientation. Pattern nodes with no edges bind to any otherwise unbound
 * name-matching host node.
 *
 * Return: CERR_OK with @match filled in (release it with
 * graph_match_release()), CERR_NOT_FOUND when the pattern doesn't occur,
 * or CERR_NOMEM / CERR_INVALID_ARGUMENTS (empty pattern).
 */
cerr_check graph_subgraph(const graph *g, const graph *find, graph_match *match);

/**
 * graph_match_release() - free a match's bindings
 * @match:  match filled in by graph_subgraph(), may be released twice
 */
void graph_match_release(graph_match *match);

/**
 * graph_dump_gv() - log the graph in graphviz format
 * @g:      the graph
 * @name:   graph name in the output, "G" when NULL
 *
 * Debugging aid: writes one dbg() line per node and edge, forming a dot
 * digraph/graph body.
 */
void graph_dump_gv(const graph *g, const char *name);

#endif /* __CLAP_GRAPH_H__ */
