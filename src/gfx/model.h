#pragma once

#include "defs.h"
#include "util/map.h"
#include "ext/sokol.h"

// global model atlas
extern model_atlas_t *g_model_atlas;

#define MODEL_DEFAULT_INDEX_GROUP_NAME "default"

// represents a named vertex group within an OBJ model
typedef struct {
    // model to which this group belongs
    model_data_t *model;

    // name of this group, on atlas->arena
    // nullable
    char *name;

    // TODO: drop after upload, but keep sometimes?
    // on atlas->arena
    // nullable
    DYNLIST(u16) indices;

    // offset into uploaded buffer (relative to parent model_data offset!)
    // (valid when data is finalized)
    int offset_indices;

    // number of indices (valid when data finalized)
    int n_indices;

    // 3D bounds of vertices in this index group
    box3f_t bounds;

    // average position
    v3 centroid;

    // true if default index group
    bool is_default;
} model_index_group_t;

typedef struct model_data {
    // unique id
    model_id_t id;

    // on atlas->arena
    char *name;

    // on atlas->arena, present for lifetime of model data
    // TODO: find a way to selectively drop these on models where we don't need
    // the data later on
    DYNLIST(model_vertex_t) vertices;

    // char* -> model_index_group_t*, on atlas->arena
    // only valid if model has non-default vertex groups
    map_t index_groups;

    // default index group, encompasses entire model
    // (not valid until uploaded)
    model_index_group_t default_group;

    // absolute offsets into model atlas data buffers
    // (not valid until uploaded)
    int offset_indices, offset_vertices;

    // min/max position
    union { struct { v3 min, max; }; box3f_t box; };

    // max - min
    v3 size;

    // true if data has been uploaded to buffer
    bool uploaded;
} model_data_t;

typedef struct {
    sg_buffer_type type;
    sg_buffer handle;
    int size, offset;
    u8 *data;           // on atlas->arena, sized by "size"
    bool dirty;
} model_atlas_buffer_t;

typedef struct model_atlas {
    // lifetime of atlas
    allocator_t arena;

    union {
        struct { model_atlas_buffer_t index_buffer, vertex_buffer; };
        model_atlas_buffer_t buffers[2];
    };

    // char* -> model_data_t*
    map_t models;

    // list of all entries
    DYNLIST(model_data_t*) models_by_id;
} model_atlas_t;

void model_atlas_init(allocator_t *al);

void model_atlas_destroy();

// inserts a raw model
const model_data_t *model_atlas_insert_raw(
        const char *name,
        DYNLIST(u16) *indices,
        DYNLIST(model_vertex_t) *vertices);

// lokup model data by name
const model_data_t *model_atlas_lookup(const char *name);

// lookup model data with fmt string
const model_data_t *model_atlas_lookupf(const char *fmt, ...);

// returns empty model if not found
const model_data_t *model_atlas_get(model_id_t id);

// returns true if name is loaded into atlas
bool model_atlas_contains(const char *name);

// remove all entries from model atlas
void model_atlas_reset();

// update dirty buffers for loaded models
void model_atlas_update();

// try to get index group with specified name from model
// NULL gets default
const model_index_group_t *model_data_try_get_index_group(
        const model_data_t *data,
        const char *name);
