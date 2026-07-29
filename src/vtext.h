#pragma once

#include "defs.h"

tex_id_t vtext_get_or_createf(const char *id, const char *fmt, ...);

tex_id_t vtext_get_or_create(const char *id, const char *str);

void vtext_ensure_ltext(const ltext_t *lt);

void vtexts_update();
