#include "gfx/tex_atlas.h"
#include "gfx/screenquad.h"
#include "gfx/shaders.h"
#include "gfx/sprite.h"
#include "game.h"
#include "util/file.h"
#include "util/hooks.h"
#include "util/image.h"
#include "util/ini.h"
#include "shader/clear.glsl.h"

// global texture atlas instance
static tex_atlas_t *atlas = NULL;
RELOAD_STATIC_GLOBAL(atlas)

tex_atlas_t *g_tex_atlas = NULL;
RELOAD_STATIC_GLOBAL(g_tex_atlas)

typedef struct tex_anim {
    int ticks_per_frame;
    int width;
} tex_anim_t;

static tex_atlas_entry_t *get_colliding(
        int layer,
        const box2i_t *box) {
    DYNLIST(tex_atlas_entry_t*) *entries =
        layer == TEX_ATLAS_LAYER_VIRTUAL ?
            &atlas->virtual_entries
            : &atlas->layer_entries[layer];

    dynlist_each(*entries, it) {
        tex_atlas_entry_t *entry = *it.el;
        if (!entry->ignore_collision
            && box2i_collides(entry->box, *box)) {
            return entry;
        }
    }

    return NULL;
}

// try to reserve "size" space in specified layer
static bool try_reserve_layer_space(
        tex_atlas_entry_t *entry,
        v2i size,
        int layer) {
    bool next_x = false, next_y = false;
    v2i offset = v2i_of(0);

    while (
        offset.x + size.x <= TEX_ATLAS_SIZE
           && offset.y + size.y <= TEX_ATLAS_SIZE) {
        const box2i_t box = box2i_ps(offset, size);
        tex_atlas_entry_t *colliding = get_colliding(layer, &box);

        if (!colliding) {
            // found a spot
            break;
        } else {
            // move to the closest side of the colliding box
            if (next_x || (size.x < size.y && !next_y)) {
                offset.x = colliding->box.max.x + 1;
                next_x = false;
            } else {
                offset.y = colliding->box.max.y + 1;
                next_y = false;
            }

            const bool
                of_x = offset.x + size.x > TEX_ATLAS_SIZE,
                of_y = offset.y + size.y > TEX_ATLAS_SIZE;

            // double overflow means out of space
            if (of_x && of_y) {
                return false;
            }

            // on overflow, reset up to the next row
            // use next_x/y to ensure that we advance on the opposite axis
            // next time
            if (of_x) {
                offset.x = 0;
                next_y = true;
            }

            if (of_y) {
                offset.y = 0;
                next_x = true;
            }
        }
    }

    // success
    const v2 unit = v2_of(1.0f / TEX_ATLAS_SIZE);
    entry->box = box2i_ps(offset, size),
    entry->uv = v2_mul(v2_from_i(offset), unit);

    const box2f_t box_uv =
        box2f_ps(
            entry->uv,
            v2_mul(v2_from_i(box2i_size(entry->box)), unit));

    entry->box_uv = box_uv;
    entry->box_px = entry->box;
    entry->size_uv = box2f_size(box_uv);
    entry->size_px = box2i_size(entry->box);
    return true;
}

static void update_entry_info_for(const tex_atlas_entry_t *entry) {
    tex_anim_t *anim = map_get(tex_anim_t, &atlas->anims, entry->name);

    atlas->entry_info.data[entry->id.index] = (tex_atlas_entry_info_t) {
        .uv_min = entry->box_uv.min,
        .uv_max = entry->box_uv.max,
        .layer = entry->layer,
        .anim_ticks_per_frame = anim ? anim->ticks_per_frame : 0,
        .anim_width = anim ? anim->width : 0,
    };

    // TODO: partial dirty!
    atlas->dirty = true;
}

