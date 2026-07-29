#pragma once

#include "level/lptr.h"
#include "gfx/screenshake.h"
#include "ext/sokol.h"
#include "gfx/dynbuf.h"
#include "defs.h"

STATIC_ASSERT(SG_MAX_COLOR_ATTACHMENTS >= 5);
// TODO: STATIC_ASSERT(_SG_DEFAULT_PIPELINE_POOL_SIZE >= 256);

// global renderer
extern renderer_t *g_renderer;

typedef struct prepared_model prepared_model_t;

typedef struct screen_tint {
	v4 color;
	bool fade;
	f32 duration;

	nstime_t start_abs_ns;
    int start_frame;

    // flags
    struct {
        bool single_frame: 1;
    };
} screen_tint_t;

typedef struct sprite_inst_desc {
	v3 pos;
	v2 size;
	v4 color;
	v4 hsva;
	lptr_t id;        // nullable
	int render_flags; // RENDER_FLAG_*
    int flags; // SPRITE_FLAG_*
	tex_id_t tex_id;
	f32 rotation;
    v3 extra_light;
    v4 extra_bloom;
} sprite_inst_desc_t;

// see particle.glsl
typedef struct particle_inst_desc {
    v3 pos;
    v3 vel;
    v2 size;
    int id;
    int type;
    tex_id_t tex_id;
    int flags;
    v4 color;
    v4 hsva;
} particle_inst_desc_t;

typedef struct sector_render_info {
    // handle of sector occupying this spot
    genlist_handle_t handle;

	// verion of sector when render info was last updated
	int version;

	// mesh hash of last meshed sector version
	hash_t mesh_hash;

	// pointers into renderer_t buffer
	void *vertices, *indices;

	// counts
	int n_vertices, n_indices;

    // frame where sector was last prepare_sector'd
    int prepared_frame;

    // frame where lights were last gathered
    int lights_gather_frame;

    // only valid if lights_gather_frame == g->time.frame.count
    DYNLIST(light_desc_t) lights;
} sector_render_info_t;

typedef struct side_render_info {
    // handle of side occupying this spot
    genlist_handle_t handle;

	// verion of side when render info was last updated
	int version;

    // offset of 6 indices for SIDE_SEGMENT_MIDDLE for this side, if present
    // only used for portal sides
    int middle_indices;
} side_render_info_t;

typedef struct decal_render_info {
    // handle of decal occupying this spot
    genlist_handle_t handle;

	// verion of decal when render info was last updated
	int version;

    // last texture
    tex_id_t tex;
} decal_render_info_t;

typedef struct entity_render_info {
    // frame for this info
    int frame;

    // models for this frame, if any
    DYNLIST(prepared_model_t) models;
} entity_render_info_t;

// vertex for model pipeline (see model.glsl)
typedef struct model_vertex {
	v3 pos;
	v3 normal;
	v2 uv;
} model_vertex_t;

typedef struct model {
	tex_id_t tex, tex_overlay;
	f32 overlay_alpha;
	v3 hsv;
    v4 tint;
    v3 extra_light;
    v4 extra_bloom;
    stime_t spawn_time;
    int corpse_tick;

    // MRF_*
    int flags;

    // RENDER_FLAG_*
    int render_flags;

    lptr_t id;      // nullable
    int special_id; // only if (render_flags & RENDER_FLAG_SPECIAL_ID)

    // transform for this model
	m4 transform;

    // lookup in model atlas
    const model_data_t *data;

    // index group to draw, if zero-len/NULL then entire model is drawn
    // NOTE: non-owning
    const char *index_group;
} model_t;

// TODO: pack/uncomplicate
// parameters shared between all light types
typedef struct {
    v3 color;
    f32 power;
    f32 attenuation, z_attenuation;
    f32 c1, c2;
    f32 ambient;
    int flags;
} light_params_t;

// instance of a light, not necessarily visible
typedef struct light_desc {
    // should be unique across frames, tag with T_* | id or LIGHT_ID_* | id
    int id;

    lptr_t sector; // sector index if light comes from sector
    lptr_t side;   // side index if light comes from side

    // position
    v3 pos;

    light_params_t params;
} light_desc_t;

