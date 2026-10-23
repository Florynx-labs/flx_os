// File Explorer: real filesystem browsing with navigation pane, breadcrumb, list/grid
// views, open in Notepad, new folder, rename, delete. Its own process.
#include "w12.h"
#include <dirent.h>
#include <sys/stat.h>

typedef struct { char name[256]; int dir; long size; time_t mtime; int exec; } Ent;
static Ent *ents; static int nents;
static char cwd[1024] = "/home/user";
static char hist[32][1024]; static int nhist, hpos = -1;
static int sel = -1, grid_view = 1;
static Scroll sc;
static int modal;            // 1 rename/new, 2 delete confirm
static char fieldbuf[256];
static char status[200];
static int ctx_open; static float ctx_x, ctx_y; static int ctx_item;
#define TOOLH 48
#define CRUMBH 44
#define NAVW 210
#define ROWH 34
#define STATH 28

static int cmp(const void *a, const void *b) {
    const Ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}
static void load(void) {
    free(ents); ents = 0; nents = 0;
    DIR *d = opendir(cwd);
    if (!d) { snprintf(status, sizeof status, "Can't open %s: %s", cwd, strerror(errno)); return; }
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (nents >= cap) { cap = cap ? cap * 2 : 64; ents = realloc(ents, cap * sizeof *ents); }
        Ent *x = &ents[nents];
        snprintf(x->name, sizeof x->name, "%s", e->d_name);
        char p[1400]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", strcmp(cwd, "/") ? cwd : "", e->d_name);
        if (stat(p, &st)) memset(&st, 0, sizeof st);
        x->dir = S_ISDIR(st.st_mode); x->size = st.st_size; x->mtime = st.st_mtime; x->exec = !x->dir && (st.st_mode & 0111);
        nents++;
    }
    closedir(d);
    qsort(ents, nents, sizeof *ents, cmp);
    sel = -1; sc.pos = 0;
    snprintf(status, sizeof status, "%d items", nents);
    char t[1100];
    const char *b = strrchr(cwd, '/');
    snprintf(t, sizeof t, "%s - File Explorer", !strcmp(cwd, "/") ? "This PC" : b ? b + 1 : cwd);
    app_set_title(t);
}
static void go(const char *p, int push) {
    char np[1024];
    if (p[0] != '/') snprintf(np, sizeof np, "%s/%s", strcmp(cwd, "/") ? cwd : "", p); else snprintf(np, sizeof np, "%s", p);
    DIR *d = opendir(np);
    if (!d) return;
    closedir(d);
    snprintf(cwd, sizeof cwd, "%s", np);
    if (push) {
        if (hpos < 31) hpos++;
        snprintf(hist[hpos], 1024, "%s", cwd);
        nhist = hpos + 1;
    }
    load();
    MARK("EXPLORER cd %s %d", cwd, nents);
}
static void path_of(int i, char *o, int n) { snprintf(o, n, "%s/%s", strcmp(cwd, "/") ? cwd : "", ents[i].name); }
static void open_item(int i) {
    if (i < 0 || i >= nents) return;
    if (ents[i].dir) { char p[1400]; path_of(i, p, sizeof p); go(p, 1); return; }
    char p[1400]; path_of(i, p, sizeof p);
    if (!strcmp(cwd, "/bin") && app_by_exe(ents[i].name) >= 0) { app_launch(ents[i].name, 0); return; }
    if (ents[i].exec || strstr(ents[i].name, ".raw") || strstr(ents[i].name, ".ttf")) { snprintf(status, sizeof status, "%s is a binary file", ents[i].name); return; }
    app_launch("notepad", p);
    app_recent(p);
    MARK("EXPLORER open %s", p);
}
static int rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path); struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[1400]; snprintf(c, sizeof c, "%s/%s", path, e->d_name); rm_rf(c);
        }
        if (d) closedir(d);
        return rmdir(path);
    }
    return unlink(path);
}
static void new_folder(void) {
    char p[1400];
    for (int k = 1; k < 100; k++) {
        snprintf(fieldbuf, sizeof fieldbuf, k == 1 ? "Nouveau dossier" : "New folder (%d)", k);
        snprintf(p, sizeof p, "%s/%s", cwd, fieldbuf);
        if (mkdir(p, 0755) == 0) break;
    }
    sync();
    load();
    for (int i = 0; i < nents; i++) if (!strcmp(ents[i].name, fieldbuf)) sel = i;
    modal = 1;
    MARK("EXPLORER mkdir %s", p);
}

