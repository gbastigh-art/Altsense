#include "editor/ui_misc.h"
#include "editor/editor.h"
#include "gfx/tex_atlas.h"
#include "util/input.h"
#include "game.h"

bool input_text(
        const char *label,
        allocator_t *a,
        char **str, 
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback cb,
        void *userdata) {
    static char buf[16384];

    // copy into edit buffer
    snprintf(buf, sizeof(buf), "%s", *str);

    const bool res = igInputText(label, buf, sizeof(buf), flags, cb, userdata);

    if (res || strcmp(buf, *str)) {
        mem_free(a, *str);
        *str = mem_strdup(a, buf);
    }

    return res;
}

bool input_checkbox_bit(const char *label, void *ptr, int offset) {
    ASSERT(
        offset >= 0 && offset != INT_MAX,
        "%s has bad offset %d", label, offset);
    const u8 mask = (1 << (offset % 8));
    u8 *byte = &((u8*)ptr)[offset / 8];
    bool value = *byte & mask, init = value;

    igCheckbox(label, &value);

    if (init != value) {
        if (value) {
            *byte |= mask;
        } else {
            *byte &= ~mask;
        }

        return true;
    }

    return false;
}

bool input_checkbox_mask(const char *label, const range_t *range, u64 mask) {
    ASSERT(mask != 0);
    union {
        u64 value;
        u8 raw[sizeof(u64)];
    } u;

    memset(u.raw, 0, sizeof(u.raw));
    memcpy(u.raw, range->ptr, min(range->size, sizeof(u.raw)));

    const bool init = !!(u.value & mask);
    bool value = init;
    igCheckbox(label, &value);

    const u8 byte_mask = mask >> ((ctz(mask) / 8) * 8);
    u8 *byte = &((u8*) range->ptr)[ctz(mask) / 8];
    ASSERT(byte >= (u8*) range->ptr && byte < ((u8*) range->ptr + range->size));

    if (init != value) {
        if (value) {
            *byte |= byte_mask;
        } else {
            *byte &= ~byte_mask;
        }

        return true;
    }

    return false;
}

bool input_int_clamped(
    const char *label,
    int *v,
    int step,
    int step_fast,
    ImGuiInputTextFlags flags,
    int mi,
    int ma) {
    int u = *v;
    if (igInputInt(label, &u, step, step_fast, ImGuiInputTextFlags_None)) {
        u = clamp(u, mi, ma);
        if (u != *v) {
            *v = u;
            return true;
        }
    }

    return false;
}

DEFN_INT_TYPE_INPUT(u8,  0,       U8_MAX)
DEFN_INT_TYPE_INPUT(u16, 0,       U16_MAX)
DEFN_INT_TYPE_INPUT(u32, 0,       U32_MAX)
DEFN_INT_TYPE_INPUT(i8,  I8_MIN,  I8_MAX)
DEFN_INT_TYPE_INPUT(i16, I16_MIN, I16_MAX)
DEFN_INT_TYPE_INPUT(i32, I32_MIN, I32_MAX)

bool input_f32_clamped(
    const char *label,
    f32 *v,
    f32 step,
    f32 step_fast,
    const char *fmt,
    ImGuiInputTextFlags flags,
    f32 mi,
    f32 ma) {
    f32 u = *v;
    if (igInputFloat(label, &u, step, step_fast, fmt, flags)) {
        u = clamp(u, mi, ma);
        if (fabsf(*v - u) > GLM_FLT_EPSILON) {
            *v = u;
            return true;
        }
    }
    return false;
}

