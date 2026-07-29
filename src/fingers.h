#pragma once

#include "defs.h"

// single finger instance
typedef struct {
    bool present;
} finger_t;

typedef enum {
    FINGER_PINKY,
    FINGER_RING,
    FINGER_MIDDLE,
    FINGER_POINTER,
    FINGER_THUMB,
    FINGER_COUNT,
    FINGER_NONE = FINGER_COUNT, // indicates no index

    // base indices
    FINGER_BASE_FIRST = FINGER_PINKY,
    FINGER_BASE_LAST  = FINGER_THUMB,

    FINGER_EX0 = FINGER_THUMB + 1,
    FINGER_EX1,
    FINGER_EX2,
    FINGER_EX3,
    FINGER_EX4,

    // ex indices
    FINGER_EX_FIRST = FINGER_EX0,
    FINGER_EX_LAST = FINGER_EX4,
    FINGER_EX_COUNT = FINGER_EX_LAST - FINGER_EX_FIRST + 1,

    // all finger indices, base + ex
    FINGER_ALL_FIRST = FINGER_PINKY,
    FINGER_ALL_LAST = FINGER_EX4,

    // standard fingers + 5 ex fingers
    FINGER_TOTAL_COUNT = 10,
} finger_e;

typedef enum {
    FINGERS_MODE_SHOOT,
    FINGERS_MODE_EDIT,
    FINGERS_MODE_MENU,
} fingers_mode_e;

typedef struct {
    // last shot tick per-side per-finger
    int last_finger_shot_tick[FINGER_COUNT];
    int last_shot_tick;
    nstime_t last_shot_ns;

    finger_t fingers[FINGER_TOTAL_COUNT];

    // from g->hand_text
    tex_id_t hand_overlay_tex;

    union {
        struct {
            struct {
                bool enabled;

                // index of held finger
                finger_e index;

                // offset from finger center to cursor pos, used to prevent
                // finger from snapping to cursor center on grab
                v3 offset;

                // current diff from initial pos
                v3 diff;
            } held;

            struct {
                // true if any finger is hovered
                bool enabled;

                // index of hovered finger
                finger_e index;

                // total_scaled_s when hover began
                stime_t begin_s;
            } hover;

            // only in !shoot mode
            box2f_t boxes[FINGER_TOTAL_COUNT];

            // only in !shoot mode
            v3 centers[FINGER_TOTAL_COUNT];

            // if mouse clicked on finger
            bool clicked[FINGER_TOTAL_COUNT];

            // if mouse released on finger
            bool released[FINGER_TOTAL_COUNT];

            // if finger is hovered
            bool hovered[FINGER_TOTAL_COUNT];

            // current mode
            fingers_mode_e mode;
        };

        u8 scratch[1024];
    };
} fingers_state_t;

// global finger state
extern fingers_state_t *g_fingers;

void fingers_init();

void fingers_update(level_t *level, entity_t *ent);

void fingers_render(level_t *level, entity_t *ent);
