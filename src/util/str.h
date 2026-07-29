#pragma once

#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "util/alloc.h"
#include "util/dynlist.h"
#include "util/types.h"
#include "util/macros.h"
#include "util/math/util.h"

// string buffer
typedef DYNLIST(char) strbuf_t;

// iterate by character
#define strbuf_each dynlist_each

// string view
// NOTE: end is exclusive, fx. if a full string then *end is \0
typedef struct str_view { const char *start, *end; } str_view_t;

// const str_view_t* -> range_t
M_INLINE range_t range_from_str_view(const str_view_t *view) {
    return
        (range_t) {
            .ptr = (void*) view->start,
            .size = ((usize) (view->end - view->start)) + 1,
        };
}

// const char* -> range_t
M_INLINE range_t range_from_str(const char *str) {
    return
        (range_t) {
            .ptr = (void*) str,
            .size = strlen(str) + 1,
        };
}

// str view from literal
// NOTE: lit is evaluated multiple times!
#define str_view_from_literal(lit) \
    ((str_view_t) { (lit), &(lit)[strlen((lit))] })

// convert string -> str_view_t
M_INLINE str_view_t str_view_from(const char *str) {
    ASSERT(str);
    return (str_view_t) { str, &str[strlen(str)] };
}

M_INLINE str_view_t str_view_from_buf(const strbuf_t *buf) {
    return (str_view_t) {
        .start = (const char*) &buf[0],
        .end = (const char*) &buf[dynlist_size(buf) - 1],
    };
}

// returns true if string view is valid
M_INLINE bool str_view_valid(const str_view_t *view) {
    return view->start && view->end && view->end >= view->start;
}

// duplicate a view to allocator
M_INLINE str_view_t str_view_dup(const str_view_t *view, allocator_t *al) {
    ASSERT(str_view_valid(view));
    const usize len = view->end - view->start;
    char *res = mem_alloc(al, len + 1);
    memcpy(res, view->start, len);
    res[len] = '\0';
    return (str_view_t) { res, &res[len] };
}

// dump view to allocator
M_INLINE char *str_view_dump(const str_view_t *view, allocator_t *al) {
    ASSERT(str_view_valid(view));
    const usize len = view->end - view->start;
    char *res = mem_alloc(al, len + 1);
    memcpy(res, view->start, len);
    res[len] = '\0';
    return res;
}

// dump view to allocator (with truncation)
M_INLINE char *str_view_dump_trunc(
    const str_view_t *view,
    allocator_t *al,
    usize trunc) {
    ASSERT(str_view_valid(view));
    const usize
        total_len = view->end - view->start,
        len = min(total_len, trunc);

    char *res = mem_alloc(al, len + 1);
    memcpy(res, view->start, len);

    if (len != total_len && trunc >= 3) {
        res[len - 1] = '.';
        res[len - 2] = '.';
        res[len - 3] = '.';
    }

    res[len] = '\0';
    return res;
}

// trim left whitespace from string view
M_INLINE str_view_t str_view_ltrim(const str_view_t *view) {
    str_view_t v = *view;
    while (v.start != v.end && isspace(*v.start)) {
        v.start++;
    }
    return v;
}

// trim right whitespace from string view
M_INLINE str_view_t str_view_rtrim(const str_view_t *view) {
    str_view_t v = *view;
    while (v.end != v.start && isspace(*(v.end - 1))) {
        v.end--;
    }
    return v;
}

// trim whitespace from both ends of string view
M_INLINE str_view_t str_view_trim(const str_view_t *view) {
    str_view_t tmp = str_view_rtrim(view);
    return str_view_ltrim(&tmp);
}

// length of str view
M_INLINE usize str_view_len(const str_view_t *view) {
    return (usize) (view->end - view->start);
}

// compare two string views
M_INLINE int str_view_cmp(const str_view_t *a, const str_view_t *b) {
    const char *pa = a->start, *pb = b->start;

    while (pa < a->end && pb < b->end) {
        const int res = ((int) *pa) - ((int) *pb);
        if (res != 0) { return res; }

        pa++;
        pb++;
    }

    if (pa == a->end && pb == b->end) { return 0; }
    if (pa == a->end) { return -1; }
    if (pa == b->end) { return 1; }

    ASSERT(false);
}

