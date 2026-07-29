#pragma once

#include "util/types.h"
#include "util/math/linalg.h"
#include "ext/sokol.h"

#define PALETTE_MAX_SIZE 256

typedef struct palette {
    // 3D image map of RGB -> palette
    sg_image map_image;
    FIXLIST(u32, PALETTE_MAX_SIZE) colors_abgr;
    FIXLIST(v3, PALETTE_MAX_SIZE) colors_rgb;
} palette_t;

// initialize palette
void palette_init(palette_t *p);

// load palette from GIMP-format .gpl file
void palette_load_gpl(palette_t *p, const char *path);

// load palette from color list
void palette_load(palette_t *p, v3 *colors, int n);

// deinit palette, free resources
void palette_destroy(palette_t *p);

// find nearest palette color for abgr color
u8 palette_nearest(const palette_t *p, u32 color);
