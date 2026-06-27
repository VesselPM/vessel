#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/limits.h>
#define VESSEL_MAX_ARCH 4
#define VESSEL_MAX_CUSTOM 16
#define VESSEL_MAX_VESSEL_DEPS 16
#define VESSEL_MAX_SYSTEM_DEPS 32
#define VESSEL_MAX_BIN 64
#define VESSEL_MAX_DESKTOP 16
#define VESSEL_MAX_DEVICES 16
#define VESSEL_MAX_IPC 25
#define VESSEL_MAX_PATH 512
#define VESSEL_MAX_ID 256
#define VESSEL_MAX_VERSION 64
#define VESSEL_MAX_BIN_NAME 64
#define VESSEL_MAX_REPOS 32
#define VESSEL_MAX_REPO_NAME 64
#define VESSEL_MAX_URL 1024
extern char vessel_home[VESSEL_MAX_PATH];
extern char vessel_packages_dir[VESSEL_MAX_PATH];
extern char vessel_run_dir[VESSEL_MAX_PATH];
extern char vessel_shims_dir[VESSEL_MAX_PATH];
extern char vessel_db_path[VESSEL_MAX_PATH];
extern char vessel_repos_path[VESSEL_MAX_PATH];
extern char vessel_cache_dir[VESSEL_MAX_PATH];
#define VESSEL_PACKAGES vessel_packages_dir
#define VESSEL_RUN vessel_run_dir
#define VESSEL_SHIMS vessel_shims_dir
#define VESSEL_DB_FILE vessel_db_path
typedef struct {
    char from[VESSEL_MAX_PATH];
    char to[VESSEL_MAX_PATH];
    char mode[8];
} vessel_mount_t;
typedef struct {
    vessel_mount_t mounts[VESSEL_MAX_CUSTOM];
    int count;
} vessel_mount_list_t;
typedef struct {
    int64_t vessel_version;
    char id[VESSEL_MAX_ID];
    char version[VESSEL_MAX_VERSION];
    char arch[VESSEL_MAX_ARCH][16];
    int arch_count;
    char entry[VESSEL_MAX_PATH];
} vessel_name_t;
typedef struct {
    vessel_mount_t orig_root;
    vessel_mount_t home;
    vessel_mount_t var;
    vessel_mount_t custom[VESSEL_MAX_CUSTOM];
    int custom_count;
} vessel_fs_t;
typedef struct {
    char vessels[VESSEL_MAX_VESSEL_DEPS][VESSEL_MAX_ID];
    int vessel_count;
    char system[VESSEL_MAX_SYSTEM_DEPS][VESSEL_MAX_PATH];
    int system_count;
} vessel_deps_t;
typedef struct {
    char bin[VESSEL_MAX_BIN][VESSEL_MAX_BIN_NAME];
    int bin_count;
    char desktop[VESSEL_MAX_DESKTOP][VESSEL_MAX_PATH];
    int desktop_count;
} vessel_expose_t;
typedef struct {
    bool network;
    bool gpu;
    bool audio;
    char devices[VESSEL_MAX_DEVICES][64];
    int device_count;
    vessel_mount_t ipc[VESSEL_MAX_IPC];
    int ipc_count;
} vessel_permissions_t;
typedef struct {
    vessel_name_t name;
    vessel_fs_t fs;
    vessel_deps_t deps;
    vessel_expose_t expose;
    vessel_permissions_t permissions;
} vessel_manifest_t;

int manifest_load(const char *path, vessel_manifest_t *m);
int manifest_load_from_buf(const char *buf, size_t len, vessel_manifest_t *m);
int manifest_save_db(const vessel_manifest_t *m);
int manifest_load_db(const char *id, vessel_manifest_t *m);
int manifest_remove_db_entry(const char *id);
int archive_extract(const char *ves_path, const char *dest);
int archive_list(const char *ves_path);
int archive_read_manifest(const char *ves_path, char **out, size_t *out_len);
int namespace_enter(const vessel_manifest_t *m, const char *pkg_dir, const char *run_dir);
int namespace_leave(void);
int overlay_teardown(const char *run_dir);
int desktop_install(const vessel_manifest_t *m, const char *pkg_dir);
int desktop_remove(const vessel_manifest_t *m, const char *pkg_dir);
int cmd_install(int argc, char **argv);
int cmd_remove(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_info(int argc, char **argv);
int cmd_run(int argc, char **argv);
int cmd_shim(int argc, char **argv);
int cmd_tui(int argc, char **argv);
int cmd_search(int argc, char **argv);
int cmd_repo(int argc, char **argv);
int cmd_update(int argc, char **argv);
int cmd_upgrade(int argc, char **argv);
int util_mkdir_p(const char *path, mode_t mode);
int util_rm_rf(const char *path);
int util_copy_file(const char *src, const char *dst);
int util_symlink(const char *target, const char *linkpath);
char *util_home(void);
int util_running_as_root(void);
int install_ves_from_file(const char *ves_path, bool resolve_deps);
int ensure_dirs(void);
void repo_refresh_all(void);
int repo_list_load(char names[][VESSEL_MAX_REPO_NAME], char urls[][VESSEL_MAX_URL], int *count);
int repo_list_save(const char names[][VESSEL_MAX_REPO_NAME], const char urls[][VESSEL_MAX_URL], int count);
int repo_fetch_index(const char *repo_url, const char *cache_path);
int repo_find_package(const char *id, char *version, size_t ver_sz, char *url, size_t url_sz, char *repo_name, size_t name_sz);
int repo_download(const char *url, const char *dest);
int repo_get_deps(const char *id, char deps[][VESSEL_MAX_ID], int *count);
