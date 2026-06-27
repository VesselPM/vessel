#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
int archive_extract(const char *ves_path, const char *dest) {
    struct archive *a = archive_read_new();
    if (!a) return -1;
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);
    if (archive_read_open_filename(a, ves_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    struct archive_entry *entry;
    int ret = -1;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        if (!pathname) continue;
        char fullpath[VESSEL_MAX_PATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dest, pathname);
        const char *basename = strrchr(pathname, '/');
        if (basename) basename++;
        else basename = pathname;
        if (pathname[strlen(pathname)-1] == '/') {
            util_mkdir_p(fullpath, archive_entry_mode(entry) & 0777);
            continue;
        }

        char dirpath[VESSEL_MAX_PATH];
        strncpy(dirpath, fullpath, sizeof(dirpath)-1);
        char *slash = strrchr(dirpath, '/');
        if (slash) {
            *slash = 0;
            util_mkdir_p(dirpath, 0755);
        }

        if (archive_entry_symlink(entry) && archive_entry_symlink(entry)[0]) {
            (void)symlink(archive_entry_symlink(entry), fullpath);
            continue;
        }

        if (archive_entry_hardlink(entry) && archive_entry_hardlink(entry)[0]) {
            (void)link(archive_entry_hardlink(entry), fullpath);
            continue;
        }

        int fd = open(fullpath, O_WRONLY | O_CREAT | O_TRUNC, archive_entry_mode(entry) & 0777);
        if (fd < 0) { ret = -1; goto done; }
        char buf[65536];
        la_ssize_t nread;
        while ((nread = archive_read_data(a, buf, sizeof(buf))) > 0) {
            ssize_t written = 0;
            while (written < nread) {
                ssize_t n = write(fd, buf + written, (size_t)(nread - written));
                if (n < 0) { close(fd); ret = -1; goto done; }
                written += n;
            }
        }
        close(fd);
    }

    ret = 0;
done:
    archive_read_close(a);
    archive_read_free(a);
    return ret;
}

int archive_list(const char *ves_path) {
    struct archive *a = archive_read_new();
    if (!a) return -1;
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);
    if (archive_read_open_filename(a, ves_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        if (pathname) printf("%s\n", pathname);
    }

    archive_read_close(a);
    archive_read_free(a);
    return 0;
}

int archive_read_manifest(const char *ves_path, char **out, size_t *out_len) {
    struct archive *a = archive_read_new();
    if (!a) return -1;
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);
    if (archive_read_open_filename(a, ves_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    int ret = -1;
    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        if (!pathname) continue;
        if (strcmp(pathname, "manifest.toml") == 0) {
            la_int64_t size = archive_entry_size(entry);
            if (size <= 0) { ret = -1; goto done; }
            char *buf = malloc((size_t)size + 1);
            if (!buf) { ret = -1; goto done; }
            la_ssize_t nread = archive_read_data(a, buf, (size_t)size);
            if (nread < 0) { free(buf); ret = -1; goto done; }
            buf[nread] = 0;
            *out = buf;
            *out_len = (size_t)nread;
            ret = 0;
            goto done;
        }
    }

done:
    archive_read_close(a);
    archive_read_free(a);
    return ret;
}
