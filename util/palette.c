#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOADHOST_CLIENT_DISABLED

#include "../src/util/assert.h"
#include "../src/util/types.h"
#include "../src/util/image.h"

#define STB_MALLOC_IMPLEMENTATION
#include "../src/ext/stb_malloc.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../src/ext/stb_image.h"

#include "../src/ext/printf.c"
#include "../src/ext/putchar.c"

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(
            stderr,
            "usage: palette <path to image> <.gpl output> [-c/--copy]\n");
        return 1;
    }

    const char *errmsg;
    u32 *data;
    ivec2s size;
    ASSERT(
        image_load_rgba(
            g_mallocator,
            argv[1],
            (u8**) &data,
            &size,
            true,
            &errmsg),
        "failed to load image: %s", errmsg);
    const int n = size.x * size.y;

    FILE *f = fopen(argv[2], "w");
    ASSERT(f, "failed to open output file");
    fprintf(
        f,
        "GIMP Palette\n"
        "#Palette Name: MIND\n"
        "#Description:\n"
        "#Colors:%d\n",
        n);
    for (int i = 0; i < n; i++) {
        fprintf(
            f,
            "%d %d %d %06x\n",
            (data[i] >>  0) & 0xFF,
            (data[i] >>  8) & 0xFF,
            (data[i] >> 16) & 0xFF,
              (((data[i] >>  0) & 0xFF) << 16)
            | (((data[i] >>  8) & 0xFF) <<  8)
            | (((data[i] >> 16) & 0xFF) <<  0));
    }
    fclose(f);

    if (argc == 4
        && (!strcmp(argv[3], "--copy") || !strcmp(argv[3], "-c"))) {
        // /Users/jdh/Library/Application Support/GIMP/2.10/palettes
        char command[1024];
        snprintf(
            command,
            sizeof(command),
            "cp %s ~/Library/Application\\ Support/GIMP/2.10/palettes/",
            argv[2]);

        ASSERT(!system(command), "failed to copy");
    }
    return 0;
}
