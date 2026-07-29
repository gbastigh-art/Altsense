#pragma once

// various file handling functions

#include "util/types.h"
#include "util/enum.h"
#include "util/str.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h> /* IWYU pragma: keep */

#define ENUM_FILE_ERROR(F, ...)       \
    F(OK,             0,  __VA_ARGS__) \
    F(NO_PARENT,      1,  __VA_ARGS__) \
    F(MKDIR_FAILED,   2,  __VA_ARGS__) \
    F(ALREADY_EXISTS, 3,  __VA_ARGS__) \
    F(STAT_FAILED,    4,  __VA_ARGS__) \
    F(FOPEN_FAILED,   5,  __VA_ARGS__) \
    F(FREAD_FAILED,   6,  __VA_ARGS__) \
    F(FWRITE_FAILED,  7,  __VA_ARGS__) \
    F(IS_NOT_DIR,     8,  __VA_ARGS__) \
    F(IS_DIR,         9,  __VA_ARGS__) \
    F(BAD_PATH,       10, __VA_ARGS__) \
    F(FPRINTF_FAILED, 11, __VA_ARGS__) \
    F(FTELL_FAILED,   12, __VA_ARGS__) \

ENUM_DECL(file_error, FILE, ENUM_FILE_ERROR)

// convert relative file path with ~, /, etc. to absolute path with no fancy
// things
char *file_expand_path(allocator_t *al, const char *path);

// clean path
char *file_clean_path(allocator_t *al, const char *path);

// use format string to create cleaned path
char *file_pathfmt(allocator_t *al, const char *fmt, ...);

// join paths a and b
char *file_join_path(allocator_t *al, const char *a, const char *b);

// returns true if file exists
bool file_exists(const char *path);

// get file parent path, returns FILE_OK on success
file_error_e file_parent(char **dst, allocator_t *al, const char *path);

// returns true if path is directory
bool file_isdir(const char *path);

// creates directory if it does not already exist
// if make_parents is true, then missing parent directories are also created
file_error_e file_mkdir(const char *path, bool make_parents);

// read entire file into string buffer
file_error_e file_read_strbuf(strbuf_t *dst, const char *path);

// read entire file into buffer
file_error_e file_read(range_t *dst, allocator_t *al, const char *path);

// write entire file from buffer
file_error_e file_write(const char *path, const range_t *data);

// write string view to file
file_error_e file_write_str_view(const char *path, const str_view_t *view);

// copy from from src to dst
file_error_e file_copy(const char *dst, const char *src);

// create backup of file at path with specified extension
// NOTE: THIS WILL OVERWRITE IF PATH ALREADY EXISTS WITH EX
file_error_e file_makebak(const char *path, const char *ext);

// find files with specified extension in path
// returns 0 on success
file_error_e file_find_with_ext(
    const char *dir, const char *ext, DYNLIST(char*) *paths, allocator_t *al);

// split file into base path, name, and extension (no ".")
// NOTE: modifies path in place!
void file_spilt(char *path, char **dir, char **name, char **ext);

// getcwd
char *file_cwd(allocator_t *al);

#ifdef UTIL_IMPL

ENUM_DEFINE(file_error, FILE, ENUM_FILE_ERROR)

#include "util/str.h"

#include <wordexp.h>

char *file_expand_path(allocator_t *al, const char *path) {
    wordexp_t exp;
    if (wordexp(path, &exp, 0)) {
        WARN("could not wordexp %s", path);
        return NULL;
    }

    char expanded[PATH_MAX];
    expanded[0] = '\0';

    for (int i = 0; i < (int) exp.we_wordc; i++) {
        xnprintf(
            expanded, PATH_MAX,
            "%s%s", exp.we_wordv[i], i == (int) exp.we_wordc - 1 ? "" : " ");
    }

    wordfree(&exp);

    /* char resolved[PATH_MAX]; */
    /* if (!realpath(expanded, resolved)) { */
    /*     WARN("could not realpath %s", expanded); */
    /*     return NULL; */
    /* } */

    /* const int resolved_len = strlen(resolved); */
    return mem_alloc_inplace(al, strlen(expanded) + 1, expanded);
}

char *file_clean_path(allocator_t *al, const char *path) {
    strbuf_t buf = strbuf_create(tlscratch());

    bool have_sep = false;
    while (*path) {
       if (*path == '/') {
           if (have_sep) {
               // remove multiple separators in sequence
               goto nextch;
           }

           have_sep = true;
       } else {
            have_sep = false;
       }

       strbuf_ap_ch(&buf, *path);

nextch:
       path++;
    }

    return strbuf_dump(&buf, al);
}

