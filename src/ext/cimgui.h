#pragma once

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../../lib/cimgui/cimgui.h"                       /* IWYU pragma: export */

#ifndef CIMGUI_API
#define CIMGUI_API __attribute__((__visibility__("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
struct SDL_Window;
struct SDL_Renderer;
typedef union SDL_Event SDL_Event;
typedef struct ImDrawData ImDrawData;

bool Imgui_ImplSDL_Init(SDL_Window *window);
void Imgui_ImplSDL_Shutdown();
void Imgui_ImplSDL_NewFrame(void);
bool Imgui_ImplSDL_ProcessEvent(const SDL_Event* event);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
