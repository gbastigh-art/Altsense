#include "gfx/model.h"
#include "gfx/renderer.h"
#include "game.h"
#include "util/file.h"
#include "reloadhost.h"

// global model atlas instance
static model_atlas_t *atlas = NULL;
RELOAD_STATIC_GLOBAL(atlas)

model_atlas_t *g_model_atlas = NULL;
RELOAD_STATIC_GLOBAL(g_model_atlas)

#define INDEX_BUFFER_SIZE  32 * 1024 * 1024
#define VERTEX_BUFFER_SIZE 64 * 1024 * 1024

// insert model and assign id
static void insert_and_assign_id(model_data_t *md) {
    md->id.index = dynlist_size(atlas->models_by_id);
    *dynlist_push(atlas->models_by_id) = md;
    map_insert(&atlas->models, mem_strdup(&atlas->arena, md->name), md);
}

static void model_data_destroy(model_data_t *md) {
    if (md->vertices) {
        dynlist_destroy(md->vertices);
    }

    if (map_valid(&md->index_groups)) {
        map_destroy(&md->index_groups);
    }
}

static void model_index_group_ptr__free(map_t*, void *p) {
    model_index_group_t *group = (*(model_index_group_t**) p);

    mem_free(&atlas->arena, group->name);
    if (group->indices) {
        dynlist_destroy(group->indices);
    }
}

// indices into each component array for a single vertex
typedef struct {
    u16 pos, uv, normal;
} model_vertex_comp_indices_t;

typedef struct {
    model_index_group_t *group;
    model_vertex_comp_indices_t comp_indices[3];

    // indices for final vertices
    u16 vertex_indices[3];
} model_face_t;

// ensure buffer has at least "size" bytes
static void ensure_buffer(
        model_atlas_buffer_t *buffer,
        int size) {
    ASSERT(size > 0);

    const int size_pot = round_up_to_pot(size);
    const int old_size = buffer->size;
    u8 *old_data = NULL;

    if (buffer->handle.id) {
        if (size_pot < buffer->size) {
            // nothing to do
            return;
        } else {
            old_data = buffer->data;
            sg_destroy_buffer(buffer->handle);
        }
    }

    buffer->handle =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = buffer->type,
                .size = size_pot,
                .usage = SG_USAGE_STREAM
            });

    buffer->data = mem_alloc(&atlas->arena, size_pot);
    buffer->size = size_pot;

    if (old_data) {
        memcpy(buffer->data, old_data, old_size);
        mem_free(&atlas->arena, old_data);
        old_data = NULL;
    }

    buffer->dirty = true;
}

static void compute_index_group_bounds_and_centroid(
        model_data_t *md,
        model_index_group_t *group) {
    v3 centroid = v3_of(0);
    v3 mi = v3_of(1e10f), ma = v3_of(-1e10f);
    dynlist_each(group->indices, it) {
        mi = v3_minv(mi, md->vertices[*it.el].pos);
        ma = v3_maxv(ma, md->vertices[*it.el].pos);
        centroid = v3_add(centroid, md->vertices[*it.el].pos);
    }
    group->bounds.min = mi;
    group->bounds.max = ma;
    group->centroid = v3_divs(centroid, dynlist_size(group->indices));
}

