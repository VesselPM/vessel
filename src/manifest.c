#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct {
    char *buf;
    size_t len;
    size_t pos;
    int line;
    int col;
} toml_ctx;
static void tctx_init(toml_ctx *c, char *buf, size_t len) {
    c->buf = buf;
    c->len = len;
    c->pos = 0;
    c->line = 1;
    c->col = 1;
}

static void tctx_skip_ws(toml_ctx *c) {
    while (c->pos < c->len) {
        char ch = c->buf[c->pos];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            c->pos++; c->col++; continue;
        }
        if (ch == '\n') {
            c->pos++; c->line++; c->col = 1; continue;
        }
        break;
    }
}

static void tctx_skip_line(toml_ctx *c) {
    while (c->pos < c->len && c->buf[c->pos] != '\n') {
        c->pos++; c->col++;
    }
    if (c->pos < c->len && c->buf[c->pos] == '\n') {
        c->pos++; c->line++; c->col = 1;
    }
}

static int tctx_peek(toml_ctx *c) {
    if (c->pos >= c->len) return -1;
    return (unsigned char)c->buf[c->pos];
}

static int tctx_next(toml_ctx *c) {
    if (c->pos >= c->len) return -1;
    char ch = c->buf[c->pos++];
    if (ch == '\n') { c->line++; c->col = 1; }
    else { c->col++; }
    return (unsigned char)ch;
}

static int parse_bare_key(toml_ctx *c, char *out, size_t max) {
    size_t start = c->pos;
    int first = 1;
    while (c->pos < c->len) {
        char ch = c->buf[c->pos];
        if (first) {
            if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') break;
            first = 0;
        } else {
            if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') break;
        }
        c->pos++; c->col++;
    }
    size_t count = c->pos - start;
    if (count == 0 || count >= max) return -1;
    memcpy(out, c->buf + start, count);
    out[count] = 0;
    return 0;
}

static int parse_basic_string(toml_ctx *c, char *out, size_t max) {
    int quote = tctx_next(c);
    if (quote != '"') return -1;
    size_t opos = 0;
    while (c->pos < c->len) {
        char ch = c->buf[c->pos];
        if (ch == '"') {
            tctx_next(c);
            out[opos] = 0;
            return 0;
        }
        if (ch == '\\') {
            tctx_next(c);
            if (c->pos >= c->len) break;
            char esc = c->buf[c->pos];
            switch (esc) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case '\\': ch = '\\'; break;
                case '"': ch = '"'; break;
                case 'r': ch = '\r'; break;
                default: ch = esc; break;
            }
            tctx_next(c);
        } else {
            tctx_next(c);
        }
        if (opos >= max - 1) return -1;
        out[opos++] = ch;
    }
    return -1;
}

static int parse_literal_string(toml_ctx *c, char *out, size_t max) {
    int q = tctx_next(c);
    if (q != '\'') return -1;
    size_t opos = 0;
    while (c->pos < c->len) {
        char ch = c->buf[c->pos];
        if (ch == '\'') {
            tctx_next(c);
            out[opos] = 0;
            return 0;
        }
        tctx_next(c);
        if (opos >= max - 1) return -1;
        out[opos++] = ch;
    }
    return -1;
}

static int parse_string(toml_ctx *c, char *out, size_t max) {
    tctx_skip_ws(c);
    int ch = tctx_peek(c);
    if (ch == '"') return parse_basic_string(c, out, max);
    if (ch == '\'') return parse_literal_string(c, out, max);
    return -1;
}

static int parse_bool(toml_ctx *c, bool *out) {
    tctx_skip_ws(c);
    if (c->pos + 4 <= c->len && memcmp(c->buf + c->pos, "true", 4) == 0) {
        char next = c->pos + 4 < c->len ? c->buf[c->pos + 4] : ' ';
        if (!isalnum((unsigned char)next) && next != '_') {
            c->pos += 4; c->col += 4;
            *out = true;
            return 0;
        }
    }
    if (c->pos + 5 <= c->len && memcmp(c->buf + c->pos, "false", 5) == 0) {
        char next = c->pos + 5 < c->len ? c->buf[c->pos + 5] : ' ';
        if (!isalnum((unsigned char)next) && next != '_') {
            c->pos += 5; c->col += 5;
            *out = false;
            return 0;
        }
    }
    return -1;
}

