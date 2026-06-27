#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <errno.h>
#include <glob.h>
static void bind_mount_or_warn(const char *src, const char *target, unsigned long flags, const char *desc) {
    if (mount(src, target, NULL, flags, NULL) != 0)
        fprintf(stderr, "warning: could not mount %s: %s\n", desc, strerror(errno));
    if (flags & MS_RDONLY)
        mount("none", target, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL);
}

static int create_target_for_source(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "warning: source %s not found, skipping mount\n", src);
        return -1;
    }
    char parent[VESSEL_MAX_PATH];
    strncpy(parent, dst, sizeof(parent)-1);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        util_mkdir_p(parent, 0755);
    }
    struct stat dst_st;
    if (stat(dst, &dst_st) == 0) {
        if (S_ISDIR(dst_st.st_mode)) rmdir(dst);
        else unlink(dst);
    }
    if (S_ISDIR(st.st_mode))
        mkdir(dst, st.st_mode & 0777);
    else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
        mknod(dst, st.st_mode, st.st_rdev);
    else {
        int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
    }
    return 0;
}

static int has_glob_chars(const char *s) {
    return strchr(s, '*') != NULL || strchr(s, '?') != NULL || strchr(s, '[') != NULL;
}

static void expand_ipc_env(char *out, size_t outsz, const char *in) {
    if (in[0] == '$') {
        const char *p = in + 1;
        const char *slash = strchr(p, '/');
        size_t varlen = slash ? (size_t)(slash - p) : strlen(p);
        size_t copy_len = varlen < 255 ? varlen : 255;
        char varname[256];
        memcpy(varname, p, copy_len);
        varname[copy_len] = '\0';
        const char *val = getenv(varname);
        if (val) {
            snprintf(out, outsz, "%s%s", val, slash ? slash : "");
        } else {
            fprintf(stderr, "warning: environment variable %s not set, skipping IPC mount\n", varname);
            out[0] = '\0';
        }
    } else {
        snprintf(out, outsz, "%s", in);
    }
}

