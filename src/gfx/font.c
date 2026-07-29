#include "gfx/font.h"
#include "game.h"
#include "gfx/palette.h"
#include "gfx/sprite.h"

#define IMAGE_SIZE v2i_of(144, 256)

void font_char(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    char c,
    DYNLIST(sprite_t) *out) {
    if (flags & FONT_DOUBLED) {
        const f32 diff = -0.4f;
        font_char(
            v2_of(pos.x + 1, pos.y - 1),
            z + 0.001f,
            v4_of(
                v3_clamp(v3_add(v3_from(col), v3_of(diff)), 0.0f, 1.0f),
                col.a),
            flags & ~FONT_DOUBLED,
            gfx_flags,
            c,
            out);
    }

    const v2i n = v2i_div(IMAGE_SIZE, FONT_GLYPH_SIZE);

    *dynlist_push(*out) = (sprite_t) {
        .tex = (flags & FONT_ITALIC) ? "x_font_i" : "x_font",
        .pos = pos,
        .color = col,
        .scale = v2_of(1),
        .box =
            box2f_ps(
                v2_of(
                    (c % n.x) * (FONT_GLYPH_SIZE.x / (f32) IMAGE_SIZE.x),
                    (n.y - (int) (c / n.x) - 1)
                        * (FONT_GLYPH_SIZE.y / (f32) IMAGE_SIZE.y)),
                v2_div(
                    v2_from_i(FONT_GLYPH_SIZE),
                    v2_from_i(IMAGE_SIZE))),
        .z = z,
        .flags = gfx_flags,
    };
}


static const char *nextch(
    const char *str,
    char *ch,
    v4 *col,
    int *flags) {
    // interpret '$xx' color control codes
    while (*str == '$'
        && *(str + 1)) {
        if (*(str + 1) == '$') {
            // skip first '$', return '$'
            str++;
            break;
        } else {
            if (!*(str + 2)) {
                WARN("bad color control %s", str);
                return NULL;
            }

            char cs[3] = { *(str + 1), *(str + 2), '\0' };

            if (!strcmp(cs, "IT")) {
                *flags |= FONT_ITALIC;
            } else if (!strcmp(cs, "RE")) {
                *flags &= ~FONT_ITALIC;
            } else {
                for (int i = 0; i < 2; i++) {
                    if (!(cs[i] >= '0' && cs[i] <= '9')
                        && !(cs[i] >= 'A' && cs[i] <= 'F')) {
                        WARN("bad control color %s", str);
                        return NULL;
                    }
                }

#define FROM_HEX(_c) ((_c) >= 'A' ? (((_c) - 'A') + 10) : ((_c) - '0'))
                *col =
                    v4_of(
                        g->palettes.generic->colors_rgb.arr[
                            FROM_HEX(cs[0]) * 16 + FROM_HEX(cs[1])],
                        col->a);
#undef FROM_HEX
            }

            // skip $, x, x
            str += 3;
        }
    }

    *ch = *str;
    return str + 1;
}

void font_str(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    const char *str,
    DYNLIST(sprite_t) *out) {
    int i = 0, j = 0;
    char c;
    while ((str = nextch(str, &c, &col, &flags)) && c) {
        if (c == '\n') {
            i = 0;
            j++;
        } else {
            font_char(
                v2_of(
                    pos.x + (i * FONT_GLYPH_SIZE.x),
                    pos.y - (j * FONT_GLYPH_SIZE.y)),
                z,
                col,
                flags,
                gfx_flags,
                c,
                out);
            i++;
        }
    }
}

void font_v(
    v2 pos,
    f32 z,
    v4 col,
    int flags,
    int gfx_flags,
    DYNLIST(sprite_t) *out,
    const char *fmt,
    ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    font_str(pos, z, col, flags, gfx_flags, buf, out);
}

int font_width(const char *str) {
    int i = 0, w = 0, flags = 0;
    char c;
    v4 col;
    while ((str = nextch(str, &c, &col, &flags)) && c) {
        if (c == '\n') {
            w = max(w, i);
            i = 0;
        } else {
            i++;
        }
    }
    return max(w, i) * FONT_GLYPH_SIZE.x;
}

int font_height(const char *str) {
    int h = FONT_GLYPH_SIZE.y, flags = 0;
    char c;
    v4 col;
    while ((str = nextch(str, &c, &col, &flags)) && c) {
        if (c == '\n') {
            h += FONT_GLYPH_SIZE.y;
        }
    }
    return h;
}

int font_len(const char *str) {
    int i = 0, flags = 0;
    char c;
    v4 col;
    while ((str = nextch(str, &c, &col, &flags)) && c) {
        i++;
    }
    return i;
}

v2i font_size(const char *str) {
    return v2i_of(font_width(str), font_height(str));
}