lptr_t button_select_lt(
        const char *label,
        int types,
        int flags) {
    char label_button[64];
    snprintf(label_button, sizeof(label_button), "%s##button", label);

    ImGuiID id = igGetID_Str(label_button);
    if (igButton(label_button, (ImVec2) { 0, 0 })) {
        g_editor->cur.mode = CM_SELECT;
        g_editor->cur.mode_select_id.item = id;
        g_editor->cur.mode_select_id.types = types;
        g_editor->cur.mode_select_id.selected = LPTR_NULL;
    }

    lptr_t res = LPTR_NULL;

    // are we currently selecting?
    if (g_editor->cur.mode == CM_SELECT
            && g_editor->cur.mode_select_id.item == id
            && !lptr_is_null(g_editor->cur.mode_select_id.selected)) {
        if (lptr_matches(g_editor->cur.mode_select_id.selected, types)) {
            res = g_editor->cur.mode_select_id.selected;
        }

        // if something was selected, change back to normal mode unless button
        // allows multiple selections and multi select button is down
        if (!(flags & BUTTON_SELECT_LT_ALLOW_MULTI)
            || !(g_editor->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
            g_editor->cur.mode = CM_DEFAULT;
        }
    }

    return res;
}

lptr_t input_lptr(
    const char *label,
    int types,
    lptr_t value,
    int flags) {
    igBeginGroup();
    igPushID_Str(label);

    const char *name = lptr_to_str(g_editor->level, value, &g->frame_arena);

    if (!(flags & INPUT_LPTR_NO_LABEL)
        && !(flags & INPUT_LPTR_LABEL_AFTER)) {
        igAlignTextToFramePadding();
        igText("%s (%s)", label, name);
        igSameLine(150, -1);
    }

    lptr_t new_ptr =
        button_select_lt(
            (flags & INPUT_LPTR_NO_LABEL) ? label : "SEL",
            types,
            BUTTON_SELECT_LT_NONE);

    if (!(flags & INPUT_LPTR_NO_LABEL)
        && (flags & INPUT_LPTR_LABEL_AFTER)) {
        sameline();
        igSetNextItemWidth(INPUT_WIDTH_LPTR);
        igAlignTextToFramePadding();
        igText("%s (%s)", label, name);
    }

    igPopID();
    igEndGroup();

    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        g_editor->highlight.ptr = value;
    }

    return new_ptr;
}

bool input_copy_apply(
        const char *label,
        lptr_t ptr,
        void *field,
        int field_size,
        const level_type_e *types,
        const int *type_offsets,
        int n_types) {
    igPushID_Ptr(field);
    igPushID_Str(label);

    bool change = false;

    // create union of all tags, map tag index T_*_INDEX -> index
    int all_types = 0;

    // index of each LT_* in *types
    int type_indices[LT_COUNT];

    for (int i = 0; i < n_types; i++) {
        all_types |= 1 << types[i];
        type_indices[types[i]] = i;
    }

    lptr_t copy = button_select_lt("CPY", all_types, BUTTON_SELECT_LT_NONE);
    if (!lptr_is_null(copy)) {
        void *other = lptr_raw_ptr(g_editor->level, copy);
        const int type_index = type_indices[lptr_type(copy)];
        ASSERT(type_index != -1);

        u8 old[field_size];
        memcpy(old, field, field_size);
        memcpy(field, &((u8*) other)[type_offsets[type_index]], field_size);
        change |= memcmp(old, field, field_size);
    }

    sameline();
    lptr_t apply =
        button_select_lt("APL", all_types, BUTTON_SELECT_LT_ALLOW_MULTI);
    if (!lptr_is_null(apply)) {
        editor_mark_ptr_change(apply);
        void *other = lptr_raw_ptr(g_editor->level, apply);
        const int type_index = type_indices[lptr_type(apply)];
        ASSERT(type_index != -1);
        memcpy(&((u8*) other)[type_offsets[type_index]], field, field_size);
        lptr_recalculate(g_editor->level, apply);
    }

    if (strlen(label) != 0) {
        sameline();
        igText("%s", label);
    }

    igPopID();
    igPopID();
    return change;
}

static struct {
    simgui_image_t virtual, layers[TEX_ATLAS_DEPTH];
} ui_images;
RELOAD_STATIC_GLOBAL(ui_images)