static tex_atlas_entry_t *tex_atlas_insert(
        const char *name,
        const u8 *data,
        v2i size,
        bool is_virtual) {
    ASSERT(name, "no name for texture?");

    int layer;

    if (is_virtual) {
        layer = TEX_ATLAS_LAYER_VIRTUAL;
    } else {
        // TODO: make use of multiple layers
        layer = 0;
    }

    tex_atlas_entry_t *entry = NULL;

    tex_id_t desired_id = { U16_MAX } ;
#ifdef TARGET_DEBUG
    {
        const tex_id_t *slot =
            map_get(tex_id_t, &atlas->old_names_to_old_ids, name);
        if (slot) {
            desired_id = *slot;
        }
    }
#endif // ifdef TARGET_DEBUG

    // try to get a spot at the desired index
    if (desired_id.index != U16_MAX) {
        entry =
            blklist_try_add_at(
                tex_atlas_entry_t,
                &atlas->entries,
                desired_id.index);

        if (entry) {
            LOG("entry %s reassigned to old ID %d", name, desired_id.index);
        } else {
            WARN(
                "could not get old id %d for entry named %s",
                desired_id.index,
                name);
        }
    }


    // otherwise just grab whatever
    if (!entry) {
        entry = blklist_add(tex_atlas_entry_t, &atlas->entries);
    }

    ASSERT(entry);
    *entry = (tex_atlas_entry_t) { 0 };

    const tex_id_t id = { blklist_index_of(&atlas->entries, entry) };

    if (!try_reserve_layer_space(entry, size, layer)) {
        // TODO: lmao fix
        ASSERT(false);
    }

    entry->id = id;
    entry->layer = layer;
    entry->name = mem_strdup(&atlas->arena, name);

    if (layer != TEX_ATLAS_LAYER_VIRTUAL) {
        for (int y = 0; y < size.y; y++) {
            memcpy(
                &atlas->data
                    [((layer * TEX_ATLAS_SIZE * TEX_ATLAS_SIZE)
                        + ((y + entry->box.min.y) * TEX_ATLAS_SIZE)
                        + entry->box.min.x) * 4],
                &data[y * (size.x * 4)],
                size.x * 4);
        }
    }

    update_entry_info_for(entry);

    if (layer == TEX_ATLAS_LAYER_VIRTUAL) {
        *dynlist_push(atlas->virtual_entries) = entry;
    } else {
        *dynlist_push(atlas->layer_entries[layer]) = entry;
    }

    map_insert(
        &atlas->entries_by_name,
        mem_strdup(&atlas->arena, name),
        entry);

    LOG(
        "%s (%d) at %" PRIv2i " (layer %d)",
        name, id, FMTv2i(entry->box_px.min), layer);

    tex_anim_t *anim = map_get(tex_anim_t, &atlas->anims, entry->name);
    if (anim) {
        LOG("  with animation %d %d", anim->ticks_per_frame, anim->width);
    }

    // TODO: better dirtying
    atlas->dirty = true;
    return entry;
}

static bool tex_atlas_insert_from_path(
        const char *name,
        const char *path) {

    u8 *data;
    v2i size;
    const char *errmsg;
    if (!image_load_rgba(
            &g->frame_arena,
            path,
            &data,
            &size,
#ifdef SOKOL_METAL
            true,
#else
            true,
#endif // ifdef SOKOL_METAL
            &errmsg)) {
        WARN("image load failure: %s", errmsg);
        return false;
    }

    return tex_atlas_insert(name, data, size, false);
}

// remove entry form layers, etc.
static void tex_atlas_free_entry(tex_atlas_entry_t *entry) {
    ASSERT_DEBUG(
        blklist_present(&atlas->entries, entry->id.index),
        "%s",
        entry->name);

    if (entry->layer == TEX_ATLAS_LAYER_VIRTUAL && entry->virtual.func) {
        RELOAD_DELETE_FUNCPTR(&entry->virtual.func);
    }

    // drop from layer
    DYNLIST(tex_atlas_entry_t*) *layer_entries =
        entry->layer == TEX_ATLAS_LAYER_VIRTUAL ?
            &atlas->virtual_entries
            : &atlas->layer_entries[entry->layer];

    bool found = false;
    dynlist_each(*layer_entries, it) {
        if (*it.el == entry) {
            dynlist_remove_it(*layer_entries, it);
            found = true;
            break;
        }
    }

    ASSERT(found, "%s", entry->name);

    map_try_remove(&atlas->entries_by_name, entry->name);
    blklist_remove(&atlas->entries, entry->id.index);
}

