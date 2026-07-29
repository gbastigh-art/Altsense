#pragma once

#include "gfx/renderer.h"
#include "level/lptr.h"
#include "level/sector_matrices.h"
#include "util/blklist.h"
#include "util/genlist.h"
#include "util/types.h"
#include "util/math.h"
#include "trace.h"
#include "config.h"
#include "defs.h"

typedef union lptr lptr_t;
typedef union lptr_nogen lptr_nogen_t;
typedef struct vertex vertex_t;
typedef struct wall wall_t;
typedef struct side side_t;
typedef struct sector sector_t;
typedef struct entity entity_t;
typedef struct decal decal_t;
typedef struct block block_t;

// TODO: replace level_flags with bitfields
// fields which all level elements must include
#define DECL_LEVEL_FIELDS_LIST()                 \
    union {                                      \
        genlist_handle_t handle;                 \
        struct {                                 \
            int id: GENLIST_HANDLE_BITS;         \
            genlist_gen_t gen: GENLIST_GEN_BITS; \
        };                                       \
    };                                           \
    int version: 24;                             \
    union {                                      \
        struct {                                 \
            bool do_not_recalc: 1;               \
            bool dirty_side: 1;                  \
            bool deleted: 1;                     \
            bool traced: 1;                      \
            bool mark: 1;                        \
            bool recalc_enqueued: 1;             \
        };                                       \
        u8 raw;                                  \
    } lflags;

// type-agnostic level fields
typedef struct level_fields {
    DECL_LEVEL_FIELDS_LIST()
} level_fields_t;

#define DECL_LEVEL_FIELDS_UNION()       \
    union {                             \
        struct {                        \
            DECL_LEVEL_FIELDS_LIST()    \
        };                              \
        level_fields_t level_fields;    \
    };

typedef struct vertex {
    v2 pos;

    // walls attached to this vertex
    // on level arena
    DYNLIST(wall_t*) walls;

    DECL_LEVEL_FIELDS_UNION()
} vertex_t;

typedef struct wall {
    // wall vertices, "front" is right of v0 -> v1
    // cannot be NULL
    union {
        struct { vertex_t *v0, *v1; };
        vertex_t *vertices[2];
    };

    // sides, NULL if no side
    side_t *sides[2];

    v2 normal; // normal (right of v0 -> v1)
    f32 len; // length(d)

    // last position of wall when its blocks were updated
    v2 last_pos[2];

    DECL_LEVEL_FIELDS_UNION()
} wall_t;

// data for a side material
typedef struct sidemat_data {
    // low/middle/high textures
    union {
        struct {
            tex_id_t tex_low, tex_mid, tex_high;
        };

        tex_id_t texs[3];
    };

    // overlay, optional
    tex_id_t tex_overlay;

    // TODO: pack as u32
    // hsva offset
    v4 hsva;

    // texture offsets in pixels
    v2i offsets;

    // TODO: map -1 to 1 !!
    // alpha value for overlay
    f32 overlay_alpha;

    // low/mid/high splits
    f32 split_bottom, split_top;

    // SDMF_*
    i32 flags;
} sidemat_data_t;

// one side of a wall
typedef struct side {
    side_t *portal;                  // side to which this side is portal

    union {
        struct {
            bool is_disconnect: 1;
            bool is_backside: 1;
            bool has_mid: 1;
        };

        // NOTE: double check when adding new flags!
        u8 flags;
    };

    sidemat_data_t mat;              // material
    struct side *like;               // if present, copy mat/flag/light
    light_params_t light;            // light, color == 0 if no light

    LLIST_NODE(side_t) sector_sides; // sector side linked list
    wall_t *wall;                    // wall to which this side belongs
    sector_t *sector;                // sector which this side belongs to
    subsector_t *subsector;          // subsector which this side belongs to
    sect_line_t *sect_line;
    LLIST(decal_t) decals;           // list of all decals on side
                                     // render info, see renderer.c

    hash_t seg_hash;                 // hash of wall segment properties, used
                                     // to track if anything changes

    DECL_LEVEL_FIELDS_UNION()
} side_t;

// one segment (bottom, top, middle, wall) of a side
typedef struct side_segment {
    bool present         : 1;
    bool portal          : 1;
    bool mesh            : 1;
    bool solid           : 1;
    side_segment_e index : 4;

    f32 zbl, zbr, ztl, ztr;
} side_segment_t;

// all segments of a side
typedef union side_segments {
    side_segment_t arr[4];
    struct { side_segment_t bottom, middle, top, wall; };
} side_segments_t;