char *file_pathfmt(allocator_t *al, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    const char *tmp = mem_vstrfmt(tlscratch(), fmt, ap);
    char *out = file_clean_path(al, tmp);

    va_end(ap);

    return out;
}

char *file_join_path(allocator_t *al, const char *a, const char *b) {
    // number of chars to print of a
    int len_a = strlen(a);

    // remove trailing separators from a
    while (len_a > 0 && a[len_a - 1] == PATH_SEP_CHAR) {
        len_a--;
    }

    // remove preceding separators from b
    while (*b == PATH_SEP_CHAR) {
        b++;
    }

    return mem_strfmt(al, "%.*s/%s", len_a, a, b);
}

bool file_exists(const char *path) {
    struct stat s;
    return !stat(path, &s);
}

file_error_e file_parent(char **dst, allocator_t *al, const char *path) {
    file_error_e err;
    char *tmp = mem_strdup(g_mallocator, path);
    char *end = tmp + strlen(tmp) - 1;

    while (end != tmp && *end != '/') {
        end--;
    }

    if (end == tmp) {
        err = FILE_NO_PARENT;
        goto done;
    }

    *dst = mem_strdup(al, tmp);
    err = FILE_OK;

done:
    mem_free(g_mallocator, tmp);
    return err;
}

bool file_isdir(const char *path) {
    struct stat s;
    if (stat(path, &s)) { return false; }
    return !S_ISREG(s.st_mode);
}

static file_error_e file_mkdir_internal(const char* path, mode_t mode) {
    if (!mkdir(path, mode)) {
        // OK
        return FILE_OK;
    }

    if (errno != EEXIST) {
        WARN("mkdir error: %s", strerror(errno));
        return FILE_ALREADY_EXISTS;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        WARN("stat error: %s", strerror(errno));
        return FILE_STAT_FAILED;
    }

    // if not directory, fail
    if (!S_ISDIR(st.st_mode)) {
        WARN("%s exists but is not dir", path);
        return FILE_IS_NOT_DIR;
    }

    return FILE_OK;
}

file_error_e file_mkdir(const char *path, bool make_parents) {
    if (file_exists(path) && file_isdir(path)) {
        return FILE_OK;
    }

    file_error_e err;
    char tmp[PATH_MAX];
    char *p = NULL;

    const int len = snprintf(tmp, sizeof(tmp), "%s", path);
    if (len < 0 || len >= (int) sizeof(tmp)) { return FILE_BAD_PATH; }

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    if (make_parents) {
        p = tmp + 1;

        while (*p) {
            if (*p == '/') {
                *p = 0;

                err = file_mkdir_internal(tmp, S_IRWXU);

                if (err != FILE_OK) {
                    goto done;
                }

                *p = '/';
            }

            p++;
        }
    }

    err = file_mkdir_internal(tmp, 0777);

done:
    return err;
}

file_error_e file_read_strbuf(strbuf_t *dst, const char *path) {
    file_error_e err = FILE_OK;

    FILE *f = fopen(path, "r");
    if (!f) {
        err = FILE_FOPEN_FAILED;
        goto done;
    }

    fseek(f, 0, SEEK_END);
    const int sz = ftell(f);
    if (sz == -1) {
        WARN("ftell error: %s", strerror(errno));
        err = FILE_FTELL_FAILED;
        goto done;
    }
    rewind(f);

    strbuf_resize(dst, sz);
    if (fread(&(*dst)[0], sz, 1, f) != 1)  {
        WARN("fread error: %s", strerror(errno));
        strbuf_resize(dst, 0);
        err = FILE_FREAD_FAILED;
        goto done;
    }

done:
    if (f) {
        fclose(f);
    }

    return err;
}

file_error_e file_read(range_t *dst, allocator_t *al, const char *path) {
    file_error_e err = FILE_OK;
    *dst = (range_t) { 0 };

    FILE *f = fopen(path, "r");
    if (!f) {
        err = FILE_FOPEN_FAILED;
        goto done;
    }

    fseek(f, 0, SEEK_END);
    const int sz = ftell(f);
    if (sz == -1) {
        WARN("ftell error: %s", strerror(errno));
        err = FILE_FTELL_FAILED;
        goto done;
    }
    rewind(f);

    dst->ptr = mem_alloc(al, sz);
    dst->size = sz;

    if (fread(dst->ptr, sz, 1, f) != 1)  {
        WARN("fread error: %s", strerror(errno));
        mem_free(al, dst->ptr);
        *dst = (range_t) { 0 };
        err = FILE_FREAD_FAILED;
        goto done;
    }

done:
    if (f) {
        fclose(f);
    }

    return err;
}