// upload to GPU, compute min, max, etc. on model data
static void finalize_model_data(model_data_t *md) {
    // upload vertices
    ensure_buffer(
        &atlas->vertex_buffer,
        atlas->vertex_buffer.offset + dynlist_size_bytes(md->vertices));

    md->offset_vertices = atlas->vertex_buffer.offset;
    memcpy(
        &atlas->vertex_buffer.data[atlas->vertex_buffer.offset],
        md->vertices,
        dynlist_size_bytes(md->vertices));
    atlas->vertex_buffer.offset += dynlist_size_bytes(md->vertices);
    atlas->vertex_buffer.dirty = true;

    // compute indices list
    DYNLIST(u16) indices = dynlist_create(u16, &g->frame_arena);

    // upload for default group
    md->default_group.offset_indices = dynlist_size(indices);
    md->default_group.n_indices = dynlist_size(md->default_group.indices);
    dynlist_push_all(indices, md->default_group.indices);

    compute_index_group_bounds_and_centroid(md, &md->default_group);
    md->default_group.model = md;

    // TODO
    //dynlist_destroy(md->default_group.indices);

    // upload for all other vertex groups
    map_each(char*, model_index_group_t*, &md->index_groups, it) {
        model_index_group_t *group = *it.value;

        group->offset_indices = dynlist_size(indices);
        group->n_indices = dynlist_size(group->indices);
        dynlist_push_all(indices, group->indices);

        compute_index_group_bounds_and_centroid(md, *it.value);
        (*it.value)->model = md;

        // TODO: drop after upload, but keep sometimes?
        // drop list, not necessary anymore
        // dynlist_destroy(group->indices);
    }

    ensure_buffer(
        &atlas->index_buffer,
        atlas->index_buffer.offset + dynlist_size_bytes(indices));

    md->offset_indices = atlas->index_buffer.offset;
    memcpy(
        &atlas->index_buffer.data[atlas->index_buffer.offset],
        indices,
        dynlist_size_bytes(indices));
    atlas->index_buffer.offset += dynlist_size_bytes(indices);
    atlas->index_buffer.dirty = true;

    v3 mi = v3_of(1e10f), ma = v3_of(-1e10f);
    dynlist_each(md->vertices, it) {
        mi = v3_minv(mi, it.el->pos);
        ma = v3_maxv(ma, it.el->pos);
    }
    md->min = mi;
    md->max = ma;
    md->size = v3_sub(ma, mi);

    LOG(
        "  uploaded (%d vertices (%.3f KiB) / (%d indices (%.3f KiB)))",
        dynlist_size(md->vertices),
        dynlist_size_bytes(md->vertices) / 1024.0f,
        dynlist_size(indices),
        dynlist_size_bytes(indices) / 1024.0f);

    // TODO: some models need vertices - find a way to tag these?
    // drop vertex list, not necessary anymore
    // dynlist_destroy(md->vertices);
}

