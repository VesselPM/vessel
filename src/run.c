#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
int cmd_run(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: vessel run <id> [args...]\n");
        return 1;
    }

    if (!util_running_as_root()) {
        fprintf(stderr, "error: run requires root privileges\n");
        return 1;
    }

    const char *prog = strrchr(argv[0], '/');
    if (prog) prog++;
    else prog = argv[0];
    int is_vessel_run = (strcmp(prog, "vessel-run") == 0);
    int is_vessel = (strcmp(prog, "vessel") == 0);
    const char *id;
    int arg_offset;
    if (is_vessel_run) {
        id = argv[1];
        arg_offset = 2;
    } else {
        if (argc < 3) {
            fprintf(stderr, "usage: vessel run <id> [args...]\n");
            return 1;
        }
        id = argv[2];
        arg_offset = 3;
    }

    char pkg_dir[VESSEL_MAX_PATH];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", VESSEL_PACKAGES, id);
    struct stat st;
    if (stat(pkg_dir, &st) != 0) {
        fprintf(stderr, "error: package %s not installed\n", id);
        return 1;
    }

    vessel_manifest_t m;
    char manifest_path[VESSEL_MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.toml", pkg_dir);
    if (manifest_load(manifest_path, &m) != 0) {
        if (manifest_load_db(id, &m) != 0) {
            fprintf(stderr, "error: could not load manifest for %s\n", id);
            return 1;
        }
    }

    char entry[VESSEL_MAX_PATH];
    if (m.name.entry[0])
        strncpy(entry, m.name.entry, sizeof(entry)-1);
    else
        strncpy(entry, "/bin/sh", sizeof(entry)-1);

    char run_dir[VESSEL_MAX_PATH];
    snprintf(run_dir, sizeof(run_dir), "%s/%s", VESSEL_RUN, id);
    util_mkdir_p(run_dir, 0755);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: fork failed: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        if (namespace_enter(&m, pkg_dir, run_dir) != 0) {
            fprintf(stderr, "error: namespace setup failed\n");
            _exit(1);
        }

        char **args = malloc(sizeof(char*) * (argc - arg_offset + 3));
        if (!args) {
            fprintf(stderr, "error: out of memory\n");
            _exit(1);
        }

        int ai = 0;
        args[ai++] = entry;
        for (int i = arg_offset; i < argc; i++) args[ai++] = argv[i];
        args[ai] = nullptr;
        execvp(entry, args);
        for (int i = ai; i >= 0; i--)args[i + 1] = args[i];
        args[0] = "/usr/bin/sh";
        execvp("/usr/bin/sh", args);
        fprintf(stderr, "error: could not execute %s: %s\n", entry, strerror(errno));
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    overlay_teardown(run_dir);
    util_rm_rf(run_dir);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}
