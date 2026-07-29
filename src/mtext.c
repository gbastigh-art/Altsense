#include "mtext.h"
#include "util/map.h"
#include "util/rand.h"
#include "util/str.h"

// uncomment for debug info
// #define DO_MTEXT_DEBUG

// separator chars
#define MTEXT_SEP " .;-,?!':()"

void mtextgen_init(
        mtextgen_t *mt,
        allocator_t *allocator,
        const char *corpus,
        int ngram_len) {
    allocator_t bump;
    bump_allocator_init(&bump, tlscratch(), 32 * 1024, NULL);

    *mt = (mtextgen_t) { .ngram_len = ngram_len };
    heap_allocator_init(&mt->arena, allocator, &mt->arena_stats);
    map_init(
        &mt->dict,
        &mt->arena,
        sizeof(char*),
        sizeof(DYNLIST(char*)),
        map_hash_str,
        map_cmp_str,
        NULL,
        NULL,
        NULL);

    // consume corpus
    char *tok, *lasts;
    char *corpus_dup = mem_strdup(&bump, corpus);
    char *corpus_dup2 = mem_strdup(&bump, corpus);

    // replace whitespace chars with ' ', "\"" with "\'"
    {
        char *p = corpus_dup;
        while (*p) {
            switch (*p) {
            case '\n':
            case '\t':
            case '\f':
            case '\r':
            case '\v':
                *p = ' ';
                break;
            default:
            }

            p++;
        }
    }

    // create a list of tokens
    DYNLIST(char*) tokens = NULL;
    dynlist_init(tokens, &bump);

    for (tok = strtok_r(corpus_dup, MTEXT_SEP, &lasts);
        tok;
        tok = strtok_r(NULL, MTEXT_SEP, &lasts)) {
        const int len = strlen(tok);
        char delim = corpus_dup2[tok - corpus_dup + len];

        *dynlist_push(tokens) = tok;

        if (!isspace(delim)) {
            char dtok[2] = { delim, '\0' };
            *dynlist_push(tokens) = mem_strdup(&bump, dtok);
        }
    }

    // init ngrams
    for (int i = 0, n = dynlist_size(tokens) - (ngram_len + 1); i < n; i++) {
        // construct a prefix of the next ngram_len tokens and a suffix of 1
        char *prefix = NULL;
        for (int j = 0; j < ngram_len; j++) {
            char *tok = tokens[i + j];
            prefix =
                mem_strfcat(
                    &bump,
                    prefix,
                    "%s%s",
                    j == 0
                    || !!strchr(MTEXT_SEP, tok[0])
                    || (prefix && prefix[strlen(prefix) - 1] == '\'') ?
                        ""
                        : " ",
                    tok);
        }

        // move onto mtext arena
        prefix = mem_strdup(&mt->arena, prefix);

        DYNLIST(char*) *psuffixes = map_get(&mt->dict, prefix);

        if (!psuffixes) {
            // insert new list
            DYNLIST(char*) list = dynlist_create(char*, &mt->arena);
            psuffixes = map_insert(&mt->dict, prefix, list);
        }

        // add next token as suffix to list
        *dynlist_push(*psuffixes) =
            mem_strdup(
                &mt->arena,
                tokens[i + ngram_len]);
    }

#ifdef DO_MTEXT_DEBUG
    map_each(char*, DYNLIST(char*), &mt->dict, it0) {
        LOG("[%s]", *it0.key);
        dynlist_each(*it0.value, it1) {
            LOG("    %s", *it1.el);
        }
    }
    LOG(
        "%.3f KiB (%.3f KiB reserved) for corpus of size %.3f KiB",
        mt->arena_stats.used / 1024.0f,
        mt->arena_stats.reserved / 1024.0f,
        (strlen(corpus) + 1) / 1024.0f);
#endif // ifdef DO_MTEXT_DEBUG

    bump_allocator_destroy(&bump);
}

void mtext_destroy(mtextgen_t *mt) {
    heap_allocator_destroy(&mt->arena);
    *mt = (mtextgen_t) { 0 };
}

bool mtextgen_gen(
        const mtextgen_t *mt,
        strbuf_t *dst,
        int count,
        rand_t *rand,
        const char **errmsg) {
    // TODO: remove this requirement, maybe?
    ASSERT(strbuf_len(dst) == 0);

    // total number of tokens
    int n = 0;

    // pick a random starting prefix from our dictionary
    {
        char *prefix = NULL;

        while (true) {
            int i = -1;
            while (i == -1 || !map_index_occupied(&mt->dict, i)) {
                i = rand_n(rand, 0, map_capacity(&mt->dict) - 1);
            }

            prefix = *map_key_at(char*, &mt->dict, i);

            // disallow initial prefixes that start with a separator
            char *psep;
            psep = strpbrk(prefix, MTEXT_SEP);

            if (psep != prefix) {
                break;
            }
        }

        ASSERT(prefix != NULL);
        strbuf_ap_fmt(dst, "%s", prefix);

#ifdef DO_MTEXT_DEBUG
        LOG("buf after prefix is %s", &(*dst)[0]);
#endif // ifdef DO_MTEXT_DEBUG

        n += mt->ngram_len;
    }

    int i = 0;
    while (n < count) {
        // current prefix is dst[i]..<end>
        DYNLIST(char*) *psuffixes =
            map_get(DYNLIST(char*), &mt->dict, &(*dst)[i]);

        if (!psuffixes) {
            if (errmsg) {
                *errmsg =
                    mem_strfmt(
                        tlscratch(),
                        "failed, current window is %s", &(*dst)[i]);
            }
            return false;
        }

        // pick a random word out of the suffixes
        char *next =
            (*psuffixes)[rand_n(rand, 0, dynlist_size(*psuffixes) - 1)];

#ifdef DO_MTEXT_DEBUG
        LOG("chose next %s out of %d suffixes", next, dynlist_size(*psuffixes));
#endif // ifdef DO_MTEXT_DEBUG

        // append, prepend space if not punctuation
        const char last = (*dst)[strbuf_len(dst) - 1];
        strbuf_ap_fmt(
            dst,
            "%s%s",
            !!strchr(MTEXT_SEP, next[0]) || last == '\'' ? "" : " ",
            next);

        // advance i by length of first token in current context window
        const char *sep = strpbrk(&(*dst)[i], MTEXT_SEP);
        const int sep_index = sep - &(*dst)[i];

        int old_i = i;

        if (*sep == ' ') {
            // go to first character after space
            i += sep_index + 1;
        } else if (sep_index == 0) {
            // first character is punctuation, advance over it
            i++;
        } else {
            // found punctuation, go directly to it
            i += sep_index;
        }

        while ((*dst)[i] == ' ') {
            i++;
        }

        if (i <= old_i) {
            if (errmsg) {
                *errmsg =
                    mem_strfmt(
                        tlscratch(),
                        "failed (i@%d/%d did not advance with sep@%d?)"
                        " current window is %s",
                        i,
                        strbuf_len(dst),
                        sep_index,
                        &(*dst)[i]);
            }
            return false;
        }

        // added a token
        n++;
    }

    return true;
}