static int parse_int(toml_ctx *c, int64_t *out) {
    tctx_skip_ws(c);
    char *end;
    *out = strtoll(c->buf + c->pos, &end, 10);
    if (end == c->buf + c->pos) return -1;
    c->pos += end - (c->buf + c->pos);
    c->col += (int)(end - (c->buf + c->pos));
    return 0;
}

static int parse_value(toml_ctx *c, char *out_str, size_t max_str, bool *out_bool, int64_t *out_int, int *type) {
    tctx_skip_ws(c);
    int ch = tctx_peek(c);
    if (ch == '"' || ch == '\'') {
        if (parse_string(c, out_str, max_str) == 0) {
            *type = 0;
            return 0;
        }
        return -1;
    }
    if (ch == 't' || ch == 'f') {
        if (parse_bool(c, out_bool) == 0) {
            *type = 1;
            return 0;
        }
        return -1;
    }
    if (ch == '-' || ch == '+' || (ch >= '0' && ch <= '9')) {
        if (parse_int(c, out_int) == 0) {
            *type = 2;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int parse_inline_table(toml_ctx *c, vessel_mount_t *mnt) {
    int brace = tctx_next(c);
    if (brace != '{') return -1;
    memset(mnt, 0, sizeof(*mnt));
    while (c->pos < c->len) {
        tctx_skip_ws(c);
        int ch = tctx_peek(c);
        if (ch == '}') { tctx_next(c); return 0; }
        if (ch == ',' || ch == '\n') { tctx_next(c); continue; }
        char key[64];
        if (parse_bare_key(c, key, sizeof(key)) != 0) return -1;
        tctx_skip_ws(c);
        if (tctx_next(c) != '=') return -1;
        tctx_skip_ws(c);
        int vtype;
        char vstr[VESSEL_MAX_PATH];
        bool vbool;
        int64_t vint;
        if (parse_value(c, vstr, sizeof(vstr), &vbool, &vint, &vtype) != 0) return -1;
        if (strcmp(key, "from") == 0 && vtype == 0) strncpy(mnt->from, vstr, sizeof(mnt->from)-1);
        else if (strcmp(key, "to") == 0 && vtype == 0) strncpy(mnt->to, vstr, sizeof(mnt->to)-1);
        else if (strcmp(key, "mode") == 0 && vtype == 0) strncpy(mnt->mode, vstr, sizeof(mnt->mode)-1);
        tctx_skip_ws(c);
    }
    return -1;
}

static int parse_array_strings(toml_ctx *c, char *arr, size_t item_size, int *count, int max) {
    if (tctx_next(c) != '[') return -1;
    *count = 0;
    while (c->pos < c->len) {
        tctx_skip_ws(c);
        int ch = tctx_peek(c);
        if (ch == ']') { tctx_next(c); return 0; }
        if (ch == ',' || ch == '\n') { tctx_next(c); continue; }
        if (*count >= max) return -1;
        char val[VESSEL_MAX_PATH];
        if (parse_string(c, val, sizeof(val)) != 0) return -1;
        strncpy(arr + (*count) * item_size, val, item_size - 1);
        (arr + (*count) * item_size)[item_size - 1] = 0;
        (*count)++;
        tctx_skip_ws(c);
    }
    return -1;
}

static int parse_array_mounts(toml_ctx *c, vessel_mount_t arr[], int *count, int max) {
    if (tctx_next(c) != '[') return -1;
    *count = 0;
    while (c->pos < c->len) {
        tctx_skip_ws(c);
        int ch = tctx_peek(c);
        if (ch == ']') { tctx_next(c); return 0; }
        if (ch == ',' || ch == '\n') { tctx_next(c); continue; }
        if (*count >= max) return -1;
        if (ch == '{') {
            if (parse_inline_table(c, &arr[*count]) != 0) return -1;
            (*count)++;
        } else {
            break;
        }
        tctx_skip_ws(c);
    }
    return -1;
}

static int parse_array_devices(toml_ctx *c, char arr[][64], int *count, int max) {
    if (tctx_next(c) != '[') return -1;
    *count = 0;
    while (c->pos < c->len) {
        tctx_skip_ws(c);
        int ch = tctx_peek(c);
        if (ch == ']') { tctx_next(c); return 0; }
        if (ch == ',' || ch == '\n') { tctx_next(c); continue; }
        if (*count >= max) return -1;
        char val[128];
        if (parse_string(c, val, sizeof(val)) != 0) return -1;
        strncpy(arr[*count], val, 63);
        arr[*count][63] = 0;
        (*count)++;
        tctx_skip_ws(c);
    }
    return -1;
}

static int manifest_parse(const char *buf, size_t len, vessel_manifest_t *m) {
    toml_ctx ctx;
    tctx_init(&ctx, (char*)buf, len);
    memset(m, 0, sizeof(*m));
    char cur_table[256] = "";
    char cur_sub[256] = "";
    while (ctx.pos < ctx.len) {
        tctx_skip_ws(&ctx);
        int ch = tctx_peek(&ctx);
        if (ch < 0) break;
        if (ch == '#') { tctx_skip_line(&ctx); continue; }
        if (ch == '\n') { tctx_next(&ctx); continue; }
        if (ch == '[') {
            tctx_next(&ctx);
            tctx_skip_ws(&ctx);
            cur_table[0] = 0;
            cur_sub[0] = 0;
            char full[512] = "";
            int first = 1;
            while (ctx.pos < ctx.len) {
                tctx_skip_ws(&ctx);
                ch = tctx_peek(&ctx);
                if (ch == ']' || ch == '\n' || ch < 0) break;
                char key[128];
                if (ch == '"' || ch == '\'') {
                    if (parse_string(&ctx, key, sizeof(key)) != 0) return -1;
                } else {
                    if (parse_bare_key(&ctx, key, sizeof(key)) != 0) return -1;
                }
                if (!first) strcat(full, ".");
                strcat(full, key);
                first = 0;
                tctx_skip_ws(&ctx);
                ch = tctx_peek(&ctx);
                if (ch == '.') tctx_next(&ctx);
            }
            tctx_skip_ws(&ctx);
            if (tctx_peek(&ctx) != ']') return -1;
            tctx_next(&ctx);
            tctx_skip_line(&ctx);
            char *dot = strchr(full, '.');
            if (dot) {
                *dot = 0;
                strncpy(cur_table, full, sizeof(cur_table)-1);
                strncpy(cur_sub, dot + 1, sizeof(cur_sub)-1);
            } else {
                strncpy(cur_table, full, sizeof(cur_table)-1);
                cur_sub[0] = 0;
            }
            continue;
        }

        char key[128];
        if (parse_bare_key(&ctx, key, sizeof(key)) != 0) {
            tctx_skip_line(&ctx); continue;
        }

        tctx_skip_ws(&ctx);
        ch = tctx_peek(&ctx);
        if (ch == '.') {
            tctx_next(&ctx);
            tctx_skip_ws(&ctx);
            char subkey[128];
            if (parse_bare_key(&ctx, subkey, sizeof(subkey)) != 0) {
                tctx_skip_line(&ctx); continue;
            }
            tctx_skip_ws(&ctx);
            if (tctx_peek(&ctx) != '=') { tctx_skip_line(&ctx); continue; }
            tctx_next(&ctx);
            tctx_skip_ws(&ctx);
            ch = tctx_peek(&ctx);
            if (ch == '{') {
                vessel_mount_t mt;
                memset(&mt, 0, sizeof(mt));
                if (parse_inline_table(&ctx, &mt) == 0) {
                    if (strcmp(cur_table, "fs") == 0) {
                        if (strcmp(key, "orig_root") == 0) m->fs.orig_root = mt;
                        else if (strcmp(key, "home") == 0) m->fs.home = mt;
                        else if (strcmp(key, "var") == 0) m->fs.var = mt;
                    }
                }
            }
            continue;
        }

        if (ch != '=') { tctx_skip_line(&ctx); continue; }
        tctx_next(&ctx);
        tctx_skip_ws(&ctx);
        int vtype;
        char vstr[VESSEL_MAX_PATH];
        bool vbool;
        int64_t vint;
        ch = tctx_peek(&ctx);
        if (ch == '{' && strcmp(cur_table, "fs") == 0 &&
            (strcmp(key, "orig_root") == 0 || strcmp(key, "home") == 0 || strcmp(key, "var") == 0)) {
            vessel_mount_t mt;
            memset(&mt, 0, sizeof(mt));
            if (parse_inline_table(&ctx, &mt) == 0) {
                if (strcmp(key, "orig_root") == 0) m->fs.orig_root = mt;
                else if (strcmp(key, "home") == 0) m->fs.home = mt;
                else if (strcmp(key, "var") == 0) m->fs.var = mt;
            }
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "fs") == 0 && strcmp(key, "custom") == 0) {
            parse_array_mounts(&ctx, m->fs.custom, &m->fs.custom_count, VESSEL_MAX_CUSTOM);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "deps") == 0 && strcmp(key, "vessels") == 0) {
            parse_array_strings(&ctx, (char*)m->deps.vessels, sizeof(m->deps.vessels[0]), &m->deps.vessel_count, VESSEL_MAX_VESSEL_DEPS);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "deps") == 0 && strcmp(key, "system") == 0) {
            parse_array_strings(&ctx, (char*)m->deps.system, sizeof(m->deps.system[0]), &m->deps.system_count, VESSEL_MAX_SYSTEM_DEPS);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "expose") == 0 && strcmp(key, "bin") == 0) {
            parse_array_strings(&ctx, (char*)m->expose.bin, sizeof(m->expose.bin[0]), &m->expose.bin_count, VESSEL_MAX_BIN);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "expose") == 0 && strcmp(key, "desktop") == 0) {
            parse_array_strings(&ctx, (char*)m->expose.desktop, sizeof(m->expose.desktop[0]), &m->expose.desktop_count, VESSEL_MAX_DESKTOP);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "permissions") == 0 &&
            (strcmp(key, "devices") == 0)) {
            parse_array_devices(&ctx, m->permissions.devices, &m->permissions.device_count, VESSEL_MAX_DEVICES);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "permissions") == 0 && strcmp(key, "ipc") == 0) {
            parse_array_mounts(&ctx, m->permissions.ipc, &m->permissions.ipc_count, VESSEL_MAX_IPC);
            tctx_skip_line(&ctx);
            continue;
        }

        if (ch == '[' && strcmp(cur_table, "name") == 0 && strcmp(key, "arch") == 0) {
            parse_array_strings(&ctx, (char*)m->name.arch, sizeof(m->name.arch[0]), &m->name.arch_count, VESSEL_MAX_ARCH);
            tctx_skip_line(&ctx);
            continue;
        }

        if (parse_value(&ctx, vstr, sizeof(vstr), &vbool, &vint, &vtype) != 0) {
            tctx_skip_line(&ctx); continue;
        }

        if (strcmp(cur_table, "") == 0 && strcmp(key, "vessel") == 0 && vtype == 2) {
            m->name.vessel_version = vint;
        } else if (strcmp(cur_table, "name") == 0) {
            if (strcmp(key, "id") == 0 && vtype == 0) strncpy(m->name.id, vstr, sizeof(m->name.id)-1);
            else if (strcmp(key, "version") == 0 && vtype == 0) strncpy(m->name.version, vstr, sizeof(m->name.version)-1);
            else if (strcmp(key, "entry") == 0 && vtype == 0) strncpy(m->name.entry, vstr, sizeof(m->name.entry)-1);
        } else if (strcmp(cur_table, "permissions") == 0) {
            if (strcmp(key, "network") == 0 && vtype == 1) m->permissions.network = vbool;
            else if (strcmp(key, "gpu") == 0 && vtype == 1) m->permissions.gpu = vbool;
            else if (strcmp(key, "audio") == 0 && vtype == 1) m->permissions.audio = vbool;
        }

        tctx_skip_line(&ctx);
    }

    return 0;
}