static void tex_atlas_load_anims(const char *path) {
    map_clear(&atlas->anims);

    ini_t ini;
    const ini_error_e err = ini_init_from_path(&ini, &g->frame_arena, path);
    if (err != INI_OK) {
        WARN("error loading animations from %s: %d", path, err);
        return;
    }

    ini_iter_t it_s = INI_ITER_INIT;
    while (ini_iter(&ini, &it_s)) {
        if (strlen(it_s.name) != 0) {
            WARN("bad animation section [%s], ignoring", it_s.name);
            continue;
        }

        ini_section_iter_t it_p = INI_SECTION_ITER_INIT(it_s);
        while (ini_iter_section(&ini, &it_p)) {
            tex_anim_t anim;
            if (sscanf(
                    it_p.value,
                    "%d,%d",
                    &anim.ticks_per_frame,
                    &anim.width) != 2) {
                continue;
            }

            LOG(
                "loaded animation %s=%d,%d",
                it_p.name, anim.ticks_per_frame, anim.width);

            map_insert(
                &atlas->anims,
                mem_strdup(&atlas->arena, it_p.name),
                anim);
        }
    }
}

void tex_atlas_reset() {
    typedef struct {
        char *name;
        v2i size;
        tex_atlas_virtual_data_t virtual;
    } tmp_virtual_t;

    DYNLIST(tmp_virtual_t) tmp_virtuals =
        dynlist_create(tmp_virtual_t, &g->frame_arena);

    // remove existing virtual functions, save current virtuals
    if (atlas->virtual_entries) {
        dynlist_each(atlas->virtual_entries, it) {
            *dynlist_push(tmp_virtuals) = (tmp_virtual_t) {
                .name = mem_strdup(&g->frame_arena, (*it.el)->name),
                .size = (*it.el)->size_px,
                .virtual = (*it.el)->virtual,
            };

            RELOAD_DELETE_FUNCPTR(&(*it.el)->virtual.func);
        }
    }

#ifdef TARGET_DEBUG
    // save old IDs
    typedef struct {
        const char *name;
        tex_id_t id;
    } tmp_name_id_pair_t;

    DYNLIST(tmp_name_id_pair_t) tmp_name_id_pairs =
        dynlist_create(tmp_name_id_pair_t, &g->frame_arena);

    if (blklist_valid(&atlas->entries)) {
        blklist_each(tex_atlas_entry_t, &atlas->entries, it) {
            *dynlist_push(tmp_name_id_pairs) = (tmp_name_id_pair_t) {
                .name = mem_strdup(&g->frame_arena, it.el->name),
                .id = it.el->id,
            };
        }
    }
#endif // ifdef TARGET_DEBUG

    // free all associated memory
    if (allocator_valid(&atlas->arena)) {
        heap_allocator_destroy(&atlas->arena);
    }

    heap_allocator_init(&atlas->arena, atlas->allocator, NULL);

    atlas->data =
        mem_calloc(
            &atlas->arena,
            TEX_ATLAS_DEPTH * TEX_ATLAS_SIZE * TEX_ATLAS_SIZE * 4);

    blklist_init(
        &atlas->entries,
        &(blklist_desc_t) {
            .allocator = &atlas->arena,
            .block_size = 256,
            .t_size = sizeof(tex_atlas_entry_t),
            .max_size = TEX_ATLAS_MAX_ENTRIES,
        });

    for (int i = 0; i < TEX_ATLAS_DEPTH; i++) {
        atlas->layer_entries[i] = NULL;
        dynlist_init(atlas->layer_entries[i], &atlas->arena);
    }

    atlas->virtual_entries = NULL;
    dynlist_init(atlas->virtual_entries, &atlas->arena);

    map_init(
        &atlas->entries_by_name,
        &atlas->arena,
        sizeof(char*),
        sizeof(tex_atlas_entry_t*),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        NULL,
        NULL);

    map_init(
        &atlas->anims,
        &atlas->arena,
        sizeof(char*),
        sizeof(tex_anim_t),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        NULL,
        NULL);

#ifdef TARGET_DEBUG
    map_init(
        &atlas->old_names_to_old_ids,
        &atlas->arena,
        sizeof(char*),
        sizeof(tex_id_t),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        NULL,
        NULL);

    // init old IDs map
    dynlist_each(tmp_name_id_pairs, it) {
        map_insert(
            &atlas->old_names_to_old_ids,
            mem_strdup(&atlas->arena, it.el->name),
            it.el->id);
    }
#endif // ifdef TARGET_DEBUG

    // generate different sizes of notex...
    const v2i sizes[] = {
        v2i_of(16, 16),
        v2i_of(32, 16),
        v2i_of(16, 32),
        v2i_of(32, 32),
        v2i_of(16, 64),
        v2i_of(64, 16),
        v2i_of(64, 64),
    };

    for (int i = 0; i < ARRLEN(sizes); i++) {
        // create texture 0
        u32 *data = mem_calloc(&g->frame_arena, sizes[i].x * sizes[i].y * 4);
        const u32 c0 = 0xFFFF00FF, c1 = 0xFF550055;

        for (int y = 0; y < sizes[i].y; y++) {
            for (int x = 0; x < sizes[i].x; x++) {
                data[(y * sizes[i].x) + x] =
                    (x / 8) % 2 == (y / 8) % 2 ? c0 : c1;
            }
        }

        const char *name = "notex";

        if (i != 0) {
            name =
                mem_strfmt(
                    &g->frame_arena,
                    "notex_%dx%d",
                    sizes[i].x,
                    sizes[i].y);
        }

        tex_atlas_entry_t *entry =
            tex_atlas_insert(name, (const u8*) data, sizes[i], false);

        ASSERT(entry);

        if (i == 0) {
            // first entry must be notex
            ASSERT(blklist_index_of(&atlas->entries, entry) == 0);
        }
    }

    // reload animations
    tex_atlas_load_anims("assets/texs/anim.ini");

    // reinsert virtuals
    dynlist_each(tmp_virtuals, it) {
        tex_atlas_alloc_virtual(
            it.el->name,
            &it.el->virtual);
    }
}

