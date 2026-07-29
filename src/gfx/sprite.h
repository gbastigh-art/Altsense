#pragma once

#include "ext/sokol.h"
#include "util/math.h"

typedef struct sprite {
    // NOTE: put this on the frame arena
    const char *tex;

    v2 pos;
    v2 scale;
    v4 color;
    f32 z;
    int flags;

    // UV box inside of sprite, zero-init for automatic calculation
    box2f_t box;
} sprite_t;

void sprite_batch_render(
    sg_attachments target,
    const sprite_t *sprites,
    int count,
    const m4 *proj,
    const m4 *view);