bool texture_image_by_id(tex_id_t id, bool *hovered) {
    bool res = false;

    const tex_atlas_entry_t *entry = tex_atlas_entry_by_id(id);

    const sg_image img = 
        entry->layer == TEX_ATLAS_LAYER_VIRTUAL ?
            g_tex_atlas->virtual_image
            : g_tex_atlas->layer_images[entry->layer];

    simgui_image_t *ui_img =
        entry->layer == TEX_ATLAS_LAYER_VIRTUAL ?
            &ui_images.virtual
            : &ui_images.layers[entry->layer];

    if (!ui_img->id) {
        *ui_img =
            simgui_make_image(
                &(simgui_image_desc_t) {
                    .image = img,
                    .sampler = g_renderer->smp_nearest,
                });
    }

    igImage(
        simgui_imtextureid(*ui_img),
        (ImVec2) { 20, 20 },
        (ImVec2) { entry->box_uv.min.x, entry->box_uv.max.y },
        (ImVec2) { entry->box_uv.max.x, entry->box_uv.min.y },
        (ImVec4) { 1, 1, 1, 1 },
        (ImVec4) { 1, 1, 1, 1 });

    if (igIsItemClicked(ImGuiMouseButton_Left)) {
        res = true;
    }

    if (hovered) { *hovered = igIsItemHovered(ImGuiHoveredFlags_None); }
    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        igBeginTooltip();
        igText(entry->name);

        const v2i size = v2i_scale(box2i_size(entry->box_px), 4);
        igImage(
            simgui_imtextureid(*ui_img),
            (ImVec2) { size.x, size.y },
            (ImVec2) { entry->box_uv.min.x, entry->box_uv.max.y },
            (ImVec2) { entry->box_uv.max.x, entry->box_uv.min.y },
            (ImVec4) { 1, 1, 1, 1 },
            (ImVec4) { 1, 1, 1, 1 });
        igEndTooltip();
    }

    return res;
}

bool texture_image_by_name(const char *name, bool *hovered) {
    return texture_image_by_id(tex_atlas_lookup(name), hovered);
}

// flags to control search_select_popup
enum {
    SSP_FLAG_NONE     = 0 << 0,
    SSP_FLAG_NOLABEL  = 1 << 0,
    SSP_FLAG_PACK     = 1 << 1,
    SSP_FLAG_DELETE   = 1 << 2,
};

// popup UI results
typedef enum {
    SSP_RESULT_NONE,
    SSP_RESULT_SELECTED,
    SSP_RESULT_PREVIEWED
} ssp_result_e;

// current state of the search select popup
typedef enum {
    SSP_STATE_CLOSED      = 1 << 0,
    SSP_STATE_OPEN        = 1 << 1,
    SSP_STATE_CANCELLED   = 1 << 2,
    SSP_STATE_JUST_OPENED = 1 << 3,
} ssp_state_e;

// set of ImGuiID
// NOTE: initialized on g->arena, so no leak
static map_t open_popups;

RELOAD_STATIC_GLOBAL(open_popups)

static void *search_select_popup(
    const char *label,
    usize max_name,
    void *(*next)(
        const void *el,
        const char **name,
        bool *can_delete,
        void*),
    void (*delete)(void*, void*),
    ssp_result_e (*do_ui)(void*, void*),
    void *userdata,
    int *ssp_state,
    int flags) {
    if (!map_valid(&open_popups)) {
        map_init(
            &open_popups,
            &g->arena,
            sizeof(ImGuiID),
            0,
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);
    }

    const ImGuiID id = igGetID_Str(label);

    if (!igBeginPopup(label, ImGuiWindowFlags_Popup)) {
        if (ssp_state) { *ssp_state |= SSP_STATE_CLOSED; }

        // was this previously marked as open ? in that case, must have been
        // cancelled
        if (map_contains(&open_popups, id)) {
            map_try_remove(&open_popups, id);
            if (ssp_state) { *ssp_state |= SSP_STATE_CANCELLED; }
        }

        return NULL;
    }

    if (ssp_state) {
        if (!map_containsp(&open_popups, &id)) {
            *ssp_state |= SSP_STATE_JUST_OPENED;
        }

        *ssp_state |= SSP_STATE_OPEN;
    }

    map_insertk(&open_popups, id);

    const usize search_size = max_name ? (max_name + 1) : 256;
    char search[search_size];
    memset(search, 0, search_size * sizeof(char));

    // if enter is pressed select first result
    const bool select_first =
        igInputText(
            "search", search, search_size,
            ImGuiInputTextFlags_EnterReturnsTrue,
            NULL, NULL);

    const bool do_search = search[0] != '\0';

    // width of one row
    const f32 row_width = 200.0f;

    // width of one element
    f32 el_width = 0.0f;

    const char *name;
    bool can_delete;

    int n = 0;
    void *el = next(NULL, &name, &can_delete, userdata);
    do {
        if (do_search && !strcasestr(name, search)) {
            continue;
        }

        if (select_first && n == 0) {
            map_try_remove(&open_popups, id);
            igCloseCurrentPopup();
            igEndPopup();
            return el;
        }

        if (!(flags & SSP_FLAG_NOLABEL)) {
            igAlignTextToFramePadding();
            igText("%s", name, n);
            igSameLine(100, -1);
        }

        if ((flags & SSP_FLAG_PACK)
            && n != 0
            && ((int) (((n - 1) * el_width) / row_width))
                == ((int) ((n * el_width) / row_width))) {
            sameline();
        }

        ssp_result_e result = SSP_RESULT_NONE;
        bool deleted = false;
        igPushID_Int(n);
        igBeginGroup();
        {
            result = do_ui(el, userdata);

            if (n == 0) {
                ImVec2 size;
                igGetItemRectSize(&size);
                el_width = size.x;
            }
        }
        igEndGroup();

        if (igIsItemClicked(ImGuiMouseButton_Left)) {
            result = SSP_RESULT_SELECTED;
        }

        if ((flags & SSP_FLAG_DELETE) && can_delete) {
            sameline();

            char label[64];
            snprintf(label, sizeof(label), "X##%d", (int) n);
            if (igButton(label, (ImVec2) { 0, 0 })) {
                delete(el, userdata);
                deleted = true;
            }
        }

        igPopID();

        if (!deleted && result == SSP_RESULT_SELECTED) {
            map_try_remove(&open_popups, id);
            igCloseCurrentPopup();
            igEndPopup();
            return el;
        }

        n++;
    } while ((el = next(el, &name, &can_delete, userdata)));

    igEndPopup();
    return NULL;
}