RELOAD_VISIBLE void tex_atlas_on_reload(void*) {
    // mark as dirty so entries re-render, even if they only draw once
    dynlist_each(g_tex_atlas->virtual_entries, it) {
        (*it.el)->virtual.dirty = true;
    }
}

void tex_atlas_init(allocator_t *allocator) {
    hook_register(HOOK_POST_RELOAD, tex_atlas_on_reload, NULL);

    atlas = mem_alloc(&g->arena, sizeof(*atlas));
    g_tex_atlas = atlas;

    *atlas = (tex_atlas_t) { .allocator = allocator };

    atlas->image =
        sg_make_image(
            &(sg_image_desc) {
                .type = SG_IMAGETYPE_ARRAY,
                .width = TEX_ATLAS_SIZE,
                .height = TEX_ATLAS_SIZE,
                .num_slices = TEX_ATLAS_DEPTH,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .usage = SG_USAGE_DYNAMIC,
                .label = "atlas.image",
            });

    atlas->virtual_image =
        sg_make_image(
            &(sg_image_desc) {
                .type = SG_IMAGETYPE_2D,
                .width = TEX_ATLAS_SIZE,
                .height = TEX_ATLAS_SIZE,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .render_target = true,
                .label = "atlas.virtual",
            });

    atlas->virtual_depth =
        sg_make_image(
            &(sg_image_desc) {
                .type = SG_IMAGETYPE_2D,
                .width = TEX_ATLAS_SIZE,
                .height = TEX_ATLAS_SIZE,
                .pixel_format = SG_PIXELFORMAT_DEPTH,
                .render_target = true,
                .label = "atlas.virtual_depth",
            });

    for (int i = 0; i < TEX_ATLAS_DEPTH; i++) {
        atlas->layer_images[i] =
            sg_make_image(
                &(sg_image_desc) {
                    .type = SG_IMAGETYPE_2D,
                    .width = TEX_ATLAS_SIZE,
                    .height = TEX_ATLAS_SIZE,
                    .pixel_format = SG_PIXELFORMAT_RGBA8,
                    .usage = SG_USAGE_DYNAMIC,
                    .label = mem_strfmt(&g->frame_arena, "atlas.layer%d", i),
                });
    }

    atlas->virtual_attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .label = "atlas.virutal.attach",
                .colors[0].image = atlas->virtual_image,
                .depth_stencil.image = atlas->virtual_depth,
            });

    atlas->entry_info.buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = TEX_ATLAS_MAX_ENTRIES * sizeof(tex_atlas_entry_info_t),
                .label = "atlas.entry_info_buffer",
            });

    tex_atlas_reset();
}

