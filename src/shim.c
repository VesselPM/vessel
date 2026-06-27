#include "vessel.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int cmd_shim(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel shim <id> [bin_name]\n");
        return 1;
    }

    if (!util_running_as_root()) {
        fprintf(stderr, "error: shim requires root privileges\n");
        return 1;
    }

    const char *id = argv[2];
    char bin_name[VESSEL_MAX_BIN_NAME] = "";
    if (argc >= 4) {
        strncpy(bin_name, argv[3], sizeof(bin_name)-1);
    } else {
        const char *last = strrchr(id, '.');
        if (last) strncpy(bin_name, last + 1, sizeof(bin_name)-1); else strncpy(bin_name, id, sizeof(bin_name)-1);
    }

    char pkg_dir[VESSEL_MAX_PATH];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", VESSEL_PACKAGES, id);
    struct stat st;
    if (stat(pkg_dir, &st) != 0) {
        fprintf(stderr, "error: package %s not installed\n", id);
        return 1;
    }

    char shim_dir[VESSEL_MAX_PATH];
    snprintf(shim_dir, sizeof(shim_dir), "%s/%s", VESSEL_SHIMS, id);
    util_mkdir_p(shim_dir, 0755);
    char shim_path[VESSEL_MAX_PATH];
    snprintf(shim_path, sizeof(shim_path), "%s/%s", shim_dir, bin_name);
    FILE *f = fopen(shim_path, "w");
    if (!f) {
        fprintf(stderr, "error: could not create shim %s\n", shim_path);
        return 1;
    }

    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "exec vessel-run %s \"$@\"\n", id);
    fclose(f);
    chmod(shim_path, 0755);
    char link_path[VESSEL_MAX_PATH];
    snprintf(link_path, sizeof(link_path), "/usr/bin/%s", bin_name);
    unlink(link_path);
    if (symlink(shim_path, link_path) != 0) {
        fprintf(stderr, "warning: could not symlink %s -> %s\n", link_path, shim_path);
    }

    printf("shim generated: /usr/bin/%s -> vessel-run %s\n", bin_name, id);
    return 0;
}
