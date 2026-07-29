#include "config.h"

#ifdef SOKOL_METAL

#include "ext/cimgui.h"
#include "util/assert.h"
#include "util/mem.h"
#include "platform.h"

#define SOKOL_IMPL
#define SOKOL_TRACE_HOOKS
#define SOKOL_ASSERT ASSERT
#define SOKOL_IMGUI_NO_SOKOL_APP

#include "ext/gl.h" /* IWYU pragma: keep */
#include "../lib/sokol/sokol_gfx.h"
#include "../lib/sokol_gp/sokol_gp.h"
#include "../lib/sokol/util/sokol_shape.h"
#include "../lib/sokol/util/sokol_gl.h"
#include "../lib/sokol/util/sokol_imgui.h"
#include "../lib/sokol/util/sokol_gfx_imgui.h"

#include "reloadhost.h"
RELOAD_STATIC_GLOBAL(_sg)
RELOAD_STATIC_GLOBAL(_sgl)
RELOAD_STATIC_GLOBAL(_sgp)
RELOAD_STATIC_GLOBAL(_simgui)

#include <TargetConditionals.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "../lib/SDL/include/SDL.h"
#include "../lib/SDL/include/SDL_metal.h"

#include "platform.h"

#define DO_NOT_INCLUDE_SOKOL_HEADERS
#include "ext/sokol.h"

static struct {
	int sample_count;
	SDL_Window* window;
	id<MTLDevice> device;
	SDL_MetalView view;
	CAMetalLayer* layer;
	id<CAMetalDrawable> drawable;
	id<MTLTexture> depth_texture;
} state;
RELOAD_STATIC_GLOBAL(state)

void platform_init(SDL_Window *window) {
    state.sample_count = 1;
    state.window = window;
    state.device = MTLCreateSystemDefaultDevice();
    state.view = SDL_Metal_CreateView(window);
    state.layer = SDL_Metal_GetLayer(state.view);
    state.layer.device = state.device;
    state.layer.magnificationFilter = kCAFilterNearest;
    state.layer.framebufferOnly = true;
}

void platform_destroy(void) {
    // nothing to do, metal automatically leans up
}

void platform_reload(void) {
    // no-op
}

sg_environment platform_environment(void) {
    return (sg_environment) {
        .defaults = {
            .color_format = SG_PIXELFORMAT_BGRA8,
            .depth_format = SG_PIXELFORMAT_NONE,
            .sample_count = state.sample_count,
        },
        .metal.device = (__bridge const void*) state.device,
    };
}

sg_swapchain platform_swapchain(void) {
    state.drawable = [state.layer nextDrawable];
    return (sg_swapchain) {
        .sample_count = state.sample_count,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_NONE,
        .width = state.layer.drawableSize.width,
        .height = state.layer.drawableSize.height,
        .metal.current_drawable = (__bridge const void*) state.drawable,
        .metal.depth_stencil_texture = NULL,
        .metal.msaa_color_texture = NULL,
    };
}

void platform_start_frame(void) {
    // NOTE: *not* getting drawable here - get it with the swapchain so that
    // we hold it for as little time as possible!
}

void platform_end_frame(void) {
    // nothing to do, sokol presents the drawable itself :) 
    state.drawable = nil;
}

void platform_size(int *w, int *h) {
    *w = state.layer.drawableSize.width;
    *h = state.layer.drawableSize.height;
}

void platform_set_vsync(bool vsync) {
    state.layer.displaySyncEnabled = vsync;
}

void platform_set_relative_mouse_mode(bool relative) {
    static bool old_relative;
    RELOAD_STATIC_VAR(old_relative);

    if (old_relative) {
        // hide cursor as SDL expects
        //[NSCursor hide];
    }

    SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);

    if (relative) {
        // unhide cursor SDL has hidden
        //[NSCursor unhide];
    }

    old_relative = relative;
}