void tex_atlas_destroy() {
    dynlist_each(atlas->virtual_entries, it) {
        RELOAD_DELETE_FUNCPTR(&(*it.el)->virtual.func);
    }

    heap_allocator_destroy(&atlas->arena);
}

void tex_atlas_update() {
    if (!atlas->dirty) { return; }
    atlas->dirty = false;

    for (int i = 0; i < TEX_ATLAS_DEPTH; i++) {
        // TODO: subimage updates
        sg_update_image(
            atlas->layer_images[i],
            &(sg_image_data) {
                .subimage[0][0] = {
                    .ptr = &atlas->data[(i * TEX_ATLAS_SIZE * TEX_ATLAS_SIZE) * 4],
                    .size = TEX_ATLAS_SIZE * TEX_ATLAS_SIZE * 4
                }
            });
    }

    // TODO: subimage updates
    sg_update_image(
        atlas->image,
        &(sg_image_data) {
            .subimage[0][0] = {
                .ptr = atlas->data,
                .size = TEX_ATLAS_DEPTH * TEX_ATLAS_SIZE * TEX_ATLAS_SIZE * 4,
            }
        });

    sg_ext_update_buffer_partial(
        atlas->entry_info.buffer,
        0,
        &(sg_range) {
            .ptr = atlas->entry_info.data,
            .size =
                (atlas->entries.max_index + 1)
                    * sizeof(atlas->entry_info.data[0]),
        });
}

// checks if a virtual texture wants to resize and attempts to resize/move it
// if it does - returns true if the texture changed size
static bool check_and_update_virtual_size(tex_atlas_entry_t *entry) {
    if (v2i_eqv(entry->virtual.size, entry->last_virtual_size)) {
        // no size change
        return false;
    }

    LOG(
        "resizing atlas entry %s from %" PRIv2i " -> %" PRIv2i,
        entry->name,
        FMTv2i(entry->last_virtual_size),
        FMTv2i(entry->virtual.size));

    entry->last_virtual_size = entry->virtual.size;

    // mark as non-colliding
    entry->ignore_collision = true;

    // reallocate layer space
    if (!try_reserve_layer_space(
            entry,
            entry->virtual.size,
            TEX_ATLAS_LAYER_VIRTUAL)) {
        WARN(
            "failed to allocate %" PRIv2i " px in virtual layer, "
            "discarding virtual texture %s",
            FMTv2i(entry->virtual.size),
            entry->name);
        entry->virtual.discard = true;
        return false;
    }

    // collide again
    entry->ignore_collision = false;

    // update info in buffer
    update_entry_info_for(entry);

    return true;
}

static void render_virtual(tex_atlas_entry_t *entry) {
    while (true) {
        ASSERT(entry->virtual.func);

        ASSERT_DEBUG(v2i_eqv(entry->virtual.size, box2i_size(entry->box_px)));

        const v2i size = entry->virtual.size;
        const v2i origin = entry->box.min;

        const bool origin_top_left =
#ifdef SOKOL_METAL
            true;
#else
            false;
#endif // ifdef SOKOL_METAL

        sg_apply_viewport(
            v2_spread(origin), v2_spread(size), origin_top_left);
        sg_apply_scissor_rectf(
            v2_spread(origin), v2_spread(size), origin_top_left);

        // clear the area
        clear_vs_params_t vs_params = { 0 };
        screenquad_mats(&vs_params.model, &vs_params.view, &vs_params.proj);

        screenquad_render_ex(
            atlas->pipeline_clear,
            &(sg_bindings) { 0 },
            SLOT_clear_vs_params,
            SG_RANGE_REF(vs_params),
            -1,
            NULL);

        DYNLIST(sprite_t) sprites = dynlist_create(sprite_t, &g->frame_arena);

        entry->virtual.func(entry, &sprites, entry->virtual.userdata);

        if (check_and_update_virtual_size(entry)) {
            // re-sized, need to re-render
            continue;
        }

        if (entry->virtual.discard) {
            // don't render any more if discarded
            return;
        }

        if (dynlist_size(sprites) != 0) {
            const m4
                view = m4_identity(),
                proj =
                    cam_ortho(
#ifdef SOKOL_METAL
                        0.0f, size.x,
                        size.y, 0.0f,
                        1.0f, -1.0f);
#else
                        0.0f, size.x,
                        0.0f, size.y,
                        1.0f, -1.0f);
#endif // ifdef SOKOL_METAL
            sprite_batch_render(
                atlas->virtual_attach,
                sprites,
                dynlist_size(sprites),
                &proj,
                &view);
        }

        // done rendering
        break;
    }
}