// an environmental light, not associated with a specific level object
typedef struct env_light {
    // seconds to live
    f32 duration_secs;

    // flags
    struct {
        // fade att, power, color
        bool fade: 1;
    };

    v3 pos;
    light_params_t params;

    // NOTE: don't set manually! this is controlled by the renderer
    f32 start_secs;

    // unique ID
    // NOTE: controlled by the renderer
    int id;
} env_light_t;

// a light stored across multiple frames
typedef struct light_info {
    struct {
        // frame where light shadow map was last updated
        int last_update_frame;

        // hash of block versions when shadow map was last generated
        hash_t blocks_hash;

        // index of light in shadow map, -1 if no index
        int index;

        // light position when shadow map generated
        v3 pos;

        // light attenuation when shadow map was generated
        f32 attenuation;

        // update priority
        int priority;
    } shadow;

    // overall priority of this light (currently just distance)
    int priority;

    // current blocks hash
    hash_t blocks_hash;

    int last_visible_frame;

    // "span" of the light surface, basically extra attenuation
    float span;

    // desc for this light
    light_desc_t desc;
} light_info_t;

typedef struct {
    // "side", "sector", etc. (name of buffer uniform)
    const char *name;

    // RT_*
    render_type_e render_type;

    // ignored if this buffer is not for a level type
    level_type_e level_type;

    // next index to assign for this frame, used only for non-level-type
    // (per-frame) buffers (models and sprites)
    int next_index;

    // see backing array
    int t_size;

    // pointer to backing array
    void *data;

    // GPU buffer
    sg_buffer buffer;

    // range of dirty indices (per-frame)
    struct { int min_index, max_index; } dirty;

    // max index used for entire level lifetime, reset when level is set
    int max_index_for_level;
} renderer_data_array_t;

typedef struct {
    void *data;             // data on r->arena, can be NULL
    v2i size;               // size of last query
    sg_pixel_format format; // image data format
    i64 last_request_frame; // frame where data was last requested
} renderer_pixel_data_t;

