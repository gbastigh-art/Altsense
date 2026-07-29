#pragma once

#include "util/types.h"
#include "level/lptr.h"
#include "defs.h"

// one hit on a movement trace from a -> b
typedef struct trace_hit {
    // swept/resolved position on hit
    v2 swept_pos;

    // "t" on from -> to
    f32 t;

    // type of entity hit
    level_type_e type;

    // lptr of thing which was hit
    lptr_t ptr;

    // block where hit occurred
    v2i block;

    // extra info depending on what was hit
    union {
        struct {
            side_t *ptr;
            v2 hit_pos;
            f32 u, x;
            wall_t *wall;
            bool is_force_portal;
        } side;

        struct {
            entity_t *ptr;

            // entities are bounding cylinders, so "t_exit" is t (>0) where the
            // trace ray exits the entity cylinder, since "t" is the entry hit
            // ignore if <0
            f32 t_exit;

            // if true then TRACE_FLAG_ENTITY_HITBOXES is enabled and this is a
            // hitbox-hit (not a collision hit)
            bool is_hitbox;

            // if TRACE_FLAG_ENTITY_HITBOXES:
            //   this indicates that in addition to hitting the hitbox
            //   (is_hitbox), this also hit the regular collision box
            // else:
            //   always true
            bool is_collision;
        } entity;

        struct {
            sector_t *ptr;
            plane_type_e plane;

            // if TRACE_FLAG_INCLUDE_TRAVERSE_SECTORS, then sectors which are
            // traversed (but not necessarily "hit") are also added
            bool traverse_only;
        } sector;

        struct {
            decal_t *ptr;
            v2 hit_pos;
        } decal;
    };

    // normal of surface (or inv. trace direction if entity hit)
    // 3D hits only
    v3 normal;
} trace_hit_t;

typedef enum {
    TRACE_FLAG_NONE = 0 << 0,

    // sectors traversed (but not necessarily intersected, esp. in 2D cases)
    // are included in results
    TRACE_FLAG_INCLUDE_TRAVERSE_SECTORS = 1 << 0,

    // nearby portals (close to camera near plane) are "forced", such that the
    // near plane won't intersect with them ("near" is <= FORCE_PORTAL_DISTANCE)
    TRACE_FLAG_FORCE_PORTALS_NEAR_TO_CAM = 1 << 1,

    // force movement away from disconnected portals with FORCE_PORTAL_DISTANCE
    TRACE_FLAG_FORCE_PORTAL_AWAY = 1 << 2,

    // process 3D trace as xy-axis movement *then* any Z movement after complete
    // XY-resolution
    TRACE_FLAG_XY_THEN_Z = 1 << 3,

    // disconnected portals return TRACE_PORTAL_RESULT_STOP instead of
    // TRACE_PORTAL_RESULT_THROUGH_DISCONNECT
    TRACE_FLAG_DISCONNECTED_PORTALS_STOP = 1 << 4,

    // only applicable if LTF_ENTITY in trace->types
    // adjust entity hits for entity_type_t.hitbox_{radius/height},
    // see trace_hit_t::entity.is_hitbox and entity.is_collision
    TRACE_FLAG_ENTITY_HITBOXES = 1 << 5,
} trace_flag_e;

// results of a trace resolve function
typedef enum {
    TRACE_RESOLVE_STOP,
    TRACE_RESOLVE_CONTINUE,
    TRACE_RESOLVE_RETRY,
} trace_resolve_result_e;

// results of a trace
typedef enum {
    TRACE_RESULT_NOT_STOPPED,
    TRACE_RESULT_STOPPED,
    TRACE_RESULT_TOO_MANY_RETRIES,
} trace_result_e;

typedef struct trace_2d {
    v2 org;     // origin point
    v2 dst;     // destination point
    f32 radius; // radius (0.0f for line)
    int types;  // LTF_* this trace responds to
    int flags;  // TRACE_FLAG_*

    // non-optional
    trace_resolve_result_e (*resolve_fn)(
        level_t*,
        struct trace_2d*,
        const trace_hit_t *hit);

    // optional, return "false" to ignore lptr entirely
    bool (*filter_fn)(level_t*, lptr_t, void*);
    void *filter_userdata;

    void *userdata;
} trace_2d_t;

// run a 2D trace, modify "trace" org and dst in-place
trace_result_e trace_2d(level_t *level, trace_2d_t *trace);

typedef struct trace_2d_seg {
    v2 org;
    v2 dst;
    int types;
    int flags;
    f32 radius; // optional

    // lptr to ignore if encountered, optional
    lptr_t ignore_ptr;

    // optional
    trace_resolve_result_e (*resolve_fn)(
        level_t*,
        struct trace_2d_seg*,
        const trace_hit_t *hit);

    // optional, return "false" to ignore lptr entirely
    bool (*filter_fn)(level_t*, lptr_t, void*);
    void *filter_userdata;

    void *userdata;
} trace_2d_seg_t;

typedef struct {
    trace_result_e result;
    trace_hit_t hit;
    v2 hit_pos;
} trace_2d_seg_result_t;

// run a 2D trace, modifiy "trace" org/dst in-place + auto-handle portals
// shoot a ray in 2D space
trace_2d_seg_result_t trace_2d_seg(level_t *level, trace_2d_seg_t *trace);