void tex_atlas_render() {
    DYNLIST(tex_atlas_entry_t*) to_remove =
        dynlist_create(tex_atlas_entry_t*, &g->frame_arena);

    // since we can't clear the entire texture before rendering, we need to do
    // it manually with a pipeline that overwrites everything + always passes
    // depth tests. resets everything to transparent + black in the area in
    // which it is run.
    if (!atlas->pipeline_clear.id) {
        atlas->pipeline_clear = sg_make_pipeline(&(sg_pipeline_desc) {
            .shader = shaders_get(SHADER_CLEAR),
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_UINT16,
            .layout = {
                .attrs = {
                    [0].format = SG_VERTEXFORMAT_FLOAT2,
                    [1].format = SG_VERTEXFORMAT_FLOAT2,
                }
            },
            .depth = {
                .compare = SG_COMPAREFUNC_ALWAYS,
                .write_enabled = true,
                .pixel_format =
                    sg_query_image_desc(atlas->virtual_depth).pixel_format,
            },
            .colors[0] = {
                .pixel_format =
                    sg_query_image_desc(atlas->virtual_image).pixel_format,
            },
            .color_count = 1,
            .face_winding = SG_FACEWINDING_CCW,
            .cull_mode = SG_CULLMODE_BACK,
        });
        shaders_register_pipeline(&atlas->pipeline_clear);
    }

    sg_begin_pass(
        &(sg_pass) {
            .attachments = atlas->virtual_attach,
            .action = {
                .colors[0] = { .load_action = SG_LOADACTION_LOAD, },
                .depth = { .load_action = SG_LOADACTION_LOAD, },
            },
            .label = "atlas.virtual.pass",
        });
    dynlist_each(atlas->virtual_entries, it) {
        tex_atlas_entry_t *entry = *it.el;

        if (entry->virtual.discard) {
            *dynlist_push(to_remove) = entry;

            // don't render
            continue;
        }

        // check size change + re-render on change
        if (check_and_update_virtual_size(entry)) {
            entry->virtual.dirty = true;
        }

        if (!(entry->virtual.render_always || entry->virtual.dirty)) {
            // nothing to render
            continue;
        }

        // un-dirty if dirty
        entry->virtual.dirty = false;

        render_virtual(entry);

        if (entry->virtual.discard) {
            *dynlist_push(to_remove) = entry;
        }
    }
    sg_end_pass();

    dynlist_each(to_remove, it) {
        tex_atlas_free_entry(*it.el);
    }
}

void tex_atlas_load_all() {
    DYNLIST(char*) images = dynlist_create(char*, &g->frame_arena);

    const file_error_e err =
        file_find_with_ext(
            "assets/texs",
            "png",
            &images,
            &g->frame_arena);

    if (err != FILE_OK) {
        WARN("error finding with ext: %s", file_error_to_str(err));
        return;
    }

    dynlist_each(images, it) {
        char *dup = mem_strdup(tlscratch(), *it.el), *basename;
        file_spilt(dup, NULL, &basename, NULL);
        tex_atlas_insert_from_path(basename, *it.el);
    }
}

