// File Explorer: real filesystem browsing with navigation pane, breadcrumb, list/grid
// views, open in Notepad, new folder, rename, delete. Its own process.
#include "w12.h"
#include <dirent.h>
#include <sys/stat.h>

typedef struct { char name[256]; int dir; long size; time_t mtime; int exec; } Ent;
static Ent *ents; static int nents;
static char cwd[1024] = "/home/user";
static char hist[32][1024]; static int nhist, hpos = -1;
static int sel = -1, grid_view;
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
        snprintf(fieldbuf, sizeof fieldbuf, k == 1 ? "New folder" : "New folder (%d)", k);
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
        text(c, F_SEMI, 20, 0, x + 24, y + 42, TH.text, "Rename");
        U.focus = 5;
        int r = ui_field(5, fieldbuf, sizeof fieldbuf, x + 24, y + 64, w - 48, 36, "Name");
        bool ok = ui_button(x + w - 252, y + h - 50, 110, 34, "Rename", B_ACCENT) || r == 1;
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
        text(c, F_REG, 13, 0, x + 24, y + 108, TH.sub, "There is no Recycle Bin in Windows 12 (yet).");
        if (ui_button(x + w - 252, y + h - 50, 110, 34, "Delete", B_ACCENT) || ui_key(K_ENTER, 0)) {
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
    struct { const char *l; int g; } acts[] = { { "New folder", G_PLUS }, { "Rename", G_EDIT }, { "Delete", G_TRASH } };
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
    // navigation pane
    float top = TOOLH + CRUMBH, ph = H - top - STATH;
    struct { const char *l, *p; int folder; } nav[] = {
        { "Home", "/home/user", 0 }, { "Desktop", "/home/user/Desktop", 1 }, { "Documents", "/home/user/Documents", 1 },
        { "Downloads", "/home/user/Downloads", 1 }, { "Pictures", "/home/user/Pictures", 1 }, { "Music", "/home/user/Music", 1 },
        { "This PC", "/", 2 }, { "System (/bin)", "/bin", 1 } };
    for (int i = 0; i < 8; i++) {
        float ny = top + 8 + i * 38 + (i >= 6 ? 14 : 0);
        if (i == 6) g_rect(c, 16, ny - 9, NAVW - 30, 1, TH.line);
        bool on = !strcmp(cwd, nav[i].p);
        if (on) g_rrect(c, 8, ny, NAVW - 16, 34, 5, 5, TH.dark ? 0x22FFFFFF : 0x0F000000);
        else if (ui_hover(8, ny, NAVW - 16, 34)) g_rrect(c, 8, ny, NAVW - 16, 34, 5, 5, TH.hover);
        if (on) g_rrect(c, 8, ny + 9, 3, 16, 1.5f, 1.5f, TH.accent);
        if (nav[i].folder == 1) icon_folder(c, 22, ny + 6, 22);
        else if (nav[i].folder == 2) icon_glyph(c, G_DISPLAY, 33, ny + 17, 18, TH.accent);
        else icon_glyph(c, G_HOME, 33, ny + 17, 18, TH.accent);
        text(c, F_REG, 14, 0, 54, ny + 22, TH.text, nav[i].l);
        if (ui_click(8, ny, NAVW - 16, 34)) go(nav[i].p, 1);
    }
    // content
    float cx = NAVW, cw = W - NAVW;
    g_rrect(c, cx, top, cw - 8, ph, 8, 8, TH.dark ? 0xFF1F1F1F : 0xFFFFFFFF);
    int sc0[4]; g_getclip(c, sc0);
    g_clip(c, cx, top, cx + cw - 8, top + ph);
    float content_h;
    if (!grid_view) {
        float hy = top + 4;
        text(c, F_REG, 13, 0, cx + 48, hy + 20, TH.sub, "Name");
        text(c, F_REG, 13, 0, cx + cw * 0.48f, hy + 20, TH.sub, "Date modified");
        text(c, F_REG, 13, 0, cx + cw * 0.68f, hy + 20, TH.sub, "Type");
        text(c, F_REG, 13, 0, cx + cw * 0.84f, hy + 20, TH.sub, "Size");
        g_rect(c, cx + 12, hy + 32, cw - 32, 1, TH.line);
        float ly = hy + 38;
        content_h = nents * ROWH + 50;
        int sc1[4]; g_getclip(c, sc1);
        g_clip(c, cx, ly, cx + cw, top + ph);
        for (int i = 0; i < nents; i++) {
            float ry = ly + i * ROWH - sc.pos;
            if (ry + ROWH < ly || ry > top + ph) continue;
            Ent *e = &ents[i];
            if (i == sel) g_rrect(c, cx + 8, ry, cw - 24, ROWH - 2, 5, 5, TH.accent_soft);
            else if (ui_hover(cx + 8, ry, cw - 24, ROWH - 2) && ui_in(cx, ly, cw, top + ph - ly)) g_rrect(c, cx + 8, ry, cw - 24, ROWH - 2, 5, 5, TH.hover);
            if (e->dir) icon_folder(c, cx + 18, ry + 5, 22); else icon_file(c, e->name, cx + 18, ry + 5, 22);
            char *nm = text_ellipsize(F_REG, 14, e->name, cw * 0.48f - 60);
            text(c, F_REG, 14, 0, cx + 48, ry + 22, TH.text, nm); free(nm);
            char d[64]; struct tm tm; localtime_r(&e->mtime, &tm); strftime(d, sizeof d, "%d/%m/%Y %H:%M", &tm);
            text(c, F_REG, 13, 0, cx + cw * 0.48f, ry + 22, TH.sub, d);
            const char *ext = strrchr(e->name, '.');
            char ty[64]; if (e->dir) snprintf(ty, sizeof ty, "File folder"); else if (e->exec) snprintf(ty, sizeof ty, "Application"); else if (ext) snprintf(ty, sizeof ty, "%s File", ext + 1); else snprintf(ty, sizeof ty, "File");
            for (char *q = ty; *q && q < ty + 8 && !e->dir && !e->exec && ext; q++) *q = toupper(*q);
            text(c, F_REG, 13, 0, cx + cw * 0.68f, ry + 22, TH.sub, ty);
            if (!e->dir) { char sz[32]; human_size(e->size, sz, sizeof sz); text(c, F_REG, 13, 0, cx + cw * 0.84f, ry + 22, TH.sub, sz); }
            if (ui_in(cx, ly, cw, top + ph - ly)) {
                if (ui_dblclick(cx + 8, ry, cw - 24, ROWH - 2)) open_item(i);
                else if (ui_downin(cx + 8, ry, cw - 24, ROWH - 2)) { sel = i; ctx_open = 0; }
                else if (ui_rclick(cx + 8, ry, cw - 24, ROWH - 2)) { sel = i; ctx_open = 1; ctx_x = U.mx; ctx_y = U.my; ctx_item = i; }
            }
        }
        g_setclip(c, sc1);
        ui_scroll(&sc, cx, ly, cw - 8, top + ph - ly, content_h);
    } else {
        int per = maxi(1, (int)((cw - 24) / 118));
        float gy = top + 12;
        content_h = ((nents + per - 1) / per) * 124 + 24;
        for (int i = 0; i < nents; i++) {
            float gx = cx + 12 + (i % per) * 118, yy = gy + (i / per) * 124 - sc.pos;
            Ent *e = &ents[i];
            if (i == sel) g_rrect(c, gx, yy, 110, 116, 6, 6, TH.accent_soft);
            else if (ui_hover(gx, yy, 110, 116)) g_rrect(c, gx, yy, 110, 116, 6, 6, TH.hover);
            if (e->dir) icon_folder(c, gx + 23, yy + 8, 64); else icon_file(c, e->name, gx + 23, yy + 8, 64);
            char *nm = text_ellipsize(F_REG, 13, e->name, 104);
            text_c(c, F_REG, 13, 0, gx + 55, yy + 96, TH.text, nm); free(nm);
            if (ui_dblclick(gx, yy, 110, 116)) open_item(i);
            else if (ui_downin(gx, yy, 110, 116)) { sel = i; ctx_open = 0; }
            else if (ui_rclick(gx, yy, 110, 116)) { sel = i; ctx_open = 1; ctx_x = U.mx; ctx_y = U.my; ctx_item = i; }
        }
        ui_scroll(&sc, cx, top, cw - 8, ph, content_h);
    }
    if (!nents) text_c(c, F_REG, 14, 0, cx + cw / 2, top + 120, TH.sub, "This folder is empty.");
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
        const char *L[] = { "Open", "Rename", "Delete", "New folder" };
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
    static AppDef a = { .exe = "explorer", .title = "File Explorer", .w = 1100, .h = 660, .mica = 1, .init = init, .draw = draw };
    return app_main(&a, argc, argv);
}
