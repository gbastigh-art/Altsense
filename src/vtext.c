#include "vtext.h"
#include "gfx/font.h"
#include "gfx/tex_atlas.h"
#include "level/level_types.h"
#include "game.h"

typedef struct {
    char *id; // unique ID, maybe just "str"
    char *str;
    tex_id_t tex;
    nstime_t last_used_ns; // last time this vtext was used (based on total_ns)
} vtext_entry_t;

typedef struct {
    allocator_t arena;

    // char* -> vtext_entry_t*
    map_t str_to_entry;
} vtexts_state_t;

// on g->arena
static vtexts_state_t *state;
RELOAD_STATIC_GLOBAL(state)

RELOAD_VISIBLE void ptr_vtext_entry_t__free(map_t *map, void *p) {
    vtext_entry_t *entry = *(vtext_entry_t**) p;
    mem_free(&state->arena, entry->id);
    mem_free(&state->arena, entry->str);
    mem_free(&state->arena, entry);
}

static void vtexts_lazy_init() {
    if (state) { return; }

    state = mem_calloc(&g->arena, sizeof(*state));

    heap_allocator_init(&state->arena, &g->arena, NULL);
    map_init(
        &state->str_to_entry,
        &state->arena,
        sizeof(char*),
        sizeof(vtext_entry_t*),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        ptr_vtext_entry_t__free,
        NULL);
}

RELOAD_VISIBLE void vtext_render(
        tex_atlas_entry_t *entry,
        DYNLIST(sprite_t) *sprites,
        void *userdata) {
    vtext_entry_t *vt = userdata;
    const v2i size = font_size(vt->str);

    if (!v2i_eqv(size, entry->virtual.size)) {
        // need to re-size before rendering
        entry->virtual.size = size;
        return;
    }

    const int height = font_height(vt->str);
    font_str(
        v2_of(0, height - FONT_GLYPH_SIZE.y),
        0.0f,
        v4_of(1.0f),
        FONT_DOUBLED,
        GFX_NO_FLAGS,
        vt->str,
        sprites);
}

tex_id_t vtext_get_or_createf(const char *id, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char *str = mem_vstrfmt(&g->frame_arena, fmt, ap);
    const tex_id_t tex = vtext_get_or_create(id, str);
    va_end(ap);
    return tex;
}

tex_id_t vtext_get_or_create(const char *id, const char *str) {
    ASSERT_DEBUG(str);

    if (str_is_empty(str)) { return (tex_id_t) { 0 }; }

    vtexts_lazy_init();

    const char *key = id ? id : str;
    vtext_entry_t **slot = map_get(vtext_entry_t*, &state->str_to_entry, key);

    if (slot) {
        vtext_entry_t *vt = *slot;

        if (strcmp(str, vt->str)) {
            // replace string with new
            mem_free(&state->arena, vt->str);
            vt->str = mem_strdup(&state->arena, str);

            // schedule for re-render
            tex_atlas_entry_t *entry = tex_atlas_entry_by_id(vt->tex);
            entry->virtual.size = font_size(vt->str);
            entry->virtual.dirty = true;
        }

        vt->last_used_ns = g->time.total_ns;
        return vt->tex;
    }

    // create entry...
    vtext_entry_t *vt = mem_calloc(&state->arena, sizeof(*vt));
    slot =
        map_insert(
            &state->str_to_entry,
            mem_strdup(&state->arena, key),
            vt);
    ASSERT_DEBUG(*slot == vt);

    vt->id = mem_strdup(&state->arena, key);
    vt->str = mem_strdup(&state->arena, str);
    vt->last_used_ns = g->time.total_ns;
    
    tex_atlas_entry_t *entry =
        tex_atlas_alloc_virtual(
            key,
            &(tex_atlas_virtual_data_t) {
                .func = vtext_render,
                .userdata = vt,
                .size = font_size(str),
                .render_always = false,
            });

    if (!entry) {
        ASSERT(false); // TODO: need to free vtext
        WARN("failed to allocate vtext \'%s\'", key);
        return (tex_id_t) { 0 };
    }

    vt->tex = entry->id;
    return vt->tex;
}

void vtext_ensure_ltext(const ltext_t *lt) {
    str_view_t view_text = str_view_from(lt->text);
    str_view_t view_unescaped = str_view_unescape(&view_text, &g->frame_arena);

    vtext_get_or_create(
        mem_strfmt(
            &g->frame_arena,
            "lt_%s",
            lt->name),
        view_unescaped.start);
}

void vtexts_update() {
    vtexts_lazy_init();

    map_each(char*, vtext_entry_t*, &state->str_to_entry, it) {
        vtext_entry_t *vt = *it.value;

        // discard if too old
        if (ns_to_secs(g->time.total_ns - vt->last_used_ns) >= 1.0f) {
            tex_atlas_entry_t *entry = tex_atlas_entry_by_id(vt->tex);
            ASSERT(entry);
            entry->virtual.discard = true;
            map_remove_it(&state->str_to_entry, it);
            continue;
        }
    }

    // ensure existence of level ltexts
    if (g->level) {
        // optimization opportunity: find a way to do this that doesn't require
        // iterating every frame and doesn't fuck up level loading
        dynlist_each(g->level->ltexts, it)  {
            vtext_ensure_ltext(it.el);
        }
    }
}