// floor or ceiling plane, belongs to sector
typedef struct plane {
    // plane Z offset in world space
    f32 z;

    // side from which slope originates, NULL if no such side
    side_t *slope_side;

    // slope angle
    f32 slope;

    // light, color is 0 if no light
    light_params_t light;
} plane_t;

// triangle, part of a sector
typedef struct sect_tri {
    union {
        struct { vertex_t *a, *b, *c; };
        vertex_t *vs[3];
    };

    // zs at vs[0..2]
    rangef_t zs[3];

    // area of triangle
    f32 area;

    // approx. volume of triangle
    f32 approx_volume;

    // approx. height of triangle
    f32 approx_height;
} sect_tri_t;

// line, maybe also a wall, but could also just divide subsectors
typedef struct sect_line {
    union {
        struct { vertex_t *a, *b; };
        vertex_t *vs[2];
    };

    // optional, line may just divide sector
    side_t *side;
} sect_line_t;

typedef struct subsector_neighbor {
    // neighbor
    subsector_t *sub;

    // the adjacent line
    sect_line_t *line;

    // true if this neighbor is done through a disconnected portal
    bool is_portal;
} subsector_neighbor_t;

typedef struct subsector {
    sector_t *parent;
    LLIST_NODE(struct subsector) sector_node;

    bool mark: 1;
    int id : 31;

    // lists are on level arena
    DYNLIST(sect_line_t) lines;
    DYNLIST(sect_tri_t*) tris;
    DYNLIST(subsector_neighbor_t) neighbors;

    union {
        box2f_t bounds;
        struct { v2 min, max; };
    };

    // center point
    v2 center;

    // approx. volume of subsector
    f32 volume;

    // sector this sector was accessed from
    // scratch value, used for pathfinding
    subsector_t *from;

    // line via which this sector was accessed
    // scratch value, used for pathfinding
    sect_line_t *via;

    // entities in this subsector
    DLIST(entity_t) entities;

    // TODO: doc
    LLIST_NODE(subsector_t) node;
} subsector_t;

// sector material data
typedef struct sectmat_data {
    union {
        tex_id_t texs[2];
        struct { tex_id_t tex_floor, tex_ceil; };
    };

    union {
        tex_id_t overlays[2];
        struct { tex_id_t overlay_floor, overlay_ceil; };
    };

    union {
        f32 overlay_alphas[2];
        struct { f32 overlay_alpha_floor, overlay_alpha_ceil; };
    };

    union {
        struct { v2i offsets_floor, offsets_ceil; };
        v2i offsets[2];
    };

    union {
        // TODO: u32
        struct { v4 hsva_floor, hsva_ceil; };
        v4 hsva[2];
    };

    // SCMF_*
    int flags;
} sectmat_data_t;

// one "area" of the level as defined by a collection of sides
typedef struct sector {
    LLIST(side_t) sides;

    // floor/ceiling planes
    // NOTE: indices must match with plane_type_e
    union {
        struct { plane_t floor, ceil; };
        plane_t planes[2];
    };

    sector_type_e type;  // type
    union {
        struct {

        };

        // NOTE: check when changing flags
        u8 flags;
    };

    sectmat_data_t mat;  // material
    struct sector *like; // if present, source to copy mat/flags/light from

    // extra data
    union {
        struct {
            f32 liquid_offset;

            // hsv for some liquids
            v3 liquid_hsv;

            // filename of teleport exit level, i.e. "level/*.json" - > "*"
            // NOTE: nullable
            char *teleport_exit_level;

            // name for hub juice approach text, nullable
            char *approach_name;
        };

#ifdef TARGET_DEBUG
        u8 scratch0[64];
#endif // ifdef TARGET_DEBUG
    };

    // runtime extra data (do not IO this)
    union {
        struct {
            int diff_trigger_tick;
            f32 diff_start_z;
            f32 diff_target_z;
            f32 diff_rate; // units/tick
        };

#ifdef TARGET_DEBUG
        u8 scratch1[64];
#endif // ifdef TARGET_DEBUG
    };

    int n_sides;  // number of sides in sector

    // minimum/maximum bounds of this sector
    union {
        box2f_t bounds;
        struct {
            v2 min, max;
        };
    };

    // 3D bounds of this sector
    box3f_t bounds_3d;

    // current block bounds of this sector (TODO: remove?)
    box2i_t block_bounds;

    // 2D area of sector
    // needed for rapid computation of random point in sector
    f32 area;

    DLIST(entity_t) entities; // entities in sector
    LLIST(decal_t) decals;    // decals on sector

    // list of particles in sector (on level arena)
    DYNLIST(particle_t) particles;

    // list of constituent subsectors
    LLIST(subsector_t) subs;

    // list of triangles in sector (on level arena)
    DYNLIST(sect_tri_t) tris;

    // direct neighbors (not including neighbors via disconnected portals!)
    DYNLIST(sector_t*) neighbors;

    // list of sides which are disconnected portals
    // TODO: maybe not worth storing - ll on side?
    DYNLIST(side_t*) disconnected_portals;

    // list of outgoing PVS sides (sides to a different sector via a
    // on-disconnected portal)
    // TODO: maybe not worth storing - ll of side?
    DYNLIST(side_t*) pvs_outgoing;

    // hash of sector geometry which affects tesselation
    hash_t tess_hash;

    // hash of PVS-affecting geometry
    hash_t pvs_hash;

    DECL_LEVEL_FIELDS_UNION()
} sector_t;