static void *input_enum__next(
    const void *el,
    const char **name,
    bool *can_delete,
    void *ex) {
    const enum_desc_t *desc = ex;
    if (desc->distinct == 0) {
        return NULL;
    } else if (!el) {
        el = &desc->names[0];
    } else {
        // use name ptr is proxy for enum
        const char **pname = (const char**) el;

        if (pname == &desc->names[desc->distinct - 1]) {
            return NULL;
        }

        const int n = (int) (pname - desc->names);
        el = &desc->names[n + 1];
    }

    *name = *(const char**) el;
    *can_delete = false;
    return (void*) el;
}

static ssp_result_e input_enum__do_ui(void *p, void*) {
    return
        igSelectable_Bool(
            *((const char **) p),
            false,
            ImGuiSelectableFlags_None,
            (ImVec2) { 0, 0 }) ?
        SSP_RESULT_SELECTED
        : SSP_RESULT_NONE;
}

bool input_enum(
        const char *label,
        void *pvalue,
        const enum_desc_t *desc) {
    igPushID_Ptr(pvalue);
    bool change = false;
    int ivalue = 0;
    memcpy(&ivalue, pvalue, desc->size);
    const int index = desc->raw_index(ivalue);

    igAlignTextToFramePadding();
    igText(
        "%s (%d)",
        index < 0 || index >= desc->distinct ? "INVALID" : desc->names[index],
        ivalue);

    const char *popup_label = mem_strfmt(tlscratch(), "%s##popup", label);

    sameline();
    if (igButton("...", (ImVec2) { 0, 0 })) {
        igOpenPopup_Str(popup_label, ImGuiPopupFlags_None);
    }

    const char **res =
        search_select_popup(
            popup_label,
            64,
            input_enum__next,
            NULL,
            input_enum__do_ui,
            (void*) desc,
            NULL,
            SSP_FLAG_NOLABEL);

    if (res) {
        const int n = (int) (res - desc->names);
        const int newval = desc->nth_value(n);
        if (memcmp(pvalue, &newval, desc->size)) {
            memcpy(pvalue, &newval, desc->size);
            change = true;
        }
    }

    sameline();
    igText("%s", label);
    igPopID();
    return change;
}

typedef struct {
    tex_atlas_entry_t *hovered;
} texture_select_popup_data_t;

