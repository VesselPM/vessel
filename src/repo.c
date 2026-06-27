#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
struct dl_buf {
    char *data;
    size_t len;
};

static size_t dl_write_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    struct dl_buf *b = user;
    size_t total = size * nmemb;
    char *np = realloc(b->data, b->len + total + 1);
    if (!np) return 0;
    memcpy(np + b->len, ptr, total);
    b->data = np;
    b->len += total;
    b->data[b->len] = 0;
    return total;
}

static int http_download(const char *url, const char *dest) {
    CURL *c = curl_easy_init();
    if (!c) return -1;
    struct dl_buf b = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) {
        free(b.data);
        return -1;
    }

    FILE *f = fopen(dest, "wb");
    if (!f) { free(b.data); return -1; }
    fwrite(b.data, 1, b.len, f);
    fclose(f);
    free(b.data);
    return 0;
}

static int version_cmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long na = strtol(a, (char**)&a, 10);
            long nb = strtol(b, (char**)&b, 10);
            if (na != nb) return na < nb ? -1 : 1;
        } else {
            if (*a != *b) return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
            a++; b++;
        }
    }
    return *a ? 1 : *b ? -1 : 0;
}

int repo_list_load(char names[][VESSEL_MAX_REPO_NAME], char urls[][VESSEL_MAX_URL], int *count) {
    ensure_dirs();
    FILE *f = fopen(vessel_repos_path, "r");
    if (!f) {
        *count = 1;
        strncpy(names[0], "main", VESSEL_MAX_REPO_NAME-1);
        strncpy(urls[0], "https://files.obsidianos.xyz/~neo/vessel", VESSEL_MAX_URL-1);
        repo_list_save(names, urls, 1);
        return 0;
    }
    *count = 0;
    char cur_name[VESSEL_MAX_REPO_NAME] = "";
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        if (*p == '[') {
            p++;
            if (strncmp(p, "repos.", 6) == 0) {
                p += 6;
                char *end = strchr(p, ']');
                if (end) {
                    *end = 0;
                    strncpy(cur_name, p, sizeof(cur_name)-1);
                }
            }
            continue;
        }
        if (strncmp(p, "url", 3) == 0 && cur_name[0]) {
            p += 3;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
            char *end = strrchr(p, '"');
            if (end) *end = 0;
            if (*count < VESSEL_MAX_REPOS) {
                strncpy(names[*count], cur_name, VESSEL_MAX_REPO_NAME-1);
                strncpy(urls[*count], p, VESSEL_MAX_URL-1);
                (*count)++;
            }
            cur_name[0] = 0;
        }
    }
    fclose(f);
    return 0;
}

int repo_list_save(const char names[][VESSEL_MAX_REPO_NAME], const char urls[][VESSEL_MAX_URL], int count) {
    FILE *f = fopen(vessel_repos_path, "w");
    if (!f) return -1;
    fprintf(f, "[repos]\n");
    for (int i = 0; i < count; i++) fprintf(f, "[repos.%s]\nurl = \"%s\"\n", names[i], urls[i]);
    fclose(f);
    return 0;
}

void repo_refresh_all(void) {
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int count;
    repo_list_load(names, urls, &count);
    if (count == 0) return;
    for (int i = 0; i < count; i++) {
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[i]);
        if (repo_fetch_index(urls[i], cache_path) != 0)
            fprintf(stderr, "warning: could not fetch repo %s\n", names[i]);
    }
}

int repo_fetch_index(const char *repo_url, const char *cache_path) {
    char url[VESSEL_MAX_URL];
    snprintf(url, sizeof(url), "%s/index.toml", repo_url);
    return http_download(url, cache_path);
}

int repo_download(const char *url, const char *dest) {
    printf("downloading %s...\n", url);
    return http_download(url, dest);
}

int repo_find_package(const char *id, char *version, size_t ver_sz, char *url, size_t url_sz, char *repo_name, size_t name_sz) {
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int repo_count;
    repo_list_load(names, urls, &repo_count);
    for (int ri = 0; ri < repo_count; ri++) {
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[ri]);
        FILE *f = fopen(cache_path, "r");
        if (!f) continue;
        bool in_target = false;
        char cur_id[VESSEL_MAX_ID] = "";
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == 0) continue;
            if (*p == '[') {
                p++;
                if (strncmp(p, "packages.", 9) == 0) {
                    p += 9;
                    char *end = strchr(p, ']');
                    if (end) {
                        *end = 0;
                        strncpy(cur_id, p, sizeof(cur_id)-1);
                        in_target = (strcmp(cur_id, id) == 0);
                    }
                } else {
                    in_target = false;
                }
                continue;
            }
            if (!in_target) continue;
            char key[128], val[VESSEL_MAX_URL];
            if (sscanf(p, "%127[^=] = \"%1023[^\"]\"", key, val) == 2) {
                char *kend = key + strlen(key) - 1;
                while (kend > key && (*kend == ' ' || *kend == '\t')) kend--;
                *(kend+1) = 0;
                if (strcmp(key, "version") == 0) strncpy(version, val, ver_sz-1);
                else if (strcmp(key, "url") == 0) strncpy(url, val, url_sz-1);
            }
        }
        fclose(f);
        if (in_target && url[0]) {
            strncpy(repo_name, names[ri], name_sz-1);
            return 0;
        }
    }
    return -1;
}

