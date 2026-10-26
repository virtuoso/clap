/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __DEMO_LDJAM60_H__
#define __DEMO_LDJAM60_H__

#include "error.h"
#include "linmath.h"
#include "model.h"
#include "sound.h"

struct character;
struct clap_context;
struct gltf_data;
struct scene;

/****************************************************************************
 * Rotatables: suspended platforms that turn a quarter at a time
 ****************************************************************************/

/**
 * enum quarter_turn - a 90 degree turn around a world axis
 * @QUARTER_TURN_X_POS: +90 degrees around X, "+x"
 * @QUARTER_TURN_X_NEG: -90 degrees around X, "-x"
 * @QUARTER_TURN_Y_POS: +90 degrees around Y, "+y"
 * @QUARTER_TURN_Y_NEG: -90 degrees around Y, "-y"
 * @QUARTER_TURN_Z_POS: +90 degrees around Z, "+z"
 * @QUARTER_TURN_Z_NEG: -90 degrees around Z, "-z"
 * @QUARTER_TURN_NR:    number of turns
 *
 * The axes are the engine's (glTF's), Y up; a positive turn goes by the
 * right hand rule.
 */
typedef enum quarter_turn {
    QUARTER_TURN_X_POS = 0,
    QUARTER_TURN_X_NEG,
    QUARTER_TURN_Y_POS,
    QUARTER_TURN_Y_NEG,
    QUARTER_TURN_Z_POS,
    QUARTER_TURN_Z_NEG,
    QUARTER_TURN_NR,
} quarter_turn;

cres_ret(quarter_turn);

/**
 * quarter_turn_parse() - a quarter turn by its name
 * @str:    "+x", "-x", "+y", "-y", "+z" or "-z"; a bare axis, "x", "y" or
 *          "z", is the positive turn around it
 *
 * Return: the turn; CERR_NOT_FOUND for any other string, CERR_INVALID_ARGUMENTS
 *         for nullptr
 */
cres(quarter_turn) quarter_turn_parse(const char *str);

/**
 * quarter_turn_name() - a quarter turn's name
 * @turn:   the turn
 *
 * Return: the name quarter_turn_parse() takes, "<invalid>" for no turn
 */
const char *quarter_turn_name(quarter_turn turn);

/**
 * quarter_turn_reversed() - the turn the other way
 * @turn:   the turn
 *
 * Return: the turn around the same axis by the opposite angle, "-x" for "+x"
 */
quarter_turn quarter_turn_reversed(quarter_turn turn);

typedef struct rotatable rotatable;

cresp_ret(rotatable);

/**
 * rotatable_new() - make an entity a rotatable
 * @e:  the entity, a static collision mesh that stays in the scene for as
 *      long as the rotatable does
 *
 * Takes over @e's update, which drives the turns, and its connect_priv, which
 * leads the update back to the rotatable. The turns go around @e's origin.
 * A turn that runs @e into another static mesh turns back to where it
 * started; the meshes glued to @e don't count.
 * Return: the rotatable, at rest; CERR_NOMEM or gpsm_init()'s error
 */
cresp(rotatable) rotatable_new(entity3d *e);

/**
 * rotatable_done() - give the entity back and free the rotatable
 * @r:  the rotatable
 *
 * A turn under way stops where it is.
 */
void rotatable_done(rotatable *r);

DECLARE_CLEANUP(rotatable);

/**
 * rotatable_entity() - the entity a rotatable turns
 * @r:  the rotatable
 *
 * Return: the entity rotatable_new() made a rotatable of
 */
entity3d *rotatable_entity(rotatable *r);

/**
 * rotatable_turn() - start a quarter turn
 * @r:      the rotatable
 * @turn:   the turn
 *
 * Return: CERR_OK with the turn under way; CERR_INVALID_OPERATION while
 *         another turn is under way or turning back, which goes on as it was
 */
cerr rotatable_turn(rotatable *r, quarter_turn turn);