// load .obj data "data" into specified model_data_t *dst
// "data" is ptr to start of data
// "end" (optional) is line on which to end scan
// "next_out" (optional) is next line after data + end
// returns true on success
static bool try_load_obj_data(
        model_data_t *md,
        const char *debug_name,
        const char *data,
        const char *end,
        const char **next_out) {
    *md = (model_data_t) { 0 };
    dynlist_init(md->vertices, &atlas->arena);

    DYNLIST(v3) vs = dynlist_create(v3, &g->frame_arena);
    DYNLIST(v3) ns = dynlist_create(v3, &g->frame_arena);
    DYNLIST(v2) uvs = dynlist_create(v2, &g->frame_arena);
    DYNLIST(model_face_t) faces = dynlist_create(model_face_t, &g->frame_arena);

    // current group (NULL -> default group)
    // could also point to a group in dst->index_groups
    // allocated on atlas->arena (if not default)
    model_index_group_t *group = NULL;

    bool ok = true;

    int dup_vertices = 0;

    // map vertex model_vertex_comp_indices -> to their inserted location so
    // that we can deduplicate vertices which are already present and avoid
    // double-inserting them
    map_t used_vertices;
    map_init(
        &used_vertices,
        &g->frame_arena,
        sizeof(model_vertex_comp_indices_t),
        sizeof(u16),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    const char *next = data;
    char line[1024];
    int lineno = 0;
    while (*next && (next = str_line(next, line, sizeof(line)))) {
        lineno++;
#define CHECK(_e, _m)                                                          \
    do { if (!(_e)) {                                                          \
        ERROR(                                                                 \
            "failed to load model %s: %s  \n(line %d \'%s\')",                 \
            debug_name, (_m), lineno, line); ok = false; goto done; } } while(0)

        if (end && !strcmp(line, end)) { break; }

        CHECK(!strstr(line, "[MMDL"), "got mmdl in the middle of OBJ data");

        // skip all whitespace
        const char *p = srt_trim(line);

        if (str_is_prefixed_by(p, "v ")) {
            v3 *v = dynlist_push(vs);
            CHECK(
                sscanf(p, "v %f %f %f", &v->x, &v->y, &v->z) == 3,
                "bad vertex");
        } else if (str_is_prefixed_by(p, "vt ")) {
            v2 *t = dynlist_push(uvs);
            CHECK(
                sscanf(p, "vt %f %f", &t->x, &t->y) == 2,
                "bad UV");
        } else if (str_is_prefixed_by(p, "vn ")) {
            v3 *n = dynlist_push(ns);
            CHECK(
                sscanf(p, "vn %f %f %f", &n->x, &n->y, &n->z) == 3,
                "bad normal");
            *n = v3_normalize(*n);
        } else if (str_is_prefixed_by(p, "f ")) {
            model_face_t f = { .group = group };
            CHECK(
                sscanf(
                    p, "f %" SCNu16 "/%" SCNu16 "/%" SCNu16
                        " %" SCNu16 "/%" SCNu16 "/%" SCNu16
                        " %" SCNu16 "/%" SCNu16 "/%" SCNu16,
                    &f.comp_indices[0].pos,
                    &f.comp_indices[0].uv,
                    &f.comp_indices[0].normal,
                    &f.comp_indices[1].pos,
                    &f.comp_indices[1].uv,
                    &f.comp_indices[1].normal,
                    &f.comp_indices[2].pos,
                    &f.comp_indices[2].uv,
                    &f.comp_indices[2].normal) == 9,
                "bad face");

            // OBJ indices are one-based, convert to zero-based
            for (int i = 0; i < 3; i++) {
                f.comp_indices[i].pos--;
                f.comp_indices[i].uv--;
                f.comp_indices[i].normal--;
            }

            *dynlist_push(faces) = f;
        } else if (str_is_prefixed_by(p, "g ")) {
            // group specifier
            char group_name[64];
            CHECK(sscanf(p, "g  %63s", group_name) == 1, "bad group");

            if (!strcmp(group_name, "(null)")) {
                // default group
                group = NULL;
            } else {
                // beginning a new group
                // ensure map exists
                if (!map_valid(&md->index_groups)) {
                    map_init(
                        &md->index_groups,
                        &atlas->arena,
                        sizeof(char*),
                        sizeof(model_index_group_t*),
                        map_hash_str,
                        map_cmp_str,
                        NULL,
                        model_index_group_ptr__free,
                        NULL);
                }

                // check if group exists
                model_index_group_t **pgroup =
                    map_get(
                        model_index_group_t*,
                        &md->index_groups,
                        (char*) group_name);

                if (pgroup) {
                    group = *pgroup;
                } else {
                    // create group, does not exist
                    group = mem_calloc(&atlas->arena, sizeof(*group));
                    group->name = mem_strdup(&atlas->arena, group_name);
                    group->is_default = false;

                    dynlist_init(group->indices, &atlas->arena);

                    pgroup = map_insert(&md->index_groups, group->name, group);
                    ASSERT(*pgroup == group);
                }
            }
        } else if (str_is_prefixed_by(p, "s ")) {
            // ignore, shading specifier
        }
#undef CHECK
    }

    // init default index group
    dynlist_init(md->default_group.indices, &atlas->arena);
    md->default_group.is_default = true;
    md->default_group.name =
        mem_strdup(&atlas->arena, MODEL_DEFAULT_INDEX_GROUP_NAME);

    // iterate faces and...
    // * compute vertices (+ their indices) for each face, deduplicating those
    //   that already exist
    // * computing indices of used vertices for each index group
    dynlist_each(faces, it) {
        for (int i = 0; i < 3; i++) {
            u16 index;
            u16 *pexisting_index =
                map_get(u16, &used_vertices, it.el->comp_indices[i]);

            if (pexisting_index) {
                // already exists -> add preexisting index
                index = *pexisting_index;
                dup_vertices++;
            } else {
                // adding new index, record in used_vertices
                index = dynlist_size(md->vertices);
                map_insert(&used_vertices, it.el->comp_indices[i], index);

                *dynlist_push(md->vertices) =
                    (model_vertex_t) {
                        .pos = vs[it.el->comp_indices[i].pos],
                        .uv = uvs[it.el->comp_indices[i].uv],
                        .normal = ns[it.el->comp_indices[i].normal],
                    };
            }

            it.el->vertex_indices[i] = index;

            if (it.el->group) {
                // add indices to specific group
                *dynlist_push(it.el->group->indices) = index;
            }

            // add all indices to default group
            *dynlist_push(md->default_group.indices) = index;
        }
    }

done:
    if (next_out) { *next_out = next; }

    LOG(
        "  %d vertices (%d deduplicated)",
        dynlist_size(md->vertices),
        dup_vertices);

    if (!ok) {
        model_data_destroy(md);
        *md = (model_data_t) { 0 };
    }

    return ok;
}

// tries to load data from obj_path into model_data_t *dst
static bool try_load_model_data(
        model_data_t *dst,
        const char *obj_path) {
    *dst = (model_data_t) { 0 };

    strbuf_t data = strbuf_create(&g->frame_arena);
    const file_error_e err = file_read_strbuf(&data, obj_path);
    if (err != FILE_OK)  {
        WARN("failed to read %s", obj_path);
        return false;
    }

    return try_load_obj_data(dst, obj_path, data, NULL, NULL);
}

RELOAD_VISIBLE void model_atlas__entries_valfree(map_t *m, void *pvalue) {
    model_data_t *md = *((model_data_t**) pvalue);

    model_data_destroy(md);
    mem_free(&atlas->arena, md->name);
    mem_free(&atlas->arena, md);
}

void model_atlas_init(allocator_t *al) {
    atlas = mem_calloc(&g->arena, sizeof(*atlas));
    g_model_atlas = atlas;

    *atlas = (model_atlas_t) { 0 };

    heap_allocator_init(&atlas->arena, al, NULL);
    dynlist_init(atlas->models_by_id, &atlas->arena);
    map_init(
        &atlas->models,
        &atlas->arena,
        sizeof(char*),
        sizeof(model_data_t*),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        model_atlas__entries_valfree,
        NULL);
    atlas->index_buffer.type = SG_BUFFERTYPE_INDEXBUFFER;
    atlas->vertex_buffer.type = SG_BUFFERTYPE_VERTEXBUFFER;

    model_atlas_reset();
}

void model_atlas_destroy() {
    sg_destroy_buffer(atlas->index_buffer.handle);
    sg_destroy_buffer(atlas->vertex_buffer.handle);
    heap_allocator_destroy(&atlas->arena);
}

const model_data_t *model_atlas_insert_raw(
        const char *name,
        DYNLIST(u16) *indices,
        DYNLIST(model_vertex_t) *vertices) {
    model_data_t *md = mem_calloc(&atlas->arena, sizeof(*md));
    md->name = mem_strdup(&atlas->arena, name);
    insert_and_assign_id(md);

    // everything goes into default group
    md->default_group.is_default = true;
    md->default_group.name =
        mem_strdup(&atlas->arena, MODEL_DEFAULT_INDEX_GROUP_NAME);
    dynlist_init(md->default_group.indices, &atlas->arena);
    dynlist_push_all(md->default_group.indices, *indices);

    dynlist_init(md->vertices, &atlas->arena);
    dynlist_push_all(md->vertices, *vertices);

    finalize_model_data(md);

    LOG("inserted raw model %s (%d)", md->name, (int) md->id.index);
    LOG(
        "  uploaded (%d vertices (%.3f KiB) / (%d indices (%.3f KiB)))",
        dynlist_size(*vertices),
        dynlist_size_bytes(*vertices) / 1024.0f,
        dynlist_size(*indices),
        dynlist_size_bytes(*indices) / 1024.0f);

    return md;
}

// try to load an .mmdl from the specified path
static bool try_load_mmdl_v2(
        const char *base_name,
        const char *path) {
    LOG("loading model (mmdl (ver >= 2)) %s (%s)", base_name, path);

    strbuf_t data = strbuf_create(&g->frame_arena);
    const file_error_e err = file_read_strbuf(&data, path);
    if (err != FILE_OK)  {
        ERROR("failed to read %s", path);
        return false;
    }


    int version = 0;

    // check version
    if (sscanf(data, "# [MMDL_VERSION] %d", &version) != 1) {
        ERROR("bad version string");
        return false;
    }

    if (version != 2) {
        ERROR("bad version %d, expected %d", version, 2);
        return false;
    }

    // seek to next line
    const char *next = str_line(data, NULL, 0);

    if (!next) {
        ERROR("bad mmdl");
        return false;
    }

    char line[1024], end[1024];
    while (str_is_prefixed_by(next, "# [MMDL_BEGIN]")) {
        // read first line
        next = str_line(next, line, sizeof(line));
        if (!next) {
            WARN("bad begin %s", path);
            return false;
        }

        bool is_base = false;
        char frame[64], frame_name[64], frame_index[64];

        // read frame header
        if (sscanf(line, "# [MMDL_BEGIN] %s", frame) != 1) {
            WARN("could not scan begin %s", path);
            return false;
        }

        if (!strchr(frame, '$')) {
            // base has no frame specifier
            is_base = true;
        } else {
            char frame_split[64];
            snprintf(frame_split, sizeof(frame_split), "%s", frame);

            // split frame into <name>$<index> if not base
            char *dollar = strchr(frame_split, '$');
            if (!dollar) {
                WARN("no $ in frame %s", frame);
                return false;
            }

            *dollar = '\0';
            snprintf(frame_name, sizeof(frame_name), "%s", frame_split);
            snprintf(frame_index, sizeof(frame_index), "%s", dollar + 1);

            if (strlen(frame_name) == 0 || strlen(frame_index) == 0) {
                WARN("bad frame for %s", frame);
                return false;
            }
        }

        model_data_t md = { 0 };

        snprintf(end, sizeof(end), "# [MMDL_END] %s", frame);
        if (!try_load_obj_data(&md, path, next, end, &next)) {
            WARN("OBJ data error (%s)", path);
            return false;
        }

        // remove leading zeros from index
        while (*frame_index == '0' && *(frame_index + 1) != '\0') {
            const int len = strlen(frame_index);
            memcpy(&frame_index[0], &frame_index[1], len - 1);
            frame_index[len - 1] = '\0';
        }

        // load name
        if (is_base) {
            md.name = mem_strdup(&atlas->arena, base_name);
        } else {
            md.name =
                mem_strfmt(
                    &atlas->arena,
                    "%s$%s",
                    frame_name,
                    frame_index);
        }

        LOG("  inserting frame %s", md.name);

        model_data_t *pmd = mem_alloc_inplace(&atlas->arena, sizeof(md), &md);
        finalize_model_data(pmd);
        insert_and_assign_id(pmd);
    }

    return true;
}

// try to load an .mmdl from the specified path
static bool try_load_mmdl(
        const char *base_name,
        const char *path) {
    LOG("loading model (mmdl) %s (%s)", base_name, path);

    strbuf_t data = strbuf_create(&g->frame_arena);
    const file_error_e err = file_read_strbuf(&data, path);
    if (err != FILE_OK)  {
        WARN("failed to read %s", path);
        return false;
    }

    if (str_is_prefixed_by(data, "# [MMDL_VERSION]")) {
        LOG("  mmdl is at least v2");
        return try_load_mmdl_v2(base_name, path);
    }

    // first line should specify some mmdl
    if (!str_is_prefixed_by(data, "# [MMDL] BEGIN")) {
        WARN("first line does not specify MMDL?");
        return false;
    }

    char line[1024], end[1024];
    const char *next = data;
    while (str_is_prefixed_by(next, "# [MMDL] BEGIN")) {
        // read first line
        next = str_line(next, line, sizeof(line));
        if (!next) {
            WARN("bad begin %s", path);
            return false;
        }

        char frame[64], frame_name[64], frame_index[64];

        // read frame header
        if (sscanf(line, "# [MMDL] BEGIN %s", frame) != 1) {
            WARN("could not scan begin %s", path);
            return false;
        }

        // base has no frame specifier
        if (!strcmp(frame, "base")) {
            snprintf(frame_name, sizeof(frame_name), "%s", "base");
            snprintf(frame_index, sizeof(frame_index), "%d", -1);
        } else {
            char frame_split[64];
            snprintf(frame_split, sizeof(frame_split), "%s", frame);

            // split frame into <name>$<index> if not base
            char *dollar = strchr(frame_split, '$');
            if (!dollar) {
                WARN("no $ in frame %s", frame);
                return false;
            }

            *dollar = '\0';
            snprintf(frame_name, sizeof(frame_name), "%s", frame_split);
            snprintf(frame_index, sizeof(frame_index), "%s", dollar + 1);

            if (strlen(frame_name) == 0 || strlen(frame_index) == 0) {
                WARN("bad frame for %s", frame);
                return false;
            }
        }

        model_data_t md = { 0 };

        snprintf(end, sizeof(end), "# [MMDL] END %s", frame);
        if (!try_load_obj_data(&md, path, next, end, &next)) {
            WARN("OBJ data error (%s)", path);
            return false;
        }

        // remove leading zeros from index
        while (*frame_index == '0' && *(frame_index + 1) != '\0') {
            const int len = strlen(frame_index);
            memcpy(&frame_index[0], &frame_index[1], len - 1);
            frame_index[len - 1] = '\0';
        }

        char *name;

        if (!strcmp(frame_name, "base")) {
            name = mem_strdup(&atlas->arena, base_name);
        } else {
            name =
                mem_strfmt(
                    &atlas->arena,
                    "%s$%s$%s",
                    base_name,
                    frame_name,
                    frame_index);
        }

        LOG("  inserting frame %s", name);

        model_data_t *pmd = mem_alloc_inplace(&atlas->arena, sizeof(md), &md);
        pmd->name = name;
        finalize_model_data(pmd);
        insert_and_assign_id(pmd);
    }

    return true;
}

const model_data_t *model_atlas_lookup(const char *name) {
    model_data_t **slot = map_get(model_data_t*, &atlas->models, name);

    if (!slot) {
        LOG("attempting to load model %s...", name);

        // split file/try to load with each $, fx. name="model$anim$1",
        // try looking for "model$anim$1", "model$anim", "model" in that order
        // to guarantee loaded animations hit on the first try
        const char *mmdl_path = NULL;

        DYNLIST(char*) tokens = strtoka(name, "$", tlscratch());
        ASSERT(dynlist_size(tokens) >= 1);

        for (int i = dynlist_size(tokens) - 1; i >= 0; i--) {
            strbuf_t buf = strbuf_create(tlscratch());

            // append tokens up to and including i, with $ as delimiter
            for (int j = 0; j <= i; j++) {
                strbuf_ap_fmt(&buf, "%s%s", tokens[j], j == i ? "" : "$");
            }

            const char *path =
                mem_strfmt(tlscratch(), "assets/models/%s.mmdl", buf);

            // have we loaded this model?
            model_data_t **pslot = map_get(model_data_t*, &atlas->models, buf);

            if (pslot) {
                return *pslot;
            }

            if (file_exists(path)) {
                mmdl_path = path;
                break;
            }
        }


        if (mmdl_path) {
            if (!try_load_mmdl(tokens[0], mmdl_path)) {
                WARN(
                    "  failed to load model (mmdl) %s (%s)",
                    name,
                    mmdl_path);
                return atlas->models_by_id[0];
            } else {
                // model has been loaded, try to grab
                slot = map_get(model_data_t*, &atlas->models, name);
                return slot ? *slot : atlas->models_by_id[0];
            }
        }

        // try .obj
        const char *path =
            mem_strfmt(
                &g->frame_arena,
                "assets/models/%s.obj",
                tokens[0]);

        if (file_exists(path)) {
            // load obj if no mmdl
            model_data_t loaded;

            if (!try_load_model_data(&loaded, path)) {
                WARN("  failed to load model (obj) %s (%s)", name, path);
                return atlas->models_by_id[0];
            } else {
                LOG("  loaded model %s (%s)", name, path);

                model_data_t *ptr_loaded =
                    mem_alloc_inplace(&atlas->arena, sizeof(loaded), &loaded);
                ptr_loaded->name = mem_strdup(&atlas->arena, name);
                finalize_model_data(ptr_loaded);
                insert_and_assign_id(ptr_loaded);
                return ptr_loaded;
            }
        } else {
            WARN("  could not find model (or frame) %s", name);
            return atlas->models_by_id[0];
        }
    }

    return slot ? *slot : atlas->models_by_id[0];
}

const model_data_t *model_atlas_lookupf(const char *fmt, ...) {
    STACK_ALLOCATOR(tmp, 1024);

    va_list ap;
    va_start(ap, fmt);
    char *name = mem_vstrfmt(&tmp, fmt, ap);
    va_end(ap);

    return model_atlas_lookup(name);
}

const model_data_t *model_atlas_get(model_id_t id) {
    if (id.index >= dynlist_size(atlas->models_by_id)) {
        id.index = 0;
    }

    return atlas->models_by_id[id.index];
}

bool model_atlas_contains(const char *name) {
    return map_contains(&atlas->models, name);
}

static void insert_model_from_sshape(
        const char *name,
        const sshape_buffer_t *ssb) {
    const int n_indices = ssb->indices.data_size / sizeof(u16);
    const int n_vertices = ssb->vertices.data_size / sizeof(sshape_vertex_t);
    ASSERT_DEBUG(n_indices != 0);
    ASSERT_DEBUG(n_vertices != 0);

    DYNLIST(u16) indices = NULL;
    dynlist_init(indices, &g->frame_arena, n_indices);
    dynlist_resize(indices, n_indices);
    memcpy(indices, ssb->indices.buffer.ptr, n_indices * sizeof(u16));

    DYNLIST(model_vertex_t) vertices = NULL;
    dynlist_init(vertices, &g->frame_arena, n_vertices);
    dynlist_resize(vertices, n_vertices);

    const sshape_vertex_t *ss_vertices = ssb->vertices.buffer.ptr;
    for (int i = 0; i < n_vertices; i++) {
        const v4 n = u32_unpack_to_v4n(ss_vertices[i].normal);
        vertices[i] = (model_vertex_t) {
            .uv.x = u16_unpack_to_f32(ss_vertices[i].u),
            .uv.y = u16_unpack_to_f32(ss_vertices[i].v),
            .pos = v3_of(ss_vertices[i].x, ss_vertices[i].y, ss_vertices[i].z),
            .normal = v3_from(n),
        };
    }

    model_atlas_insert_raw(name, &indices, &vertices);
}

void model_atlas_reset() {
    dynlist_resize(atlas->models_by_id, 0);
    map_clear(&atlas->models);

    // insert model 0/empty model
    model_data_t *empty_model = mem_calloc(&atlas->arena, sizeof(*empty_model));
    dynlist_init(empty_model->vertices, &atlas->arena);
    empty_model->name = mem_strdup(&atlas->arena, "(empty model)");
    insert_and_assign_id(empty_model);
    ASSERT(empty_model->id.index == 0);

    // insert unit models
    struct {
        sshape_vertex_t vertices[1024];
        u16 indices[1024];
    } buffer;

    sshape_buffer_t ssb = {
        .indices.buffer = SSHAPE_RANGE(buffer.indices),
        .vertices.buffer = SSHAPE_RANGE(buffer.vertices),
    };

    const sshape_buffer_t ssb_box =
        sshape_build_box(
            &ssb,
            &(sshape_box_t) { .width = 1.0f, .height = 1.0f, .depth = 1.0f, });
    insert_model_from_sshape("unit_cube", &ssb_box);

    const sshape_buffer_t ssb_torus =
        sshape_build_torus(
            &ssb,
            &(sshape_torus_t) { .radius = 0.5f, .ring_radius = 0.25f });
    insert_model_from_sshape("unit_torus", &ssb_torus);

    const sshape_buffer_t ssb_sphere =
        sshape_build_sphere(
            &ssb,
            &(sshape_sphere_t) { .radius = 0.5f });
    insert_model_from_sshape("unit_sphere", &ssb_sphere);

    const sshape_buffer_t ssb_cylinder =
        sshape_build_cylinder(
            &ssb,
            &(sshape_cylinder_t) { .height = 1.0f, .radius = 0.5f });
    insert_model_from_sshape("unit_cylinder", &ssb_cylinder);
}

void model_atlas_update() {
    bool any_dirty = false;

    for (int i = 0; i < ARRLEN(atlas->buffers); i++) {
        model_atlas_buffer_t *buffer = &atlas->buffers[i];
        if (!buffer->dirty) { continue; }
        buffer->dirty = false;
        any_dirty = true;

        sg_update_buffer(
            buffer->handle,
            &(sg_range) {
                .size = buffer->size,
                .ptr = buffer->data,
            });
    }

    // ensure all models are marked as uploaded
    if (any_dirty) {
        map_each(char*, model_data_t*, &atlas->models, it) {
            if (!(*it.value)->uploaded) {
                (*it.value)->uploaded = true;
            }
        }
    }
}

const model_index_group_t *model_data_try_get_index_group(
        const model_data_t *data,
        const char *name) {
    if (!name
        || !strcmp(name, MODEL_DEFAULT_INDEX_GROUP_NAME)
        || !map_valid(&data->index_groups)) {
        return &data->default_group;
    }

    model_index_group_t **pslot =
        map_get(model_index_group_t*, &data->index_groups, name);
    return pslot ? *pslot : NULL;
}
