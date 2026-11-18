/*
 * Native CFBundleExecutable for test (and similar) Open Nectar app bundles.
 *
 * macOS will not launch a bundle whose main executable is a shell script
 * (Launchd error 162, Finder "no permission"). This stub execs the
 * open-nectar.sh wrapper that sits beside it and forwards arguments.
 */
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char path[PATH_MAX];
    uint32_t size = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return 1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        return 1;
    }
    char script[PATH_MAX];
    snprintf(script, sizeof(script), "%s/open-nectar.sh", dirname(resolved));
    char **newargv = calloc((size_t)argc + 2, sizeof(char *));
    if (!newargv) {
        return 1;
    }
    newargv[0] = "/bin/bash";
    newargv[1] = script;
    if (argc >= 2) {
        memcpy(newargv + 2, argv + 1, (size_t)(argc - 1) * sizeof(char *));
    }
    execv("/bin/bash", newargv);
    perror("execv");
    return 1;
}