/**
 * rotatable_of() - the rotatable an entity is, or is glued to
 * @e:  the entity, may be nullptr
 *
 * Return: the nearest rotatable among @e and the entities it's glued to;
 *         CERR_NOT_FOUND if there's none
 */
cresp(rotatable) rotatable_of(entity3d *e);

/**
 * rotatable_reset() - put a rotatable back the way the scene had it
 * @r:      the rotatable
 *
 * At once: whatever it's doing stops, and it's at rest in the rotation
 * rotatable_new() found it in.
 */
void rotatable_reset(rotatable *r);

/****************************************************************************
 * Floor switches: they turn the rotatables when clappy steps on them
 ****************************************************************************/

struct switch_node;
struct floor_switch;

/**
 * struct switches - the floor switches and the rotatables they turn
 * @nodes:      the glTF nodes switches_node_spawn() has seen, until
 *              switches_resolve()
 * @switches:   the floor switches
 * @rotatables: one for each entity a switch turns
 * @sound:      the sound context the switches play on
 * @scene:      the scene they're in, whose camera a switch that starts a turn
 *              shakes
 * @accept:     the sound of a switch that starts a turn; nullptr until the
 *              sound engine runs and a switch plays it
 * @refuse:     the sound of a switch whose rotatable is busy turning; likewise
 */
typedef struct switches {
    darray(struct switch_node, nodes);
    darray(struct floor_switch *, switches);
    darray(rotatable *, rotatables);
    sound_context   *sound;
    struct scene    *scene;
    synth           *accept;
    synth           *refuse;
} switches;

/**
 * switches_init() - get ready for a scene's switches
 * @sws:    the switches
 * @ctx:    clap context, for the sound and the scene
 *
 * Context: before the scene load whose nodes switches_node_spawn() sees
 */
void switches_init(switches *sws, struct clap_context *ctx);

/**
 * switches_node_spawn() - note a node's entity and its extras; a scene_node_spawn_fn
 * @scene:  the scene
 * @gd:     the node's glTF
 * @node:   the node's index in @gd
 * @e:      the entity the node spawned
 * @priv:   the &struct switches
 *
 * Every node goes on record by its name, for the switches' extras to refer
 * to. The extras the node may carry: "switch.rotatable", the node a switch
 * turns; "switch.direction", the &enum quarter_turn it turns it by; and
 * "parent.name", the node its entity is glued to. Warns about "switch."
 * extras it doesn't know, about values that aren't strings or turns, and
 * about a switch that lacks either of its two, and leaves those switches out.
 * Return: CERR_OK, the error of the node's name lookup, or CERR_NOMEM
 */
cerr switches_node_spawn(struct scene *scene, struct gltf_data *gd, int node, entity3d *e, void *priv);

/**
 * switches_resolve() - wire up the switches that the scene load brought in
 * @sws:    the switches
 *
 * Looks up the nodes that the extras name, glues the entities with a
 * "parent.name" to their parents where they stand in the world, makes a
 * rotatable of each entity a switch turns, and has the switches turn them
 * when a character steps on them. Warns about each name that matches no
 * node, or several, and leaves out what it was for. The nodes' record goes
 * after this.
 * Context: once the scene load is over, from its done callback
 * Return: CERR_OK, CERR_NOMEM or rotatable_new()'s error
 */
cerr switches_resolve(switches *sws);

/**
 * switches_reset() - put the rotatables back the way the scene sets them
 * @sws:    the switches
 *
 * Reset all rotatables at once, the way rotatable_reset() does it.
 */
void switches_reset(switches *sws);

#ifndef CONFIG_FINAL
/**
 * switches_debug() - the switches' debug window
 * @sws:    the switches
 *
 * The "game" debug window: each switch with the rotatable it turns and its
 * direction, which flips with a click or takes any of the turns, and a button
 * that sets the turn going as a step on the switch does. Each change of
 * direction goes in the log, which then says what to fix in the asset.
 * Context: once a frame, from the frame callback
 */
void switches_debug(switches *sws);
#else
static inline void switches_debug(switches *sws) {}
#endif /* CONFIG_FINAL */