typedef struct sector_type {
    bool is_liquid: 1;
    bool is_door: 1;

    struct {
        const char *tex;

        bool has_light: 1;
        bool no_move: 1;
        bool use_custom_hsv: 1;

        // HSV to apply to texture
        v3 hsv;

        // screen tint when under liquid
        v4 tint;

        v4 extra_bloom;

        // light (only if has_light)
        light_params_t light;

        // if non-zero, liquid does damage
        int damage_ticks;
        f32 damage_per_hit;

        // offset for viscosity, positive = more drag on movement, negative = less
        f32 viscosity;

        // particles measured in particles/unit^2/second
        f32 particle_amount;

        // called according to particle_amount, v3 is a random surface position
        void (*particle_fn)(level_t*, sector_t*, v3);

        // acts on entities inside of the liquid (per-fixed update)
        void (*inside_fn)(level_t*, sector_t*, entity_t*);

        // acts on entries that enter liquid
        void (*on_enter_fn)(level_t*, sector_t*, entity_t*);

        // acts on entries that exit liquid
        void (*on_exit_fn)(level_t*, sector_t*, entity_t*);
    } liquid;

    struct {

    } door;
} sector_type_t;

extern sector_type_t SECTOR_TYPES[SECTOR_TYPE_COUNT];

typedef struct decal {
    decal_type_e type;

    bool is_on_side;

    // not a union so IO is easier :)
    struct {
        struct {
            side_t *ptr;
            v2 offsets;

            // side seg_hash when this decal was last valid
            hash_t seg_hash;
        } side;

        struct {
            sector_t *ptr;
            v2 pos;
            plane_type_e plane;
        } sector;
    };

    tex_id_t tex; // texture
    v2i tex_offsets; // texture offsets
    v4 tint;

    // ticks to live, -1 if permanent
    int ticks;

    // "priority" of this decal
    // 0 -> non-removable
    int priority;

    // tick when spawned
    int spawn_tick;

    // sprite rotation
    f32 rotation;

    // list of decals on either side or sector
    LLIST_NODE(decal_t) node;

    // see level_t.decals_by_type_ordered
    DLIST_NODE(decal_t) level_by_type_node;

    DECL_LEVEL_FIELDS_UNION()
} decal_t;

// type information for DECAL_TYPE*
typedef struct decal_type {
    // default texture
    const char *tex;

    // flags
    struct {
        bool age_removable: 1;
        bool has_light: 1;
        bool clamped_to_surface: 1;
    };
} decal_type_t;

// see l_decal.c
extern decal_type_t DECAL_TYPES[DECAL_TYPE_COUNT];

