// SPDX-License-Identifier: Apache-2.0
//
// FFI wrappers over the scene graph: entities, their transforms, the camera and
// scene control. These use only the ffi_* accessors from ffi.h, so they carry
// no interpreter specifics; engine handles cross the boundary as opaque pointers.

#include "camera.h"
#include "clap.h"
#include "logger.h"
#include "model.h"
#include "scene.h"
#include "script/ffi.h"
#include "shader_constants.h"
#include "transform.h"

CLAP_SCRIPT_TYPE("type Entity3d* = struct { _h: uint }")
CLAP_SCRIPT_TYPE("type Xform* = struct { _h: uint }")
CLAP_SCRIPT_TYPE("type Camera* = struct { _h: uint }")
CLAP_SCRIPT_TYPE("type Vec3* = struct { x, y, z: real }")

/* Logging */

CLAP_SCRIPT_FN(ffi_dbg, "fn dbg*(msg: str)")
void ffi_dbg(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    const char *msg = ffi_str(params, 0);
    dbg("%s\n", msg ? msg : "");
}

/* Entities */

CLAP_SCRIPT_FN(ffi_get_entity3d, "fn get_entity3d*(name: str): Entity3d")
void ffi_get_entity3d(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct scene *scene = clap_get_scene(ffi_ctx(result));
    const char *name = ffi_str(params, 0);
    entity3d *e = NULL;

    if (scene && name)
        e = CRES_RET(mq_find_entity(&scene->mq, name),
                     { ffi_ret_ptr(params, result, NULL); return; });

    ffi_ret_ptr(params, result, e);
}

CLAP_SCRIPT_METHOD(ffi_entity_valid, "fn (e: Entity3d) valid*(): bool")
void ffi_entity_valid(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_bool(params, result, ffi_ptr(params, 0) != NULL);
}

CLAP_SCRIPT_METHOD(ffi_entity_name, "fn (e: Entity3d) name*(): str")
void ffi_entity_name(UmkaStackSlot *params, UmkaStackSlot *result)
{
    entity3d *e = ffi_ptr(params, 0);
    ffi_ret_str(params, result, e ? entity_name(e) : "");
}

CLAP_SCRIPT_METHOD(ffi_entity_get_xform, "fn (e: Entity3d) get_xform*(): Xform")
void ffi_entity_get_xform(UmkaStackSlot *params, UmkaStackSlot *result)
{
    entity3d *e = ffi_ptr(params, 0);
    ffi_ret_ptr(params, result, e ? &e->xform : NULL);
}

/*
 * Per-entity render knobs. The scalar/flag ones are generated straight from the
 * field-accessor markers in ffi.h; color passthrough goes through entity3d_color()
 * because it validates the mode bits.
 */
FFI_GET_FLOAT(ffi_entity_bloom_intensity, "fn (e: Entity3d) bloom_intensity*(): real",
              entity3d, bloom_intensity)
FFI_SET_FLOAT_RANGE(ffi_entity_set_bloom_intensity, "fn (e: Entity3d) set_bloom_intensity*(v: real)",
                    entity3d, bloom_intensity, 0.0f, 20.0f)
FFI_GET_FLOAT(ffi_entity_bloom_threshold, "fn (e: Entity3d) bloom_threshold*(): real",
              entity3d, bloom_threshold)
FFI_SET_FLOAT_RANGE(ffi_entity_set_bloom_threshold, "fn (e: Entity3d) set_bloom_threshold*(v: real)",
                    entity3d, bloom_threshold, 0.0f, 10.0f)
FFI_GET_BOOL(ffi_entity_outline_exclude, "fn (e: Entity3d) outline_exclude*(): bool",
             entity3d, outline_exclude)
FFI_SET_BOOL(ffi_entity_set_outline_exclude, "fn (e: Entity3d) set_outline_exclude*(v: bool)",
             entity3d, outline_exclude)
FFI_GET_INT(ffi_entity_color_pt, "fn (e: Entity3d) color_pt*(): int", entity3d, color_pt)

/* Flat-color an entity (replace its RGB) to pick it out in a screenshot; alpha too. */
CLAP_SCRIPT_METHOD(ffi_entity_set_color, "fn (e: Entity3d) set_color*(r, g, b, a: real)")
void ffi_entity_set_color(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    entity3d *e = ffi_ptr(params, 0);
    if (e)
        entity3d_color(e, COLOR_PT_REPLACE_RGB | COLOR_PT_REPLACE_ALPHA,
                       (vec4){ ffi_real(params, 1), ffi_real(params, 2),
                               ffi_real(params, 3), ffi_real(params, 4) });
}