// compare string view vs. str
M_INLINE int str_view_cmp_str(const str_view_t *a, const char *b) {
    const str_view_t b_view = str_view_from(b);
    return str_view_cmp(a, &b_view);
}

M_INLINE strbuf_t strbuf_create(allocator_t *a) {
    strbuf_t buf = dynlist_create(char, a, 1);
    *dynlist_push(buf) = '\0';
    return buf;
}

M_INLINE void strbuf_init(strbuf_t *buf, allocator_t *a) {
    dynlist_init(*buf, a, 1);
    *dynlist_push(*buf) = '\0';
}

M_INLINE void strbuf_destroy(strbuf_t *buf) {
    dynlist_destroy(*buf);
}

M_INLINE strbuf_t strbuf_dup(strbuf_t *buf) {
    return dynlist_copy(*buf);
}

M_INLINE usize strbuf_len(const strbuf_t *buf) {
    return dynlist_size(*buf) - 1;
}

M_INLINE void strbuf_reserve(strbuf_t *buf, int size) {
    dynlist_reserve(*buf, size);
}

// resize to hold string of at least "len" characters
// NOTE: string data is not guaranteed to be valid after resize
M_INLINE void strbuf_resize(strbuf_t *buf, int len) {
    dynlist_resize(*buf, len + 1);
    (*buf)[len] = '\0';
}

M_INLINE strbuf_t strbuf_dup_to(strbuf_t *buf, allocator_t *a) {
    const int sz = dynlist_size(*buf);
    strbuf_t other = dynlist_create(char, a, sz);
    dynlist_resize(other, sz);
    memcpy(&other[0], &(*buf)[0], sz);
    return other;
}

M_INLINE void strbuf_concat(strbuf_t *buf, const strbuf_t *other) {
    ASSERT(dynlist_size(*buf) >= 1);
    ASSERT(dynlist_size(*other) >= 1);

    // remove buf's null terminator
    dynlist_pop(buf);
    dynlist_push_all(*buf, *other);
}

M_INLINE void strbuf_ap_vfmt(strbuf_t *buf, const char *fmt, va_list ap) {
    ASSERT(dynlist_size(*buf) >= 1);

    const int len = vsnprintf(NULL, 0, fmt, ap);
    if (len == 0) { return; }

    const int offset = dynlist_size(*buf) - 1;

    ASSERT(offset >= 0);
    ASSERT((*buf)[offset] == '\0');

    dynlist_resize(*buf, offset + len + 1);

    vsnprintf(&(*buf)[offset], len + 1, fmt, ap);

    ASSERT((*buf)[offset + len] == '\0');
    ASSERT((*buf)[dynlist_size(*buf) - 1] == '\0');
}

M_INLINE void strbuf_ap_fmt(strbuf_t *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    strbuf_ap_vfmt(buf, fmt, ap);
    va_end(ap);
}

M_INLINE void strbuf_ap_str(strbuf_t *buf, const char *s) {
    strbuf_ap_fmt(buf, "%s", s);
}

M_INLINE void strbuf_ap_ch(strbuf_t *buf, char ch) {
    const int offset = dynlist_size(*buf) - 1;
    dynlist_resize(*buf, offset + 1 + 1);
    ASSERT((*buf)[offset] == '\0');
    (*buf)[offset] = ch;
    (*buf)[offset + 1] = '\0';
    ASSERT((*buf)[dynlist_size(*buf) - 1] == '\0');
}

M_INLINE void strbuf_setv(strbuf_t *buf, const char *fmt, va_list ap) {
    dynlist_resize_no_contract(*buf, 1); // resized in ap_vfmt
    (*buf)[0] = '\0';
    strbuf_ap_vfmt(buf, fmt, ap);
}

M_INLINE void strbuf_setf(strbuf_t *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    dynlist_resize_no_contract(*buf, 1); // resized in ap_vfmt
    (*buf)[0] = '\0';
    strbuf_ap_vfmt(buf, fmt, ap);
    va_end(ap);
}

M_INLINE void strbuf_set(strbuf_t *buf, const char *str) {
    const int len = strlen(str);
    dynlist_resize(*buf, len + 1);
    memcpy(*buf, str, len + 1);
}

