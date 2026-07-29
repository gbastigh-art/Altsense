#include "gfx/palette.h"
#include "util/assert.h"
#include "util/file.h"
#include "util/fixlist.h"
#include "util/str.h"

#define PALETTE_IMAGE_SIZE 32

M_INLINE f32 abgr_dist(u32 x, u32 y) {
    // approximate l/u/v distance
    // see https://www.compuphase.com/cmetric.htm
    const int
        ravg = (((x >> 0) & 0xFF) + ((y >> 0) & 0xFF)) / 2,
        b = ((int) ((x >> 16) & 0xFF)) - ((int) ((y >> 16) & 0xFF)),
        g = ((int) ((x >>  8) & 0xFF)) - ((int) ((y >>  8) & 0xFF)),
        r = ((int) ((x >>  0) & 0xFF)) - ((int) ((y >>  0) & 0xFF));

    return sqrtf(
        (((512 + ravg) * r * r) >> 8)
            + (4 * g * g)
            + (((767 - ravg) * b * b) >> 8));
}

void palette_init(palette_t *p) {
    *p = (palette_t) { 0 };

    p->map_image =
        sg_make_image(
            &(sg_image_desc) {
                .label = "palette.img",
                .type = SG_IMAGETYPE_3D,
                .width = PALETTE_IMAGE_SIZE,
                .height = PALETTE_IMAGE_SIZE,
                .num_slices = PALETTE_IMAGE_SIZE,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .usage = SG_USAGE_DYNAMIC
            });
}

static void update_images(palette_t *p) {
    // calculate image
    const usize n_bytes =
        PALETTE_IMAGE_SIZE
            * PALETTE_IMAGE_SIZE
            * PALETTE_IMAGE_SIZE
            * sizeof(u32);

    u32 *pixels = mem_alloc(tlscratch(), n_bytes);

    const f32 step = 255.0f / (PALETTE_IMAGE_SIZE - 1);
    for (int b = 0; b < PALETTE_IMAGE_SIZE; b++) {
        for (int g = 0; g < PALETTE_IMAGE_SIZE; g++) {
            for (int r = 0; r < PALETTE_IMAGE_SIZE; r++) {
                const u32 abgr =
                    0xFF000000
                    | ((((u32) (b * step)) & 0xFF) << 16)
                    | ((((u32) (g * step)) & 0xFF) <<  8)
                    | ((((u32) (r * step)) & 0xFF) <<  0);
                const u8 col = palette_nearest(p, abgr);
                const u32 abgr_p = p->colors_abgr.arr[col];
                pixels[
                    (b * PALETTE_IMAGE_SIZE * PALETTE_IMAGE_SIZE)
                        + (g * PALETTE_IMAGE_SIZE)
                        + r] = abgr_p;
            }
        }
    }

    sg_update_image(
        p->map_image,
        &(sg_image_data) {
            .subimage[0][0] = { .ptr = pixels, .size = n_bytes }
        });
}

void palette_load_gpl(palette_t *p, const char *path) {
    p->colors_abgr.n = 0;
    p->colors_rgb.n = 0;

    strbuf_t buf = strbuf_create(tlscratch());
    const file_error_e err = file_read_strbuf(&buf, path);
    ASSERT(err == FILE_OK, "failed to read palette file %s", path);

    const char *next = &buf[0];
    char line[1024];
    while (*next && (next = str_line(next, line, sizeof(line)))) {
        const char *s = line;

        // skip all whitespace, continue on empty or comment lines
        s = srt_trim(line);

        // ignore opening line
        if (!strcmp(line, "GIMP Palette")) {
            continue;
        }

        if (!*s || *s == '#') {
            continue;
        }

        int ignore[3];
        u32 rgb = 0;
        ASSERT(
            sscanf(
                s,
                "%d %d %d %06x",
                &ignore[0], &ignore[1], &ignore[2], &rgb) == 4,
            "malformed palette file line %s", line);

        *fixlist_push(p->colors_abgr) =
            0xFF000000
            | ((rgb & 0xFF) << 16)
            | (rgb & 0x00FF00)
            | ((rgb >> 16) & 0xFF);

        *fixlist_push(p->colors_rgb) =
            v3_of(
                ((rgb >> 16) & 0xFF) / 255.0f,
                ((rgb >>  8) & 0xFF) / 255.0f,
                ((rgb >>  0) & 0xFF) / 255.0f);
    }

    update_images(p);
}

void palette_load(palette_t *p, v3 *colors, int n) {
    p->colors_abgr.n = 0;
    p->colors_rgb.n = 0;

    for (int i = 0; i < n; i++) {
        *fixlist_push(p->colors_abgr) =
            0xFF000000
            | ((((int) (colors[i].b * 255.0f) & 0xFF)) << 16)
            | ((((int) (colors[i].g * 255.0f) & 0xFF)) <<  8)
            | ((((int) (colors[i].r * 255.0f) & 0xFF)) <<  0);
        *fixlist_push(p->colors_rgb) = colors[i];
    }
    
    update_images(p);
}

void palette_destroy(palette_t *p) {
    sg_destroy_image(p->map_image);
    *p = (palette_t) { 0 };
}

u8 palette_nearest(const palette_t *p, u32 color) {
    f32 dist = 1e100;
    u8 nearest = p->colors_abgr.n - 1;

    for (int i = 0; i < p->colors_abgr.n; i++) {
        if ((p->colors_abgr.arr[i] & 0xFFFFFF) == (color & 0xFFFFFF)) {
            return i;
        }

        const f32 d = abgr_dist(color, p->colors_abgr.arr[i]);
        if (d < dist) {
            dist = d;
            nearest = i;
        }
    }

    return nearest;
}
