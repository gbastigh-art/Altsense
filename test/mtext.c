#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/bitmap.h"
#include "util/assert.h"
#include "util/log.h"
#include "util/map.h"
#include "util/time.h"
#include "util/file.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

#include "ext/printf.c"
#include "ext/putchar.c"

#include "../src/mtext.h"
#include "../src/mtext.c"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        goto usage_and_exit;
    }

    strbuf_t buf = strbuf_create(tlscratch());
    const file_error_e err = file_read_strbuf(&buf, argv[1]);
    if (err != FILE_OK) {
        ERROR("file error %d", err);
        return 1;
    }

    int n_tokens;
    if (sscanf(argv[2], "%d", &n_tokens) != 1 || n_tokens < 2) {
        goto usage_and_exit;
    }

    int count;
    if (sscanf(argv[3], "%d", &count) != 1 || count < 0) {
        goto usage_and_exit;
    }

    mtextgen_t mt;
    mtextgen_init(&mt, tlscratch(), buf, 2);

    const char *errmsg;
    rand_t r = rand_create(time_epoch_ns());

    printf("%d tokens x %d times...\n", n_tokens, count);
    for (int i = 0; i < count; i++) {
        strbuf_t res = strbuf_create(tlscratch());
        if (!mtextgen_gen(&mt, &res, n_tokens, &r, &errmsg)) {
            ERROR("errmsg is: %s", errmsg);
        }
        printf("    %s\n", res);
    }
    return 0;

usage_and_exit:
    ERROR("usage: mtext <corpus> <# tokens (min. 2)> <# times>");
    return 1;
}