static void draw_modal(Canvas *c) {
    g_rect(c, 0, 0, U.w, U.h, TH.dark ? 0x80000000 : 0x50000000);
    float w = 440, h = 200, x = (U.w - w) / 2, y = (U.h - h) / 2;
    g_shadow(c, x, y, w, h, 10, 30, 90, 8);
    g_rrect(c, x, y, w, h, 10, 10, TH.surface);
    g_rrect_line(c, x, y, w, h, 10, 1, TH.line);
    g_rrect(c, x, y + h - 68, w, 68, 0, 10, TH.bg);
    if (sel < 0 || sel >= nents) { modal = 0; return; }
    char p[1400]; path_of(sel, p, sizeof p);
    if (modal == 1) {
        text(c, F_SEMI, 20, 0, x + 24, y + 42, TH.text, "Renommer");
        U.focus = 5;
        int r = ui_field(5, fieldbuf, sizeof fieldbuf, x + 24, y + 64, w - 48, 36, "Name");
        bool ok = ui_button(x + w - 252, y + h - 50, 110, 34, "Renommer", B_ACCENT) || r == 1;
        if (ui_button(x + w - 132, y + h - 50, 110, 34, "Cancel", B_STD) || ui_key(K_ESC, 0)) modal = 0;
        if (ok && fieldbuf[0] && !strchr(fieldbuf, '/')) {
            char np[1400]; snprintf(np, sizeof np, "%s/%s", cwd, fieldbuf);
            if (strcmp(np, p) && rename(p, np)) snprintf(status, sizeof status, "Rename failed: %s", strerror(errno));
            else MARK("EXPLORER rename %s -> %s", p, np);
            sync();
            modal = 0;
            char keep[256]; snprintf(keep, sizeof keep, "%s", fieldbuf);
            load();
            for (int i = 0; i < nents; i++) if (!strcmp(ents[i].name, keep)) sel = i;
        }
    } else {
        text(c, F_SEMI, 20, 0, x + 24, y + 42, TH.text, ents[sel].dir ? "Delete folder" : "Delete file");
        char q[400]; snprintf(q, sizeof q, "Permanently delete \"%s\"?", ents[sel].name);
        text(c, F_REG, 15, 0, x + 24, y + 84, TH.text, q);
        text(c, F_REG, 13, 0, x + 24, y + 108, TH.sub, "La corbeille n'est pas encore implementee.");
        if (ui_button(x + w - 252, y + h - 50, 110, 34, "Supprimer", B_ACCENT) || ui_key(K_ENTER, 0)) {
            if (rm_rf(p)) snprintf(status, sizeof status, "Delete failed: %s", strerror(errno));
            else MARK("EXPLORER delete %s", p);
            sync();
            modal = 0; load();
        }
        if (ui_button(x + w - 132, y + h - 50, 110, 34, "Cancel", B_STD) || ui_key(K_ESC, 0)) modal = 0;
    }
}