/**
 * switches_done() - free the switches and the rotatables
 * @sws:    the switches
 *
 * Context: while their entities are still in the scene
 */
void switches_done(switches *sws);

/****************************************************************************
 * Clappy: the player's robot
 ****************************************************************************/

/**
 * struct clappy_antennae - clappy's antennae: the binding of their pose class
 * @base:       the joint they stand on, which looks around
 * @spin:       the one that spins about its length
 * @aim:        the one that sways
 * @activity:   how lively they're to be, 1 standing and less going;
 *              clappy_update() writes it
 * @lively:     how lively they are, following @activity
 * @spun:       how far the spinning one has turned, radians
 */
struct clappy_antennae {
    int     base;
    int     spin;
    int     aim;
    float   activity;
    float   lively;
    float   spun;
};

/**
 * struct clappy - the player's robot, beyond what its character does
 * @c:              clappy's character, nullptr before clappy_init()
 * @sound:          the sound context the motors play on
 * @motor:          the wheels' electric motors, a drone; nullptr until the
 *                  sound engine runs
 * @left:           what he last stood on: a rotatable he falls off of, or whose
 *                  switch he falls off of, goes back on a death fall
 * @parts:          what a death fall breaks him into: his entity and its
 *                  child entities, the visible ones
 * @pieces:         the loose pieces while he's broken; empty otherwise
 * @last_pos:       where clappy was the frame before
 * @start:          where he started: where he comes back if nothing is left
 *                  under @safe
 * @safe:           where he stood on plain ground a moment before he last
 *                  left it, not the edge he went over or a switch: where a
 *                  death fall brings him back
 * @recent:         where he stood after that: @safe's next
 * @speed:          wheel speed, smoothed, in units per second
 * @stood:          seconds he's stood on plain ground since @recent
 * @fall_top:       the highest he's been since he last stood
 * @dead:           seconds since a death fall broke him; negative while whole
 * @idle:           seconds he's stood idle with the camera on his front side
 *                  since he last went
 * @antennae:       his antennae's own motions
 * @airborne:       whether he was in the air the frame before
 * @clapped:        he's clapped since he last went
 */
typedef struct clappy {
    struct character    *c;
    sound_context       *sound;
    synth               *motor;
    entity3d            *left;
    darray(entity3d *, parts);
    darray(entity3d *, pieces);
    vec3                last_pos;
    vec3                start;
    vec3                safe;
    vec3                recent;
    float               speed;
    float               stood;
    float               fall_top;
    float               dead;
    float               idle;
    struct clappy_antennae antennae;
    bool                airborne;
    bool                clapped;
} clappy;

/**
 * clappy_init() - find clappy in the scene
 * @cl:     clappy
 * @scene:  the scene, loaded
 *
 * Return: CERR_OK; CERR_NOT_FOUND if the scene has no "clappy", the error of
 *         its character lookup, or CERR_NOMEM
 */
cerr clappy_init(clappy *cl, struct scene *scene);

/**
 * clappy_update() - follow clappy's motion with the motors' sound
 * @cl:     clappy
 * @scene:  the scene
 *
 * And the death fall: a landing from too high breaks him into his parts, and
 * a while later he's whole again where he stood a moment before he fell. Does
 * nothing before clappy_init().
 * Context: once a frame, after the entities' updates
 * Return: CERR_OK, CERR_NOMEM if a piece couldn't be made, or the error of a
 *         synth setter
 */
cerr clappy_update(clappy *cl, struct scene *scene);

/**
 * clappy_restart() - revert clappy back where he started
 * @cl:     clappy
 * @scene:  the scene
 *
 * Similarly to the death fall, only revert clappy to where he started:
 * whole again if he's in pieces, and still. Does  nothing before
 * clappy_init().
 */
void clappy_restart(clappy *cl, struct scene *scene);

/**
 * clappy_done() - silence clappy's motors and drop his pieces
 * @cl:     clappy
 */
void clappy_done(clappy *cl);

#endif /* __DEMO_LDJAM60_H__ */