/* Stop overriding the entity's color. */
CLAP_SCRIPT_METHOD(ffi_entity_clear_color, "fn (e: Entity3d) clear_color*()")
void ffi_entity_clear_color(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    entity3d *e = ffi_ptr(params, 0);
    if (e)  entity3d_color(e, COLOR_PT_NONE, (vec4){});
}

/* Camera */

CLAP_SCRIPT_FN(ffi_get_camera, "fn get_camera*(): Camera")
void ffi_get_camera(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct scene *scene = clap_get_scene(ffi_ctx(result));
    ffi_ret_ptr(params, result, scene ? scene->camera : NULL);
}

CLAP_SCRIPT_METHOD(ffi_camera_get_xform, "fn (c: Camera) get_xform*(): Xform")
void ffi_camera_get_xform(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct camera *c = ffi_ptr(params, 0);
    ffi_ret_ptr(params, result, c ? &c->xform : NULL);
}

/* Scene control */

/* Follow @e; a null (zeroed) handle detaches the camera into free-fly mode. */
CLAP_SCRIPT_FN(ffi_control, "fn control*(e: Entity3d)")
void ffi_control(UmkaStackSlot *params, UmkaStackSlot *result)
{
    entity3d *e = ffi_ptr(params, 0);
    struct scene *scene = clap_get_scene(ffi_ctx(result));
    if (!scene)         return;

    scene->control = e;
    if (scene->camera)  transform_set_updated(&scene->camera->xform);
}

CLAP_SCRIPT_FN(ffi_detach, "fn detach*()")
void ffi_detach(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    struct scene *scene = clap_get_scene(ffi_ctx(result));
    if (!scene)         return;

    scene->control = NULL;
    if (scene->camera)  transform_set_updated(&scene->camera->xform);
}

/* Transforms */

CLAP_SCRIPT_METHOD(ffi_xform_move, "fn (x: Xform) move*(dx, dy, dz: real)")
void ffi_xform_move(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (!xf)    return;
    transform_move(xf, (vec3){ ffi_real(params, 1), ffi_real(params, 2), ffi_real(params, 3) });
}

CLAP_SCRIPT_METHOD(ffi_xform_set_position, "fn (x: Xform) set_position*(px, py, pz: real)")
void ffi_xform_set_position(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (!xf)    return;
    transform_set_pos(xf, (vec3){ ffi_real(params, 1), ffi_real(params, 2), ffi_real(params, 3) });
}

CLAP_SCRIPT_METHOD(ffi_xform_pos, "fn (x: Xform) pos*(): Vec3")
void ffi_xform_pos(UmkaStackSlot *params, UmkaStackSlot *result)
{
    transform_t *xf = ffi_ptr(params, 0);
    vec3 pos = { 0, 0, 0 };
    if (xf)     transform_pos(xf, pos);
    ffi_ret_vec3(params, result, pos[0], pos[1], pos[2]);
}

/* Absolute orientation from euler angles (degrees, XYZ order). */
CLAP_SCRIPT_METHOD(ffi_xform_set_rotation, "fn (x: Xform) set_rotation*(rx, ry, rz: real)")
void ffi_xform_set_rotation(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (!xf)    return;
    float angles[3] = { ffi_real(params, 1), ffi_real(params, 2), ffi_real(params, 3) };
    transform_set_angles(xf, angles, true);
}

/* Incremental rotation of @deg degrees around axis (ax, ay, az). */
CLAP_SCRIPT_METHOD(ffi_xform_rotate, "fn (x: Xform) rotate*(ax, ay, az, deg: real)")
void ffi_xform_rotate(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (!xf)    return;
    vec3 axis = { ffi_real(params, 1), ffi_real(params, 2), ffi_real(params, 3) };
    transform_rotate_axis(xf, axis, ffi_real(params, 4), true);
}

CLAP_SCRIPT_METHOD(ffi_xform_rotate_X, "fn (x: Xform) rotate_X*(deg: real)")
void ffi_xform_rotate_X(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (xf)     transform_rotate_axis(xf, (vec3){ 1, 0, 0 }, ffi_real(params, 1), true);
}

CLAP_SCRIPT_METHOD(ffi_xform_rotate_Y, "fn (x: Xform) rotate_Y*(deg: real)")
void ffi_xform_rotate_Y(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (xf)     transform_rotate_axis(xf, (vec3){ 0, 1, 0 }, ffi_real(params, 1), true);
}

CLAP_SCRIPT_METHOD(ffi_xform_rotate_Z, "fn (x: Xform) rotate_Z*(deg: real)")
void ffi_xform_rotate_Z(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    transform_t *xf = ffi_ptr(params, 0);
    if (xf)     transform_rotate_axis(xf, (vec3){ 0, 0, 1 }, ffi_real(params, 1), true);
}
