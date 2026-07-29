#pragma once

#include "defs.h"

#define IO_VERSION 1

// IO results
#define ENUM_IO_ERROR(F, ...)         \
    F(OK,             0, __VA_ARGS__) \
    F(BAD_VALUE,      1, __VA_ARGS__) \
    F(MISSING_FIELD,  2, __VA_ARGS__) \
    F(BAD_PARSE,      3, __VA_ARGS__) \
    F(NOT_DICT,       4, __VA_ARGS__) \
    F(BAD_VERSION,    5, __VA_ARGS__) \
    F(MALFORMATTED,   6, __VA_ARGS__) \
    F(BAD_INDEX,      7, __VA_ARGS__) \
    F(BAD_TEX_ID_MAP, 8, __VA_ARGS__) \

ENUM_DECL(io_error, IO, ENUM_IO_ERROR)

// load level, returns IO_OK on success
io_error_e io_load_level(level_t *level, const range_t *data);

// save level to bytes, returns IO_OK on success
io_error_e io_save_level(const level_t *level, DYNLIST(u8) *dst);
