// Notepad: open/save, caret, mouse + keyboard selection, clipboard via the compositor,
// word wrap, unsaved-changes prompt. Its own process, drawn with libw12ui.
#include "w12.h"
#include <dirent.h>
#include <sys/stat.h>

static char *txt; static int len, cap;
static int cur, anc;                 // caret and selection anchor (byte offsets)
static char path[512];
static int modified, wrap = 1;
static Scroll sc;
static int modal;                    // 0 none, 1 unsaved prompt, 2 save as, 3 open
static int quit_after_save;
static char fname[256];
static char status_msg[128]; static double status_t;
static Scroll osc;
#define FS 16
#define LH 24
#define TOOL 46
#define STAT 30
#define PADX 18

static void ensure(int n) { if (len + n + 1 > cap) { cap = (len + n + 1) * 2; txt = realloc(txt, cap); } }
static void set_text(const char *s) { len = strlen(s); cap = len + 64; free(txt); txt = malloc(cap); memcpy(txt, s, len + 1); cur = anc = 0; modified = 0; sc.pos = 0; }
static void title(void) {
    char t[600];
    const char *b = path[0] ? strrchr(path, '/') + 1 : "Untitled";
    snprintf(t, sizeof t, "%s%s - Notepad", modified ? "*" : "", b);
    app_set_title(t);
}
static void flash(const char *m) { snprintf(status_msg, sizeof status_msg, "%s", m); status_t = now(); }
static int sel_a(void) { return cur < anc ? cur : anc; }
static int sel_b(void) { return cur < anc ? anc : cur; }
static void del_range(int a, int b) { if (b <= a) return; memmove(txt + a, txt + b, len - b + 1); len -= b - a; cur = anc = a; modified = 1; }
static void insert(const char *s) {
    if (sel_a() != sel_b()) del_range(sel_a(), sel_b());
    int n = strlen(s);
    ensure(n);
    memmove(txt + cur + n, txt + cur, len - cur + 1);
    memcpy(txt + cur, s, n);
    len += n; cur += n; anc = cur; modified = 1;
}

static int load(const char *p) {
    size_t n;
    char *d = read_file(p, &n);
    if (!d) { flash("Couldn't open the file"); return -1; }
    set_text(d);
    free(d);
    snprintf(path, sizeof path, "%s", p);
    app_recent(path);
    MARK("NOTEPAD_OPEN %s %zu", path, n);
    title();
    return 0;
}
static int save(void) {
    if (!path[0]) { modal = 2; snprintf(fname, sizeof fname, "Untitled.txt"); return -1; }
    if (write_file(path, txt, len)) { flash("Save failed"); MARK("ERROR notepad save %s", path); return -1; }
    sync();
    modified = 0;
    app_recent(path);
    MARK("SAVED %s %d", path, len);
    flash("Saved");
    title();
    return 0;
}

// ---- layout: visual lines ----------------------------------------------------------------------
typedef struct { int st, ln; } VL;
static VL *vl; static int nvl, vcap;
static void layout(float width) {
    nvl = 0;
    int p = 0;
    static int st[4096], ln[4096];
    for (;;) {
        char *e = memchr(txt + p, '\n', len - p);
        int l = e ? (int)(e - (txt + p)) : len - p;
        char save_c = txt[p + l]; txt[p + l] = 0;
        int n = 1;
        if (wrap && l > 0) n = text_wrap(F_REG, FS, txt + p, width, st, ln, 4096);
        else { st[0] = 0; ln[0] = l; }
        txt[p + l] = save_c;
        for (int i = 0; i < n; i++) {
            if (nvl >= vcap) { vcap = vcap ? vcap * 2 : 256; vl = realloc(vl, vcap * sizeof *vl); }
            vl[nvl].st = p + st[i]; vl[nvl].ln = ln[i];
            nvl++;
        }
        if (!e) break;
        p += l + 1;
    }
}
static int line_of(int off) {
    for (int i = nvl - 1; i >= 0; i--) if (off >= vl[i].st) return i;
    return 0;
}
static int off_at(int line, float x) {
    line = mini(maxi(line, 0), nvl - 1);
    VL *v = &vl[line];
    int best = v->st;
    for (int i = 0; i <= v->ln; i++) {
        if (i < v->ln && ((unsigned char)txt[v->st + i] & 0xc0) == 0x80) continue;
        float w = text_wn(F_REG, FS, 0, txt + v->st, i);
        if (w > x) { float pw = text_wn(F_REG, FS, 0, txt + v->st, best - v->st); return (x - pw < w - x) ? best : v->st + i; }
        best = v->st + i;
    }
    return v->st + v->ln;
}

