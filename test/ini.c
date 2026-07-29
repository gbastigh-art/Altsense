#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/ini.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

const char *test_ini = "\n"
" foo = bar \n"
" baz = \"qux\" #comment \n"
" underscore_in_my_name = also_over_here ; comment \n"
" i_am_an_int = 2\n"
" empty =\n"
" \n"
" ; comment \n"
" [foo_section] \n"
" only_in_foo_section = false \n"
" foo = baz \n";

static void do_test_ini(ini_t *ini) {
    ASSERT(ini_has_section(ini, "foo_section"));
    ASSERT(ini_has_section(ini, ""));

    ASSERT(ini_has_property(ini, "", "foo"));
    ASSERT(!strcmp(ini_get(ini, "", "foo"), "bar"));

    ASSERT(ini_has_property(ini, "", "baz"));
    ASSERT(!strcmp(ini_get(ini, "", "baz"), "\"qux\""));

    char *str;
    ASSERT(ini_get_str(ini, "", "baz", &str, g_mallocator));
    ASSERT(!strcmp(str, "qux"));

    mem_free(g_mallocator, str);

    ASSERT(ini_has_property(ini, "", "underscore_in_my_name"));
    ASSERT(!strcmp(ini_get(ini, "", "underscore_in_my_name"), "also_over_here"));

    ASSERT(ini_has_property(ini, "foo_section", "foo"));
    ASSERT(!strcmp(ini_get(ini, "foo_section", "foo"), "baz"));

    ASSERT(!ini_has_property(ini, "", "only_in_foo_section"));
    ASSERT(ini_has_property(ini, "foo_section", "only_in_foo_section"));
    ASSERT(!strcmp(ini_get(ini, "foo_section", "only_in_foo_section"), "false"));

    ASSERT(ini_has_property(ini, "", "empty"));
    ASSERT(!strcmp(ini_get(ini, "", "empty"), ""));

    bool only_in_foo_section;
    ASSERT(
        ini_get_bool(
            ini,
            "foo_section",
            "only_in_foo_section",
            &only_in_foo_section));
    ASSERT(!only_in_foo_section);

    int i;
    ASSERT(!ini_get_int(ini, "foo_section", "only_in_foo_section", &i));

    ASSERT(
        ini_get_str(
            ini, "foo_section", "only_in_foo_section", &str, g_mallocator));

    ASSERT(!strcmp(str, "false"));

    mem_free(g_mallocator, str);

    ASSERT(ini_get_int_or_default(ini, "", "aksjfhklasjfhasf", 12) == 12);
    ASSERT(ini_get_int_or_default(ini, "", "i_am_an_int", 0) == 2);
}

int main(int argc, char *argv[]) {
    int res;
    {
        ini_t ini;
        ini_init(&ini, g_mallocator);
        ini_destroy(&ini);
    }

    {
        ini_t ini;
        ini_init(&ini, g_mallocator);
        ASSERT((res = ini_parse(&ini, test_ini)) == INI_OK, "%d", res);
        do_test_ini(&ini);

        // add values
        ini_set_int(&ini, "new_section", "new_prop", 333);
        ini_set_str(&ini, "new_section", "new_str", "hello, world");

        // dump to file
        static char buf[16 * 1024];
        FILE *f = fmemopen(buf, sizeof(buf), "w");
        ini_dump_to_file(&ini, f);

        ini_destroy(&ini);

        // load from file, parse from BUFFER!
        ini_init(&ini, g_mallocator);
        ASSERT((res = ini_parse(&ini, buf)) == INI_OK, "%d", res);
        do_test_ini(&ini);

        ASSERT(ini_get_int_or_default(&ini, "new_section", "new_prop", 0) == 333);

        char *str;
        ASSERT(
            !strcmp(
                (str =
                    ini_get_str_or_null(
                       &ini, "new_section", "new_str", g_mallocator)),
                "hello, world"));
        mem_free(g_mallocator, str);

        ini_dump_to_file(&ini, stdout);

        // TODO: actual test
        ini_iter_t iter_s = INI_ITER_INIT;
        while (ini_iter(&ini, &iter_s)) {
            /* LOG("got section %s", iter_s.name); */

            ini_section_iter_t iter_p = INI_SECTION_ITER_INIT(iter_s);
            while (ini_iter_section(&ini, &iter_p)) {
                /* LOG("  %s = %s", iter_p.name, iter_p.value); */
            }
        }

        ini_destroy(&ini);
    }

    {

        ini_t ini;
        ini_init(&ini, g_mallocator);
        ASSERT((res = ini_parse(&ini, "")) == INI_OK, "%d", res);
        ASSERT(ini_has_section(&ini, ""));
        ini_destroy(&ini);
    }

static const char *bad_inis[] = {
    "= 12",
    "12",
    "askfhsfkasf",
    "=",
};

    for (int i = 0; i < ARRLEN(bad_inis); i++) {
        ini_t ini;
        ini_init(&ini, g_mallocator);
        ASSERT((res = ini_parse(&ini, bad_inis[i])) != INI_OK, "%d", res);
        ini_destroy(&ini);
    }
    return 0;
}
