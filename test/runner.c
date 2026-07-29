#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/log.h"
#include "util/map.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

#define ANSIGREEN "\033[32;1m"
#define ANSIRED   "\033[31;1m"
#define ANSIRESET "\033[0m"

int main(int argc, char *argv[]) {
    ASSERT(argc == 2, "expected 2 args");

    const char *testdir = argv[1];
    ASSERT(!access(testdir, F_OK), "failed to access");

    struct stat sb;
    ASSERT(!stat(testdir, &sb), "failed to stat");
    ASSERT(S_ISDIR(sb.st_mode), "path is not directory");

    DIR *d;
    ASSERT(d = opendir(testdir), "failed to open directory");

    struct stat exstat;
    ASSERT(!stat(argv[0], &exstat));

    int n = 0, npass = 0, nignore = 0;

    struct dirent *e;
    while ((e = readdir(d))) {
        // construct path
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", testdir, e->d_name);

        ASSERT(!stat(path, &sb), "could not stat %s", path);

        if (e->d_name[0] == '.') {
            // hidden file
            continue;
        } else if (sb.st_ino == exstat.st_ino) {
            // test executable
            continue;
        } else if (
            !strchr(e->d_name, '.')
            && (sb.st_mode & S_IXUSR)) {
            n++;
            printf("%s ...", path);

            if (!system(path)) {
                printf("%s\n", ANSIGREEN " [OK]" ANSIRESET);
                npass++;
            } else {
                printf("%s\n", ANSIRED " [FAIL]" ANSIRESET);
            }
        } else {
            nignore++;
        }
    }

    closedir(d);

    printf("%s", n == npass ? ANSIGREEN : ANSIRED);
    printf("%d / %d passed", npass, n);
    printf("%s", ANSIRESET);
    printf(" (%d ignored)\n", nignore);

    return n == npass ? 0 : -1;
}