static void *texture_select_popup__next(
    const void *el,
    const char **name,
    bool *can_delete,
    void *ex) {
    const int index =
        blklist_next_index(
            &g_tex_atlas->entries,
            el ? blklist_index_of(&g_tex_atlas->entries, el) : -1);

    if (index == -1) {
        return NULL;
    }

    el = blklist_ptr(tex_atlas_entry_t, &g_tex_atlas->entries, index);
    *name = ((tex_atlas_entry_t*) el)->name;
    *can_delete = false;

    return (void*) el;
}

static ssp_result_e texture_select_popup__do_ui(
    void *p,
    void *userdata) {
    bool hovered;
    const bool clicked =
        texture_image_by_id(((tex_atlas_entry_t*) p)->id, &hovered);

    if (hovered) {
        ((texture_select_popup_data_t*) userdata)->hovered = p;
    }

    return
        clicked ?
            SSP_RESULT_SELECTED
            : (hovered ? SSP_RESULT_PREVIEWED : SSP_RESULT_NONE);
}

// returns true on change (selection AND preview)
static bool texture_select_popup(
    const char *label,
    tex_id_t *id) {
    texture_select_popup_data_t data = { NULL };
    int st = 0;
    tex_atlas_entry_t *popup_result =
        search_select_popup(
            label,
            32,
            texture_select_popup__next,
            NULL,
            texture_select_popup__do_ui,
            &data,
            &st,
            SSP_FLAG_NOLABEL
            | SSP_FLAG_PACK);

    if (st & SSP_STATE_CLOSED) {
        return NULL;
    }

    const ImGuiID ig_id = igGetItemID();

    typedef struct { ImGuiID id; tex_id_t init, last; } cache_entry_t;

    // stores backups for active popups by ID
    static struct {
        uint i;
        cache_entry_t entries[16];
    } cache;

    // get or create cache entry
    cache_entry_t *cache_entry = NULL;

    for (uint i = 0; i < ARRLEN(cache.entries); i++) {
        if (ig_id == cache.entries[i].id) {
            cache_entry = &cache.entries[i];
            break;
        }
    }

    if (!cache_entry) {
        // claim next entry
        cache_entry = &cache.entries[cache.i];
        cache.i = (cache.i + 1) % ARRLEN(cache.entries);

        cache_entry->id = ig_id;
    }

    if ((st & SSP_STATE_JUST_OPENED) && id) {
        cache_entry->init = *id;
    }

    // use selected result, otherwise use hovered, otherwise use intial value
    if (popup_result) {
        *id = popup_result->id;
    } else if (data.hovered) {
        *id = data.hovered->id;
    } else {
        *id = cache_entry->init;
    }

    // return true on any change
    const bool change = id->index != cache_entry->last.index;
    cache_entry->last = *id;
    return change;
}

bool texture_select(
    const char *label,
    tex_id_t *id,
    int flags) {
    igPushID_Str(label);
    igBeginGroup();
    bool change = false;

    const tex_atlas_entry_t *entry = tex_atlas_entry_by_id(*id);

    // image, clicking opens popup
    if (texture_image_by_name(entry->name, NULL)) {
        igOpenPopup_Str(label, ImGuiPopupFlags_None);
    }

    change |= texture_select_popup(label, id);

    if (flags & TEXTURE_SELECT_PICKER) {
        sameline();
        lptr_t ptr = button_select_lt("PICK", LTF_SECTOR | LTF_SIDE, BUTTON_SELECT_LT_NONE);

        if (g_editor->mode == EDITOR_MODE_CAM && !lptr_is_null(ptr)) {
            *id = g_editor->cam.texture;
            change = true;
        }
    }

    igPopID();
    igEndGroup();
    return change;
}

bool input_flags(
    int *flags, int mask, const char **names, const int *groups, int ngroups) {
    bool change = false;

    // get max width of name
    int n = 0, max_width = 10;
    for (u32 i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) {
            continue;
        }

        ImVec2 size;
        igCalcTextSize(&size, names[n], NULL, false, -1.0f);
        max_width = max(max_width, size.x);

        n++;
    }

    const f32 base = igGetCursorPosX();

    n = 0;
    int ng = 0, g = 0;
    for (u32 i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) {
            continue;
        }

        if (groups) {
            if (ng >= groups[g]) {
                ng = 0;
                g++;
            } else if (n != 0) {
                igSameLine(0, -1);
                igSetCursorPosX(base + ng * (max_width + 30));
            }
        }

        bool value = !!(*flags & (1u << i));
        if (igCheckbox(names[n], &value)) {
            change = true;
            *flags = (*flags & ~(1u << i)) | (value ? (1u << i) : 0);
        }

        n++;
        ng++;
    }

    return change;
}

