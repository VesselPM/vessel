#include "vessel.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int cmd_remove(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel remove <id>\n");
        return 1;
    }

    const char *id = argv[2];
    char pkg_dir[VESSEL_MAX_PATH];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", VESSEL_PACKAGES, id);
    struct stat st;
    if (stat(pkg_dir, &st) != 0) {
        fprintf(stderr, "error: package %s not installed\n", id);
        return 1;
    }

    vessel_manifest_t m;
    if (manifest_load_db(id, &m) == 0 || stat(pkg_dir, &st) == 0) if (m.name.id[0]) desktop_remove(&m, pkg_dir);
    char shim_dir[VESSEL_MAX_PATH];
    snprintf(shim_dir, sizeof(shim_dir), "%s/%s", VESSEL_SHIMS, id);
    util_rm_rf(shim_dir);
    util_rm_rf(pkg_dir);
    char shims_link[VESSEL_MAX_PATH];
    DIR *d = opendir(VESSEL_SHIMS);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            snprintf(shims_link, sizeof(shims_link), "/usr/bin/%s", ent->d_name);
            char target[VESSEL_MAX_PATH];
            ssize_t tlen = readlink(shims_link, target, sizeof(target)-1);
            if (tlen > 0) {
                target[tlen] = 0;
                if (strstr(target, id)) {
                    unlink(shims_link);
                }
            }
        }
        closedir(d);
    }

    manifest_remove_db_entry(id);
    printf("removed %s\n", id);
    return 0;
}
