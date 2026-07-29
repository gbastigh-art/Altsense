#pragma once

#include "util/types.h"
#include "util/alloc.h"
#include "ext/stb_image.h"

// returns true on success
bool image_load_rgba(
    allocator_t *al,
    const char *path,
    u8 **pdata,
    v2i *psize,
    bool flip_y,
    const char **errmsg);

#ifdef UTIL_IMPL

#include "util/math.h"
#include "util/alloc.h"

bool image_load_rgba(
    allocator_t *al,
    const char *path,
    u8 **pdata,
    v2i *psize,
    bool flip_y,
    const char **errmsg) {
    int channels;
    stbi_set_flip_vertically_on_load(flip_y);
    u8 *data = stbi_load(path, &psize->x, &psize->y, &channels, 4);

    if (!data) {
        *errmsg =
            mem_strfmt(
                tlscratch(),
                "stbi failed (%s): %s",
                path,
                stbi_failure_reason());
        return false;
    }

    *pdata = mem_alloc(al, psize->x * psize->y * 4);
    memcpy(*pdata, data, psize->x * psize->y * 4);
    free(data);

    return true;
}
#endif // ifdef UTIL_IMPL
