#include "util/assert.h"

#define STBM_ASSERT ASSERT
#define STBM_MEMSET memset
#define STBM_MEMCPY memcpy

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"