int namespace_enter(const vessel_manifest_t *m, const char *pkg_dir, const char *run_dir) {
    int flags = CLONE_NEWNS | CLONE_NEWUTS;
    if (!m->permissions.network) flags |= CLONE_NEWNET;
    if (unshare(flags) != 0) {
        fprintf(stderr, "error: unshare(0x%x) failed: %s\n", flags, strerror(errno));
        return -1;
    }

    if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        fprintf(stderr, "error: making root mount private failed: %s\n", strerror(errno));
        return -1;
    }

    char upperdir[VESSEL_MAX_PATH];
    snprintf(upperdir, sizeof(upperdir), "%s/upper", run_dir);
    char workdir[VESSEL_MAX_PATH];
    snprintf(workdir, sizeof(workdir), "%s/work", run_dir);
    char merge_target[VESSEL_MAX_PATH];
    snprintf(merge_target, sizeof(merge_target), "%s/merged", run_dir);
    mkdir(upperdir, 0755);
    mkdir(workdir, 0755);
    mkdir(merge_target, 0755);
    if (access(pkg_dir, F_OK) != 0) {
        fprintf(stderr, "error: package directory %s not found: %s\n", pkg_dir, strerror(errno));
        return -1;
    }

    char all_deps[VESSEL_MAX_VESSEL_DEPS][VESSEL_MAX_ID];
    int all_dep_count = 0;
    for (int i = 0; i < m->deps.vessel_count && all_dep_count < VESSEL_MAX_VESSEL_DEPS; i++) {
        int already = 0;
        for (int j = 0; j < all_dep_count; j++)
            if (strcmp(all_deps[j], m->deps.vessels[i]) == 0) { already = 1; break; }
        if (!already) strncpy(all_deps[all_dep_count++], m->deps.vessels[i], VESSEL_MAX_ID-1);
    }
    for (int i = 0; i < all_dep_count; i++) {
        char dep_mf[VESSEL_MAX_PATH];
        snprintf(dep_mf, sizeof(dep_mf), "%s/%s/manifest.toml", VESSEL_PACKAGES, all_deps[i]);
        vessel_manifest_t dm;
        if (manifest_load(dep_mf, &dm) == 0) {
            for (int j = 0; j < dm.deps.vessel_count && all_dep_count < VESSEL_MAX_VESSEL_DEPS; j++) {
                int already = 0;
                for (int k = 0; k < all_dep_count; k++)
                    if (strcmp(all_deps[k], dm.deps.vessels[j]) == 0) { already = 1; break; }
                if (!already) strncpy(all_deps[all_dep_count++], dm.deps.vessels[j], VESSEL_MAX_ID-1);
            }
        }
    }
    char lowerdirs[4096];
    snprintf(lowerdirs, sizeof(lowerdirs), "%s", pkg_dir);
    for (int i = 0; i < all_dep_count; i++) {
        char dep_dir[VESSEL_MAX_PATH];
        snprintf(dep_dir, sizeof(dep_dir), "%s/%s", VESSEL_PACKAGES, all_deps[i]);
        struct stat dep_st;
        if (stat(dep_dir, &dep_st) == 0) {
            size_t l = strlen(lowerdirs);
            snprintf(lowerdirs + l, sizeof(lowerdirs) - l, ":%s", dep_dir);
        }
    }
    strncat(lowerdirs, ":/", sizeof(lowerdirs) - strlen(lowerdirs) - 1);
    char opts[4096];
    snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s", lowerdirs, upperdir, workdir);
    if (mount("overlay", merge_target, "overlay", 0, opts) != 0) {
        fprintf(stderr, "error: overlay mount failed (%s): %s\n", opts, strerror(errno));
        return -1;
    }
    {
        const char *needed[][2] = { //neo: i guess bro
            {"/bin/sh", "/usr/bin/sh"},
            {"/bin/env", "/usr/bin/env"},
            {"/bin/bash", "/usr/bin/bash"},
        };
        for (size_t i = 0; i < sizeof(needed)/sizeof(needed[0]); i++) {
            char bp[VESSEL_MAX_PATH];
            snprintf(bp, sizeof(bp), "%s%s", merge_target, needed[i][0]);
            struct stat bst;
            if (stat(bp, &bst) != 0) {
                char *slash = strrchr(bp, '/');
                if (slash) { *slash = 0; mkdir(bp, 0755); *slash = '/'; }
                symlink(needed[i][1], bp);
            }
        }
    }

    for (int i = 0; i < m->deps.system_count; i++) {
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, m->deps.system[i]);
        char *slash = strrchr(target, '/');
        if (slash) {
            *slash = 0;
            mkdir(target, 0755);
            *slash = '/';
        }
        struct stat dep_st;
        if (stat(m->deps.system[i], &dep_st) == 0)
            bind_mount_or_warn(m->deps.system[i], target,
                MS_BIND | (S_ISDIR(dep_st.st_mode) ? MS_REC : 0),
                m->deps.system[i]);
    }
    {
        char devp[VESSEL_MAX_PATH];
        snprintf(devp, sizeof(devp), "%s/dev", merge_target);
        mkdir(devp, 0755);
        mount("tmpfs", devp, "tmpfs", 0, "size=32M");
        struct { const char *name; mode_t mode; dev_t dev; } devs[] = {
            { "/null",    S_IFCHR | 0666, makedev(1, 3) },
            { "/zero",    S_IFCHR | 0666, makedev(1, 5) },
            { "/random",  S_IFCHR | 0666, makedev(1, 8) },
            { "/urandom", S_IFCHR | 0666, makedev(1, 9) },
            { "/full",    S_IFCHR | 0666, makedev(1, 7) },
            { "/tty",     S_IFCHR | 0666, makedev(5, 0) },
            { "/console", S_IFCHR | 0600, makedev(5, 1) },
            { "/ptmx",    S_IFCHR | 0666, makedev(5, 2) },
        };
        for (size_t i = 0; i < sizeof(devs)/sizeof(devs[0]); i++) {
            char path[VESSEL_MAX_PATH];
            snprintf(path, sizeof(path), "%s%s", devp, devs[i].name);
            mknod(path, devs[i].mode, devs[i].dev);
        }

        char dpath[VESSEL_MAX_PATH];
        snprintf(dpath, sizeof(dpath), "%s/dev/pts", merge_target);
        mkdir(dpath, 0755);
        mount("devpts", dpath, "devpts", 0, "mode=0620,ptmxmode=666");
        snprintf(dpath, sizeof(dpath), "%s/dev/shm", merge_target);
        mkdir(dpath, 0777);
        mount("tmpfs", dpath, "tmpfs", 0, "size=64M");
        snprintf(dpath, sizeof(dpath), "%s/tmp", merge_target);
        mkdir(dpath, 0777);
        mount("tmpfs", dpath, "tmpfs", 0, "size=256M");
        snprintf(dpath, sizeof(dpath), "%s/proc", merge_target);
        mkdir(dpath, 0755);
        mount("proc", dpath, "proc", 0, NULL);
        snprintf(dpath, sizeof(dpath), "%s/sys", merge_target);
        mkdir(dpath, 0755);
        mount("sysfs", dpath, "sysfs", 0, NULL);
        snprintf(dpath, sizeof(dpath), "%s/run", merge_target);
        mkdir(dpath, 0755);
        mount("tmpfs", dpath, "tmpfs", 0, "size=64M");
    }
    const char *hide_root_paths[] = {
        "/home", "/root", "/mnt", "/media", "/lost+found", "/srv"
    };
    for (size_t i = 0; i < sizeof(hide_root_paths)/sizeof(hide_root_paths[0]); i++) {
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, hide_root_paths[i]);
        util_mkdir_p(target, 0755);
        if (mount("tmpfs", target, "tmpfs", 0, "size=1M") != 0)
            fprintf(stderr, "warning: could not hide %s: %s\n", hide_root_paths[i], strerror(errno));
    }

    if (m->fs.orig_root.from[0]) {
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, m->fs.orig_root.to);
        mkdir(target, 0755);
        unsigned long mflags = MS_BIND | MS_REC;
        if (strcmp(m->fs.orig_root.mode, "ro") == 0) mflags |= MS_RDONLY;
        bind_mount_or_warn(m->fs.orig_root.from, target, mflags, m->fs.orig_root.from);
    }

    if (m->fs.home.from[0]) {
        char resolved_from[VESSEL_MAX_PATH];
        char *home_env = getenv("HOME");
        if (!home_env) home_env = "/root";
        const char *from = m->fs.home.from;
        if (from[0] == '$' && strcmp(from, "$HOME") == 0) strncpy(resolved_from, home_env, sizeof(resolved_from)-1);else strncpy(resolved_from, from, sizeof(resolved_from)-1);
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, m->fs.home.to);
        mkdir(target, 0755);
        unsigned long mflags = MS_BIND | MS_REC;
        if (strcmp(m->fs.home.mode, "ro") == 0) mflags |= MS_RDONLY;
        bind_mount_or_warn(resolved_from, target, mflags, resolved_from);
    }

    if (m->fs.var.from[0]) {
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, m->fs.var.to);
        mkdir(target, 0755);
        mount("tmpfs", target, "tmpfs", 0, "size=256M");
    }

    for (int i = 0; i < m->fs.custom_count; i++) {
        char target[VESSEL_MAX_PATH];
        snprintf(target, sizeof(target), "%s%s", merge_target, m->fs.custom[i].to);
        mkdir(target, 0755);
        unsigned long mflags = MS_BIND | MS_REC;
        if (strcmp(m->fs.custom[i].mode, "ro") == 0) mflags |= MS_RDONLY;
        bind_mount_or_warn(m->fs.custom[i].from, target, mflags, m->fs.custom[i].from);
    }
    {
        int var_mounted = m->fs.var.from[0];
        if (!var_mounted) for (int i = 0; i < m->fs.custom_count; i++) if (strcmp(m->fs.custom[i].to, "/var") == 0) { var_mounted = 1; break; }
        if (var_mounted) {
            char target[VESSEL_MAX_PATH];
            snprintf(target, sizeof(target), "%s/var/lib/vessel", merge_target);
            util_mkdir_p(target, 0755);
            mount("tmpfs", target, "tmpfs", 0, "size=1M");
        }
    }

    if (!getenv("XDG_RUNTIME_DIR")) {
        const char *sudo_uid = getenv("SUDO_UID");
        if (sudo_uid) {
            char fallback[64];
            snprintf(fallback, sizeof(fallback), "/run/user/%s", sudo_uid);
            setenv("XDG_RUNTIME_DIR", fallback, 1);
        }
    }

    {
        const char *xrd = getenv("XDG_RUNTIME_DIR");
        if (xrd) {
            char target[VESSEL_MAX_PATH];
            snprintf(target, sizeof(target), "%s%s", merge_target, xrd);
            util_mkdir_p(target, 0700);
        }
    }

    for (int i = 0; i < m->permissions.ipc_count; i++) {
        char from_buf[VESSEL_MAX_PATH];
        expand_ipc_env(from_buf, sizeof(from_buf), m->permissions.ipc[i].from);
        char to_buf[VESSEL_MAX_PATH];
        expand_ipc_env(to_buf, sizeof(to_buf), m->permissions.ipc[i].to);
        if (has_glob_chars(from_buf)) {
            glob_t g;
            if (glob(from_buf, 0, NULL, &g) == 0) {
                const char *from_star = strchr(from_buf, '*');
                if (from_star) {
                    size_t from_pre_len = from_star - from_buf;
                    const char *from_suf = from_star + 1;
                    size_t from_suf_len = strlen(from_suf);
                    for (size_t j = 0; j < g.gl_pathc; j++) {
                        const char *match = g.gl_pathv[j];
                        size_t mlen = strlen(match);
                        if (mlen < from_pre_len + from_suf_len) continue;
                        size_t mid_len = mlen - from_pre_len - from_suf_len;
                        const char *mid = match + from_pre_len;
                        char to_glob[VESSEL_MAX_PATH];
                        const char *to_star = strchr(to_buf, '*');
                        if (to_star) {
                            size_t to_pre_len = to_star - to_buf;
                            snprintf(to_glob, sizeof(to_glob), "%.*s%.*s%s",
                                     (int)to_pre_len, to_buf,
                                     (int)mid_len, mid, to_star + 1);
                        } else {
                            snprintf(to_glob, sizeof(to_glob), "%s", to_buf);
                        }
                        char target[VESSEL_MAX_PATH];
                        snprintf(target, sizeof(target), "%s%s", merge_target, to_glob);
                        if (create_target_for_source(match, target) == 0)
                            bind_mount_or_warn(match, target, MS_BIND, match);
                    }
                }
                globfree(&g);
            }
        } else {
            char target[VESSEL_MAX_PATH];
            snprintf(target, sizeof(target), "%s%s", merge_target, to_buf);
            if (create_target_for_source(from_buf, target) == 0)
                bind_mount_or_warn(from_buf, target, MS_BIND, from_buf);
        }
    }

    if (m->permissions.gpu) {
        char dpath[VESSEL_MAX_PATH];
        snprintf(dpath, sizeof(dpath), "%s/dev/dri", merge_target);
        mkdir(dpath, 0755);
        bind_mount_or_warn("/dev/dri", dpath, MS_BIND, "/dev/dri");
    }

    if (m->permissions.audio) { //neo: yes it doesnt include pipewire nor pulseaudio. use the IPC permission.
        char dpath[VESSEL_MAX_PATH];
        snprintf(dpath, sizeof(dpath), "%s/dev/snd", merge_target);
        mkdir(dpath, 0755);
        bind_mount_or_warn("/dev/snd", dpath, MS_BIND, "/dev/snd");
    }

    for (int i = 0; i < m->permissions.device_count; i++) {
        const char *dev = m->permissions.devices[i];
        const char *name = strchr(dev, ':');
        if (name) name++; else name = dev;
        char pattern[VESSEL_MAX_PATH];
        snprintf(pattern, sizeof(pattern), "/dev/%s", name);
        if (has_glob_chars(name)) {
            glob_t g;
            if (glob(pattern, 0, NULL, &g) == 0) {
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    const char *src = g.gl_pathv[j];
                    const char *rel = src + 5;
                    char dst[VESSEL_MAX_PATH];
                    snprintf(dst, sizeof(dst), "%s/dev/%s", merge_target, rel);
                    struct stat dev_st;
                    unsigned long mf = MS_BIND;
                    if (stat(src, &dev_st) == 0 && S_ISDIR(dev_st.st_mode))
                        mf |= MS_REC;
                    if (create_target_for_source(src, dst) == 0)
                        bind_mount_or_warn(src, dst, mf, src);
                }
                globfree(&g);
            }
        } else {
            char dst[VESSEL_MAX_PATH];
            snprintf(dst, sizeof(dst), "%s/dev/%s", merge_target, name);
            struct stat dev_st;
            unsigned long mf = MS_BIND;
            if (stat(pattern, &dev_st) == 0 && S_ISDIR(dev_st.st_mode))
                mf |= MS_REC;
            if (create_target_for_source(pattern, dst) == 0)
                bind_mount_or_warn(pattern, dst, mf, pattern);
        }
    }

    if (chroot(merge_target) != 0) {
        fprintf(stderr, "error: chroot to %s failed: %s\n", merge_target, strerror(errno));
        return -1;
    }

    if (chdir("/") != 0) {
        fprintf(stderr, "error: chdir to / after chroot failed: %s\n", strerror(errno));
        return -1;
    }

    setenv("HOME", "/home", 1);
    char *user = getenv("USER");
    setenv("USER", user ? user : "user", 1);
    char *xrd = getenv("XDG_RUNTIME_DIR");
    if (xrd) setenv("XDG_RUNTIME_DIR", xrd, 1);
    return 0;
}

int namespace_leave(void) {
    return 0;
}
