#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
int util_mkdir_p(const char *path, mode_t mode) {
    char tmp[VESSEL_MAX_PATH];
    strncpy(tmp, path, sizeof(tmp)-1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

int util_rm_rf(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        if (errno == ENOTDIR) {
            unlink(path);
            return 0;
        }
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[VESSEL_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) util_rm_rf(full); else unlink(full);
    }

    closedir(d);
    rmdir(path);
    return 0;
}

int util_copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    struct stat st;
    fstat(sfd, &st);
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (dfd < 0) { close(sfd); return -1; }
    char buf[65536];
    ssize_t nread;
    while ((nread = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < nread) {
            ssize_t n = write(dfd, buf + written, (size_t)(nread - written));
            if (n < 0) { close(sfd); close(dfd); return -1; }
            written += n;
        }
    }

    close(sfd);
    close(dfd);
    return 0;
}

int util_symlink(const char *target, const char *linkpath) {
    unlink(linkpath);
    return symlink(target, linkpath);
}

char *util_home(void) {
    char *home = getenv("HOME");
    if (!home) return "/root";
    return home;
}

int util_running_as_root(void) {
    return geteuid() == 0;
}

int ensure_dirs(void) {
    util_mkdir_p(vessel_home, 0755);
    util_mkdir_p(VESSEL_PACKAGES, 0755);
    util_mkdir_p(VESSEL_RUN, 0755);
    util_mkdir_p(VESSEL_SHIMS, 0755);
    util_mkdir_p(vessel_cache_dir, 0755);
    return 0;
}

static int install_repo_package(const char *id);
int install_ves_from_file(const char *ves_path, bool resolve_deps) {
    char *manifest_buf = nullptr;
    size_t manifest_len = 0;
    if (archive_read_manifest(ves_path, &manifest_buf, &manifest_len) != 0) {
        fprintf(stderr, "error: could not read manifest from %s\n", ves_path);
        return 1;
    }

    vessel_manifest_t m;
    memset(&m, 0, sizeof(m));
    if (manifest_load_from_buf(manifest_buf, manifest_len, &m) != 0) {
        free(manifest_buf);
        fprintf(stderr, "error: could not parse manifest\n");
        return 1;
    }

    free(manifest_buf);
    if (!m.name.id[0]) {
        fprintf(stderr, "error: manifest missing package id\n");
        return 1;
    }

    if (resolve_deps) {
        for (int i = 0; i < m.deps.vessel_count; i++) {
            char dep_path[VESSEL_MAX_PATH];
            snprintf(dep_path, sizeof(dep_path), "%s/%s", VESSEL_PACKAGES, m.deps.vessels[i]);
            struct stat dep_st;
            if (stat(dep_path, &dep_st) == 0) continue;
            printf("installing dependency %s...\n", m.deps.vessels[i]);
            if (install_repo_package(m.deps.vessels[i]) != 0) fprintf(stderr, "warning: could not install dependency %s\n", m.deps.vessels[i]);
        }
    }

    char pkg_dir[VESSEL_MAX_PATH];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", VESSEL_PACKAGES, m.name.id);
    struct stat st;
    if (stat(pkg_dir, &st) == 0) {
        fprintf(stderr, "error: package %s already installed\n", m.name.id);
        return 1;
    }

    printf("installing %s v%s...\n", m.name.id, m.name.version);
    if (archive_extract(ves_path, pkg_dir) != 0) {
        util_rm_rf(pkg_dir);
        fprintf(stderr, "error: could not extract %s\n", ves_path);
        return 1;
    }

    char extracted_manifest[VESSEL_MAX_PATH];
    snprintf(extracted_manifest, sizeof(extracted_manifest), "%s/manifest.toml", pkg_dir);
    vessel_manifest_t m2;
    if (manifest_load(extracted_manifest, &m2) != 0) {
        util_rm_rf(pkg_dir);
        fprintf(stderr, "error: invalid manifest in package\n");
        return 1;
    }

    manifest_save_db(&m2);
    desktop_install(&m2, pkg_dir);
    printf("installed %s v%s\n", m2.name.id, m2.name.version);
    return 0;
}

static int install_repo_package(const char *id) {
    repo_refresh_all();
    char version[VESSEL_MAX_VERSION];
    char url[VESSEL_MAX_URL];
    char repo_name[VESSEL_MAX_REPO_NAME];
    if (repo_find_package(id, version, sizeof(version), url, sizeof(url), repo_name, sizeof(repo_name)) != 0) return -1;
    char tmp_path[VESSEL_MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.ves", vessel_cache_dir, id);
    if (repo_download(url, tmp_path) != 0) return -1;
    int ret = install_ves_from_file(tmp_path, true);
    unlink(tmp_path);
    return ret;
}

int cmd_install(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel install <file.ves | package-id>\n");
        return 1;
    }

    ensure_dirs();
    const char *target = argv[2];
    struct stat st;
    if (stat(target, &st) == 0) return install_ves_from_file(target, true);
    return install_repo_package(target);
}