file_error_e file_write(const char *path, const range_t *data) {
    file_error_e err = FILE_OK;

    FILE *dst = fopen(path, "wb");
    if (!dst) {
        WARN("fopen error: %s", strerror(errno));
        err = FILE_FOPEN_FAILED;
        goto done;
    }

    if (fwrite(data->ptr, data->size, 1, dst) != 1) {
        WARN("fwrite error: %s", strerror(errno));
        err = FILE_FWRITE_FAILED;
        goto done;
    }

done:
    if (dst) {
        fclose(dst);
    }

    return err;
}

file_error_e file_write_str_view(const char *path, const str_view_t *view) {
    return
        file_write(
            path,
            &(range_t) {
                .ptr = (void*) view->start,
                .size = str_view_len(view),
            });
}

file_error_e file_copy(const char *dst, const char *src) {
    file_error_e err = FILE_OK;
    allocator_t *allocator = g_mallocator;

    range_t data = { 0 };
    if ((err = file_read(&data, allocator, src)) != FILE_OK) {
        WARN("error reading file %s", src);
        goto done;
    }

    if ((err = file_write(dst, &data)) != FILE_OK) {
        WARN("error writing file %s", dst);
        goto done;
    }

done:
    if (data.ptr) {
        mem_free(allocator, data.ptr);
    }

    return err;
}

file_error_e file_makebak(const char *path, const char *ext) {
    if (file_isdir(path)) {
        return FILE_IS_DIR;
    }

    char new_path[PATH_MAX];
    const int c = snprintf(new_path, sizeof(new_path), "%s%s", path, ext);
    if (c < 0 || c >= (int) sizeof(new_path)) {
        return FILE_BAD_PATH;
    }

    LOG("backing up %s -> %s", path, new_path);

    file_error_e err;
    range_t data;

    err = file_read(&data, tlscratch(), path);
    if (err != FILE_OK) {
        return err;
    }

    err = file_write(new_path, &data);
    if (err != FILE_OK) {
        return err;
    }

    return FILE_OK;
}

file_error_e file_find_with_ext(
        const char *dir,
        const char *ext,
        DYNLIST(char*) *paths,
        allocator_t *al) {
    DIR *d = opendir(dir);
    if (!d) { return FILE_IS_NOT_DIR; }

    int res;
    char buf[PATH_MAX], suf[PATH_MAX];
    res = snprintf(suf, sizeof(suf), ".%s", ext);
    if (res < 0 || res >= (int) sizeof(suf)) {
        return FILE_BAD_PATH;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type != DT_REG) { continue; }
        if (!str_is_suffixed_by(ent->d_name, suf)) { continue; }

        // make full path
        res = snprintf(buf, sizeof(buf), "%s/%s", dir, ent->d_name);
        if (res < 0 || res >= (int) sizeof(buf)) {
            return FILE_BAD_PATH;
        }

        const int len = strlen(buf);
        char *p = mem_alloc(al, len + 1);

        res = snprintf(p, len + 1, "%s", buf);
        if (res < 0 || res >= (int) sizeof(buf)) {
            return FILE_BAD_PATH;
        }

        *dynlist_push(*paths) = p;
    }

    closedir(d);
    return FILE_OK;
}

void file_spilt(char *path, char **pdir, char **pname, char **pext) {
    char *dir = NULL, *name = NULL, *ext = NULL;

    char *last_sep = strrchr(path, '/');
    char *dot = strrchr(path, '.');

    if (dot && last_sep) {
        *last_sep = '\0';
        dir = path;
        name = last_sep + 1;

        // only extension if occurs after last separator
        if (dot > last_sep) {
            *dot = '\0';
            ext = dot + 1;
        }
    } else if (!dot && last_sep) {
        *last_sep = '\0';
        dir = path;
        name = last_sep + 1;
    } else if (dot && !last_sep) {
        *dot = '\0';
        name = path;
        ext = dot + 1;
    } else {
        // nothing else other than name
        name = path;
    }

    if (pdir) { *pdir = dir; }
    if (pname) { *pname = name; }
    if (pext) { *pext = ext; }
}

char *file_cwd(allocator_t *al) {
    char buf[PATH_MAX];
    getcwd(buf, sizeof(buf));
    return mem_strdup(al, buf);
}
#endif // ifdef UTIL_IMPL
