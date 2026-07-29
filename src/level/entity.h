#pragma once

#include "defs.h"
#include "gfx/renderer.h"
#include "util/math.h"
#include "level/level_types.h"
#include "trace.h"

#define DECL_ENTITY_TYPE_FIELD(type_, name_)              \
    type_ name_;                                          \
    type_ (*name_##_fn)(const level_t*, const entity_t*)  \

typedef struct {
    f32 radius, height;

    // 0.0f: same as non-hitbox_*
    f32 hitbox_radius, hitbox_height;

    // v3(0) -> entity_t.origin
    v3 origin;
} entity_bounds_t;

typedef void (*entity_models_f)(level_t*, entity_t*, DYNLIST(model_t)*);

// type information for each ENTITY_TYPE_*
typedef struct entity_type {
    // if entity is !ENTITY_TYPE_FLAG_MODEL
    const char *sprite;

    // collision details
    DECL_ENTITY_TYPE_FIELD(entity_bounds_t, bounds);

    // baseline drag
    DECL_ENTITY_TYPE_FIELD(v3, drag_floor);
    DECL_ENTITY_TYPE_FIELD(v3, drag_air);

    // baseline gravity
    DECL_ENTITY_TYPE_FIELD(v3, gravity);

    // used to determine knockback, relative to mass of player which is 0.0
    f32 mass;

    // only applicable for ENTITY_TYPE_FLAG_DAMAGEABLE entities
    f32 max_health;

    // for walking entities
    DECL_ENTITY_TYPE_FIELD(f32, step_height);

    // get entity light if ENTITY_TYPE_LIGHT
    light_desc_t (*light_fn)(const level_t*, const entity_t*);

    // update functions
    void (*tick_fn)(level_t*, entity_t*);
    void (*frame_update_fn)(level_t*, entity_t*);
    void (*fixed_update_fn)(level_t*, entity_t*, f32 dt);

    // only if ENTITY_TYPE_FLAG_MODEL && !ENTITY_TYPE_FLAG_INVISIBLE
    entity_models_f models_fn;

    // if !ENTITY_TYPE_FLAG_MODEL && !ENTITY_TYPE_FLAG_INVISIBLE
    void (*sprite_fn)(level_t*, entity_t*, sprite_inst_desc_t*);

    // returns true if entity collides with other entity
    // two entities must agree in order ot hit each other
    // assumed "true" if function is not present
    bool (*collides_fn)(const level_t*, const entity_t*, const entity_t*);

    // called when this entity (first) hits another (second)
    void (*on_hit_fn)(level_t*, entity_t*, entity_t*);

    // called when tracing movement for entity, see entity.c
    // if true, whatever is put in the last result parameter is used as move
    // trace resolution result. otherwise move trace continues as normal.
    bool (*move_trace_resolve_fn)(
        level_t*,
        entity_t*,
        trace_3d_t*,
        const trace_hit_t*,
        trace_resolve_result_e*);

    // called when entity hits something which stops it while moving
    void (*move_on_hit_fn)(
        level_t*,
        entity_t*,
        const trace_hit_t*);

    // called when entity portals through a disconnected portal
    void (*on_move_portal_fn)(
        level_t*,
        entity_t*,
        const trace_hit_t*);

    struct {
        bool only_hit_target;
    } projectile;

    // if using entity_edit_cube_models
    v3 edit_cube_hsv;

    // flags
    struct {
        bool is_enemy:      1;
        bool is_edit_only:  1;
        bool is_invisible:  1;
        bool is_projectile: 1;
        bool has_model:     1;
        bool has_light:     1;
        bool no_physics:    1;
        bool is_damageable: 1;
        bool is_flyer:      1;
        bool is_attach:     1;
        bool is_z_free:     1;

        // if true, fixed updates for this type of entity are distributed by
        // "id % 4" such that every 4 fixed updates, this entity gets one "big"
        // fixed update
        bool distributed_fixed_update: 1;
    };
} entity_type_t;

#undef DECL_ENTITY_TYPE_FIELD

// see entity.c
extern entity_type_t ENTITY_TYPES[ENTITY_TYPE_COUNT];

#define ENTITY_TYPE_REGISTER(type_, ...)                                       \
    __attribute__((constructor)) static void register_entity_type__##type_() { \
        ENTITY_TYPES[(type_)] = (__VA_ARGS__);                                 \
    }

