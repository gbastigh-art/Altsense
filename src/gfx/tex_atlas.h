#pragma once

#include "defs.h"
#include "ext/sokol.h"
#include "util/math.h"
#include "util/types.h"
#include "util/map.h"
#include "util/blklist.h"

// global texture atlas
extern tex_atlas_t *g_tex_atlas;

typedef struct tex_anim tex_anim_t;
typedef struct tex_atlas_entry tex_atlas_entry_t;

typedef void (*virtual_texture_render_f)(
    tex_atlas_entry_t*,
    DYNLIST(sprite_t)*,
    void*);

typedef struct tex_atlas_virtual_data {
    // mark texture as dirty
    bool dirty: 1;

    // always render this texture, not just when dirty
    bool render_always: 1;

    // deletes virtual texture if flag is set
    bool discard: 1;

    // size, change to move around
    v2i size;

    virtual_texture_render_f func;
    void *userdata;
} tex_atlas_virtual_data_t;

typedef struct tex_atlas_entry {
    // unique ID
    tex_id_t id;

    box2i_t box;
    v2 uv;
    int layer;

    // uv coordinates on layer
    box2f_t box_uv;
    v2 size_uv;

    // px coordnates on layer
    box2i_t box_px;
    v2i size_px;

    // name, allocated on atlas->allocator
    const char *name;

    // only applicable if layer is TEX_ATLAS_LAYER_VIRTUAL
    tex_atlas_virtual_data_t virtual;

    // if true, layer box collision is ignored for this entry
    bool ignore_collision;

    v2i last_virtual_size;
} tex_atlas_entry_t;

typedef struct tex_atlas {
    // parent allocator
    allocator_t *allocator;

    // atlas-lifetime arena
    allocator_t arena;

    // base atlas 2D texture array
    sg_image image;

    // virtual textures (TEX_ATLAS_LAYER_VIRTUAL)
    sg_image virtual_image;

    // depth_stencil image for virtual textures
    sg_image virtual_depth;

    // buffer of tex_atlas_entry_info_t
    struct {
        sg_buffer buffer;
        tex_atlas_entry_info_t data[TEX_ATLAS_MAX_ENTRIES];
    } entry_info;

    // entries in each layer
    DYNLIST(tex_atlas_entry_t*) layer_entries[TEX_ATLAS_DEPTH];

    // entries in virtual layer
    DYNLIST(tex_atlas_entry_t*) virtual_entries;

    // data for entire atlas
    u8 *data;

    // char *name -> tex_atlas_entry_t*
    map_t entries_by_name;

    // if true, atlas is uploaded next tex_atlas_update
    bool dirty;

    // image of each atlas layer
    sg_image layer_images[TEX_ATLAS_DEPTH];

    // atlas entries (tex_atlas_entry_t)
    blklist_t entries;

    // animated teture specifications loaded from anim.ini
    // map of const char* -> tex_anim_t
    map_t anims;

    // for rendering to virtual textures
    sg_attachments virtual_attach;

    // for clearing virtual textures
    sg_pipeline pipeline_clear;

#ifdef TARGET_DEBUG
    // char* -> tex_id_t
    // map of IDs from before reset, used to reassign IDs properly on asset
    // reload
    map_t old_names_to_old_ids;
#endif // ifdef TARGET_DEBUG
} tex_atlas_t;

void tex_atlas_init(allocator_t *allocator);
void tex_atlas_destroy();

// must be called each frame
void tex_atlas_update();

// must be called each frame
void tex_atlas_render();

// get or create texture by name, returns 0 if not found
tex_id_t tex_atlas_lookup(const char *name);

// get or create texture by (formatted) name, returns 0 if not found
tex_id_t tex_atlas_lookupf(const char *fmt, ...);

// get entry info by ID, returns notex if not found
tex_atlas_entry_t *tex_atlas_entry_by_id(tex_id_t id);

// get entry info by name, returns notex if not found
tex_atlas_entry_t *tex_atlas_entry_by_name(const char *name);

// get entry info from format str, returns notex if not found
tex_atlas_entry_t *tex_atlas_entry_by_namef(const char *fmt, ...);

// true if texture in atlas
bool tex_atlas_contains(const char *name);

// loads all textures found in resource directory into atlas
void tex_atlas_load_all();

// reset texture atlas to blank, flush caches
void tex_atlas_reset();

// allocate a virtual texture
// returns NULL if texture could not be allocated
tex_atlas_entry_t *tex_atlas_alloc_virtual(
        const char *name,
        const tex_atlas_virtual_data_t *data);

void tex_atlas_free_virtual_by_id(tex_id_t id);

// applies bindings corresponding to @block tex_atlas (see common.glsl)
void tex_atlas_apply_bindings(
    sg_pipeline pip,
    sg_bindings *bindings);
