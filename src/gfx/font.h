#pragma once

#include "util/math.h"
#include "defs.h"

enum {
    FONT_DEFAULT  = 0,
    FONT_DOUBLED  = 1 << 0,
    FONT_ITALIC   = 1 << 1,
};

#define FONT_GLYPH_SIZE v2i_of(9, 16)

void font_char(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    char c,
    DYNLIST(sprite_t) *out);

void font_str(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    const char *str,
    DYNLIST(sprite_t) *out);

void font_v(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    DYNLIST(sprite_t) *out,
    const char *fmt,
    ...);

int font_width(const char *str);

int font_height(const char *str);

int font_len(const char *str);

v2i font_size(const char *str);
