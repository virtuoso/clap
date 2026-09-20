// SPDX-License-Identifier: Apache-2.0
/*
 * A model's animation library: the definitions an animator plays by handle,
 * and the state transition table that picks them. Pure data, built once per
 * model and shared by every entity of it; see core/animator.h.
 */
#define IMPLEMENTOR
#include "animator.h"
#undef IMPLEMENTOR
#include "internal.h"

void anim_lib_init(anim_lib_t *lib, struct model3d *m)
{
    lib->model = m;
    darray_init(lib->defs);
    darray_init(lib->transitions);
}

void anim_lib_done(anim_lib_t *lib)
{
    struct anim_def *def;

    darray_for_each(def, lib->defs)
        if (def->kind == ANIM_SEQ)
            darray_clearout(def->seq.elems);
    darray_clearout(lib->defs);
    darray_clearout(lib->transitions);
}

struct anim_def *anim_lib_def(anim_lib_t *lib, anim_t ani)
{
    if (ani >= darray_count(lib->defs))     return nullptr;

    return &lib->defs.x[ani];
}

static cres(anim_t) def_add(anim_lib_t *lib, anim_kind kind)
{
    struct anim_def *def = darray_add(lib->defs);

    if (!def)   return cres_error(anim_t, CERR_NOMEM);

    def->kind = kind;

    return cres_val(anim_t, (anim_t)(darray_count(lib->defs) - 1));
}

cres(anim_t) anim_lib_clip(anim_lib_t *lib, const char *name)
{
    int index = animation_by_name(lib->model, name);

    if (index < 0)  return cres_error(anim_t, CERR_NOT_FOUND);

    /* the same clip is the same handle */
    struct anim_def *def;
    darray_for_each(def, lib->defs)
        if (def->kind == ANIM_CLIP && def->clip.index == index) return cres_val(anim_t, (anim_t)(def - lib->defs.x));

    anim_t ani = CRES_RET_SCALAR(def_add(lib, ANIM_CLIP), anim_t);
    anim_lib_def(lib, ani)->clip.index = index;

    return cres_val(anim_t, ani);
}

cres(anim_t) anim_lib_hold(anim_lib_t *lib, double seconds)
{
    /* the same hold is the same handle, as with clips */
    struct anim_def *def;
    darray_for_each(def, lib->defs)
        if (def->kind == ANIM_HOLD && def->hold.seconds == seconds)
            return cres_val(anim_t, (anim_t)(def - lib->defs.x));

    anim_t ani = CRES_RET_SCALAR(def_add(lib, ANIM_HOLD), anim_t);

    anim_lib_def(lib, ani)->hold.seconds = seconds;

    return cres_val(anim_t, ani);
}

cres(anim_t) anim_lib_seq_make(anim_lib_t *lib)
{
    anim_t ani = CRES_RET_SCALAR(def_add(lib, ANIM_SEQ), anim_t);

    darray_init(anim_lib_def(lib, ani)->seq.elems);

    return cres_val(anim_t, ani);
}

cerr anim_lib_seq_push(anim_lib_t *lib, anim_t seq, anim_t ani, bool repeat)
{
    struct anim_def *sd = anim_lib_def(lib, seq), *ed = anim_lib_def(lib, ani);

    if (!sd || !ed || sd->kind != ANIM_SEQ || ed->kind == ANIM_SEQ) return CERR_INVALID_ARGUMENTS;

    struct anim_seq_elem *el = darray_add(sd->seq.elems);
    if (!el)    return CERR_NOMEM;

    el->ani = ani;
    el->repeat = repeat;

    return CERR_OK;
}

static cerr transition_add(anim_lib_t *lib, int to, int from, bool from_any, anim_t ani, double fade)
{
    if (!anim_lib_def(lib, ani))    return CERR_INVALID_ARGUMENTS;

    struct anim_transition *tr = darray_add(lib->transitions);
    if (!tr)    return CERR_NOMEM;

    tr->to = to;
    tr->from = from;
    tr->from_any = from_any;
    tr->ani = ani;
    tr->fade = fade;

    return CERR_OK;
}

cerr anim_lib_transition_add(anim_lib_t *lib, int to, int from, anim_t ani, double fade)
{
    return transition_add(lib, to, from, false, ani, fade);
}

cerr anim_lib_transition_add_any(anim_lib_t *lib, int to, anim_t ani, double fade)
{
    return transition_add(lib, to, 0, true, ani, fade);
}

cres(anim_t) anim_lib_transition_find(anim_lib_t *lib, int to, int from, double *fade)
{
    struct anim_transition *tr, *any = nullptr;

    darray_for_each(tr, lib->transitions) {
        if (tr->to != to)   continue;
        if (tr->from_any) {
            if (!any)   any = tr;
            continue;
        }
        if (tr->from == from) {
            any = tr;
            break;
        }
    }

    if (!any)   return cres_error(anim_t, CERR_NOT_FOUND);
    if (fade)   *fade = any->fade;

    return cres_val(anim_t, any->ani);
}

unsigned int anim_lib_nr(anim_lib_t *lib)
{
    return darray_count(lib->defs);
}

unsigned int anim_lib_nr_transitions(anim_lib_t *lib)
{
    return darray_count(lib->transitions);
}

anim_kind anim_lib_kind(anim_lib_t *lib, anim_t ani)
{
    struct anim_def *def = anim_lib_def(lib, ani);

    return def ? def->kind : ANIM_HOLD;
}

const char *anim_lib_name(anim_lib_t *lib, anim_t ani)
{
    struct anim_def *def = anim_lib_def(lib, ani);

    if (!def)   return "<invalid>";

    switch (def->kind) {
        case ANIM_CLIP: return lib->model->anis.x[def->clip.index].name;
        case ANIM_HOLD: return "hold";
        case ANIM_SEQ:  return "sequence";
    }

    return "<invalid>";
}