int repo_get_deps(const char *id, char deps[][VESSEL_MAX_ID], int *count) {
    *count = 0;
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int repo_count;
    repo_list_load(names, urls, &repo_count);
    for (int ri = 0; ri < repo_count; ri++) {
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[ri]);
        FILE *f = fopen(cache_path, "r");
        if (!f) continue;
        bool in_target = false;
        char cur_id[VESSEL_MAX_ID] = "";
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == 0) continue;
            if (*p == '[') {
                p++;
                if (strncmp(p, "packages.", 9) == 0) {
                    p += 9;
                    char *end = strchr(p, ']');
                    if (end) {
                        *end = 0;
                        strncpy(cur_id, p, sizeof(cur_id)-1);
                        in_target = (strcmp(cur_id, id) == 0);
                    }
                } else {
                    in_target = false;
                }
                continue;
            }
            if (!in_target) continue;
            if (strncmp(p, "vessel_deps", 11) == 0) {
                char *eq = strchr(p, '=');
                if (!eq) continue;
                eq++;
                while (*eq && *eq != '[') eq++;
                if (*eq == '[') {
                    eq++;
                    while (*eq && *count < VESSEL_MAX_VESSEL_DEPS) {
                        while (*eq && (*eq == ' ' || *eq == '\t' || *eq == ',' || *eq == '"')) eq++;
                        if (*eq == ']') break;
                        char dep[VESSEL_MAX_ID];
                        int di = 0;
                        while (*eq && *eq != '"' && *eq != ',' && *eq != ']' && *eq != '\n' && di < VESSEL_MAX_ID-1)
                            dep[di++] = *eq++;
                        dep[di] = 0;
                        if (dep[0]) {
                            strncpy(deps[*count], dep, VESSEL_MAX_ID-1);
                            (*count)++;
                        }
                    }
                }
            }
        }
        fclose(f);
        if (in_target) return 0;
    }
    return *count > 0 ? 0 : -1;
}

int cmd_search(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel search <query>\n");
        return 1;
    }

    const char *query = argv[2];
    ensure_dirs();
    util_mkdir_p(vessel_cache_dir, 0755);
    repo_refresh_all();
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int repo_count;
    repo_list_load(names, urls, &repo_count);
    if (repo_count == 0) {
        fprintf(stderr, "no repos configured. use 'vessel repo add <name> <url>'\n");
        return 1;
    }

    int found = 0;
    for (int ri = 0; ri < repo_count; ri++) {
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[ri]);
        FILE *f = fopen(cache_path, "r");
        if (!f) continue;
        char cur_id[VESSEL_MAX_ID] = "";
        char cur_version[VESSEL_MAX_VERSION] = "";
        char cur_desc[VESSEL_MAX_PATH] = "";
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == 0) continue;
            if (*p == '[') {
                if (cur_id[0] && (strstr(cur_id, query) || strstr(cur_desc, query))) {
                    printf("%-30s %-15s %-20s %s\n", cur_id, cur_version, names[ri], cur_desc);
                    found++;
                }
                cur_id[0] = 0;
                cur_version[0] = 0;
                cur_desc[0] = 0;
                p++;
                if (strncmp(p, "packages.", 9) == 0) {
                    p += 9;
                    char *end = strchr(p, ']');
                    if (end) {
                        *end = 0;
                        strncpy(cur_id, p, sizeof(cur_id)-1);
                    }
                }
                continue;
            }
            char key[128], val[VESSEL_MAX_PATH];
            if (sscanf(p, "%127[^=] = \"%511[^\"]\"", key, val) == 2) {
                char *kend = key + strlen(key) - 1;
                while (kend > key && (*kend == ' ' || *kend == '\t')) kend--;
                *(kend+1) = 0;
                if (strcmp(key, "version") == 0) strncpy(cur_version, val, sizeof(cur_version)-1);
                else if (strcmp(key, "description") == 0) strncpy(cur_desc, val, sizeof(cur_desc)-1);
            }
        }
        if (cur_id[0] && (strstr(cur_id, query) || strstr(cur_desc, query))) {
            printf("%-30s %-15s %-20s %s\n", cur_id, cur_version, names[ri], cur_desc);
            found++;
        }
        fclose(f);
    }

    if (found == 0) printf("no packages found for '%s'\n", query);
    return 0;
}

