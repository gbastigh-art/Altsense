#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "../lib/cimgui/imgui/backends/imgui_impl_sdlrenderer2.cpp"
#include "../lib/cimgui/imgui/backends/imgui_impl_sdl2.cpp"

extern "C" {

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
struct SDL_Window;
struct SDL_Renderer;
typedef union SDL_Event SDL_Event;
typedef struct ImDrawData ImDrawData;

bool Imgui_ImplSDL_Init(SDL_Window *window) {
    return ImGui_ImplSDL2_InitForOther(window);
}

void Imgui_ImplSDL_Shutdown() {
    return ImGui_ImplSDL2_Shutdown();
}

void Imgui_ImplSDL_NewFrame(void) {
    ImGui_ImplSDL2_NewFrame();
}

bool Imgui_ImplSDL_ProcessEvent(const SDL_Event* event) {
    return ImGui_ImplSDL2_ProcessEvent(event);
}

}
