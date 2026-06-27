#include "vessel.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
int overlay_teardown(const char *run_dir) {
    char merged[VESSEL_MAX_PATH];
    snprintf(merged, sizeof(merged), "%s/merged", run_dir);
    umount2(merged, MNT_DETACH);
    char upperdir[VESSEL_MAX_PATH];
    snprintf(upperdir, sizeof(upperdir), "%s/upper", run_dir);
    char workdir[VESSEL_MAX_PATH];
    snprintf(workdir, sizeof(workdir), "%s/work", run_dir);
    rmdir(merged);
    rmdir(upperdir);
    rmdir(workdir);
    return 0;
}