// see entity.h
typedef struct entity {
    entity_type_e itype;
    const entity_type_t *ptype;

    union { struct { v2 pos; f32 z; }; v3 pos_xyz; };
    union { struct { v2 vel; f32 vel_z; }; v3 vel_xyz; };

    // velocity impulse
    v3 impulse;

    // for grounded entities: always just 2D movement
    // for flyers: can include Z
    v3 dir;

    // io data
    struct {
        // which bookmark this is
        u8 bookmark_index;

        // TODO: only for fill lights
        light_params_t light;

        f32 spawn_point_angle;
    };

    // flags
    struct {
        bool destroy       : 1; // if true, entity is removed next update
        bool update_blocks : 1; // set when entity needs blocks updated
        bool grounded      : 1; // true if entity is on ground
        bool dashing       : 1; // player only: is dashing
        bool dash_hold     : 1; // player only: is holding dash
        bool sliding       : 1; // player only: is sliding
        bool in_liquid     : 1; // true if entity is in liquid in current sector
        bool corpse        : 1; // true if this entity is a corpse (dying)
    };

    // tick when entity became a corpse
    // destroyed after ENEMY_DEATH_TIME_S
    int corpse_tick;

    int last_shot; // last shot tick

    f32 swing;
    f32 jump;
    int last_jump_tick;

    // tick when last slide began
    int last_slide_begin_tick;

    // tick when player was last sliding
    int last_slide_tick;

    // angle of current slide (look dir vs. travel dir at start)
    f32 slide_angle;

    // current stamina
    f32 stamina;

    i64 last_dash_abs_ns; // last dashin *ABSOLUTE TIME* (not scaled!)
    v3 dash_dir;

    int last_heal;      // last heal tick
    int last_damage_tick;    // last damaged tick
    f32 health;         // current health
    v3 last_damage_dir; // last damage direction from entity

    // last time this entity was damaged by a slide, used to give
    // some invulnerable ticks after slide damage
    int last_slide_hit_tick;

    // for is_attach entities which are attached to sides or sectors
    struct {
        side_t *side;
        sector_t *sector;
        plane_type_e plane;
        v2 offset;

        // if true, entity acts normally (not is_attach'd)
        // disabled when entity is reattached
        bool disabled;
    } attach;

    struct {
        lptr_t source;
        v3 dir;
        v3 fake_origin;
        v3 fake_dir;
        f32 damage;
    } projectile;

    // last tick where entity was grounded/airbore
    int last_grounded_tick, last_airborne_tick;

    // last tick where entity STARTED being grounded/airborne
    int last_grounded_start_tick, last_airborne_start_tick;

    // tick where z velocity "crested", that is, the last tick when Z velocity
    // went from being positive to negative (the crest of a jump)
    int z_vel_crest_tick;

    // z at z_vel_crest_tick
    f32 z_crest;
 
    // ticks when spawned
    int spawn_tick;

    // total_scaled_ns when spawned
    nstime_t spawn_ns;

    // velocity at last movement
    v3 last_vel;

    // lptr to liquid sector entity was in last tick, LPTR_NULL if no such
    // sector
    lptr_t last_liquid;

    // tick when liquid was entered
    int liquid_enter_tick;

    // tick when last liquid teleport occurred
    int last_liquid_teleport_tick;

    // TODO
    v3 liquid_teleport_target;

    // sector which was exited from liquid teleport
    lptr_t liquid_teleport_sector;

    // tick when last portal travel occurred
    int last_portal_tick;

    union {
        struct {
            bool target_through_portal: 1;
            int target_last_seen_tick: 31;
            int target_last_update_tick;
            v3 target_last_seen_pos;
            v3 target_last_seen_dir;
            f32 target_last_seen_dist;
            v2 walk_dir;
            v3 fly_dir;
            int move_ticks;
            int awake_tick;
            int last_attack_tick;

            FIXLIST(path_point_t, 16) path;
            int path_ticks;
            int last_path_tick;
            
            // position on the previous tick
            v3 last_tick_pos;

            int move_frustration;

            int pain_ticks;

            // usually 1.0f == 1 walk cycle
            f32 anim_time;

            v3 turret_dir;
            v3 turret_eye_dir;
            v3 turret_eye_pos;
            int turret_attack_start_tick;

            int scion_last_charge_tick;
            int scion_start_charge_tick; // start of current scion charge
            int scion_charge_ticks;

            int head_shot_tick;

            struct {
                bool is_mini: 1;
            } bullet;
        };

#ifdef TARGET_DEBUG
        u8 scratch[1024];
#endif // ifdef TARGET_DEBUG
    };

    sector_t *sector;
    subsector_t *subsector; // only valid if sector != NULL
    room_t *room;           // current room, can be NULL

    // list of entities in sector
    DLIST_NODE(entity_t) sector_node;

    // list of entities in subsector
    DLIST_NODE(entity_t) subsector_node;

    // blocks this entity is in
    struct { block_t *arr[4]; int n; } blocks;

    // node for arbitrary entity linked lists
    LLIST_NODE(entity_t) node;

    DLIST_NODE(entity_t) level_by_type_node;

    DECL_LEVEL_FIELDS_UNION()
} entity_t;

// TODO: color to u32?
// TODO: pack
typedef struct particle {
    int id;
    int duration, start;

    u8 type;
    sector_t *sector;

    bool delete: 1;   // true if particle should be deleted from level
    bool moved: 1;    // true if particle is moved to another sector 
    bool grounded: 1; // true if particle is on ground

    union {
        struct {
            v2 pos;
            f32 z;
        };

        v3 pos_xyz;
    };

    union {
        struct {
            v2 vel;
            f32 vel_z;
        };

        v3 vel_xyz;
    };
    
    // initial direction
    v3 dir;

    // "extra" per-particle data
    union {
        struct {
            lptr_t target;
        } spawn;

        struct {
            v3 normal;
        } enemy_bullet;
    };

    // NOTE: does not apply to all particle types
    v4 color;
} particle_t;

