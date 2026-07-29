#ifndef CONFIG_H
#define CONFIG_H

#ifdef TARGET_PLATFORM_macos
    #define SOKOL_METAL
#else
    #define SOKOL_GLCORE
#endif // ifdef TARGET_PLATFORM_macos

#ifdef TARGET_PLATFORM_windows
    #define PATH_SEP "\\"
    #define PATH_SEP_CHAR '\\'
#else
    #define PATH_SEP "/"
    #define PATH_SEP_CHAR '/'
#endif // ifdef TARGET_PLATFORM_windows

// target (pixelated) resolution
#define TARGET_WIDTH 448
#define TARGET_HEIGHT 252
// #define TARGET_WIDTH 512
// #define TARGET_HEIGHT 288
// #define TARGET_WIDTH 384
// #define TARGET_HEIGHT 216

#define PALETTE_GENERIC "assets/palette.gpl"

// tex_atlas.h
// maximum number of textures
#define TEX_ATLAS_MAX_ENTRIES 4096
#define TEX_ATLAS_SIZE 4096
#define TEX_ATLAS_UNIT (1.0 / TEX_ATLAS_SIZE)
#define TEX_ATLAS_DEPTH 1

// update rates
#define TICKS_PER_SECOND         60
#define FIXED_UPDATES_PER_SECOND 240

// animation ticks
#define ANIM_TICKS_PER_SECOND    10
#define TICKS_PER_ANIM_TICK      6

// texture scroll control
#define TEXTURE_SCROLL_TICK_DIVISOR 3
#define TEXTURE_SCROLL_PX_PER_TICK (0.5)

// imprecision of 2^-8 (~0.003) with coordinates this large
#define MAX_COORD 32768.0f

#define LEVEL_PALETTE_SIZE 8

// see level/block.{c,h}
// controls size of blocks in level units
#define BLOCK_SIZE 4

// pixels per world unit
#define PX_PER_UNIT 32

// near/far plane config, must be reasonably small to work nicely with portals
#ifdef SOKOL_METAL
    #define NEAR_PLANE 0.0005f
#else
    // TODO(opengl): test
    #define NEAR_PLANE 0.0025f
#endif // ifdef SOKOL_METAL

#define FAR_PLANE 96.0f

#ifdef SOKOL_METAL
    #define FORCE_PORTAL_DISTANCE 0.025f
#else
    // TODO(opengl): test
    #define FORCE_PORTAL_DISTANCE 0.005f
#endif // ifdef SOKOL_METAL

// TODO(refactor) player file
#define PLAYER_SWING_PERIOD 260

#define STEP_HEIGHT_DEFAULT 0.751f

#define PLAYER_REACH 3.0f
#define PLAYER_Z_REACH 3.5f
#define PLAYER_MAX_SPEED 18.0f

#define PLAYER_HEIGHT 1.80f
#define CAMERA_HEIGHT 1.65f
#define SLIDE_Z_DIFF -1.00f

// light limits (see gfx/renderer.c/h + gfx/light.c/h)
#define MAX_LIGHTS 2048
#define MAX_LIGHT_LINES 32
#define MAX_SHADOW_MAP_MS_PER_FRAME 0.5 // ms dedicated to shadow maps/frame

// shadow map limits
#define SHADOW_MAP_SIZE 4096
#define SHADOW_MAP_PER_LIGHT 512
#define SHADOW_MAP_SIZE_LIGHTS (SHADOW_MAP_SIZE / SHADOW_MAP_PER_LIGHT)

// maximum milliseconds which can be dedicated to updating sector matrices per
// frame
#define MAX_SECTOR_MATRIX_MS_PER_FRAME 1.0

// TODO(refactor) player file
#define SCRY_DISTANCE 3.0

// limits for levels
#define MAX_VERTICES   65536
#define MAX_SIDES      65536
#define MAX_WALLS      32768
#define MAX_SECTORS    16384
#define MAX_DECALS     16384
#define MAX_ENTITIES   8192
#define MAX_ROOMS      1024
#define MAX_SUBSECTORS 131072
#define MAX_BLOCKS 4096

// limits for renderer (see renderer.h)
#define MAX_PER_FRAME_MODELS 2048
#define MAX_PER_FRAME_SPRITES 16384
#define MAX_PORTAL_DEPTH 8

// number of "steps" for bloom, must be even
// see passes.c/h, renderer.c/h
#define NUM_BLOOM_PASSES 4

// steps for blur, must be even
#define NUM_BLUR_PASSES 4

// threshold for "near" sector matrix
#define SECTOR_NEAR_THRESHOLD 32.0f

// time in which entities which are corpses remain in play
// see entity/*.c, model.glsl
#define ENTITY_CORPSE_TIME_S 0.18

#define DEFAULT_FOG_DIST 24.0f

#define PLAY_TRANSITION_TIME_S 1.0

#endif // ifndef CONFIG_H