int cmd_repo(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vessel repo <list|add|remove>\n");
        return 1;
    }

    ensure_dirs();
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int count;
    repo_list_load(names, urls, &count);
    const char *sub = argv[2];
    if (strcmp(sub, "list") == 0) {
        if (count == 0) {
            printf("no repos configured\n");
            return 0;
        }
        for (int i = 0; i < count; i++) printf("%-20s %s\n", names[i], urls[i]);
        return 0;
    }

    if (strcmp(sub, "add") == 0) {
        if (argc < 5) {
            fprintf(stderr, "usage: vessel repo add <name> <url>\n");
            return 1;
        }
        const char *name = argv[3];
        const char *url = argv[4];
        for (int i = 0; i < count; i++) {
            if (strcmp(names[i], name) == 0) {
                fprintf(stderr, "error: repo '%s' already exists\n", name);
                return 1;
            }
        }
        if (count >= VESSEL_MAX_REPOS) {
            fprintf(stderr, "error: max repos reached\n");
            return 1;
        }
        strncpy(names[count], name, VESSEL_MAX_REPO_NAME-1);
        strncpy(urls[count], url, VESSEL_MAX_URL-1);
        count++;
        repo_list_save(names, urls, count);
        printf("added repo '%s' (%s)\n", name, url);
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, name);
        printf("fetching repo index...\n");
        if (repo_fetch_index(url, cache_path) != 0) fprintf(stderr, "warning: could not fetch repo index\n");
        return 0;
    }

    if (strcmp(sub, "remove") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: vessel repo remove <name>\n");
            return 1;
        }
        const char *name = argv[3];
        int found = -1;
        for (int i = 0; i < count; i++) {
            if (strcmp(names[i], name) == 0) { found = i; break; }
        }
        if (found < 0) {
            fprintf(stderr, "error: repo '%s' not found\n", name);
            return 1;
        }
        for (int i = found; i < count - 1; i++) {
            strncpy(names[i], names[i+1], VESSEL_MAX_REPO_NAME-1);
            strncpy(urls[i], urls[i+1], VESSEL_MAX_URL-1);
        }
        count--;
        repo_list_save(names, urls, count);
        char cache_path[VESSEL_MAX_PATH];
        snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, name);
        unlink(cache_path);
        printf("removed repo '%s'\n", name);
        return 0;
    }

    fprintf(stderr, "unknown repo subcommand: %s\n", sub);
    return 1;
}

int cmd_update(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ensure_dirs();
    util_mkdir_p(vessel_cache_dir, 0755);
    repo_refresh_all();
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int repo_count;
    repo_list_load(names, urls, &repo_count);
    int updates = 0;
    DIR *d = opendir(VESSEL_PACKAGES);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            vessel_manifest_t installed;
            if (manifest_load_db(ent->d_name, &installed) != 0) continue;
            char latest_ver[VESSEL_MAX_VERSION] = "";
            char latest_repo[VESSEL_MAX_REPO_NAME] = "";
            char latest_url[VESSEL_MAX_URL] = "";
            for (int ri = 0; ri < repo_count; ri++) {
                char cache_path[VESSEL_MAX_PATH];
                snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[ri]);
                FILE *f = fopen(cache_path, "r");
                if (!f) continue;
                bool in_target = false;
                char cur_id[VESSEL_MAX_ID] = "";
                char line[4096];
                while (fgets(line, sizeof(line), f)) {
                    char *p = line;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '[') {
                        p++;
                        if (strncmp(p, "packages.", 9) == 0) {
                            p += 9;
                            char *end = strchr(p, ']');
                            if (end) {
                                *end = 0;
                                strncpy(cur_id, p, sizeof(cur_id)-1);
                                in_target = (strcmp(cur_id, ent->d_name) == 0);
                            }
                        } else {
                            in_target = false;
                        }
                        continue;
                    }
                    if (!in_target) continue;
                    char key[128], val[VESSEL_MAX_URL];
                    if (sscanf(p, "%127[^=] = \"%1023[^\"]\"", key, val) == 2) {
                        char *kend = key + strlen(key) - 1;
                        while (kend > key && (*kend == ' ' || *kend == '\t')) kend--;
                        *(kend+1) = 0;
                        if (strcmp(key, "version") == 0) {
                            strncpy(latest_ver, val, sizeof(latest_ver)-1);
                            strncpy(latest_repo, names[ri], sizeof(latest_repo)-1);
                        } else if (strcmp(key, "url") == 0) {
                            strncpy(latest_url, val, sizeof(latest_url)-1);
                        }
                    }
                }
                fclose(f);
            }

            if (latest_ver[0] && version_cmp(latest_ver, installed.name.version) > 0) {
                printf("%s: %s -> %s (repo: %s)\n", ent->d_name, installed.name.version, latest_ver, latest_repo);
                updates++;
            }
        }
        closedir(d);
    }

    if (updates == 0) printf("all packages up to date\n");
    return 0;
}

