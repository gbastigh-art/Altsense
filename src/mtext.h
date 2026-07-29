#pragma once

#include "util/map.h"

typedef struct mtextgen {
    allocator_t arena;
    allocator_stats_t arena_stats;

    // char *prefix (ngram_len tokens)
    //  -> DYNLIST(char*) suffixes (one token each)
    map_t dict;

    // length of prefixes in dict/size of context window
    int ngram_len;
} mtextgen_t;

void mtextgen_init(
    mtextgen_t *mt,
    allocator_t *allocator,
    const char *corpus,
    int ngram_len);

void mtext_destroy(mtextgen_t *mt);

bool mtextgen_gen(
    const mtextgen_t *mt,
    strbuf_t *dst,
    int count,
    rand_t *rand,
    const char **errmsg);