#define DECL_ENTITY_TYPE_FIELD_GET(type_, name_)                               \
    M_INLINE type_ entity_##name_(const level_t *l, const entity_t *e) {       \
        if (e->ptype->name_##_fn) {                                            \
            return e->ptype->name_##_fn(l, e);                                 \
        }                                                                      \
        return e->ptype->name_;                                                \
    }

DECL_ENTITY_TYPE_FIELD_GET(v3, gravity)
DECL_ENTITY_TYPE_FIELD_GET(v3, drag_floor)
DECL_ENTITY_TYPE_FIELD_GET(v3, drag_air)
DECL_ENTITY_TYPE_FIELD_GET(f32, step_height)

#undef DECL_ENTITY_TYPE_FIELD_GET

bool entity_is_active(const level_t *l, const entity_t *e);

entity_t *entity_new(level_t *level, const entity_t *defaults);
void entity_delete(level_t *level, entity_t *entity);

// destroy an entity *during gameplay*
void entity_destroy(level_t *level, entity_t *ent);

// set entity's type, loads defaults from corresponding entity_type_t
void entity_set_type(
    level_t *level,
    entity_t *entity,
    entity_type_e type_index);

// force an update of the blocks spanned by this entity
void entity_update_blocks(level_t *level, entity_t *entity);

// set entity position
bool entity_try_move(level_t *level, entity_t *entity, v2 pos);

// perform per-frame update on entity
void entity_frame_update(level_t *level, entity_t *ent);

// perform per-timestep update on entity
void entity_fixed_update(level_t *level, entity_t *ent);

// tick entity - don't use for specific behavior, use an actor for that
void entity_tick(level_t *level, entity_t *ent);

// gets 3D center of entity (based on bounds!)
v3 entity_center(const level_t *l, const entity_t *e);

// 2D distance between two entities, taking their radii into account
f32 entity_between_2d(
        const level_t *level,
        const entity_t *a,
        const entity_t *b);

// 3D distance between two entities, taking their radii and height into account
f32 entity_between_3d(
        const level_t *level,
        const entity_t *a,
        const entity_t *b);

typedef struct entity_damage_desc {
    f32 amount;
    f32 knockback;
    v3 dir;

    // tint if recipient is player
    struct {
        bool disabled;
        screen_tint_t params;
    } tint;

    // screenshake if recipient is player
    struct {
        bool disabled;
        f32 intensity;
        f32 duration;
    } screenshake;

    bool is_melee;

    const entity_t *source;
} entity_damage_desc_t;

void entity_try_damage(entity_t *ent, const entity_damage_desc_t *desc);

// attach is_attach entity to side
void entity_attach_side(
        level_t *level,
        entity_t *ent,
        side_t *side,
        v2 offset);

// attach is_attach entity to sector
void entity_attach_sector(
        level_t *level,
        entity_t *ent,
        sector_t *sector,
        v2 offset,
        plane_type_e plane);

// copy attach of "src" -> "dst", both must be is_attach
void entity_attach_copy(
        level_t *level,
        entity_t *dst,
        const entity_t *src);

// gets surface normal for an attached (ENTITY_TYPE_FLAG_ATTACH) entity
v3 entity_attach_surface_normal(const level_t *level, const entity_t *ent);

M_INLINE entity_bounds_t entity_bounds(const level_t *l, const entity_t *e) {
    entity_bounds_t bounds;

    if (e->ptype->bounds_fn) {
        bounds = e->ptype->bounds_fn(l, e);
    } else {
        bounds = e->ptype->bounds;
    }

    if (v3_eqv_eps(bounds.origin, v3_of(0))) {
        bounds.origin = e->pos_xyz;
    }

    return bounds;
}

M_INLINE f32 entity_height(const level_t *l, const entity_t *e) {
    return entity_bounds(l, e).height;
}

M_INLINE f32 entity_radius(const level_t *l, const entity_t *e) {
    return entity_bounds(l, e).radius;
}

M_INLINE f32 entity_hitbox_height(const level_t *l, const entity_t *e) {
    return entity_bounds(l, e).hitbox_height;
}

M_INLINE f32 entity_hitbox_radius(const level_t *l, const entity_t *e) {
    return entity_bounds(l, e).hitbox_radius;
}

// TODO: doc
void entity_do_liquid_exit(level_t *level, entity_t *ent);

// rotate to face the specified direction where rot_speed is in rotations/sec
void entity_face_dir(
        level_t *level,
        entity_t *e,
        v3 dir,
        f32 rot_speed,
        f32 dt);

void entity_edit_cube_models(
        level_t *level,
        entity_t *ent,
        DYNLIST(model_t) *models);
