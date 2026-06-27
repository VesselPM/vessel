#include "vessel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
char vessel_home[VESSEL_MAX_PATH];
char vessel_packages_dir[VESSEL_MAX_PATH];
char vessel_run_dir[VESSEL_MAX_PATH];
char vessel_shims_dir[VESSEL_MAX_PATH];
char vessel_db_path[VESSEL_MAX_PATH];
char vessel_repos_path[VESSEL_MAX_PATH];
char vessel_cache_dir[VESSEL_MAX_PATH];
static void init_paths(void) {
    const char *vh = getenv("VESSEL_HOME");
    if (!vh) {
        const char *home = util_home();
        snprintf(vessel_home, sizeof(vessel_home), "%s/.local/share/vessel", home);
    } else {
        strncpy(vessel_home, vh, sizeof(vessel_home)-1);
    }
    snprintf(vessel_packages_dir, sizeof(vessel_packages_dir), "%s/packages", vessel_home);
    snprintf(vessel_run_dir, sizeof(vessel_run_dir), "%s/run", vessel_home);
    snprintf(vessel_shims_dir, sizeof(vessel_shims_dir), "%s/shims", vessel_home);
    snprintf(vessel_db_path, sizeof(vessel_db_path), "%s/db.toml", vessel_home);
    snprintf(vessel_repos_path, sizeof(vessel_repos_path), "%s/repos.toml", vessel_home);
    snprintf(vessel_cache_dir, sizeof(vessel_cache_dir), "%s/cache", vessel_home);
}

static void print_usage(void) {
    printf("Vessel v%s - almost-sandboxed package runtime\n\n", VESSEL_VERSION);
    printf("usage:\n");
    printf("  vessel install <file.ves | pkg>   Install a package (local or from repo)\n");
    printf("  vessel remove <id>                Remove a package\n");
    printf("  vessel list                       List installed packages\n");
    printf("  vessel info <id>                  Show package info\n");
    printf("  vessel run <id> [args...]         Run a package\n");
    printf("  vessel shim <id> [name]           Generate shim for a package\n");
    printf("  vessel search <query>             Search packages in repos\n");
    printf("  vessel repo list                  List configured repos\n");
    printf("  vessel repo add <name> <url>      Add a repository\n");
    printf("  vessel repo remove <name>         Remove a repository\n");
    printf("  vessel update                     Check for updates\n");
    printf("  vessel upgrade [pkg...]           Upgrade packages\n");
    printf("  vessel tui                        Open the TUI\n");
    printf("  vessel-run <id> [args...]         Run a package (shorthand)\n");
}

int cmd_list(int argc, char **argv) {
    (void)argc;
    (void)argv;
    DIR *d = opendir(VESSEL_PACKAGES);
    if (!d) {
        printf("no packages installed\n");
        return 0;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char manifest_path[VESSEL_MAX_PATH];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.toml", VESSEL_PACKAGES, ent->d_name);
        vessel_manifest_t m;
        char version[64] = "?";
        if (manifest_load(manifest_path, &m) == 0) strncpy(version, m.name.version, sizeof(version)-1);
        else {
            if (manifest_load_db(ent->d_name, &m) == 0) strncpy(version, m.name.version, sizeof(version)-1);
        }
        printf("%-30s %s\n", ent->d_name, version);
        count++;
    }
    closedir(d);
    if (count == 0) printf("no packages installed\n");
    return 0;
}

int cmd_info(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel info <id>\n");
        return 1;
    }

    const char *id = argv[2];
    char manifest_path[VESSEL_MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.toml", VESSEL_PACKAGES, id);
    vessel_manifest_t m;
    if (manifest_load(manifest_path, &m) != 0) {
        fprintf(stderr, "error: package %s not found\n", id);
        return 1;
    }

    printf("ID:      %s\n", m.name.id);
    printf("Version: %s\n", m.name.version);
    printf("Entry:   %s\n", m.name.entry);
    printf("Arch:    ");
    for (int i = 0; i < m.name.arch_count; i++)
        printf("%s ", m.name.arch[i]);
    printf("\n");
    printf("\nPermissions:\n");
    printf("  Network: %s\n", m.permissions.network ? "yes" : "no");
    printf("  GPU:     %s\n", m.permissions.gpu ? "yes" : "no");
    printf("  Audio:   %s\n", m.permissions.audio ? "yes" : "no");
    if (m.expose.bin_count > 0) {
        printf("\nExposed binaries:\n");
        for (int i = 0; i < m.expose.bin_count; i++)
            printf("  - %s\n", m.expose.bin[i]);
    }

    if (m.expose.desktop_count > 0) {
        printf("\nDesktop files:\n");
        for (int i = 0; i < m.expose.desktop_count; i++)
            printf("  - %s\n", m.expose.desktop[i]);
    }

    if (m.deps.vessel_count > 0) {
        printf("\nVessel dependencies:\n");
        for (int i = 0; i < m.deps.vessel_count; i++)
            printf("  - %s\n", m.deps.vessels[i]);
    }

    if (m.deps.system_count > 0) {
        printf("\nSystem dependencies:\n");
        for (int i = 0; i < m.deps.system_count; i++)
            printf("  - %s\n", m.deps.system[i]);
    }

    if (m.fs.orig_root.from[0])
        printf("\nHost root: %s -> %s (%s)\n", m.fs.orig_root.from, m.fs.orig_root.to, m.fs.orig_root.mode);
    if (m.fs.home.from[0])
        printf("Home:     %s -> %s (%s)\n", m.fs.home.from, m.fs.home.to, m.fs.home.mode);

    return 0;
}

int main(int argc, char **argv) {
    init_paths();
    if (argc < 2) {
        const char *prog = strrchr(argv[0], '/');
        if (prog) prog++;
        else prog = argv[0];
        if (strcmp(prog, "vessel-run") == 0) return cmd_run(argc, argv);
        print_usage();
        return 0;
    }

    const char *cmd = argv[1];
    const char *prog = strrchr(argv[0], '/');
    if (prog) prog++;
    else prog = argv[0];
    if (strcmp(prog, "vessel-run") == 0 || strcmp(cmd, "run") == 0) return cmd_run(argc, argv);
    if (strcmp(cmd, "install") == 0) return cmd_install(argc, argv);
    if (strcmp(cmd, "remove") == 0) return cmd_remove(argc, argv);
    if (strcmp(cmd, "list") == 0) return cmd_list(argc, argv);
    if (strcmp(cmd, "info") == 0) return cmd_info(argc, argv);
    if (strcmp(cmd, "shim") == 0) return cmd_shim(argc, argv);
    if (strcmp(cmd, "tui") == 0) return cmd_tui(argc, argv);
    if (strcmp(cmd, "search") == 0) return cmd_search(argc, argv);
    if (strcmp(cmd, "repo") == 0) return cmd_repo(argc, argv);
    if (strcmp(cmd, "update") == 0) return cmd_update(argc, argv);
    if (strcmp(cmd, "upgrade") == 0) return cmd_upgrade(argc, argv);
    fprintf(stderr, "unknown command: %s\n", cmd);
    print_usage();
    return 1;
}