int cmd_upgrade(int argc, char **argv) {
    ensure_dirs();
    util_mkdir_p(vessel_cache_dir, 0755);
    repo_refresh_all();
    char names[VESSEL_MAX_REPOS][VESSEL_MAX_REPO_NAME];
    char urls[VESSEL_MAX_REPOS][VESSEL_MAX_URL];
    int repo_count;
    repo_list_load(names, urls, &repo_count);
    int upgrade_count = 0;
    int spec_count = argc - 2;
    char **specs = argv + 2;
    DIR *d = opendir(VESSEL_PACKAGES);
    if (!d) {
        printf("no packages installed\n");
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (spec_count > 0) {
            bool match = false;
            for (int i = 0; i < spec_count; i++) {
                if (strcmp(ent->d_name, specs[i]) == 0) { match = true; break; }
            }
            if (!match) continue;
        }

        vessel_manifest_t installed;
        if (manifest_load_db(ent->d_name, &installed) != 0) continue;
        char new_version[VESSEL_MAX_VERSION] = "";
        char new_url[VESSEL_MAX_URL] = "";
        char new_repo[VESSEL_MAX_REPO_NAME] = "";
        for (int ri = 0; ri < repo_count; ri++) {
            char cache_path[VESSEL_MAX_PATH];
            snprintf(cache_path, sizeof(cache_path), "%s/%s", vessel_cache_dir, names[ri]);
            FILE *f = fopen(cache_path, "r");
            if (!f) continue;
            bool in_target = false;
            char cur_id[VESSEL_MAX_ID] = "";
            char line[4096];
            while (fgets(line, sizeof(line), f)) {
                char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '[') {
                    p++;
                    if (strncmp(p, "packages.", 9) == 0) {
                        p += 9;
                        char *end = strchr(p, ']');
                        if (end) {
                            *end = 0;
                            strncpy(cur_id, p, sizeof(cur_id)-1);
                            in_target = (strcmp(cur_id, ent->d_name) == 0);
                        }
                    } else {
                        in_target = false;
                    }
                    continue;
                }
                if (!in_target) continue;
                char key[128], val[VESSEL_MAX_URL];
                if (sscanf(p, "%127[^=] = \"%1023[^\"]\"", key, val) == 2) {
                    char *kend = key + strlen(key) - 1;
                    while (kend > key && (*kend == ' ' || *kend == '\t')) kend--;
                    *(kend+1) = 0;
                    if (strcmp(key, "version") == 0) strncpy(new_version, val, sizeof(new_version)-1);
                    else if (strcmp(key, "url") == 0) strncpy(new_url, val, sizeof(new_url)-1);
                }
            }
            fclose(f);
            if (new_version[0] && new_url[0]) {
                strncpy(new_repo, names[ri], sizeof(new_repo)-1);
                break;
            }
        }

        if (!new_version[0] || version_cmp(new_version, installed.name.version) <= 0) continue;
        printf("upgrading %s: %s -> %s (from %s)...\n", ent->d_name, installed.name.version, new_version, new_repo);
        char pkg_dir[VESSEL_MAX_PATH];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", VESSEL_PACKAGES, ent->d_name);
        vessel_manifest_t m;
        manifest_load_db(ent->d_name, &m);
        if (m.name.id[0]) desktop_remove(&m, pkg_dir);
        util_rm_rf(pkg_dir);
        manifest_remove_db_entry(ent->d_name);
        char tmp_path[VESSEL_MAX_PATH];
        snprintf(tmp_path, sizeof(tmp_path), "%s/%s.ves", vessel_cache_dir, ent->d_name);
        if (repo_download(new_url, tmp_path) != 0) {
            fprintf(stderr, "error: could not download %s\n", new_url);
            continue;
        }

        if (install_ves_from_file(tmp_path, false) != 0)fprintf(stderr, "error: could not install %s\n", ent->d_name);
        unlink(tmp_path);
        upgrade_count++;
    }

    closedir(d);
    if (upgrade_count == 0) printf("no packages to upgrade\n");
    return 0;
}