static void copy_sel(void) {
    if (sel_a() == sel_b()) return;
    char *s = strndup(txt + sel_a(), sel_b() - sel_a());
    app_clip_set(s);
    free(s);
}

static void key(void) {
    int k = U.key, m = U.mods;
    bool sh = m & MOD_SHIFT, ctl = m & MOD_CTRL;
    int vline = line_of(cur);
    if (ctl) {
        if (k == 's') { save(); return; }
        if (k == 'a') { anc = 0; cur = len; return; }
        if (k == 'c') { copy_sel(); return; }
        if (k == 'x') { copy_sel(); del_range(sel_a(), sel_b()); return; }
        if (k == 'v') { char *s = app_clip_get(); insert(s); free(s); return; }
        if (k == 'n') { if (!modified) { set_text(""); path[0] = 0; title(); } return; }
        if (k == 'o') { modal = 3; return; }
        if (k == K_HOME) { cur = 0; if (!sh) anc = cur; return; }
        if (k == K_END) { cur = len; if (!sh) anc = cur; return; }
    }
    switch (k) {
    case K_LEFT: if (!sh && cur != anc) { cur = anc = sel_a(); return; } if (cur > 0) { cur--; while (cur > 0 && ((unsigned char)txt[cur] & 0xc0) == 0x80) cur--; } if (!sh) anc = cur; return;
    case K_RIGHT: if (!sh && cur != anc) { cur = anc = sel_b(); return; } if (cur < len) { cur++; while (cur < len && ((unsigned char)txt[cur] & 0xc0) == 0x80) cur++; } if (!sh) anc = cur; return;
    case K_UP: case K_DOWN: {
        float x = text_wn(F_REG, FS, 0, txt + vl[vline].st, cur - vl[vline].st);
        int nl = vline + (k == K_UP ? -1 : 1);
        if (nl >= 0 && nl < nvl) cur = off_at(nl, x);
        if (!sh) anc = cur;
        return;
    }
    case K_HOME: cur = vl[vline].st; if (!sh) anc = cur; return;
    case K_END: cur = vl[vline].st + vl[vline].ln; if (!sh) anc = cur; return;
    case K_PGUP: case K_PGDN: { int nl = vline + (k == K_PGUP ? -20 : 20); cur = off_at(nl, 0); if (!sh) anc = cur; return; }
    case K_BACKSPACE:
        if (cur != anc) del_range(sel_a(), sel_b());
        else if (cur > 0) { int a = cur - 1; while (a > 0 && ((unsigned char)txt[a] & 0xc0) == 0x80) a--; del_range(a, cur); }
        return;
    case K_DEL:
        if (cur != anc) del_range(sel_a(), sel_b());
        else if (cur < len) { int b = cur + 1; while (b < len && ((unsigned char)txt[b] & 0xc0) == 0x80) b++; del_range(cur, b); }
        return;
    case K_ENTER: insert("\n"); return;
    case K_TAB: insert("    "); return;
    }
    if (U.text[0] && (unsigned char)U.text[0] >= 32) insert(U.text);
}

