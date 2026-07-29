#include "config.h"

#ifdef CLANGD
    #undef SOKOL_METAL
    #define SOKOL_GLCORE
#endif // ifdef CLANGD

#ifdef SOKOL_GLCORE

#include "ext/cimgui.h" /* IWYU pragma: keep */

#include "util/assert.h"

#define SOKOL_IMPL
#define SOKOL_TRACE_HOOKS
#define SOKOL_ASSERT ASSERT
#define SOKOL_EXTERNAL_GL_LOADER
#define SOKOL_GLCORE
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

#include "../lib/SDL/include/SDL.h" /* IWYU pragma: keep */
#include "ext/glad.h"
#include "ext/gl.h"
#include "platform.h"
#include "util/assert.h"
#include "reloadhost.h"

#define DO_NOT_INCLUDE_SOKOL_HEADERS
#include "ext/sokol.h"

static struct {
    SDL_Window *window;
    void *gl_context;
} state;
RELOAD_STATIC_GLOBAL(state)

void platform_init(SDL_Window *window) {
    state.window = window;

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    state.gl_context = SDL_GL_CreateContext(window);
    ASSERT(
        state.gl_context,
        "could not get GL 4.3 context from SDL: %s",
        SDL_GetError());
    ASSERT(
        !SDL_GL_MakeCurrent(window, state.gl_context),
        "could not make GL 4.3 context current: %s",
        SDL_GetError());

    // load opengl functions
    ASSERT(gladLoadGL((GLADloadfunc) SDL_GL_GetProcAddress));

    int gl_err;
    ASSERT(!(gl_err = glGetError()), "got gl error %d", gl_err);
    LOG("GL Version={%s}", glGetString(GL_VERSION));
    LOG("GLSL Version={%s}", glGetString(GL_SHADING_LANGUAGE_VERSION));
}

void platform_destroy() {
    SDL_GL_DeleteContext(state.gl_context);
}

void platform_reload(void) {
    ASSERT(gladLoadGL((GLADloadfunc) SDL_GL_GetProcAddress));
}

sg_environment platform_environment(void) {
    return (sg_environment) {
        .defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_NONE,
            .sample_count = 1,
        },
    };
}

sg_swapchain platform_swapchain(void) {
    int w, h;
    platform_size(&w, &h);
    return (sg_swapchain) {
        .sample_count = 1,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_NONE,
        .width = w,
        .height = h,
        .gl.framebuffer = 0,
    };
}

void platform_start_frame(void) {

}

void platform_end_frame(void) {
    SDL_GL_SwapWindow(state.window);
}

void platform_size(int *w, int *h) {
    SDL_GL_GetDrawableSize(state.window, w, h);
}

void platform_set_vsync(bool vsync) {
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
}

void platform_set_relative_mouse_mode(bool relative) {
    SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
}

void sg_ext_query_image_pixels(
    sg_image _img,
    void (*callback)(sg_image, const void*, int, void*),
    void *userdata) {
    _sg_image_t *img = _sg_lookup_image(&_sg.pools, _img.id);

    SOKOL_ASSERT(img->gl.target == GL_TEXTURE_2D);
    SOKOL_ASSERT(0 != img->gl.tex[img->cmn.active_slot]);

    _sg_gl_cache_store_texture_sampler_binding(0);
    _sg_gl_cache_bind_texture_sampler(
        0,
        img->gl.target,
        img->gl.tex[img->cmn.active_slot], 0);

    const GLenum format = _sg_gl_teximage_format(img->cmn.pixel_format);
    const GLenum type = _sg_gl_teximage_type(img->cmn.pixel_format);

    const int size =
        img->cmn.width
            * img->cmn.height
            * _sg_pixelformat_bytesize(img->cmn.pixel_format);
    void *buf = _sg_malloc(size);
    glGetTexImage(img->gl.target, 0, format, type, buf);
    _SG_GL_CHECK_ERROR();
    _sg_gl_cache_restore_texture_sampler_binding(0);
    callback(_img, buf, size, userdata);
    _sg_free(buf);
}

void sg_ext_update_buffer_partial(sg_buffer buf_, int offset, const sg_range* data) {
    _sg_buffer_t *buf = _sg_lookup_buffer(&_sg.pools, buf_.id);

    SOKOL_ASSERT(buf && data && data->ptr && (data->size > 0));
    SOKOL_ASSERT(offset + (int) data->size <= buf->cmn.size); 

    // DO NOT! update the active slot - we *must* use the same buffer in order
    // for partial updates to work at all
    SOKOL_ASSERT(buf->cmn.active_slot < SG_NUM_INFLIGHT_FRAMES);

    GLenum gl_tgt = _sg_gl_buffer_target(buf->cmn.type);
    GLuint gl_buf = buf->gl.buf[buf->cmn.active_slot];
    SOKOL_ASSERT(gl_buf);
    _SG_GL_CHECK_ERROR();
    _sg_gl_cache_store_buffer_binding(gl_tgt);
    _sg_gl_cache_bind_buffer(gl_tgt, gl_buf);
    glBufferSubData(
        gl_tgt,
        (GLintptr) offset,
        (GLsizeiptr) data->size,
        data->ptr);
    _sg_gl_cache_restore_buffer_binding(gl_tgt);
    _SG_GL_CHECK_ERROR();
}

void sg_ext_set_wireframe(bool wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_FILL : GL_LINE);
}

#endif // ifdef SOKOL_GLCORE