M_INLINE void strbuf_from(strbuf_t *buf, allocator_t *a, const char *str) {
    strbuf_init(buf, a);
    strbuf_ap_str(buf, str);
}

M_INLINE char *strbuf_dump(strbuf_t *buf, allocator_t *a) {
    char *s = mem_alloc(a, dynlist_size(*buf));
    memcpy(s, &(*buf)[0], dynlist_size(*buf));
    return s;
}

M_INLINE str_view_t strbuf_dump_to_view(strbuf_t *buf, allocator_t *a) {
    char *s = mem_alloc(a, dynlist_size(*buf));
    memcpy(s, &(*buf)[0], dynlist_size(*buf));
    return (str_view_t) { s, &s[dynlist_size(*buf)] };
}

// set strbuf to empty string
M_INLINE void strbuf_clear(strbuf_t *buf) {
    dynlist_resize(*buf, 1);
    (*buf)[0] = '\0';
}

M_INLINE char *strtokm(char *input, char *delimiter, char **lasts) {
    if (input != NULL)
        *lasts = input;

    if (*lasts == NULL)
        return *lasts;

    char *end = strstr(*lasts, delimiter);
    if (end == NULL) {
        char *temp = *lasts;
        *lasts = NULL;
        return temp;
    }

    char *temp = *lasts;

    *end = '\0';
    *lasts = end + strlen(delimiter);
    return temp;
}

M_INLINE DYNLIST(char*) strtoka(
        const char *input,
        char *delimeter,
        allocator_t *a) {
    char *dup = mem_strdup(a, input);

    DYNLIST(char*) res = dynlist_create(char*, a);

    char *lasts;
    for (char *tok = strtokm(dup, delimeter, &lasts);
         tok != NULL;
         tok = strtokm(NULL, delimeter, &lasts)) {
        *dynlist_push(res) = tok;
    }

    return res;
}

#define STR_FMT_TIME_MAX 128

M_INLINE size_t str_fmt_time(
    char *p,
    usize n,
    const char *fmt,
    const struct tm *t) {
    size_t sz = strftime(p, n, fmt, t);
    if (sz == 0) {
        char buf[STR_FMT_TIME_MAX];
        sz = strftime(buf, sizeof buf, fmt, t);
        if (sz == 0) {
            return 0;
        }
        p[0] = 0;
        strncat(p, buf, n - 1);
    }
    return sz;
}

M_INLINE void str_fmt_timestamp(char *p, usize sz) {
    time_t tm = time(NULL);
    str_fmt_time(p, sz, "%c", gmtime(&tm));
}

M_INLINE void str_to_safe_filename(char *s) {
    while (s && *s) {
        if (*s != '.'
            && *s != '_'
            && *s != '-'
            && !isalpha(*s)
            && !isdigit(*s)) {
            *s = '_';
        }

        s++;
    }
}

// reads the next line of 'str' into 'buf', or the entire string if it contains
// no newlines
// returns NULL if buf cannot contain line, otherwise returns pointer to next
// line in str (which can be pointer to \0 if this was the last line)
M_INLINE const char *str_line(const char *str, char *line, usize n) {
    // TODO: should not return NULL if buffer is not large enough, should
    // indicate an error...
    const char *end = strchr(str, '\n');

    if (end) {
        if (line) {
            if ((usize) (end - str) > n) { return NULL; }
            memcpy(line, str, end - str);
            line[end - str] = '\0';
        }

        return end + 1;
    } else {
        const usize len = strlen(str);

        if (line) {
            if (len > n) { return NULL; }
            memcpy(line, str, len);
            line[len] = '\0';
        }

        return &str[len];
    }
}

// returns true if str is prefixed by or equal to pre
M_INLINE bool str_is_prefixed_by(const char *str, const char *pre) {
    while (*str && *pre && *str == *pre) {
        str++;
        pre++;
    }
    return (!*pre && !*str) || (!*pre && *str);
}

// returns true if str is suffixed by or equal to suf
M_INLINE bool str_is_suffixed_by(const char *str, const char *suf) {
    const char
        *e_str = str + strlen(str) - 1,
        *e_suf = suf + strlen(suf) - 1;

    while (e_str != str && e_suf != suf && *e_str== *e_suf) {
        e_str--;
        e_suf--;
    }

    return (e_str == str && e_suf == suf) || (e_str != str && e_suf == suf);
}