static void draw(Canvas *c) {
    int ev = U.ev;
    if (modal) U.ev = EV_NONE;
    float W = U.w, H = U.h;
    // toolbar
    float x = 10;
    if (ui_icon_btn(x, 8, 36, 32, G_BACK, 0) && hpos > 0) { hpos--; snprintf(cwd, sizeof cwd, "%s", hist[hpos]); load(); }
    x += 40;
    if (ui_icon_btn(x, 8, 36, 32, G_FWD, 0) && hpos < nhist - 1) { hpos++; snprintf(cwd, sizeof cwd, "%s", hist[hpos]); load(); }
    x += 40;
    if (ui_icon_btn(x, 8, 36, 32, G_UP, 0) && strcmp(cwd, "/")) { char p[1024]; snprintf(p, sizeof p, "%s", cwd); char *s = strrchr(p, '/'); if (s == p) s[1] = 0; else *s = 0; go(p, 1); }
    x += 40;
    if (ui_icon_btn(x, 8, 36, 32, G_REFRESH, 0)) load();
    x += 50;
    g_rect(c, x - 6, 14, 1, 20, TH.line);
    struct { const char *l; int g; } acts[] = { { "Nouveau dossier", G_PLUS }, { "Renommer", G_EDIT }, { "Supprimer", G_TRASH } };
    for (int i = 0; i < 3; i++) {
        float w = text_w(F_REG, 14, 0, acts[i].l) + 44;
        bool en = i == 0 || sel >= 0;
        if (en && ui_hover(x, 8, w, 32)) g_rrect(c, x, 8, w, 32, 5, 5, TH.hover);
        icon_glyph(c, acts[i].g, x + 17, 24, 16, en ? TH.text : TH.dim);
        text(c, F_REG, 14, 0, x + 32, 29, en ? TH.text : TH.dim, acts[i].l);
        if (en && ui_click(x, 8, w, 32)) {
            if (i == 0) new_folder();
            else if (i == 1) { snprintf(fieldbuf, sizeof fieldbuf, "%s", ents[sel].name); modal = 1; }
            else modal = 2;
        }
        x += w + 4;
    }
    if (ui_icon_btn(W - 90, 8, 36, 32, G_LIST, !grid_view)) grid_view = 0;
    if (ui_icon_btn(W - 50, 8, 36, 32, G_GRID, grid_view)) grid_view = 1;
    // breadcrumb
    float by = TOOLH;
    g_rrect(c, 10, by + 4, W - 20, CRUMBH - 10, 6, 6, TH.dark ? 0xFF2B2B2B : 0xFFFFFFFF);
    g_rrect_line(c, 10, by + 4, W - 20, CRUMBH - 10, 6, 1, TH.line);
    icon_glyph(c, G_HOME, 30, by + 21, 15, TH.sub);
    float bx = 46;
    {
        char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", cwd);
        char acc[1024] = "";
        char *parts[64]; int np = 0;
        for (char *t = strtok(tmp, "/"); t && np < 64; t = strtok(0, "/")) parts[np++] = t;
        // root crumb
        const char *root = "This PC";
        float w = text_w(F_REG, 14, 0, root) + 16;
        if (ui_hover(bx, by + 8, w, CRUMBH - 18)) g_rrect(c, bx, by + 8, w, CRUMBH - 18, 4, 4, TH.hover);
        text(c, F_REG, 14, 0, bx + 8, by + 26, TH.text, root);
        if (ui_click(bx, by + 8, w, CRUMBH - 18)) go("/", 1);
        bx += w;
        for (int i = 0; i < np; i++) {
            icon_glyph(c, G_CHEV_R, bx + 7, by + 21, 9, TH.sub);
            bx += 16;
            strcat(acc, "/"); strcat(acc, parts[i]);
            w = text_w(F_REG, 14, 0, parts[i]) + 16;
            if (ui_hover(bx, by + 8, w, CRUMBH - 18)) g_rrect(c, bx, by + 8, w, CRUMBH - 18, 4, 4, TH.hover);
            text(c, i == np - 1 ? F_SEMI : F_REG, 14, 0, bx + 8, by + 26, TH.text, parts[i]);
            if (ui_click(bx, by + 8, w, CRUMBH - 18)) { char a2[1024]; snprintf(a2, sizeof a2, "%s", acc); go(a2, 1); }
            bx += w;
        }
    }
    // navigation pane (matches reference design)
    float top = TOOLH + CRUMBH, ph = H - top - STATH;
    g_rect(c, 0, top, NAVW, ph, 0xFF060F13);
    g_rect(c, NAVW - 1, top, 1, ph, 0xFF122329);

    struct { const char *l, *p; int g; } nav_items[] = {
        { "Récents", "/home/user/.config/recent", G_CLOCK },
        { "Dossier personnel", "/home/user", G_HOME },
        { "Bureau", "/home/user/Desktop", G_DISPLAY },
        { "Documents", "/home/user/Documents", G_FILE },
        { "Images", "/home/user/Pictures", G_PALETTE },
        { "Musique", "/home/user/Music", G_VOLUME },
        { "Vidéos", "/home/user/Videos", G_STAR },
        { "Téléchargements", "/home/user/Downloads", G_CHEV_D },
        { "Corbeille", "/home/user/Trash", G_TRASH }
    };
    int nnav = sizeof(nav_items) / sizeof(nav_items[0]);

    float ny = top + 10;
    for (int i = 0; i < nnav; i++) {
        bool on = !strcmp(cwd, nav_items[i].p);
        if (on) {
            g_rrect(c, 10, ny, NAVW - 20, 32, 6, 6, 0x33F5AD32);
            g_rrect_line(c, 10, ny, NAVW - 20, 32, 6, 1.5f, 0xFFF5AD32);
        } else if (ui_hover(10, ny, NAVW - 20, 32)) {
            g_rrect(c, 10, ny, NAVW - 20, 32, 6, 6, 0x18FFFFFF);
        }
        icon_glyph(c, nav_items[i].g, 28, ny + 16, 16, on ? 0xFFFFD477 : 0xFF8AA0A6);
        text(c, on ? F_SEMI : F_REG, 13, 0, 48, ny + 21, on ? 0xFFFFFFFF : 0xFFCBD5E1, nav_items[i].l);
        if (ui_click(10, ny, NAVW - 20, 32)) go(nav_items[i].p, 1);
        ny += 36;
    }

    // Devices & Network section
    ny += 12;
    text(c, F_SEMI, 12, 0, 16, ny, 0xFF527078, "Périphériques");
    ny += 16;
    icon_glyph(c, G_SAVE, 26, ny + 10, 16, 0xFFE0A21B);
    text(c, F_REG, 13, 0, 44, ny + 15, 0xFFE2E8F0, "Disque système");
    ny += 22;
    // Storage bar
    g_rrect(c, 44, ny, 140, 5, 2.5f, 2.5f, 0xFF142830);
    g_rrect(c, 44, ny, 60, 5, 2.5f, 2.5f, 0xFF2FD5C6);
    ny += 14;
    text(c, F_REG, 11, 0, 44, ny, 0xFF708890, "42.8 Go / 100 Go");

    ny += 22;
    text(c, F_SEMI, 12, 0, 16, ny, 0xFF527078, "Réseau");
    ny += 16;
    icon_glyph(c, G_NET, 26, ny + 10, 16, 0xFFE0A21B);
    text(c, F_REG, 13, 0, 44, ny + 15, 0xFFE2E8F0, "Réseau local");

    // content area
    float cx = NAVW, cw = W - NAVW;
    g_rect(c, cx, top, cw, ph, 0xFF040A0D);

    int sc0[4]; g_getclip(c, sc0);
    g_clip(c, cx, top, cx + cw, top + ph);
    float content_h;

    if (!grid_view) {
        float hy = top + 4;
        text(c, F_REG, 13, 0, cx + 48, hy + 20, TH.sub, "Nom");
        text(c, F_REG, 13, 0, cx + cw * 0.48f, hy + 20, TH.sub, "Date de modification");
        text(c, F_REG, 13, 0, cx + cw * 0.68f, hy + 20, TH.sub, "Type");
        text(c, F_REG, 13, 0, cx + cw * 0.84f, hy + 20, TH.sub, "Taille");
        g_rect(c, cx + 12, hy + 32, cw - 32, 1, 0xFF122329);
        float ly = hy + 38;
        content_h = nents * ROWH + 50;
        int sc1[4]; g_getclip(c, sc1);
        g_clip(c, cx, ly, cx + cw, top + ph);
        for (int i = 0; i < nents; i++) {
            float ry = ly + i * ROWH - sc.pos;
            if (ry + ROWH < ly || ry > top + ph) continue;
            Ent *e = &ents[i];
            if (i == sel) g_rrect(c, cx + 8, ry, cw - 24, ROWH - 2, 5, 5, 0x22F5AD32);
            else if (ui_hover(cx + 8, ry, cw - 24, ROWH - 2) && ui_in(cx, ly, cw, top + ph - ly)) g_rrect(c, cx + 8, ry, cw - 24, ROWH - 2, 5, 5, 0x14FFFFFF);
            if (e->dir) icon_folder(c, cx + 18, ry + 5, 22); else icon_file(c, e->name, cx + 18, ry + 5, 22);
            char *nm = text_ellipsize(F_REG, 14, e->name, cw * 0.48f - 60);
            text(c, F_REG, 14, 0, cx + 48, ry + 22, 0xFFE2E8F0, nm); free(nm);
            char d[64]; struct tm tm; localtime_r(&e->mtime, &tm); strftime(d, sizeof d, "%d/%m/%Y %H:%M", &tm);
            text(c, F_REG, 13, 0, cx + cw * 0.48f, ry + 22, 0xFF708890, d);
            const char *ext = strrchr(e->name, '.');
            char ty[64]; if (e->dir) snprintf(ty, sizeof ty, "Dossier"); else if (e->exec) snprintf(ty, sizeof ty, "Application"); else if (ext) snprintf(ty, sizeof ty, "Fichier %s", ext + 1); else snprintf(ty, sizeof ty, "Fichier");
            text(c, F_REG, 13, 0, cx + cw * 0.68f, ry + 22, 0xFF708890, ty);
            if (!e->dir) { char sz[32]; human_size(e->size, sz, sizeof sz); text(c, F_REG, 13, 0, cx + cw * 0.84f, ry + 22, 0xFF708890, sz); }
            if (ui_in(cx, ly, cw, top + ph - ly)) {
                if (ui_dblclick(cx + 8, ry, cw - 24, ROWH - 2)) open_item(i);
                else if (ui_downin(cx + 8, ry, cw - 24, ROWH - 2)) { sel = i; ctx_open = 0; }
                else if (ui_rclick(cx + 8, ry, cw - 24, ROWH - 2)) { sel = i; ctx_open = 1; ctx_x = U.mx; ctx_y = U.my; ctx_item = i; }
            }
        }
        g_setclip(c, sc1);
        ui_scroll(&sc, cx, ly, cw - 8, top + ph - ly, content_h);
    } else {
        // Modern Teal Glass Folder Cards (Matches files_explorer_windows_exemple.png)
        int card_w = 148, card_h = 138;
        int per = maxi(1, (int)((cw - 32) / (card_w + 16)));
        float gy = top + 18;
        content_h = ((nents + per - 1) / per) * (card_h + 16) + 32;

        for (int i = 0; i < nents; i++) {
            float gx = cx + 20 + (i % per) * (card_w + 16), yy = gy + (i / per) * (card_h + 16) - sc.pos;
            if (yy + card_h < top || yy > top + ph) continue;
            Ent *e = &ents[i];
            bool hov = ui_hover(gx, yy, card_w, card_h);
            bool is_sel = (i == sel);

            // Folder Glass Card Body
            if (e->dir) {
                g_vgrad(c, gx, yy, card_w, card_h, 12, hov ? 0xDD15363D : 0xAA0D242A, hov ? 0xEE091B20 : 0xDD061318);
                g_rrect_line(c, gx, yy, card_w, card_h, 12, 1.5f, is_sel ? 0xFFF5AD32 : (hov ? 0xFF2CD5C6 : 0x401E4A52));

                // Glowing folder icon
                icon_folder(c, gx + card_w / 2 - 28, yy + 18, 56);

                // Title
                char *nm = text_ellipsize(F_SEMI, 14, e->name, card_w - 16);
                text_c(c, F_SEMI, 14, 0, gx + card_w / 2, yy + 94, 0xFFFFFFFF, nm);
                free(nm);

                // Subtitle (element count or subtitle)
                text_c(c, F_REG, 12, 0, gx + card_w / 2, yy + 114, 0xFF35D6C7, "Dossier");
            } else {
                g_vgrad(c, gx, yy, card_w, card_h, 12, hov ? 0xCC132026 : 0x88091419, hov ? 0xDD09151A : 0xBB050D11);
                g_rrect_line(c, gx, yy, card_w, card_h, 12, 1.2f, is_sel ? 0xFFF5AD32 : (hov ? 0x60FFFFFF : 0x24FFFFFF));

                icon_file(c, e->name, gx + card_w / 2 - 24, yy + 20, 48);

                char *nm = text_ellipsize(F_REG, 13, e->name, card_w - 16);
                text_c(c, F_REG, 13, 0, gx + card_w / 2, yy + 94, 0xFFE2E8F0, nm);
                free(nm);

                char sz[32]; human_size(e->size, sz, sizeof sz);
                text_c(c, F_REG, 11, 0, gx + card_w / 2, yy + 114, 0xFF708890, sz);
            }

            if (ui_dblclick(gx, yy, card_w, card_h)) open_item(i);
            else if (ui_downin(gx, yy, card_w, card_h)) { sel = i; ctx_open = 0; }
            else if (ui_rclick(gx, yy, card_w, card_h)) { sel = i; ctx_open = 1; ctx_x = U.mx; ctx_y = U.my; ctx_item = i; }
        }
        ui_scroll(&sc, cx, top, cw - 8, ph, content_h);
    }
    if (!nents) text_c(c, F_REG, 15, 0, cx + cw / 2, top + 140, 0xFF708890, "Ce dossier est vide.");
    if (ui_rclick(cx, top, cw, ph)) { ctx_open = 1; ctx_x = U.mx; ctx_y = U.my; ctx_item = -1; }
    if (ui_downin(cx, top, cw, ph) && !ctx_open) sel = -1;
    g_setclip(c, sc0);
    // keyboard
    if (U.ev == EV_KEY && !U.consumed) {
        if (U.key == K_DOWN && nents) sel = mini(nents - 1, sel + 1);
        else if (U.key == K_UP && nents) sel = maxi(0, sel - 1);
        else if (U.key == K_ENTER && sel >= 0) open_item(sel);
        else if (U.key == K_DEL && sel >= 0) modal = 2;
        else if (U.key == K_F1 + 1 && sel >= 0) { snprintf(fieldbuf, sizeof fieldbuf, "%s", ents[sel].name); modal = 1; }
        else if (U.key == K_BACKSPACE && hpos > 0) { hpos--; snprintf(cwd, sizeof cwd, "%s", hist[hpos]); load(); }
    }
    // status bar
    g_rect(c, 0, H - STATH, W, STATH, 0);
    char st[300];
    if (sel >= 0 && sel < nents) { char sz[32]; human_size(ents[sel].size, sz, sizeof sz); snprintf(st, sizeof st, "%d items   ·   1 item selected%s%s", nents, ents[sel].dir ? "" : "   ·   ", ents[sel].dir ? "" : sz); }
    else snprintf(st, sizeof st, "%s", status);
    text(c, F_REG, 13, 0, 16, H - 9, TH.sub, st);
    // context menu
    if (ctx_open && !modal) {
        const char *L[] = { "Ouvrir", "Renommer", "Supprimer", "Nouveau dossier" };
        int G[] = { G_FILE, G_EDIT, G_TRASH, G_PLUS };
        int first = ctx_item < 0 ? 3 : 0;
        float mw = 190, mh = (4 - first) * 36 + 8, mx = fminf(ctx_x, W - mw - 8), my = fminf(ctx_y, H - mh - 8);
        g_shadow(c, mx, my, mw, mh, 8, 18, 70, 4);
        g_rrect(c, mx, my, mw, mh, 8, 8, TH.surface);
        g_rrect_line(c, mx, my, mw, mh, 8, 1, TH.line);
        for (int i = first; i < 4; i++) {
            float iy = my + 4 + (i - first) * 36;
            if (ui_hover(mx + 4, iy, mw - 8, 34)) g_rrect(c, mx + 4, iy, mw - 8, 34, 5, 5, TH.hover);
            icon_glyph(c, G[i], mx + 22, iy + 17, 15, TH.text);
            text(c, F_REG, 14, 0, mx + 42, iy + 22, TH.text, L[i]);
            if (ui_click(mx + 4, iy, mw - 8, 34)) {
                ctx_open = 0;
                if (i == 0) open_item(ctx_item);
                else if (i == 1) { snprintf(fieldbuf, sizeof fieldbuf, "%s", ents[ctx_item].name); modal = 1; }
                else if (i == 2) modal = 2;
                else new_folder();
            }
        }
        if (U.ev == EV_DOWN && !ui_in(mx, my, mw, mh)) ctx_open = 0;
    }
    U.ev = ev;
    if (modal) draw_modal(c);
}

static void init(int argc, char **argv) {
    go(argc > 1 ? argv[1] : "/home/user", 1);
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "explorer", .title = "Fichiers", .w = 1100, .h = 660, .mica = 1, .init = init, .draw = draw };
    return app_main(&a, argc, argv);
}
