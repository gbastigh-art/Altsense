#pragma once

#define FRAME_LOCAL static

#define _FRAME_LOCAL_BLOCK_IMPL(var0) \
        static int var0;              \
        if (var0 != g->time.frame.count && (var0 = g->time.frame.count, true))

#define FRAME_LOCAL_BLOCK _FRAME_LOCAL_BLOCK_IMPL(CONCAT(flframe_, __COUNTER__))