typedef struct renderer {
	// renderer-lifetime memory arena
	allocator_t arena;

    // current level
	level_t *level;

    // global nearest neighbor sampler
    sg_sampler smp_nearest;

    // global linear filtering sampler
    sg_sampler smp_linear;

    // all pipelines
    struct {
        union {
            struct {
                sg_pipeline
                    composite,
                    downsample,
                    upsample,
                    shadow,
                    post0,
                    post1,
                    post2,
                    phantom,
                    edge;
            };

            sg_pipeline arr[9];
        };

        union {
            struct {
                sg_pipeline level_incr;
                sg_pipeline level_depth;
                sg_pipeline level_decr;
                sg_pipeline level_reg;
                sg_pipeline model;
                sg_pipeline sprite;
                sg_pipeline particle;
                sg_pipeline fancy_particle;
            };

            sg_pipeline arr[8];
        } stencil[MAX_PORTAL_DEPTH + 1];
    } pipelines;

    // queried pixel data from extra_id_pos texture
    struct {
        renderer_pixel_data_t extra_id_pos;
    } pixel_data;

    // CPU-side renderer information for level elements
    struct {
        side_render_info_t sides[MAX_SIDES];
        sector_render_info_t sectors[MAX_SECTORS];
        decal_render_info_t decals[MAX_DECALS];
        entity_render_info_t entities[MAX_ENTITIES];
    } render_info;

    // buffers for level element render data
    struct {
        union {
            struct {
                // must correspond to RENDER_TYPE_* indices
                renderer_data_array_t
                    sides,
                    sectors,
                    decals,
                    models,
                    sprites;
            };

            renderer_data_array_t arrays[RENDER_TYPE_COUNT];
        };

        struct {
            side_render_data_t side[MAX_SIDES];
            sector_render_data_t sector[MAX_SECTORS];
            decal_render_data_t decal[MAX_DECALS];
            model_render_data_t model[MAX_PER_FRAME_MODELS];
            sprite_render_data_t sprite[MAX_PER_FRAME_SPRITES];
        } data;
    } render_data;

    // render data for each particle type
    struct {
        // per-frame tex id for particle type
        tex_id_t tex_id;

        // size of particle type
        v2 size;
    } particle_render_info[PARTICLE_TYPE_COUNT];

    // data for blocks
    struct {
        // data for block wall data buffer
        block_wall_data_t data[MAX_WALLS];

        // buffer of block_wall_data_t for each wall in each block
        sg_buffer data_buffer;

        // data for block wall index buffer
        block_walls_indices_t indices[MAX_BLOCKS];

        // buffer of block_walls_indices_t for each block
        sg_buffer indices_buffer;

        // total number of walls used in data
        int n_walls;

        // total number of blocks used  in "indices"
        int n_blocks;

        // if true, buffer is updated next frame
        bool dirty;

        // version of level blocks when this was last updated
        int version;
    } blocks;

    // GPU-side level vertex/index buffers (see db_*)
	sg_buffer level_vbuf, level_ibuf;

    // whole level vertex/index CPU-side buffer data (managed memory)
    // correspond to level_vbuf, level_ibuf
	dynbuf_t db_vertices, db_indices;

    // sprite vertex/index buffer (simple quad) + instance buffer
	sg_buffer sprite_vbuf, sprite_ibuf, sprite_instbuf;

    // particle instance buffer (v/i buffers are same as sprite)
    sg_buffer particle_instbuf;

    // fancy particle v/i buffers (cube) + instance buffer
    sg_buffer fancy_particle_vbuf, fancy_particle_ibuf, fancy_particle_instbuf;

    // model instance buffer
    sg_buffer model_instbuf;

	// current screenshakes
	DYNLIST(screenshake_t) screenshakes;

	// screen tint colors (layered)
	DYNLIST(screen_tint_t) tints;

	// lights to process this frame
	DYNLIST(light_desc_t) frame_lights;

	// environmental lights
	DYNLIST(env_light_t) env_lights;

	// extra models to render this frame
	DYNLIST(model_t) frame_models;

    // models for phantom pass
	DYNLIST(model_t) frame_phantom_models;

    // pass-prepared frame models
	DYNLIST(prepared_model_t) prepared_frame_models;

	// extra sprites to render this frame
	DYNLIST(sprite_inst_desc_t) frame_sprites;

    // sectors which need to be remeshed this frame
    DYNLIST(sector_t*) mesh_sectors;

    struct {
        // map of light_desc_t.id -> light_info
        // persists across frames
        map_t info;

        // used/free light indices
        BITMAP_DECL(indices, SHADOW_MAP_SIZE_LIGHTS * SHADOW_MAP_SIZE_LIGHTS);

        // number of lights in arr
        int n_lights;

        // light data (CPU/GPU shared data!)
        light_t arr[MAX_LIGHTS];

        // buffer for light_t data array
        sg_buffer buffer;
    } light;

    // TODO: doc
    struct {
        sg_buffer particles_buffer;
        sg_buffer cells_buffer;
        int n_particles;
        v2i cell_dims;
    } psim;
} renderer_t;

void renderer_init();

// reset all renderer caches, force everythign to remesh
void renderer_reset();

// set current level for renderer
// level is optional, level=NULL will not initialize for a new level
void renderer_set_level(level_t *level);

// destroy renderer, free resources
void renderer_destroy();

// use when sector is deleted to free associated renderer resources, if there
// are any
void renderer_free_sector(sector_t *sector);

// update camera state
void renderer_update_cam();

// query pixels for renderer, must be done at end of frame
void renderer_do_query_pixels();

// render
void renderer_render();

// get info buffer data at specified pixel position
v4 renderer_info_at(v2i pos);

// push a tint for the renderer
void renderer_add_tint(const screen_tint_t *tint);

// get list of lights for specified lptr_t
// NOTE: data is only valid for the frame upon which is is retrieved. pointers
// may become invalidated in between frames!
void renderer_lights_for(lptr_t ptr, DYNLIST(light_info_t*) *out);
