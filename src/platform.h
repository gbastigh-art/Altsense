#pragma once

typedef struct SDL_Window SDL_Window;
typedef struct sg_environment sg_environment;
typedef struct sg_swapchain sg_swapchain;
typedef struct sg_image sg_image;

void platform_init(SDL_Window *window);
void platform_destroy(void);
void platform_reload(void);
sg_environment platform_environment(void);
sg_swapchain platform_swapchain(void);
void platform_start_frame(void);
void platform_end_frame(void);
void platform_size(int *w, int *h);
void platform_set_vsync(bool vsync);
void platform_set_relative_mouse_mode(bool relative);
