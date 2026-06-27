#include "vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ncurses.h>
typedef struct {
    char id[VESSEL_MAX_ID];
    char version[VESSEL_MAX_VERSION];
    char entry[VESSEL_MAX_PATH];
    bool has_manifest;
} pkg_entry_t;
typedef struct {
    pkg_entry_t *pkgs;
    int count;
    int cap;
} pkg_list_t;
static int load_packages(pkg_list_t *pl) {
    pl->count = 0;
    pl->cap = 0;
    pl->pkgs = nullptr;
    DIR *d = opendir(VESSEL_PACKAGES);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char manifest_path[VESSEL_MAX_PATH];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.toml", VESSEL_PACKAGES, ent->d_name);
        if (pl->count >= pl->cap) {
            int ncap = pl->cap ? pl->cap * 2 : 32;
            pkg_entry_t *np = realloc(pl->pkgs, ncap * sizeof(pkg_entry_t));
            if (!np) break;
            pl->pkgs = np;
            pl->cap = ncap;
        }

        pkg_entry_t *p = &pl->pkgs[pl->count];
        memset(p, 0, sizeof(*p));
        strncpy(p->id, ent->d_name, sizeof(p->id)-1);
        vessel_manifest_t m;
        if (manifest_load(manifest_path, &m) == 0) {
            strncpy(p->version, m.name.version, sizeof(p->version)-1);
            strncpy(p->entry, m.name.entry, sizeof(p->entry)-1);
            p->has_manifest = true;
        } else {
            if (manifest_load_db(ent->d_name, &m) == 0) {
                strncpy(p->version, m.name.version, sizeof(p->version)-1);
                strncpy(p->entry, m.name.entry, sizeof(p->entry)-1);
                p->has_manifest = true;
            }
        }

        pl->count++;
    }

    closedir(d);
    return 0;
}

static void free_packages(pkg_list_t *pl) {
    free(pl->pkgs);
    pl->pkgs = nullptr;
    pl->count = 0;
    pl->cap = 0;
}

static void draw_main(WINDOW *left, WINDOW *right, WINDOW *footer, pkg_list_t *pl, int selection) {
    werase(left);
    werase(right);
    werase(footer);
    int lh, lw, rh, rw;
    (void)rh; (void)rw;
    getmaxyx(left, lh, lw);
    getmaxyx(right, rh, rw);
    mvwprintw(left, 0, 1, "Installed Packages");
    for (int i = 0; i < lw - 2; i++) mvwaddch(left, 1, i, '-');
    int start = 0;
    if (selection >= lh - 3 && lh > 3) start = selection - (lh - 4);
    for (int i = start; i < pl->count && i - start < lh - 3; i++) {
        if (i == selection) wattron(left, A_REVERSE);
        mvwprintw(left, 2 + i - start, 1, "%-*s", lw - 3, pl->pkgs[i].id);
        if (i == selection) wattroff(left, A_REVERSE);
    }

    mvwprintw(right, 0, 1, "Package Details");
    for (int i = 0; i < rw - 2; i++) mvwaddch(right, 1, i, '-');
    if (selection >= 0 && selection < pl->count) {
        pkg_entry_t *p = &pl->pkgs[selection];
        mvwprintw(right, 3, 2, "ID:      %s", p->id);
        mvwprintw(right, 4, 2, "Version: %s", p->version);
        mvwprintw(right, 5, 2, "Entry:   %s", p->entry);
        mvwprintw(right, 6, 2, "Status:  %s", p->has_manifest ? "okay" : "no manifest");
        vessel_manifest_t m;
        char manifest_path[VESSEL_MAX_PATH];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.toml", VESSEL_PACKAGES, p->id);
        if (manifest_load(manifest_path, &m) == 0) {
            int row = 8;
            mvwprintw(right, row++, 2, "Permissions:");
            mvwprintw(right, row++, 4, "Network: %s", m.permissions.network ? "yes" : "no");
            mvwprintw(right, row++, 4, "GPU:     %s", m.permissions.gpu ? "yes" : "no");
            mvwprintw(right, row++, 4, "Audio:   %s", m.permissions.audio ? "yes" : "no");
            if (m.expose.bin_count > 0) {
                row++;
                mvwprintw(right, row++, 2, "Exposed binaries:");
                for (int j = 0; j < m.expose.bin_count; j++) mvwprintw(right, row++, 4, "- %s", m.expose.bin[j]);
            }
            if (m.deps.vessel_count > 0) {
                row++;
                mvwprintw(right, row++, 2, "Vessel deps:");
                for (int j = 0; j < m.deps.vessel_count; j++) mvwprintw(right, row++, 4, "- %s", m.deps.vessels[j]);
            }
        }
    }

    mvwprintw(footer, 0, 1, "[Q] Quit  [R] Remove  [I] Install  [S] Search  [U] Update  [G] Upgrade  [arrows]");
    wnoutrefresh(left);
    wnoutrefresh(right);
    wnoutrefresh(footer);
    doupdate();
}

