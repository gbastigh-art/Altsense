#pragma once

#include "util/types.h"
#include "util/range.h"
#include "defs.h"

typedef struct ImGuiInputTextCallbackData ImGuiInputTextCallbackData;
typedef int (*ImGuiInputTextCallback)(ImGuiInputTextCallbackData* data);
typedef int ImGuiInputTextFlags;

// shortcut to keep element on same line
#define sameline() igSameLine(0, -1)

// fixed imgui element sizes
#define INPUT_WIDTH_INT 100
#define INPUT_WIDTH_INT_SMALL 50
#define INPUT_WIDTH_NAME 100
#define ITEM_WIDTH_NAME 130
#define INPUT_WIDTH_LPTR 150

#define INDENT_WIDTH_L0 5
#define INDENT_WIDTH_L1 20

// input text with a backing allocator
// NOTE: up to 16 KiB before breaking
bool input_text(
    const char *label,
    allocator_t *a,
    char **str,
    ImGuiInputTextFlags flags,
    ImGuiInputTextCallback cb,
    void *userdata);

// checkbox for a bitfield, use bitoffsetof
// NOTE: "ptr" is pointer to base of struct
bool input_checkbox_bit(const char *label, void *ptr, int offset);

// checkbox for a flag field
bool input_checkbox_mask(const char *label, const range_t *range, u64 mask);

// igInputInt, but you can clamp it
bool input_int_clamped(
    const char *label,
    int *v,
    int step,
    int step_fast,
    int mi,
    int ma,
    ImGuiInputTextFlags flags);

#define DECL_INT_TYPE_INPUT(T)                                           \
    bool input_##T(                                                      \
        const char *label,                                               \
        T *v,                                                            \
        T step,                                                          \
        T step_fast,                                                     \
        ImGuiInputTextFlags flags);                                      \
    bool input_##T##_clamped(                                            \
        const char *label,                                               \
        T *v,                                                            \
        T step,                                                          \
        T step_fast,                                                     \
        T mi,                                                            \
        T ma,                                                            \
        ImGuiInputTextFlags flags);                                      \

#define DEFN_INT_TYPE_INPUT(T, T_MIN, T_MAX)                             \
    bool input_##T##_clamped(                                            \
        const char *label,                                               \
        T *v,                                                            \
        T step,                                                          \
        T step_fast,                                                     \
        T mi,                                                            \
        T ma,                                                            \
        ImGuiInputTextFlags flags) {                                     \
        int u = *v;                                                      \
        if (igInputInt(label, &u, (int) step, (int) step_fast, flags)) { \
            u = clamp(u, mi, ma);                                        \
            if (((T) u) != *v) {                                         \
                *v = u;                                                  \
                return true;                                             \
            }                                                            \
        }                                                                \
        return false;                                                    \
    }                                                                    \
                                                                         \
    bool input_##T(                                                      \
        const char *label,                                               \
        T *v,                                                            \
        T step,                                                          \
        T step_fast,                                                     \
        ImGuiInputTextFlags flags) {                                     \
        return                                                           \
            input_##T##_clamped(                                         \
                label, v, step, step_fast, T_MIN, T_MAX, flags);         \
    }

DECL_INT_TYPE_INPUT(u8)
DECL_INT_TYPE_INPUT(u16)
DECL_INT_TYPE_INPUT(u32)
DECL_INT_TYPE_INPUT(i8)
DECL_INT_TYPE_INPUT(i16)
DECL_INT_TYPE_INPUT(i32)

// igInputFloat, but it's clamped
bool input_f32_clamped(
    const char *label,
    f32 *v,
    f32 step,
    f32 step_fast,
    const char *fmt,
    ImGuiInputTextFlags flags,
    f32 mi,
    f32 ma);

bool input_flags(
    int *flags, int mask, const char **names, const int *groups, int ngroups);

// button_select_lt flags
enum {
    BUTTON_SELECT_LT_NONE        = 0,
    BUTTON_SELECT_LT_ALLOW_MULTI = 1 << 0,
};

// button which changes cursor mode to SELECT and returns LPTR_NULL when a
// selection is made
lptr_t button_select_lt(
    const char *label,
    int types,
    int flags);

enum {
    INPUT_LPTR_LABEL_AFTER = 1 << 0,
    INPUT_LPTR_NO_LABEL    = 1 << 1,
};

// input which shows an LPTR as a string AND has a "SELECT" button to change it
// returns non-LPTR_NULL when selection is made
lptr_t input_lptr(
    const char *label,
    int types,
    lptr_t value,
    int flags);

// create a copy button ("CPY"), apply ("APL"), and a label ("label") which
// will select lptr_ts of the specified tag and copy/apply the specified field
// to other lptr_ts of the same type
bool input_copy_apply(
    const char *label,
    lptr_t ptr,
    void *field,
    int field_size,
    const level_type_e *types,
    const int *type_offsets,
    int n_types);

// show 20x20 image of texture (hover to zoom), returns true if clicked
bool texture_image_by_id(tex_id_t id, bool *hovered);

// like above, just with name
bool texture_image_by_name(const char *name, bool *hovered);

// searchable enum input
bool input_enum(
    const char *label,
    void *pvalue,
    const enum_desc_t *desc);

enum {
    TEXTURE_SELECT_NONE   = 0 << 0,
    TEXTURE_SELECT_PICKER = 1 << 0,
};

// texture icon which, when clicked, can be changed via a popup list
// if TEXTURE_SELECT_PICKER then textures can also be selected from the world
bool texture_select(
    const char *label,
    tex_id_t *id,
    int flags);

bool input_plane(const char *label, plane_type_e *p);

enum {
    INPUT_HSVA_NONE   = 0 << 0,
    INPUT_HSVA_PICKER = 1 << 0,
};

bool input_button_hsva_offsets(const char *label, v4 *hsva, int flags);

bool input_hsva_offsets(const char *label, v4 *hsva);

bool input_hsv_offsets(const char *label, v3 *hsv);

bool input_button_hsv_offsets(const char *label, v3 *hsv);