typedef struct particle_type {
    const char *tex;
    v3 gravity;
    v3 air_drag;
    v3 floor_drag;
    v3 restitution;

    // flags
    struct {
        bool is_fancy    : 1; // if true, rendered as fancy particle
        bool has_light: 1;
    };
} particle_type_t;

extern particle_type_t PARTICLE_TYPES[PARTICLE_TYPE_COUNT];

// one map "block" containing:
// * walls which collide with it
// * sectors which collide with it
// * subsectors which collide with it
// * entities inside of it
typedef struct block {
    DYNLIST(sector_t*) sectors;
    DYNLIST(wall_t*) walls;
    DYNLIST(int) subsectors;
    DYNLIST(entity_t*) entities;
    LLIST_NODE(struct block) node;
    int index, version;
} block_t;

// represents one (separated) room of a level, bounds are whole integer units
typedef struct room {
    box2i_t bounds;

    // TODO: should be bitfields but this gives problems with IO
#ifdef TARGET_DEBUG
    union {
#endif // ifdef TARGET_DEBUG

        struct {
            // IO fields
            bool is_entry;

            // runtime fields
            bool is_completed;
        };

#ifdef TARGET_DEBUG
        u8 scratch[16];
    };
#endif // ifdef TARGET_DEBUG

    DECL_LEVEL_FIELDS_UNION()
} room_t;

// on level arena
typedef struct ltext {
    char *name;
    char *text;
} ltext_t;

typedef struct level {
    // heap tied to level lifetime (for allocations which live at most as long
    // as the level)
    allocator_t arena;

    // arbitrary "version" number, bumped whenever anything is recalc'd
    int version;

    // user-facing level name, on arena
    const char *name;

    // flags
    union {
        struct {
            bool is_hub: 1;
        };

        int flags;
    };

    struct {
        // TODO: actually use
        f32 dist;
    } fog;

    // next particle id to use
    int next_particle_id;

    // level palette
    v3 palette[LEVEL_PALETTE_SIZE];

    // sides which need to have their sector updated (level_update_side_sector)
    // must have "dirty_side" flag set
    DYNLIST(lptr_t) dirty_sect_sides;

    // pointers to delete next update
    DYNLIST(lptr_t) delete_ptrs;

    DYNLIST(lptr_t) recalc_ptrs;

    union {
        struct {
            genlist_t
                vertices,
                walls,
                sides,
                sectors,
                decals,
                entities,
                rooms;
        };

        genlist_t lists[LT_COUNT];
    };

    // extra string textures for this level
    DYNLIST(ltext_t) ltexts;

    // list of all subsectors
    blklist_t subsectors;

    // counts of each particle type
    int particle_counts[PARTICLE_TYPE_COUNT];

    // list of decals of each type, in order of addition (front is oldest)
    // DECAL_TYPE_PLACEHOLDER list is always empty, decals of this type are not
    // tracked
    DLIST(decal_t) decals_by_type[DECAL_TYPE_COUNT];

    // list of entities of each type, in order of addition (front is oldest)
    DLIST(entity_t) entities_by_type[ENTITY_TYPE_COUNT];

    // sector matrices
    // see sector_matrices.c/h
    struct {
        union {
            struct {
                // visibility matrix ((p)otentially (v)isible (s)et)
                sector_matrix_t pvs;

                // visibility matrix w/ portals ((e)xhaustive (v)isible (s)et)
                sector_matrix_t evs;

                // reachability matrix
                sector_matrix_t reachable;

                // near sectors matrix (sectors within 32 units of each other)
                sector_matrix_t near;
            };

            sector_matrix_t arr[4];
        };

        // true if update is in process
        bool dirty, last_dirty, last_calc;

        // queues of sectors to update
        DYNLIST(lptr_t)
            pvs_queue,
            pvs_finalize_queue,
            evs_queue,
            evs_finalize_queue;

        // number of queue entries processed up to 3 * num_sectors
        int progress;
    } matrices;

    // level bounds, also define bounds for "blocks" array
    box2f_t bounds;

    // 2D array of collision blocks
    struct {
        // all blocks data + lists
        allocator_t arena;

        block_t *arr;
        v2i offset, size;

        int version;
    } blocks;
} level_t;

#undef DECL_LEVEL_FIELDS_LIST
#undef DECL_LEVEL_FIELDS_UNION
