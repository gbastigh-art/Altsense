#pragma once

#include "defs.h"

// create new room
room_t *room_new(level_t *level);

// delete room
void room_delete(level_t *level, room_t *room);

// get box2i of handle at min pos for box
box2f_t room_min_box(room_t *room);

// get box2i of handle at max pos for box
box2f_t room_max_box(room_t *room);

// returns true if lptr is part of room
bool room_contains_lptr(const level_t *level, const room_t *room, lptr_t ptr);

// get lptrs inside of room bounds
void room_get_lptrs(
    const level_t *level,
    const room_t *room,
    int tags,
    DYNLIST(lptr_t) *ptrs);

// get entities in room
void room_get_entities(
    const level_t *level,
    const room_t *room,
    DYNLIST(entity_t*) *entities);

// find room for specified position
// returns NULL if there is no such room
room_t *level_find_room(const level_t *level, v2 pos);

// finds first sector with specified type in the room, NULL if no such sector
sector_t *room_find_sector_with_type(
        const level_t *level,
        const room_t *room,
        sector_type_e type);