// trim left isspace chars
M_INLINE char *str_ltrim(char *str) {
    while (isspace(*str)) { str++; }
    return str;
}

// trim right isspace chars
M_INLINE char *str_rtrim(char *str) {
    const usize len = strlen(str);
    if (len == 0) { return str; }

    char *end = str + len - 1;
    while (isspace(*end)) {
        *end = '\0';

        if (end == str) {
            break;
        }

        end--;
    }

    return str;
}

M_INLINE char *srt_trim(char *str) {
    return str_ltrim(str_rtrim(str));
}

// safe snprintf alternative which can be to concatenate onto the end of a
// buffer
//
// char buf[100];
// snprintf(buf, sizeof(buf), "%s", ...);
// xnprintf(buf, sizeof(buf), "%d %f %x", ...);
//
// returns index of null terminator in buf
M_INLINE int xnprintf(char *buf, int n, const char *fmt, ...) {
    va_list args;
    int res = 0;
    int len = strnlen(buf, n);
    va_start(args, fmt);
    if (n - len > 0) {
        res = vsnprintf(buf + len, n - len, fmt, args);
    }
    va_end(args);
    return res + len;
}

M_INLINE void str_to_upper(char *str) {
    while (*str) { *str = toupper(*str); str++; }
}

M_INLINE void str_to_lower(char *str) {
    while (*str) { *str = tolower(*str); str++; }
}

// trim string to max n characters
M_INLINE void str_trunc(char *str, int n) {
    int i = 0;
    while (*str) {
        if (i == n) { *str = '\0'; }
        str++;
        i++;
    }
}

// trim string to max n characters, adding a suffix if over the specified length
M_INLINE void str_trunc_suffix(char *str, int n, const char *suffix) {
    char *p = str;
    int i = 0;
    while (*str) {
        if (i == n) {
            *str = '\0';

            if (suffix) {
                char *dst = str - strlen(suffix);
                if (dst < p) { dst = p; }

                while (*dst && *suffix) {
                    *dst = *suffix;
                    dst++;
                    suffix++;
                }
            }
        }
        str++;
        i++;
    }
}

// sscanf on a str_view_t
// NOTE: uses tlscratch!
M_INLINE int str_view_scanf(const str_view_t *view, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char *str = str_view_dump(view, tlscratch());
    const int n = vscanf(str, ap);
    va_end(ap);
    return n;
}

// process escape sequences and "inline" them into the string with their actual
// values.
// returns "view" (and doens't allocate) if no escaped characters were were
// otherwise returns a new string view allocated on "al" which is unescaped.
// returns NULL view on error
str_view_t str_view_unescape(const str_view_t *view, allocator_t *al);

// escape chars in string (\t -> \ + t, etc.), the opposite of str_unescape
// returns "view" (and doens't allocate) if characters were found which require
// escaping, otherwise returns a new string allocated on "al" which is unescaped
// returns NULL view on error
str_view_t str_view_escape(const str_view_t *view, allocator_t *al);

// returns true if string is null or empty
M_INLINE bool str_is_empty(const char *s) {
    return !s || !*s;
}

// seek in "str" to next character "sep" (or end of string), ignoring any
// separators inside of "quote_chars" (taking backslash escapes into account),
// and ignoring any pairs of characters in "pair_chars" (specify as fx. "{}[]"
// for pairs of <open pair char><close pair char>
//
// returns ptr to separator "sep" in "str" if found, pointer to \0 if end of
// string (view->end), otherwise returns NULL on error
const char *str_view_seek_fancy_sep(
    const str_view_t *view,
    char sep,
    const char *quote_chars,
    const char *pair_chars);

// use str_view_seek_fancy_sep to separate "view" into a list of
// sub-string-views, excluding "sep", until (and including) the end of the
// string.
// returns false on error
bool str_view_fancy_sep(
    const str_view_t *view,
    char sep,
    const char *quote_chars,
    const char *pair_chars,
    DYNLIST(str_view_t) *out);