static void draw_modal(Canvas *c) {
    g_rect(c, 0, 0, U.w, U.h, TH.dark ? 0x80000000 : 0x50000000);
    float w = 460, h = modal == 3 ? 420 : 210, x = (U.w - w) / 2, y = (U.h - h) / 2;
    g_shadow(c, x, y, w, h, 10, 30, 90, 8);
    g_rrect(c, x, y, w, h, 10, 10, TH.surface);
    g_rrect_line(c, x, y, w, h, 10, 1, TH.line);
    if (modal == 1) {
        text(c, F_SEMI, 20, 0, x + 24, y + 44, TH.text, "Notepad");
        const char *b = path[0] ? strrchr(path, '/') + 1 : "Untitled";
        char q[400];
        snprintf(q, sizeof q, "Do you want to save changes to %s?", b);
        text(c, F_REG, 15, 0, x + 24, y + 86, TH.text, q);
        g_rrect(c, x, y + h - 70, w, 70, 0, 10, TH.bg);
        if (ui_button(x + w - 372, y + h - 52, 110, 34, "Save", B_ACCENT) || ui_key(K_ENTER, 0)) { quit_after_save = 1; modal = 0; if (save() == 0) app_quit(); }
        if (ui_button(x + w - 252, y + h - 52, 110, 34, "Don't save", B_STD) || ui_key('n', 0)) { MARK("NOTEPAD_DISCARD"); app_quit(); }
        if (ui_button(x + w - 132, y + h - 52, 110, 34, "Cancel", B_STD) || ui_key(K_ESC, 0)) modal = 0;
    } else if (modal == 2) {
        text(c, F_SEMI, 20, 0, x + 24, y + 44, TH.text, "Save as");
        text(c, F_REG, 13, 0, x + 24, y + 74, TH.sub, "File name (saved in Documents)");
        U.focus = 77;
        int r = ui_field(77, fname, sizeof fname, x + 24, y + 86, w - 48, 36, "name.txt");
        g_rrect(c, x, y + h - 70, w, 70, 0, 10, TH.bg);
        bool ok = ui_button(x + w - 252, y + h - 52, 110, 34, "Save", B_ACCENT) || r == 1;
        if (ui_button(x + w - 132, y + h - 52, 110, 34, "Cancel", B_STD) || ui_key(K_ESC, 0)) { modal = 0; quit_after_save = 0; }
        if (ok && fname[0] && !strchr(fname, '/')) {
            snprintf(path, sizeof path, "/home/user/Documents/%s", fname);
            modal = 0;
            if (save() == 0 && quit_after_save) app_quit();
        }
    } else if (modal == 3) {
        text(c, F_SEMI, 20, 0, x + 24, y + 44, TH.text, "Open");
        text(c, F_REG, 13, 0, x + 24, y + 70, TH.sub, "/home/user/Documents");
        DIR *d = opendir("/home/user/Documents");
        float ly = y + 86, lh = 36;
        int i = 0;
        int sc_[4]; g_getclip(c, sc_);
        g_clip(c, x, ly, x + w, y + h - 76);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
            float iy = ly + i * lh - osc.pos;
            if (ui_hover(x + 16, iy, w - 32, lh - 2)) g_rrect(c, x + 16, iy, w - 32, lh - 2, 5, 5, TH.hover);
            icon_file(c, e->d_name, x + 24, iy + 6, 24);
            text(c, F_REG, 15, 0, x + 58, iy + 23, TH.text, e->d_name);
            if (ui_click(x + 16, iy, w - 32, lh - 2)) { char p[600]; snprintf(p, sizeof p, "/home/user/Documents/%s", e->d_name); modal = 0; load(p); }
            i++;
        }
        if (d) closedir(d);
        g_setclip(c, sc_);
        ui_scroll(&osc, x, ly, w, y + h - 76 - ly, i * lh);
        g_rrect(c, x, y + h - 70, w, 70, 0, 10, TH.bg);
        if (ui_button(x + w - 132, y + h - 52, 110, 34, "Cancel", B_STD) || ui_key(K_ESC, 0)) modal = 0;
    }
}