static int tui_install_dialog(void) {
    echo();
    curs_set(1);
    int lh, lw;
    getmaxyx(stdscr, lh, lw);
    WINDOW *win = newwin(3, lw - 10, lh / 2 - 1, 5);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Package or .ves path: ");
    wrefresh(win);
    char input[VESSEL_MAX_PATH] = "";
    wgetnstr(win, input, sizeof(input) - 1);
    delwin(win);
    noecho();
    curs_set(0);
    if (input[0] == 0) return 0;

    def_prog_mode();
    endwin();
    char *args[] = { "vessel", "install", input, nullptr };
    cmd_install(3, args);
    reset_prog_mode();
    return 0;
}

static int tui_search_dialog(void) {
    echo();
    curs_set(1);
    int lh, lw;
    getmaxyx(stdscr, lh, lw);
    WINDOW *win = newwin(3, lw - 10, lh / 2 - 1, 5);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Search: ");
    wrefresh(win);
    char query[VESSEL_MAX_PATH] = "";
    wgetnstr(win, query, sizeof(query) - 1);
    delwin(win);
    noecho();
    curs_set(0);
    if (query[0] == 0) return 0;

    def_prog_mode();
    endwin();
    char *args[] = { "vessel", "search", query, nullptr };
    cmd_search(3, args);
    reset_prog_mode();
    return 0;
}

int cmd_tui(int argc, char **argv) {
    (void)argc;
    (void)argv;
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();
    pkg_list_t pl = {0};
    load_packages(&pl);
    int selection = 0;
    bool running = true;
    while (running) {
        int lh, lw;
        getmaxyx(stdscr, lh, lw);
        int half = lw * 2 / 5;
        if (half < 20) half = 20;
        WINDOW *left = newwin(lh - 3, half, 0, 0);
        WINDOW *right = newwin(lh - 3, lw - half, 0, half);
        WINDOW *footer = newwin(3, lw, lh - 3, 0);
        draw_main(left, right, footer, &pl, selection);
        int ch = getch();
        switch (ch) {
            case 'q':
            case 'Q':
                running = false;
                break;
            case 'r':
            case 'R': {
                if (selection >= 0 && selection < pl.count) {
                    def_prog_mode();
                    endwin();
                    char *args[] = { "vessel", "remove", pl.pkgs[selection].id, nullptr };
                    cmd_remove(3, args);
                    reset_prog_mode();
                    free_packages(&pl);
                    load_packages(&pl);
                    if (selection >= pl.count) selection = pl.count - 1;
                    if (selection < 0) selection = 0;
                }
                break;
            }
            case 'i':
            case 'I':
                tui_install_dialog();
                free_packages(&pl);
                load_packages(&pl);
                break;
            case 's':
            case 'S':
                tui_search_dialog();
                break;
            case 'u':
            case 'U': {
                def_prog_mode();
                endwin();
                char *args[] = { "vessel", "update", nullptr };
                cmd_update(2, args);
                reset_prog_mode();
                break;
            }
            case 'g':
            case 'G': {
                def_prog_mode();
                endwin();
                char *args[] = { "vessel", "upgrade", nullptr };
                cmd_upgrade(2, args);
                reset_prog_mode();
                free_packages(&pl);
                load_packages(&pl);
                break;
            }
            case KEY_UP:
                if (selection > 0) selection--;
                break;
            case KEY_DOWN:
                if (selection < pl.count - 1) selection++;
                break;
        }

        delwin(left);
        delwin(right);
        delwin(footer);
    }

    free_packages(&pl);
    endwin();
    return 0;
}