bool input_plane(const char *label, plane_type_e *p) {
    return input_enum("", p, plane_type_desc());
}

bool input_button_hsva_offsets(const char *label, v4 *hsva, int flags) {
    igPushID_Str(label);

    const v4 color =
        v4_of(
            color_offset_with_hsv(v3_of(1, 0, 1), v3_from(*hsva)),
            hsva->a);
    if (igColorButton(
            "##b", (ImVec4) { v4_spread(color) }, 0, (ImVec2) { 20, 20 })) {
        igOpenPopup_Str("edit", 0);
    }

    bool change = false;

    if (igBeginPopup("edit", 0)) {
        change |= input_hsva_offsets(label, hsva);
        igEndPopup();
    }

    if (flags & INPUT_HSVA_PICKER) {
        sameline();
        lptr_t ptr = button_select_lt("PICK", LTF_SECTOR | LTF_SIDE, BUTTON_SELECT_LT_NONE);

        if (g_editor->mode == EDITOR_MODE_CAM && !lptr_is_null(ptr)) {
            *hsva = g_editor->cam.hsva;
            change = true;
        }
    }

    igPopID();
    return change;
}

// everything ranges in [-1, 1]
bool input_hsva_offsets(const char *label, v4 *hsva) {
    igPushID_Str(label);
    igBeginGroup();
    const v4 init = *hsva;
    igPushItemWidth(120);
    f32 h = hsva->x * 180.0f, s = hsva->y * 100.0f, v = hsva->z * 100.0f;
    igSliderFloat("H", &h, -180.0f, 180.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##h", (ImVec2) { 0 })) h = 0.0f;
    igSliderFloat("S", &s, -100.0f, 100.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##s", (ImVec2) { 0 })) s = 0.0f;
    igSliderFloat("V", &v, -100.0f, 100.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##v", (ImVec2) { 0 })) v = 0.0f;
    igSliderFloat("A", &hsva->a, -1.0f, 1.0f, "%.3f", 0);
    sameline(); if (igButton("ZERO##a", (ImVec2) { 0 })) hsva->w = 0.0f;
    hsva->x = h / 180.0f;
    hsva->y = s / 100.0f;
    hsva->z = v / 100.0f;
    igPopItemWidth();
    igEndGroup();
    igPopID();
    return !v4_eqv_eps(init, *hsva);
}

bool input_hsv_offsets(const char *label, v3 *hsv) {
    igPushID_Str(label);
    igBeginGroup();
    const v3 init = *hsv;
    igPushItemWidth(120);
    f32 h = hsv->x * 180.0f, s = hsv->y * 100.0f, v = hsv->z * 100.0f;
    igSliderFloat("H", &h, -180.0f, 180.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##h", (ImVec2) { 0 })) h = 0.0f;
    igSliderFloat("S", &s, -100.0f, 100.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##s", (ImVec2) { 0 })) s = 0.0f;
    igSliderFloat("V", &v, -100.0f, 100.0f, "%.0f", 0);
    sameline(); if (igButton("ZERO##v", (ImVec2) { 0 })) v = 0.0f;
    hsv->x = h / 180.0f;
    hsv->y = s / 100.0f;
    hsv->z = v / 100.0f;
    igPopItemWidth();
    igEndGroup();
    igPopID();
    return !v3_eqv_eps(init, *hsv);
}

bool input_button_hsv_offsets(const char *label, v3 *hsv) {
    igPushID_Str(label);

    const v4 color =
        v4_of(
            color_offset_with_hsv(v3_of(1, 0, 1), *hsv),
            1.0f);
    if (igColorButton(
            "##b", (ImVec4) { v4_spread(color) }, 0, (ImVec2) { 20, 20 })) {
        igOpenPopup_Str("edit", 0);
    }

    bool change = false;

    if (igBeginPopup("edit", 0)) {
        change |= input_hsv_offsets(label, hsv);
        igEndPopup();
    }

    igPopID();
    return change;
}