// like str_view_seek_fancy_sep(view, ',', "\'\"", "[]{}") except hardcoded for
// MAX SPEED! (it's like 200-250% faster)
const char *str_view_seek_json_sep(const str_view_t *view);

#ifdef UTIL_IMPL

str_view_t str_view_unescape(
        const str_view_t *view,
        allocator_t *al) {
    const char *p = view->start;

    char *dst = NULL, *dst_start = NULL;

    while (p != view->end) {
        // trailing backslashes are just treated as backslashes
        if (*p != '\\' || (p + 1) == view->end) {
            if (dst) {
                *dst = *p;
                dst++;
            }

            p++;
        } else {
            if (!dst) {
                // have to copy into new escaped string
                dst = mem_alloc(al, str_view_len(view));
                dst_start = dst;
                memcpy(dst, view->start, p - view->start);
                dst += (p - view->start);
            }

            char c = 0;

            switch (*(p + 1)) {
                case '\\': c = '\\'; p++; break;
                case 'a':  c = '\a'; p++; break;
                case 'b':  c = '\b'; p++; break;
                case 't':  c = '\t'; p++; break;
                case 'r':  c = '\r'; p++; break;
                case 'n':  c = '\n'; p++; break;
                case ';':  c = ';';  p++; break;
                case '#':  c = '#';  p++; break;
                case '=':  c = '=';  p++; break;
                case 'x': {
                    u16 v = 0;
                    for (int i = 0; i < 4; i++) {
                        char d = *p;

                        // transform to uppercase letters
                        if (d >= 'a' && d <= 'f') {
                            d = (d - 'a') + 'A';
                        }

                        u16 x;
                        if (d >= '0' && d <= '9') {
                            x = d - '0';
                        } else if (d >= 'A' && d <= 'F') {
                            x = d - 'A';
                        } else {
                            return (str_view_t) {};
                        }

                        v |= (x & 0xF) << ((3 - i) * 4);
                    }

                    *dst = v & 0xFF;
                    c = v >> 8;
                } break;
            }

            *dst = c;
            dst++;
            p++;
        }
    }

    if (dst) {
        *dst = '\0';
    }

    return dst ? (str_view_t) { dst_start, dst } : *view;
}

str_view_t str_view_escape(const str_view_t *view, allocator_t *al) {
    const char *p = view->start;

    usize n_escape = 0;

    // scan for any to escape...
    while (p != view->end) {
        char c = *p;

        switch (c) {
            case '\\':
            case '\a':
            case '\b':
            case '\t':
            case '\r':
            case '\n':
                n_escape++;
            default:
                break;
        }

        p++;
    }

    if (n_escape == 0) {
        return *view;
    }

    // found some escaped characters, copy and escape...
    // size is orignal string + one for each extra backslash + null terminator
    char *dst_start = mem_alloc(al, str_view_len(view) + n_escape + 1);
    char *dst = dst_start;

    p = view->start;
    while (*p) {
        char c = *p;

        switch (c) {
            case '\\': c = '\\'; *dst = '\\'; dst++; break;
            case '\a': c = 'a';  *dst = '\\'; dst++; break;
            case '\b': c = 'b';  *dst = '\\'; dst++; break;
            case '\t': c = 't';  *dst = '\\'; dst++; break;
            case '\r': c = 'r';  *dst = '\\'; dst++; break;
            case '\n': c = 'n';  *dst = '\\'; dst++; break;
            default: break;
        }

        *dst = c;
        dst++;
        p++;
    }

    ASSERT(dst == dst_start + str_view_len(view) + n_escape);
    *dst = '\0';
    return (str_view_t) { dst_start, dst };
}

