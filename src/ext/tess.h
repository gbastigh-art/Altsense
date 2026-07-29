#pragma once

#include "ext/libtess2/tesselator.h"
#include "util/alloc.h"

// create an allocator for TESStesselator which uses the specified allocator
void tess_make_allocator(TESSalloc *alloc, allocator_t *allocator);