int manifest_load(const char *path, vessel_manifest_t *m) {
    FILE *f = fopen(path, "rbe");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = malloc((size_t)flen + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, (size_t)flen, f);
    fclose(f);
    buf[nread] = 0;
    int ret = manifest_parse(buf, nread, m);
    free(buf);
    return ret;
}

int manifest_load_from_buf(const char *buf, size_t len, vessel_manifest_t *m) {
    return manifest_parse(buf, len, m);
}

int manifest_save_db(const vessel_manifest_t *m) {
    char path[VESSEL_MAX_PATH];
    snprintf(path, sizeof(path), VESSEL_DB_FILE);
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fprintf(f, "\n[%s]\n", m->name.id);
    fprintf(f, "version = \"%s\"\n", m->name.version);
    fprintf(f, "entry = \"%s\"\n", m->name.entry);
    fprintf(f, "vessel = %ld\n", (long)m->name.vessel_version);
    for (int i = 0; i < m->expose.desktop_count; i++) fprintf(f, "desktop_%d = \"%s\"\n", i, m->expose.desktop[i]);
    for (int i = 0; i < m->expose.bin_count; i++) fprintf(f, "bin_%d = \"%s\"\n", i, m->expose.bin[i]);
    fclose(f);
    return 0;
}

typedef struct {
    char **lines;
    int count;
    int cap;
} db_lines;
static int db_load(char ***out, int *out_count) {
    FILE *f = fopen(VESSEL_DB_FILE, "r");
    if (!f) return -1;
    int cap = 256;
    int count = 0;
    char **lines = calloc(cap, sizeof(char*));
    if (!lines) { fclose(f); return -1; }
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        if (count >= cap) {
            cap *= 2;
            char **nl = realloc(lines, cap * sizeof(char*));
            if (!nl) {
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines); fclose(f); return -1;
            }
            lines = nl;
        }
        lines[count] = strdup(buf);
        if (!lines[count]) {
            for (int i = 0; i < count; i++) free(lines[i]);
            free(lines); fclose(f); return -1;
        }
        count++;
    }
    fclose(f);
    *out = lines;
    *out_count = count;
    return 0;
}