const char *str_view_seek_fancy_sep(
    const str_view_t *view,
    char sep,
    const char *quote_chars,
    const char *pair_chars) {
    const usize len_pair_chars = pair_chars ? strlen(pair_chars) : 0;
    ASSERT(len_pair_chars % 2 == 0, "bad pair chars: %s", pair_chars);

    const usize n_pair = len_pair_chars / 2;

    uint pair[max(n_pair, 1)];
    memset(pair, 0, sizeof(pair[0]) * max(n_pair, 1));

    // if != -1, then this is index of the current quote
    int cur_quote = -1;

    const char
        *cur = view->start,
        *prev = NULL;

    while (cur != view->end) {
        const bool escaped = prev && *prev == '\\';

        // handle quote chars
        if (quote_chars) {
            if (cur_quote != -1) {
                // check for end
                if (*cur == quote_chars[cur_quote] && !escaped) {
                    cur_quote = -1;
                }

                goto next_char;
            } else if (!escaped) {
                // check for non-escaped opening
                const char *q = quote_chars;
                while (*q) {
                    if (*cur == *q) {
                        // start quote
                        cur_quote = (int) (q - quote_chars);
                        goto next_char;
                    }

                    q++;
                }
            }
        }

        // true if in any pair
        bool in_pair = false;

        // check for pair chars
        for (uint i = 0; i < n_pair; i++) {
            if (*cur == pair_chars[(i * 2) + 0]) {
                pair[i]++;
            } else if (*cur == pair_chars[(i * 2) + 1]) {
                if (pair[i] == 0) {
                    WARN(
                        "malformed pair in string \"%s\"",
                        str_view_dump(view, tlscratch()));
                    return NULL;
                } else {
                    pair[i]--;
                }
            }

            if (pair[i] != 0) { in_pair = true; }
        }

        if (*cur == sep && !in_pair) {
            return cur;
        }

next_char:
        prev = cur;
        cur++;
    }

    // if quote, something was not terminated
    if (cur_quote != -1) {
        WARN(
            "unterminated quote (%c) for end of string \"%s\"",
            quote_chars[cur_quote],
            str_view_dump(view, tlscratch()));
        return NULL;
    }

    // check for pairs
    for (uint i = 0; i < n_pair; i++) {
        if (pair[i]) {
            WARN(
                "unterminated pair (%c / %c) for end of string \"%s\"",
                pair_chars[(i * 2) + 0],
                pair_chars[(i * 2) + 1],
                str_view_dump(view, tlscratch()));
            return NULL;
        }
    }

    // got to end, OK
    return view->end;
}

bool str_view_fancy_sep(
    const str_view_t *view,
    char sep,
    const char *quote_chars,
    const char *pair_chars,
    DYNLIST(str_view_t) *out) {
    str_view_t v = *view;
    const char *p;
    while ((p = str_view_seek_fancy_sep(&v, sep, quote_chars, pair_chars))) {
        // push view, excluding separator
        *dynlist_push(*out) = (str_view_t) { v.start, p };

        // adjust to start after separator
        v.start = p + 1;

        if (p == view->end) {
            // success, got to end of string
            return true;
        }
    }

    // failed, p == NULL indicates error
    return false;
}

const char *str_view_seek_json_sep(const str_view_t *view) {
    bool dq = false, sq = false;
    int depth_curly = 0, depth_square = 0;

    const char *cur = view->start, *prev = NULL;

    while (cur != view->end) {
        switch (*cur) {
            case '\"':
                if (!sq && (!prev || *prev != '\\')) {
                    dq = !dq;
                }
                break;
            case '\'':
                if (!dq && (!prev || *prev != '\\')) {
                    sq = !sq;
                }
                break;
            case '{':
                if (!dq && !sq) {
                    depth_curly++;
                }
                break;
            case '}':
                if (!dq && !sq) {
                    depth_curly--;
                }
                break;
            case '[':
                if (!dq && !sq) {
                    depth_square++;
                }
                break;
            case ']':
                if (!dq && !sq) {
                    depth_square--;
                }
                break;
            case ',':
                if (!dq && !sq && depth_curly == 0 && depth_square == 0) {
                    // got separator
                    return cur;
                }
            default:
        }

        prev = cur;
        cur++;
    }

    // if quote, something was not terminated
    if (dq || sq) {
        WARN(
            "unterminated quote (%c) for end of string \"%s\"",
            dq ? '\"' : '\'',
            str_view_dump(view, tlscratch()));
        return NULL;
    }

    // check for pairs
    if (depth_square != 0 || depth_curly != 0) {
        WARN(
            "unterminated pair (%s) for end of string \"%s\"",
            depth_square != 0 ? "[]" : "{}",
            str_view_dump(view, tlscratch()));
        return NULL;
    }

    // got to end, OK
    return view->end;
}

#endif // ifdef UTIL_IMPL