typedef struct trace_3d {
    v3 org;          // origin point
    v3 dst;          // destination point
    f32 radius;      // radius (0.0f for line)
    f32 height;      // height (NOTE: movement is for "bottom" if height != 0.0)
    f32 step_height; // if >0.0f, the applied to portals to determine legal
                     // movement
    int types;       // LTF_* this trace responds to
    int flags;       // TRACE_FLAG_*

    // non-optional
    trace_resolve_result_e (*resolve_fn)(
        level_t*,
        struct trace_3d*,
        const trace_hit_t *hit);
    void *userdata;

    // optional, return "false" to ignore lptr entirely
    bool (*filter_fn)(level_t*, lptr_t, void*);
    void *filter_userdata;

    // last hit, only valid if result != TRACE_RESULT_NOT_STOPPED
    trace_hit_t last_hit;

    // internal 2D trace
    trace_2d_t trace_2d;
} trace_3d_t;

// run a 3D trace, modify "trace" org and dst in-place
trace_result_e trace_3d(level_t *level, trace_3d_t *trace);

typedef struct trace_3d_seg {
    v3 org;
    v3 dst;
    int types;
    int flags;

    // lptr to ignore if encountered, optional
    lptr_t ignore_ptr;

    trace_resolve_result_e (*resolve_fn)(
        level_t*,
        struct trace_3d_seg*,
        const trace_hit_t *hit);

    // optional, return "false" to ignore lptr entirely
    bool (*filter_fn)(level_t*, lptr_t, void*);
    void *filter_userdata;

    void *userdata;
} trace_3d_seg_t;

typedef struct {
    trace_result_e result;
    trace_hit_t hit;
    v3 hit_pos;
} trace_3d_seg_result_t;

// run a 3D trace, modifiy "trace" org/dst in-place + auto-handle portals
// shoot a ray in 3D space
trace_3d_seg_result_t trace_3d_seg(level_t *level, trace_3d_seg_t *trace);

typedef enum trace_portal_result {
    // hit portal, went through
    TRACE_PORTAL_RESULT_THROUGH,

    // ignore (not portal, etc.)
    TRACE_PORTAL_RESULT_IGNORE,

    // stop movement
    TRACE_PORTAL_RESULT_STOP,

    // hit disconnected portal, went through
    TRACE_PORTAL_RESULT_THROUGH_DISCONNECT,

    // can hit portal, but not applicable atm. don't stop movement.
    TRACE_PORTAL_RESULT_REJECT,
} trace_portal_result_e;

// resolve a potential portal hit for a 2D path trace
trace_portal_result_e trace_resolve_portal_2d(
        level_t *level,
        trace_2d_t *trace,
        const trace_hit_t *hit,
        f32 *angle);

// resolve a potential portal hit for a 3D path trace
trace_portal_result_e trace_resolve_portal_3d(
        level_t *level,
        trace_3d_t *trace,
        const trace_hit_t *hit,
        f32 *angle);

typedef struct {
    bool changed;
    line2f_t movement;
    v2 velocity;
} move_project_result_t;

// "project" movement with a specified velocity according to the specified hit
move_project_result_t move_project_hit_velocity(
        const level_t *level,
        const trace_hit_t *hit,
        line2f_t movement,
        v2 velocity,
        v2 restitution,
        f32 dt);

typedef struct {
    subsector_t *sub;
    sect_line_t *via; // NOTE: optional!! first point in path has via == NULL
} subsector_path_point_t;

// attempt to path from "start" to "goal" with an (optional) cost function
// (cost_fn <0 -> impassable), returning a list of subsectors to traverse
// and the lines through which the path was made.
// NOTE: (*out)[0].via will be NULL since the first subsector doesn't come from
// anywhere
// returns true if path was found
bool path_subsectors_to_goal(
    level_t *level,
    v2 start,
    v2 goal,
    f32 (*cost_fn)(
        level_t*,
        const subsector_t*,
        const subsector_t*,
        const sect_line_t*,
        void*),
    DYNLIST(subsector_path_point_t) *out,
    void *userdata);

enum {
    PATH_TO_GOAL_NO_FLAGS            = 0 << 0,
    PATH_TO_GOAL_DISCONNECT_PROTRUDE = 1 << 0,
};

typedef struct {
    v2 point;

    // if true, path point is on disconnected portal
    bool is_on_disconnect: 1;
} path_point_t;

// attempt to path from "start" to "goal" with an (optional) cost function
// (see path_subsectors_to_goal) and an (optional) "trivial" function which
// can be used to simplify paths by determining if three points are trivially
// traversable (that is for trivial(a, b, c), a -> b -> c is can be simplified
// to a -> c
// returns true if path was found
bool path_to_goal(
        level_t *level,
        v2 start,
        v2 goal,
        f32 (*cost_fn)(
            level_t*,
            const subsector_t*,
            const subsector_t*,
            const sect_line_t*,
            void*),
        bool (*trivial_fn)(
            level_t*,
            const path_point_t*,
            const path_point_t*,
            const path_point_t*,
            void*),
        DYNLIST(path_point_t) *out,
        void *userdata,
        int flags);

// gets 2D sightline between two points, taking portals, etc. into account
bool trace_sightline_2d(
        level_t *level,
        v2 start,
        v2 goal,
        f32 max_dist,
        DYNLIST(line2f_t) *out);

// gets 3D sightline between two points, taking portals, etc. into account
// NOTE: can be pretty rough, only checks disconnected portal midpoints
bool trace_sightline_3d(
        level_t *level,
        v3 start,
        v3 goal,
        f32 max_dist,
        DYNLIST(line3f_t) *out);