void sg_ext_query_image_pixels(
    sg_image _img,
    void (*callback)(sg_image, const void*, int, void*),
    void *userdata) {
    _sg_image_t *img = _sg_lookup_image(&_sg.pools, _img.id);

    id<MTLTexture> mtl_src_texture = _sg.mtl.idpool.pool[img->mtl.tex[0]];
    const int w = mtl_src_texture.width, h = mtl_src_texture.height;

    const int bytes_per_px = _sg_pixelformat_bytesize(img->cmn.pixel_format);
    __block id<MTLBuffer> dst_buffer = [mtl_src_texture.device newBufferWithLength:(w * h * bytes_per_px) options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cmd_buffer = [_sg.mtl.cmd_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit_encoder = [cmd_buffer blitCommandEncoder];
    [blit_encoder copyFromTexture:mtl_src_texture
        sourceSlice:0
        sourceLevel:0
        sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(w,h,1)
        toBuffer:dst_buffer
        destinationOffset:0
        destinationBytesPerRow:(w * bytes_per_px)
        destinationBytesPerImage:(w * h * bytes_per_px)
     ];
     [blit_encoder endEncoding];

     [cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buf) {
         callback((sg_image) { img->slot.id }, dst_buffer.contents, w * h * bytes_per_px, userdata);
        _SG_OBJC_RELEASE(dst_buffer);
     }];
     [cmd_buffer commit];
}

void sg_ext_update_buffer_partial(sg_buffer buf_, int offset, const sg_range* data) {
    _sg_buffer_t *buf = _sg_lookup_buffer(&_sg.pools, buf_.id);
    SOKOL_ASSERT(buf && data && data->ptr && (data->size > 0));
    SOKOL_ASSERT(offset + (int) data->size <= buf->cmn.size);

    // DO NOT! update the active slot - we *must* use the same buffer in order
    // for partial updates to work at all

    __unsafe_unretained id<MTLBuffer> mtl_buf = _sg_mtl_id(buf->mtl.buf[buf->cmn.active_slot]);

    void* dst_ptr = [mtl_buf contents];
    memcpy(&((u8*) dst_ptr)[offset], data->ptr, data->size);

    // MacOS only
    if (_sg_mtl_resource_options_storage_mode_managed_or_shared() == MTLResourceStorageModeManaged) {
        [mtl_buf didModifyRange:NSMakeRange(offset, data->size)];
    }
}

void sg_ext_set_wireframe(bool wireframe) {
    if (_sg.mtl.cmd_encoder != nil) {
        const MTLTriangleFillMode mode =
            wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
        [_sg.mtl.cmd_encoder setTriangleFillMode:mode];
    }
}

void sg_ext_draw_multi(const sg_range *commands) {
    SOKOL_ASSERT(commands->size != 0);
    SOKOL_ASSERT(commands->size % sizeof(sg_ext_indirect_command) == 0);
    const int n_commands = commands->size / sizeof(sg_ext_indirect_command);
    SOKOL_ASSERT(n_commands <= SG_EXT_DRAW_MULTI_MAX);

    const int args_size = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);

    // LEAK: this buffer leaks
    // upload arguments...
    static id<MTLBuffer> buffer = nil;
    RELOAD_STATIC_VAR(buffer);

    if (buffer == nil) {
        buffer =
            [_sg.mtl.device
                newBufferWithLength:((NSUInteger) SG_EXT_DRAW_MULTI_MAX * args_size)
                options:
                    MTLResourceCPUCacheModeWriteCombined
                    | _sg_mtl_resource_options_storage_mode_managed_or_shared()
            ];
    }

    // current frame + offset into argument buffer for this frame
    static u32 frame_index, frame_offset;
    RELOAD_STATIC_VAR(frame_index);
    RELOAD_STATIC_VAR(frame_offset);

    if (frame_index != _sg.frame_index) {
        frame_index = _sg.frame_index;
        frame_offset = 0;
    }

    void* dst_ptr = [buffer contents];

    for (int i = 0; i < n_commands; i++) {
        sg_ext_indirect_command *cmd =
            &((sg_ext_indirect_command*) commands->ptr)[i];

        MTLDrawIndexedPrimitivesIndirectArguments *args =
            &((MTLDrawIndexedPrimitivesIndirectArguments*) dst_ptr)[frame_offset + i];

        args->indexCount = cmd->num_elements;
        args->instanceCount = cmd->num_instances;
        args->indexStart = cmd->base_element;
        args->baseVertex = cmd->base_vertex;
        args->baseInstance = cmd->base_instance;
    }

    // notify of update
    if (_sg_mtl_resource_options_storage_mode_managed_or_shared() == MTLResourceStorageModeManaged) {
        [buffer didModifyRange:NSMakeRange(
            frame_offset * args_size,
            n_commands * args_size)];
    }

    // bump
    const int cur_frame_offset = frame_offset;
    frame_offset += n_commands;

    // must have command encoder
    SOKOL_ASSERT(nil != _sg.mtl.cmd_encoder);

    // must have pipeline
    SOKOL_ASSERT(
        _sg.mtl.state_cache.cur_pipeline
        && (_sg.mtl.state_cache.cur_pipeline->slot.id == _sg.mtl.state_cache.cur_pipeline_id.id));

    // must be indexed rendering
    SOKOL_ASSERT(SG_INDEXTYPE_NONE != _sg.mtl.state_cache.cur_pipeline->cmn.index_type);

    // must have index buffer
    SOKOL_ASSERT(
        _sg.mtl.state_cache.cur_indexbuffer
        && (_sg.mtl.state_cache.cur_indexbuffer->slot.id == _sg.mtl.state_cache.cur_indexbuffer_id.id));

    const NSUInteger index_buffer_offset = (NSUInteger) (_sg.mtl.state_cache.cur_indexbuffer_offset);

    const _sg_buffer_t* ib = _sg.mtl.state_cache.cur_indexbuffer;
    SOKOL_ASSERT(ib->mtl.buf[ib->cmn.active_slot] != _SG_MTL_INVALID_SLOT_INDEX);

    for (int i = 0; i < n_commands; i++) {
        [_sg.mtl.cmd_encoder
            drawIndexedPrimitives:_sg.mtl.state_cache.cur_pipeline->mtl.prim_type
            indexType:_sg.mtl.state_cache.cur_pipeline->mtl.index_type
            indexBuffer:_sg_mtl_id(ib->mtl.buf[ib->cmn.active_slot])
            indexBufferOffset:index_buffer_offset
            indirectBuffer:buffer
            indirectBufferOffset:((cur_frame_offset + i) * args_size)
        ];
    }
}

#endif // ifdef SOKOL_METAL