int manifest_load_db(const char *id, vessel_manifest_t *m) {
    memset(m, 0, sizeof(*m));
    strncpy(m->name.id, id, sizeof(m->name.id)-1);
    char **lines;
    int count;
    if (db_load(&lines, &count) != 0) return -1;
    char target[256];
    snprintf(target, sizeof(target), "[%s]", id);
    bool in_section = false;
    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == '\n' || *line == 0) continue;
        if (*line == '[') {
            char section[256];
            int j = 0;
            line++;
            while (*line && *line != ']' && j < 255) section[j++] = *line++;
            section[j] = 0;
            in_section = (strcmp(section, id) == 0);
            if (!in_section && strncmp(section, id, strlen(id)) != 0) continue;
            continue;
        }

        if (!in_section) continue;
        char key[128], val[512];
        if (sscanf(line, "%127[^=] = \"%511[^\"]\"", key, val) == 2) {
            while (strlen(key) > 0 && (key[strlen(key)-1] == ' ' || key[strlen(key)-1] == '\t')) key[strlen(key)-1] = 0;
            if (strcmp(key, "version") == 0) strncpy(m->name.version, val, sizeof(m->name.version)-1);
            else if (strcmp(key, "entry") == 0) strncpy(m->name.entry, val, sizeof(m->name.entry)-1);
            else if (strncmp(key, "desktop_", 8) == 0 && m->expose.desktop_count < VESSEL_MAX_DESKTOP) {
                strncpy(m->expose.desktop[m->expose.desktop_count++], val, VESSEL_MAX_PATH-1);
            }
            else if (strncmp(key, "bin_", 4) == 0 && m->expose.bin_count < VESSEL_MAX_BIN) {
                strncpy(m->expose.bin[m->expose.bin_count++], val, VESSEL_MAX_BIN_NAME-1);
            }
        }

        long lval;
        if (sscanf(line, "%127[^=] = %ld", key, &lval) == 2) {
            while (strlen(key) > 0 && (key[strlen(key)-1] == ' ' || key[strlen(key)-1] == '\t'))
                key[strlen(key)-1] = 0;
            if (strcmp(key, "vessel") == 0) m->name.vessel_version = lval;
        }
    }

    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    return m->name.version[0] ? 0 : -1;
}

int manifest_remove_db_entry(const char *id) {
    char **lines;
    int count;
    if (db_load(&lines, &count) != 0) return 0;
    char target[256];
    snprintf(target, sizeof(target), "[%s]", id);
    FILE *f = fopen(VESSEL_DB_FILE, "w");
    if (!f) {
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return -1;
    }

    bool skipping = false;
    for (int i = 0; i < count; i++) {
        char *p = lines[i];
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            char section[256];
            int j = 0; p++;
            while (*p && *p != ']' && j < 255) section[j++] = *p++;
            section[j] = 0;
            skipping = (strcmp(section, id) == 0);
            if (skipping) continue;
        }
        if (skipping) continue;
        fputs(lines[i], f);
    }

    fclose(f);
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    return 0;
}
