#include "vessel.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
int desktop_install(const vessel_manifest_t *m, const char *pkg_dir) {
    char desktop_dir[VESSEL_MAX_PATH];
    snprintf(desktop_dir, sizeof(desktop_dir), "%s/share/applications", pkg_dir);
    struct stat st;
    if (stat(desktop_dir, &st) != 0) return 0;
    for (int i = 0; i < m->expose.desktop_count; i++) {
        char src[VESSEL_MAX_PATH];
        snprintf(src, sizeof(src), "%s/%s", pkg_dir, m->expose.desktop[i]);
        if (stat(src, &st) != 0) continue;
        const char *base = strrchr(m->expose.desktop[i], '/');
        if (base) base++;
        else base = m->expose.desktop[i];
        char dst[VESSEL_MAX_PATH];
        snprintf(dst, sizeof(dst), "/usr/share/applications/%s", base);
        if (stat(dst, &st) == 0) unlink(dst);
        if (symlink(src, dst) != 0) {
            if (errno == EACCES || errno == EPERM)
                util_copy_file(src, dst);
        }

        if (stat(dst, &st) == 0) {
            char deployed_flag[VESSEL_MAX_PATH];
            snprintf(deployed_flag, sizeof(deployed_flag),
                     "%s/.deployed_%s", pkg_dir, base);
            FILE *f = fopen(deployed_flag, "w");
            if (f) {
                fprintf(f, "%s\n", dst);
                fclose(f);
            }
        }
    }

    return 0;
}

int desktop_remove(const vessel_manifest_t *m, const char *pkg_dir) {
    for (int i = 0; i < m->expose.desktop_count; i++) {
        const char *base = strrchr(m->expose.desktop[i], '/');
        if (base) base++;
        else base = m->expose.desktop[i];
        char deployed_flag[VESSEL_MAX_PATH];
        snprintf(deployed_flag, sizeof(deployed_flag),
                 "%s/.deployed_%s", pkg_dir, base);

        FILE *f = fopen(deployed_flag, "r");
        if (f) {
            char dst[VESSEL_MAX_PATH];
            if (fgets(dst, sizeof(dst), f)) {
                dst[strcspn(dst, "\n")] = 0;
                unlink(dst);
            }
            fclose(f);
            unlink(deployed_flag);
        } else {
            char dst[VESSEL_MAX_PATH];
            snprintf(dst, sizeof(dst), "/usr/share/applications/%s", base);
            unlink(dst);
        }
    }

    return 0;
}