tex_id_t tex_atlas_lookup(const char *name) {
    // quick fail on NULL, empty, notex
    if (!name || !*name || !strcasecmp(name, "notex")) {
        return (tex_id_t) { 0 };
    }

    // attempt to lookup
    tex_atlas_entry_t **pentry =
        map_getp(tex_atlas_entry_t*, &atlas->entries_by_name, &name);

    if (!pentry) {
        // attempt to load from file if not found
        const char *path =
            mem_strfmt(tlscratch(), "assets/texs/%s.png", name);
        if (file_exists(path)) {
            if (!tex_atlas_insert_from_path(name, path)) {
                WARN("insert failure for atlas image %s",  path);
                return (tex_id_t) { 0 };
            }

            // lookup again
            pentry = map_getp(tex_atlas_entry_t*, &atlas->entries_by_name, &name);
            ASSERT(pentry);
        } else {
            WARN("attempt to get invalid texture %s", path);
            return (tex_id_t) { 0 };
        }
    }

    return (*pentry)->id;
}

tex_id_t tex_atlas_lookupf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const tex_id_t id = tex_atlas_lookup(mem_vstrfmt(&g->frame_arena, fmt, ap));
    va_end(ap);
    return id;
}

tex_atlas_entry_t *tex_atlas_entry_by_id(tex_id_t id) {
    tex_atlas_entry_t *entry =
        blklist_try_ptr(tex_atlas_entry_t, &atlas->entries, id.index);
    return entry ? entry : blklist_ptr(tex_atlas_entry_t, &atlas->entries, 0);
}

tex_atlas_entry_t *tex_atlas_entry_by_name(const char *name) {
    return tex_atlas_entry_by_id(tex_atlas_lookup(name));
}

tex_atlas_entry_t *tex_atlas_entry_by_namef(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tex_atlas_entry_t *entry =
        tex_atlas_entry_by_id(
            tex_atlas_lookup(
                mem_vstrfmt(&g->frame_arena, fmt, ap)));
    return entry;
}

bool tex_atlas_contains(const char *name) {
    if (str_is_empty(name)) {
        return false;
    }

    return map_contains(&atlas->entries_by_name, name);
}

tex_atlas_entry_t *tex_atlas_alloc_virtual(
        const char *name,
        const tex_atlas_virtual_data_t *data) {
    ASSERT(data->size.x > 0 && data->size.y > 0);

    if (map_contains(&atlas->entries_by_name, name)) {
        WARN(
            "already have tex atlas entry %s, removing for virtual",
            name);
        tex_atlas_free_entry(tex_atlas_entry_by_name(name));
    }

    // try insert new virtual texture
    tex_atlas_entry_t *entry;
    if (!(entry =
          tex_atlas_insert(
              name,
              NULL,
              data->size,
              true))) {
        WARN("failed to insert tex atlas virtual entry: %s", name);
        return NULL;
    }

    entry->virtual = *data;
    RELOAD_FUNCPTR(&entry->virtual.func);

    entry->last_virtual_size = data->size;
    entry->virtual.dirty = true; // initial render

    LOG(
        "alloc'd virtual texture %s @ %d",
        entry->name,
        blklist_index_of(&atlas->entries, entry));
    return entry;
}

void tex_atlas_free_virtual_by_id(tex_id_t id) {
    if (!blklist_present(&atlas->entries, id.index)) {
        WARN("attempt to free invalid vtex id");
        return;
    }

    tex_atlas_free_entry(
        blklist_ptr(tex_atlas_entry_t, &atlas->entries, id.index));
}

void tex_atlas_apply_bindings(
        sg_pipeline pip,
        sg_bindings *bindings) {
    const shader_refl_t *refl = shader_reflect(shader_for_pipeline(pip));

    const int image_slot = refl->image_slot_fn(SG_SHADERSTAGE_FS, "tex_atlas");
    if (image_slot != -1) {
        bindings->fs.images[image_slot] = atlas->image;
    }

    const int virtual_slot =
        refl->image_slot_fn(SG_SHADERSTAGE_FS, "tex_atlas_virtual");
    if (virtual_slot != -1) {
        bindings->fs.images[virtual_slot] = atlas->virtual_image;
    }

    const int storagebuffer_slot =
        refl->storagebuffer_slot_fn(
            SG_SHADERSTAGE_FS,
            "tex_atlas_entries_buffer");
    if (storagebuffer_slot != -1) {
        bindings->fs.storage_buffers[storagebuffer_slot] =
            atlas->entry_info.buffer;
    }
}