static void draw(Canvas *c) {
    int ev = U.ev;
    if (modal) U.ev = EV_NONE;   // the modal gets the event, not the editor
    // toolbar
    g_rect(c, 0, 0, U.w, TOOL, c_alpha(TH.surface, 0.5f));
    float bx = 10;
    struct { const char *l; int g; } tb[] = { { "New", G_FILE }, { "Open", G_FOLDER }, { "Save", G_SAVE }, { "Copy", G_COPY }, { "Paste", G_PASTE } };
    for (int i = 0; i < 5; i++) {
        float w = text_w(F_REG, 14, 0, tb[i].l) + 44;
        bool h = ui_hover(bx, 7, w, 32);
        if (h) g_rrect(c, bx, 7, w, 32, 5, 5, TH.hover);
        icon_glyph(c, tb[i].g, bx + 17, 23, 16, TH.text);
        text(c, F_REG, 14, 0, bx + 32, 28, TH.text, tb[i].l);
        if (ui_click(bx, 7, w, 32)) {
            if (i == 0 && !modified) { set_text(""); path[0] = 0; title(); }
            else if (i == 0) modal = 1;
            if (i == 1) modal = 3;
            if (i == 2) save();
            if (i == 3) copy_sel();
            if (i == 4) { char *s = app_clip_get(); insert(s); free(s); }
        }
        bx += w + 4;
    }
    float wx = U.w - 150;
    text(c, F_REG, 14, 0, wx, 28, TH.sub, "Word wrap");
    ui_toggle(wx + 82, 12, &wrap);
    g_rect(c, 0, TOOL, U.w, 1, TH.line);
    // editor area
    float ex = 0, ey = TOOL + 1, ew = U.w, eh = U.h - TOOL - 1 - STAT;
    g_rect(c, ex, ey, ew, eh, TH.dark ? 0xFF1E1E1E : 0xFFFFFFFF);
    layout(ew - 2 * PADX - 12);
    float content = nvl * LH + 24;
    ui_scroll(&sc, ex, ey, ew, eh, content);
    float top = ey + 12 - sc.pos;
    // mouse: place caret / select
    if (!modal && (U.ev == EV_DOWN || (U.ev == EV_MOVE && (U.buttons & 1) && ui_in(0, 0, U.w, U.h) && U.py > ey)) && U.my >= ey && U.my < ey + eh && U.mx < ew - 14 && !U.consumed) {
        int ln = (int)floorf((U.my - top) / LH);
        int o = off_at(ln, U.mx - PADX);
        if (U.ev == EV_DOWN && U.btn == 1) { cur = o; if (!(U.mods & MOD_SHIFT)) anc = o; if (U.clicks == 2) { anc = o; while (anc > 0 && txt[anc - 1] != ' ' && txt[anc - 1] != '\n') anc--; cur = o; while (cur < len && txt[cur] != ' ' && txt[cur] != '\n') cur++; } }
        else if (U.ev == EV_MOVE) cur = o;
    }
    if (!modal && U.ev == EV_KEY && !U.consumed) {
        key();
        // keep the caret visible
        int l = line_of(cur);
        layout(ew - 2 * PADX - 12);
        l = line_of(cur);
        float cy = 12 + l * LH;
        if (cy < sc.pos) sc.pos = cy - 12;
        if (cy + LH > sc.pos + eh - 12) sc.pos = cy + LH - eh + 24;
        title();
    }
    int scl[4]; g_getclip(c, scl);
    g_clip(c, ex, ey, ex + ew, ey + eh);
    int a = sel_a(), b = sel_b();
    for (int i = 0; i < nvl; i++) {
        float y = top + i * LH;
        if (y + LH < ey || y > ey + eh) continue;
        VL *v = &vl[i];
        if (a != b && b >= v->st && a <= v->st + v->ln) {
            int s0 = maxi(a, v->st) - v->st, s1 = mini(b, v->st + v->ln) - v->st;
            float x0 = text_wn(F_REG, FS, 0, txt + v->st, s0), x1 = text_wn(F_REG, FS, 0, txt + v->st, s1);
            if (b > v->st + v->ln) x1 += 6;
            g_rect(c, PADX + x0, y + 2, x1 - x0, LH, TH.sel);
        }
        text_n(c, F_REG, FS, 0, PADX, y + 18, TH.text, txt + v->st, v->ln);
    }
    if (app_focused || !modal) {
        int l = line_of(cur);
        float cx = PADX + text_wn(F_REG, FS, 0, txt + vl[l].st, cur - vl[l].st);
        g_rect(c, cx, top + l * LH + 3, 1.5f, LH - 2, TH.text);
    }
    g_setclip(c, scl);
    // status bar
    float sy = U.h - STAT;
    g_rect(c, 0, sy, U.w, STAT, c_alpha(TH.surface, 0.6f));
    g_rect(c, 0, sy, U.w, 1, TH.line);
    int l = line_of(cur), col = cur - vl[l].st + 1, ln = 1;
    for (int i = 0; i < cur; i++) if (txt[i] == '\n') ln++;
    char st[200];
    snprintf(st, sizeof st, "Ln %d, Col %d   ·   %d characters   ·   UTF-8   ·   %s", ln, col, len, wrap ? "Word wrap on" : "Word wrap off");
    text(c, F_REG, 13, 0, 16, sy + 20, TH.sub, st);
    if (status_msg[0] && now() - status_t < 3) text_r(c, F_SEMI, 13, 0, U.w - 16, sy + 20, TH.accent, status_msg);
    U.ev = ev;
    if (modal) draw_modal(c);
}

static int on_close(void) {
    if (!modified) return 1;
    modal = 1;
    MARK("NOTEPAD_PROMPT unsaved");
    return 0;
}

static void init(int argc, char **argv) {
    set_text("");
    if (argc > 1 && argv[1][0]) load(argv[1]);
    title();
}
static void tick(void) { if (status_msg[0] && now() - status_t > 3 && now() - status_t < 3.5) app_redraw(); }

int main(int argc, char **argv) {
    static AppDef a = { .exe = "notepad", .title = "Notepad", .w = 900, .h = 620, .mica = 0, .init = init, .draw = draw, .close = on_close, .tick = tick, .tick_ms = 500 };
    return app_main(&a, argc, argv);
}
