// FLX OS shell surfaces drawn by the compositor: boot animation, lock screen,
// floating glass taskbar, top status bar, Start/Launcher menu, Quick Settings panel
// and the Alt+Tab switcher. Settings persist on the home disk.
#include "comp.h"
#include <unistd.h>

int phase = PH_BOOT, dirty = 1;
int start_open, widgets_open, copilot_open, alttab_open;
float start_p, widgets_p, copilot_p;
int hover_id = -1;
static double boot_t0, unlock_t = -1, lock_shown_t;
static float lock_drag; static float lock_y0; static int lock_down;
static char query[128];
static int power_menu, all_apps, qs_open, dm_open; static float dm_x, dm_y, qs_vol = 0.7f, qs_bright = 1.0f; static int qs_drag;
static int super_alone;
static int alt_sel; static int alt_list[MAXWIN], alt_n;
static char recent[8][256]; static int nrecent;
static int start_marked;
static double tip_t; static int tip_id = -1;

#define TOP 36
#define TBH 56
#define TBY (SCR_H - 12 - TBH)
#define SM_W 680
#define SM_H 700
#define SM_X ((SCR_W - SM_W) / 2)
#define SM_Y (TBY - 12 - SM_H)
static const char *PINNED[] = { "explorer", "browser", "terminal", "settings", "taskmgr", "calc", "minesweeper", "proof" };
#define NPIN 8
enum {
    H_NONE, H_START_BTN, H_SEARCH_BTN, H_WIDGETS_BTN, H_COPILOT_BTN, H_TOP_SEARCH, H_NET, H_CLOCK, H_VOL,
    H_START_PANEL, H_START_SEARCH, H_POWER, H_PWR_LOCK, H_PWR_RESTART, H_PWR_SHUT, H_USER, H_ALLAPPS,
    H_QS_PANEL = 20, H_QS_NET, H_QS_DARK, H_QS_ACCENT, H_QS_WALL, H_QS_LOCK, H_QS_SETTINGS, H_QS_VOL, H_QS_BRIGHT, H_QS_GEAR,
    H_DM_PANEL = 35, H_DM_PERSONAL, H_DM_DISPLAY, H_DM_TERMINAL, H_DM_REFRESH,
    H_TB_APP = 50, H_START_APP = 80, H_RESULT = 100, H_RECENT = 130,
};

// ---- hits ------------------------------------------------------------------------------------
typedef struct { int id; float x, y, w, h; } Hit;
static Hit hits[256]; static int nhits;
void hit_add(int id, float x, float y, float w, float h) { if (nhits < 256) hits[nhits++] = (Hit){ id, x, y, w, h }; }
int hit_at(float x, float y) {
    for (int i = nhits - 1; i >= 0; i--) { Hit *h = &hits[i]; if (x >= h->x && y >= h->y && x < h->x + h->w && y < h->y + h->h) return h->id; }
    return -1;
}

// ---- settings & recent files ------------------------------------------------------------------------
void settings_save(void) {
    char *s = strfmt("dark=%d\naccent=%d\nwall=%d\n", TH.dark, TH.accent_i, TH.wall);
    write_file("/home/.config/flx.conf", s, strlen(s));
    free(s);
    sync();
}
static void settings_load(void) {
    int d = 1, a = 0, w = 0;  // FLX OS defaults: dark mode, gold accent, sunset city
    char *s = read_file("/home/.config/flx.conf", 0);
    if (!s) s = read_file("/home/.config/w12.conf", 0);  // backward compat
    if (s) { char *p; if ((p = strstr(s, "dark="))) d = atoi(p + 5); if ((p = strstr(s, "accent="))) a = atoi(p + 7); if ((p = strstr(s, "wall="))) w = atoi(p + 5); free(s); }
    theme_set(d, a, w);
}
static void recent_load(void) {
    nrecent = 0;
    char *s = read_file("/home/.config/recent", 0);
    if (!s) return;
    for (char *l = strtok(s, "\n"); l && nrecent < 8; l = strtok(0, "\n")) snprintf(recent[nrecent++], 256, "%s", l);
    free(s);
}
void recent_add(const char *path) {
    char out[8 * 260] = "";
    int n = snprintf(out, sizeof out, "%s\n", path);
    for (int i = 0, k = 1; i < nrecent && k < 8; i++) if (strcmp(recent[i], path)) { n += snprintf(out + n, sizeof out - n, "%s\n", recent[i]); k++; }
    write_file("/home/.config/recent", out, n);
    recent_load();
    dirty = 1;
}

// ---- wallpaper -------------------------------------------------------------------------------------
Canvas *wall, *wall_blur;
static int wall_loaded = -1;
static void load_wallpaper(void) {
    if (wall_loaded == TH.wall) return;
    static const char *names[NWALL] = { "bloom", "aurora", "dune" };
    char p[64];
    snprintf(p, sizeof p, "/share/wallpapers/%s.raw", names[TH.wall]);
    size_t n;
    uint32_t *d = (uint32_t *)read_file(p, &n);
    if (!wall) { wall = canvas_new(SCR_W, SCR_H); wall_blur = canvas_new(SCR_W, SCR_H); }
    if (d && n >= 12 + (size_t)SCR_W * SCR_H * 4 && d[1] == SCR_W && d[2] == SCR_H) memcpy(wall->px, d + 3, (size_t)SCR_W * SCR_H * 4);
    else { LOG("shell: wallpaper %s missing", p); g_vgrad(wall, 0, 0, SCR_W, SCR_H, 0, 0xFF0B1A3F, 0xFF1B3C8C); }
    free(d);
    double t = now();
    g_blur_full(wall_blur, wall, 2);
    LOG("shell: wallpaper %s, blur cache built in %.1f ms", names[TH.wall], (now() - t) * 1000);
    wall_loaded = TH.wall;
}

void shell_init(void) {
    settings_load();
    load_wallpaper();
    recent_load();
    boot_t0 = now();
    MARK("READY boot");
}

void lock_now(void) { phase = PH_LOCK; unlock_t = -1; lock_drag = 0; lock_shown_t = now(); close_panels(); dirty = 1; MARK("READY lock"); }
static void unlock(void) { if (unlock_t < 0) { unlock_t = now(); MARK("UNLOCK"); } }
void close_panels(void) {
    if (start_open || widgets_open || copilot_open || qs_open || dm_open) dirty = 1;
    start_open = widgets_open = copilot_open = 0; power_menu = 0; qs_open = 0; dm_open = 0;
}
static void toggle_start(void) {
    int o = !start_open;
    close_panels();
    start_open = o; query[0] = 0; start_marked = 0; all_apps = 0;
    MARK("START %s", o ? "open" : "close");
}

int shell_animating(void) {
    if (phase == PH_BOOT || unlock_t >= 0) return 1;
    if (phase == PH_LOCK) return 1;   // fade-in + the bobbing unlock hint
    float ts = start_open, tw = widgets_open, tc = copilot_open;
    return fabsf(start_p - ts) > 0.001f || fabsf(widgets_p - tw) > 0.001f || fabsf(copilot_p - tc) > 0.001f || copilot_busy;
}

void shell_tick(void) {
    static double last;
    double t = now();
    float dt = (float)fmin(t - last, 0.05); last = t;
    float k = dt / 0.25f;   // panel slides: 250 ms (reads well on 30 fps video)
#define STEP(p, tgt) do { float T_ = (tgt); if (p < T_) p = fminf(T_, p + k); else p = fmaxf(T_, p - k); } while (0)
    float before = start_p + widgets_p + copilot_p;
    STEP(start_p, start_open);
    STEP(widgets_p, widgets_open);
    STEP(copilot_p, copilot_open);
    if (start_p + widgets_p + copilot_p != before) dirty = 1;
    if (phase == PH_BOOT && t - boot_t0 > 3.0) { phase = PH_LOCK; lock_shown_t = t; MARK("READY lock"); dirty = 1; }
    if (phase == PH_LOCK && unlock_t >= 0 && t - unlock_t > 0.42) { phase = PH_DESK; unlock_t = -1; lock_drag = 0; MARK("READY desktop"); dirty = 1; }
    if (wall_loaded != TH.wall) { load_wallpaper(); dirty = 1; }
    static int last_min = -1;
    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    if (tm.tm_min != last_min) { last_min = tm.tm_min; dirty = 1; }
    if (tip_id >= 0 && t - tip_t > 0.6 && t - tip_t < 0.7) dirty = 1;
}

float taskbar_icon_x(const char *exe) {
    for (int i = 0; i < nhits; i++) if (hits[i].id >= H_TB_APP && hits[i].id < H_TB_APP + 20) {
        // slots are registered in order; find by exe through the same ordering as draw_taskbar
    }
    int slot = -1;
    for (int i = 0; i < NPIN; i++) if (!strcmp(PINNED[i], exe)) slot = i;
    for (int i = 0; i < nhits; i++) if (hits[i].id == H_TB_APP + (slot < 0 ? NPIN : slot)) return hits[i].x + hits[i].w / 2;
    return SCR_W / 2;
}

// ---- drawing helpers -------------------------------------------------------------------------------
static uint32_t glass_tint(void) { return TH.dark ? 0xB01C1C20 : 0xB8F3F4F7; }
static uint32_t glass_line(void) { return TH.dark ? 0x30FFFFFF : 0x40FFFFFF; }
static void glass_panel(Canvas *c, float x, float y, float w, float h, float r, float alpha) {
    if (alpha <= 0.01f) return;
    g_shadow(c, x, y, w, h, r, 30, (int)(90 * alpha), 8);
    g_glass(c, x, y, w, h, r, c_alpha(glass_tint(), (glass_tint() >> 24) / 255.0f * fminf(1, alpha * 1.2f)));
    g_rrect_line(c, x, y, w, h, r, 1, c_alpha(glass_line(), alpha));
    g_rrect_line(c, x + 1, y + 1, w - 2, h - 2, r - 1, 1, c_alpha(TH.dark ? 0x22000000 : 0x10000000, alpha));
}
static void clock_strings(char *hm, int n1, char *date, int n2, char *dshort, int n3) {
    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    static const char *DAYS[] = { "Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam" };
    static const char *MON[]  = { "Janv.", "F\u00e9vr.", "Mars", "Avr.", "Mai", "Juin", "Juil.", "Ao\u00fbt", "Sept.", "Oct.", "Nov.", "D\u00e9c." };
    if (hm) snprintf(hm, n1, "%d:%02d", tm.tm_hour, tm.tm_min);
    if (date) snprintf(date, n2, "%s %d %s %d", DAYS[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon], tm.tm_year + 1900);
    if (dshort) snprintf(dshort, n3, "%d/%d/%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

// ---- boot ------------------------------------------------------------------------------------------
static void draw_boot(Canvas *c, float alpha) {
    double t = now() - boot_t0;
    g_rect(c, 0, 0, SCR_W, SCR_H, c_alpha(0xFF000000, alpha));
    float la = clampf((float)t / 0.6f, 0, 1) * alpha;
    icon_logo(c, SCR_W / 2, SCR_H / 2 - 70, 150, (int)(255 * la));
    // Windows-style loading dots: five dots chasing around a circle, slow at the top
    float cx = SCR_W / 2, cy = SCR_H / 2 + 150, R = 26;
    for (int i = 0; i < 5; i++) {
        double tt = t - 0.35 - i * 0.16;
        if (tt < 0) continue;
        float p = (float)fmod(tt, 2.2) / 2.2f;
        float ang = -(float)M_PI / 2 + 2 * (float)M_PI * (p - 0.12f * sinf(2 * (float)M_PI * p) * 1.6f) * 1.0f;
        float a = clampf(fminf(p, 1 - p) * 12, 0, 1) * alpha;
        g_circle(c, cx + R * cosf(ang), cy + R * sinf(ang), 4.2f, c_alpha(0xFFFFFFFF, a));
    }
    text_c(c, F_SEMILIGHT, 14, 0, SCR_W / 2, SCR_H - 48, c_alpha(0xFF8A8A8A, la), "FLX OS  ·  logiciel libre, par Florynx Labs");
}

// ---- lock ------------------------------------------------------------------------------------------
static void draw_lock(Canvas *c) {
    float off = 0;
    if (unlock_t >= 0) off = -SCR_H * ease_inout((float)(now() - unlock_t) / 0.42f);
    else off = -lock_drag;
    int oy = (int)off;
    // blurred wallpaper slides up as a sheet
    int y0 = maxi(0, oy), y1 = mini(SCR_H, SCR_H + oy);
    if (y1 <= y0) return;
    g_buf(c, wall_blur->px + (size_t)(y0 - oy) * SCR_W, SCR_W, SCR_W, y1 - y0, 0, y0, SCR_W, y1 - y0, 0, 0, 255, 1);
    g_rect(c, 0, y0, SCR_W, y1 - y0, 0x30000000);
    char hm[16], date[64];
    clock_strings(hm, sizeof hm, date, sizeof date, 0, 0);
    float fade = clampf((float)(now() - lock_shown_t) / 0.5f, 0, 1);
    uint32_t wc = c_alpha(0xFFFFFFFF, fade);
    text_c(c, F_SEMI, 150, 0, SCR_W / 2, 330 + oy, wc, hm);
    text_c(c, F_SEMILIGHT, 34, 0, SCR_W / 2, 395 + oy, wc, date);
    if (weather.ok) {
        char w[64];
        snprintf(w, sizeof w, "%.0f°C  %s · %s", weather.temp, weather.cond, weather.place);
        float tw = text_w(F_REG, 20, 0, w);
        icon_glyph(c, weather_glyph(weather.code), SCR_W / 2 - tw / 2 - 18, 442 + oy, 22, wc);
        text(c, F_REG, 20, 0, SCR_W / 2 - tw / 2 + 6, 449 + oy, wc, w);
    }
    // bottom hint
    float bob = sinf((float)now() * 3) * 3;
    icon_glyph(c, G_CHEV_U, SCR_W / 2, SCR_H - 112 + oy + bob, 26, c_alpha(0xFFFFFFFF, 0.9f * fade));
    text_c(c, F_REG, 17, 0, SCR_W / 2, SCR_H - 76 + oy, c_alpha(0xFFFFFFFF, 0.85f * fade), "Click, press Enter or swipe up to unlock");
    NetInfo ni; netinfo_read(&ni);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, SCR_W - 110, SCR_H - 50 + oy, 22, wc);
    icon_glyph(c, G_BATTERY, SCR_W - 64, SCR_H - 50 + oy, 24, wc);
}

// ---- top bar -----------------------------------------------------------------------------------------
static void draw_topbar(Canvas *c) {
    g_glass(c, 0, 0, SCR_W, TOP, 0, glass_tint());
    g_rect(c, 0, TOP - 1, SCR_W, 1, TH.dark ? 0x20FFFFFF : 0x20000000);
    uint32_t fg = TH.text;
    // left: FLX OS logo + name (clicking opens widgets panel)
    hit_add(H_WIDGETS_BTN, 6, 3, 150, 30);
    if (hover_id == H_WIDGETS_BTN || widgets_open) g_rrect(c, 6, 3, 150, 30, 6, 6, TH.hover);
    icon_logo(c, 24, TOP / 2, 20, 255);
    text(c, F_SEMI, 13, 0.5f, 38, TOP / 2 + 5, TH.accent, "FLX OS");
    // centre: search
    float sw = 400, sx = (SCR_W - sw) / 2;
    hit_add(H_TOP_SEARCH, sx, 4, sw, 28);
    g_rrect(c, sx, 4, sw, 28, 14, 14, hover_id == H_TOP_SEARCH ? (TH.dark ? 0x30FFFFFF : 0xB0FFFFFF) : (TH.dark ? 0x18FFFFFF : 0x70FFFFFF));
    g_rrect_line(c, sx, 4, sw, 28, 14, 1, TH.dark ? 0x18FFFFFF : 0x14000000);
    icon_glyph(c, G_SEARCH, sx + 20, 18, 14, TH.sub);
    label(c, F_REG, 13, sx + 36, 4, 28, TH.sub, "Rechercher...");
    // right: network, battery, clock, copilot
    NetInfo ni; netinfo_read(&ni);
    float rx = SCR_W - 10;
    rx -= 40; hit_add(H_COPILOT_BTN, rx, 3, 34, 30);
    if (hover_id == H_COPILOT_BTN || copilot_open) g_rrect(c, rx, 3, 34, 30, 6, 6, TH.hover);
    icon_copilot(c, rx + 17, 18, 20);
    char hm[16], ds[32];
    clock_strings(hm, sizeof hm, 0, 0, ds, sizeof ds);
    float wt = text_w(F_SEMI, 13, 0, hm), wd = text_w(F_REG, 13, 0, ds), tw = wt + wd + 32;
    rx -= tw + 6; hit_add(H_CLOCK, rx, 3, tw, 30);
    if (hover_id == H_CLOCK) g_rrect(c, rx, 3, tw, 30, 6, 6, TH.hover);
    text(c, F_REG, 13, 0, rx + 12, 22, TH.sub, ds);
    text(c, F_SEMI, 13, 0, rx + 20 + wd, 22, fg, hm);
    rx -= 4;
    rx -= 110; hit_add(H_NET, rx, 3, 110, 30);
    if (hover_id == H_NET) g_rrect(c, rx, 3, 110, 30, 6, 6, TH.hover);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, rx + 20, 18, 17, fg);
    icon_glyph(c, G_VOLUME, rx + 50, 18, 17, fg);
    icon_glyph(c, G_BATTERY, rx + 82, 18, 19, fg);
}

// ---- taskbar -----------------------------------------------------------------------------------------
typedef struct { const char *exe; int app; } Slot;
static int slots(Slot *s) {
    int n = 0;
    for (int i = 0; i < NPIN; i++) s[n++] = (Slot){ PINNED[i], app_by_exe(PINNED[i]) };
    for (int i = 0; i < MAXWIN && n < 16; i++) {
        Win *w = &wins[i];
        if (!w->used || w->closing || !w->exe[0]) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) if (!strcmp(s[k].exe, w->exe)) dup = 1;
        if (!dup) s[n++] = (Slot){ w->exe, w->app };
    }
    return n;
}
static int running(const char *exe, int *focused, int *count) {
    int r = 0; *focused = 0; *count = 0;
    for (int i = 0; i < MAXWIN; i++) {
        Win *w = &wins[i];
        if (w->used && !w->closing && !strcmp(w->exe, exe)) { r = 1; (*count)++; if (w == focus && !w->minimized) *focused = 1; }
    }
    return r;
}
static void draw_taskbar(Canvas *c) {
    Slot s[16];
    int n = slots(s);
    float slot = 52, w = 20 + (n + 2) * slot + 14, x = (SCR_W - w) / 2, y = TBY;
    glass_panel(c, x, y, w, TBH, 14, 1);
    float ix = x + 10;
    // start
    hit_add(H_START_BTN, ix, y + 4, slot - 4, TBH - 8);
    if (hover_id == H_START_BTN || start_open) g_rrect(c, ix, y + 6, slot - 4, TBH - 12, 8, 8, TH.hover);
    icon_logo(c, ix + slot / 2 - 2, y + TBH / 2, 28, 255);
    ix += slot;
    hit_add(H_SEARCH_BTN, ix, y + 4, slot - 4, TBH - 8);
    if (hover_id == H_SEARCH_BTN) g_rrect(c, ix, y + 6, slot - 4, TBH - 12, 8, 8, TH.hover);
    icon_glyph(c, G_SEARCH, ix + slot / 2 - 2, y + TBH / 2, 22, TH.text);
    ix += slot + 4;
    g_rect(c, ix - 4, y + 16, 1, TBH - 32, TH.dark ? 0x40FFFFFF : 0x30000000);
    ix += 4;
    for (int i = 0; i < n; i++) {
        int id = H_TB_APP + i;
        hit_add(id, ix, y + 4, slot - 4, TBH - 8);
        int foc, cnt;
        int run = running(s[i].exe, &foc, &cnt);
        bool hov = hover_id == id;
        if (hov || foc) g_rrect(c, ix, y + 6, slot - 4, TBH - 12, 8, 8, foc ? (TH.dark ? 0x26FFFFFF : 0x90FFFFFF) : TH.hover);
        float press = (hov && (mouse_buttons & 1)) ? 0.86f : 1;
        float is = 30 * press;
        if (s[i].app >= 0) icon_app(c, s[i].app, ix + (slot - 4) / 2 - is / 2, y + TBH / 2 - is / 2 - 2, is);
        if (run) {
            float iw = foc ? 16 : 6;
            g_rrect(c, ix + (slot - 4) / 2 - iw / 2, y + TBH - 8, iw, 3, 1.5f, 1.5f, foc ? TH.accent : TH.dim);
        }
        ix += slot;
    }
}

// ---- start menu -----------------------------------------------------------------------------------------
static int matches(const char *title, const char *q) {
    if (!q[0]) return 1;
    return strcasestr(title, q) != 0;
}
static int results(int *out) {
    int n = 0;
    for (int i = 0; i < NAPPS; i++) if (matches(APPS[i].title, query) || matches(APPS[i].exe, query)) out[n++] = i;
    for (int i = 0; i < nrecent && n < 12; i++) { const char *b = strrchr(recent[i], '/'); if (matches(b ? b + 1 : recent[i], query)) out[n++] = 1000 + i; }
    return n;
}
static void open_result(int r) {
    if (r >= 1000) launch("notepad", recent[r - 1000]);
    else launch(APPS[r].exe, 0);
    close_panels();
}
static void draw_start(Canvas *c) {
    float p = ease_out(start_p);
    if (p <= 0.01f) return;
    float x = SM_X, y = SM_Y + (1 - p) * 40;
    glass_panel(c, x, y, SM_W, SM_H, 12, p);
    if (p < 0.6f) return;
    hit_add(H_START_PANEL, x, y, SM_W, SM_H);
    float a = clampf((p - 0.6f) / 0.4f, 0, 1);
    uint32_t fg = c_alpha(TH.text, a), sub = c_alpha(TH.sub, a);
    // search field
    float fx = x + 28, fy = y + 26, fw = SM_W - 56, fh = 40;
    hit_add(H_START_SEARCH, fx, fy, fw, fh);
    g_rrect(c, fx, fy, fw, fh, 20, 20, TH.dark ? 0xFF2A2A2E : 0xFFFFFFFF);
    g_rrect(c, fx + 14, fy + fh - 2, fw - 28, 2, 1, 1, query[0] ? TH.accent : 0);
    icon_glyph(c, G_SEARCH, fx + 24, fy + fh / 2, 16, sub);
    if (query[0]) {
        float tw = text(c, F_REG, 16, 0, fx + 44, fy + 26, fg, query);
        g_rect(c, tw + 2, fy + 10, 1.5f, 20, TH.accent);
    } else {
        text(c, F_REG, 16, 0, fx + 44, fy + 26, sub, "Rechercher une application, un fichier, une commande\u2026");
        g_rect(c, fx + 43, fy + 10, 1.5f, 20, c_alpha(TH.accent, (fmod(now(), 1.0) < 0.55) ? a : 0));
    }
    if (query[0]) {
        int r[16];
        int n = results(r);
        text(c, F_SEMI, 15, 0, x + 40, y + 108, fg, n ? "Meilleur résultat" : "Aucun résultat");
        for (int i = 0; i < n && i < 8; i++) {
            float ry = y + 124 + i * 60;
            int id = H_RESULT + i;
            hit_add(id, x + 28, ry, SM_W - 56, 56);
            if (hover_id == id || i == 0) g_rrect(c, x + 28, ry, SM_W - 56, 56, 8, 8, i == 0 ? TH.accent_soft : TH.hover);
            if (r[i] < 1000) {
                icon_app(c, r[i], x + 44, ry + 10, 36);
                text(c, F_SEMI, 15, 0, x + 96, ry + 25, fg, APPS[r[i]].title);
                text(c, F_REG, 13, 0, x + 96, ry + 44, sub, i == 0 ? "Application · Entrée pour ouvrir" : "App");
            } else {
                const char *pth = recent[r[i] - 1000], *b = strrchr(pth, '/');
                icon_file(c, b ? b + 1 : pth, x + 44, ry + 10, 36);
                text(c, F_SEMI, 15, 0, x + 96, ry + 25, fg, b ? b + 1 : pth);
                text(c, F_REG, 13, 0, x + 96, ry + 44, sub, pth);
            }
        }
    } else {
        text(c, F_SEMI, 15, 0, x + 40, y + 106, fg, all_apps ? "All apps" : "Pinned");
        hit_add(H_ALLAPPS, x + SM_W - 130, y + 88, 96, 26);
        g_rrect(c, x + SM_W - 130, y + 88, 96, 26, 5, 5, hover_id == H_ALLAPPS ? TH.hover : (TH.dark ? 0x18FFFFFF : 0x70FFFFFF));
        if (all_apps) { icon_glyph(c, G_CHEV_L, x + SM_W - 116, y + 101, 12, fg); text(c, F_REG, 13, 0, x + SM_W - 100, y + 106, fg, "Back"); }
        else { text(c, F_REG, 13, 0, x + SM_W - 118, y + 106, fg, "All apps"); icon_glyph(c, G_CHEV_R, x + SM_W - 48, y + 101, 12, fg); }
      if (all_apps) {
        int order[NAPPS];
        for (int i = 0; i < NAPPS; i++) order[i] = i;
        for (int i = 0; i < NAPPS; i++) for (int k = i + 1; k < NAPPS; k++) if (strcmp(APPS[order[k]].title, APPS[order[i]].title) < 0) { int t = order[i]; order[i] = order[k]; order[k] = t; }
        char last = 0;
        float ly = y + 124;
        for (int i = 0; i < NAPPS; i++) {
            int a = order[i];
            if (APPS[a].title[0] != last) { last = APPS[a].title[0]; char L[2] = { last, 0 }; text(c, F_SEMI, 13, 0, x + 44, ly + 18, TH.accent, L); ly += 26; }
            int id = H_START_APP + a;
            hit_add(id, x + 28, ly, SM_W - 56, 44);
            if (hover_id == id) g_rrect(c, x + 28, ly, SM_W - 56, 44, 6, 6, TH.hover);
            icon_app(c, a, x + 40, ly + 8, 28);
            text(c, F_REG, 14, 0, x + 82, ly + 27, fg, APPS[a].title);
            text_r(c, F_REG, 12, 0, x + SM_W - 44, ly + 27, sub, APPS[a].desc);
            ly += 44;
            if (ly > y + SM_H - 120) break;
        }
      } else {
        float cw = (SM_W - 56) / 6.0f;
        for (int i = 0; i < NAPPS; i++) {
            float cx = x + 28 + (i % 6) * cw, cy = y + 126 + (i / 6) * 96;
            int id = H_START_APP + i;
            hit_add(id, cx + 4, cy, cw - 8, 90);
            if (!start_marked && start_p >= 1) MARK("START_ITEM %s %d %d", APPS[i].exe, (int)(cx + cw / 2), (int)(cy + 45));
            if (hover_id == id) g_rrect(c, cx + 4, cy, cw - 8, 90, 8, 8, TH.hover);
            icon_app(c, i, cx + cw / 2 - 20, cy + 12, 40);
            char *t = text_ellipsize(F_REG, 13, APPS[i].title, cw - 12);
            text_c(c, F_REG, 13, 0, cx + cw / 2, cy + 76, fg, t);
            free(t);
        }
        if (start_p >= 1) start_marked = 1;
      }
      if (!all_apps) {
        float ry0 = y + 340;
        text(c, F_SEMI, 15, 0, x + 40, ry0, fg, "Recommended");
        if (!nrecent) text(c, F_REG, 13, 0, x + 40, ry0 + 36, sub, "Files you open in Notepad and File Explorer show up here.");
        for (int i = 0; i < nrecent && i < 6; i++) {
            float rx = x + 28 + (i % 2) * ((SM_W - 56) / 2), ry = ry0 + 18 + (i / 2) * 64;
            int id = H_RECENT + i;
            hit_add(id, rx, ry, (SM_W - 56) / 2 - 8, 58);
            if (hover_id == id) g_rrect(c, rx, ry, (SM_W - 56) / 2 - 8, 58, 8, 8, TH.hover);
            const char *b = strrchr(recent[i], '/');
            icon_file(c, b ? b + 1 : recent[i], rx + 10, ry + 11, 36);
            char *t = text_ellipsize(F_SEMI, 14, b ? b + 1 : recent[i], (SM_W - 56) / 2 - 80);
            text(c, F_SEMI, 14, 0, rx + 56, ry + 26, fg, t);
            free(t);
            text(c, F_REG, 12, 0, rx + 56, ry + 44, sub, i == 0 ? "Recently opened" : "Opened earlier");
        }
      }
    }
    // user + power strip
    float by = y + SM_H - 68;
    g_rrect(c, x, by, SM_W, 68, 0, 12, TH.dark ? 0x30000000 : 0x30FFFFFF);
    g_rect(c, x, by, SM_W, 1, TH.dark ? 0x20FFFFFF : 0x18000000);
    hit_add(H_USER, x + 24, by + 12, 180, 44);
    if (hover_id == H_USER) g_rrect(c, x + 24, by + 12, 180, 44, 8, 8, TH.hover);
    g_circle(c, x + 52, by + 34, 16, TH.accent);
    text_c(c, F_SEMI, 15, 0, x + 52, by + 40, TH.accent_fg, "F");
    text(c, F_SEMI, 14, 0, x + 78, by + 39, fg, "FLX User");
    hit_add(H_POWER, x + SM_W - 72, by + 12, 44, 44);
    if (hover_id == H_POWER || power_menu) g_rrect(c, x + SM_W - 72, by + 12, 44, 44, 8, 8, TH.hover);
    icon_glyph(c, G_POWER, x + SM_W - 50, by + 34, 20, fg);
    if (power_menu) {
        float mx = x + SM_W - 220, my = by - 140, mw = 200, mh = 132;
        glass_panel(c, mx, my, mw, mh, 8, 1);
        const char *L[3] = { "Lock", "Restart", "Shut down" };
        int G[3] = { G_LOCK, G_RESTART, G_POWER };
        for (int i = 0; i < 3; i++) {
            int id = H_PWR_LOCK + i;
            float iy = my + 6 + i * 40;
            hit_add(id, mx + 6, iy, mw - 12, 38);
            if (hover_id == id) g_rrect(c, mx + 6, iy, mw - 12, 38, 6, 6, TH.hover);
            icon_glyph(c, G[i], mx + 26, iy + 19, 16, TH.text);
            text(c, F_REG, 14, 0, mx + 48, iy + 24, TH.text, L[i]);
        }
    }
}

// ---- quick settings + desktop menu ---------------------------------------------------------------
static void shell_theme(int dark, int accent, int wall) {
    crossfade_begin();
    theme_set(dark, accent, wall);
    settings_save();
    wm_broadcast_theme();
    MARK("THEME dark=%d accent=%d wall=%d", TH.dark, TH.accent_i, TH.wall);
}
static void qs_tile(Canvas *c, int id, float x, float y, int glyph, const char *l1, const char *l2, int on) {
    hit_add(id, x, y, 110, 74);
    bool hov = hover_id == id;
    g_rrect(c, x, y, 110, 74, 8, 8, on ? (hov ? c_mix(TH.accent, 0xFFFFFFFF, 0.12f) : TH.accent) : (hov ? (TH.dark ? 0x40FFFFFF : 0xE0FFFFFF) : (TH.dark ? 0x28FFFFFF : 0xB0FFFFFF)));
    if (!on) g_rrect_line(c, x, y, 110, 74, 8, 1, TH.dark ? 0x20FFFFFF : 0x18000000);
    uint32_t fg = on ? TH.accent_fg : TH.text;
    icon_glyph(c, glyph, x + 22, y + 22, 18, fg);
    text(c, F_SEMI, 13, 0, x + 12, y + 52, fg, l1);
    if (l2) text(c, F_REG, 11, 0, x + 12, y + 67, c_alpha(fg, 0.8f), l2);
}
static void qs_slider(Canvas *c, int id, float x, float y, float w, int glyph, float v) {
    icon_glyph(c, glyph, x + 10, y + 12, 18, TH.text);
    hit_add(id, x + 30, y, w - 30, 24);
    float sx = x + 40, sw = w - 50;
    g_rrect(c, sx, y + 10, sw, 4, 2, 2, TH.dark ? 0x40FFFFFF : 0x30000000);
    g_rrect(c, sx, y + 10, sw * v, 4, 2, 2, TH.accent);
    g_circle(c, sx + sw * v, y + 12, 9, TH.dark ? 0xFF454545 : 0xFFFFFFFF);
    g_circle(c, sx + sw * v, y + 12, hover_id == id ? 6 : 5, TH.accent);
}
static void draw_quick(Canvas *c) {
    if (!qs_open) return;
    float w = 384, h = 316, x = SCR_W - 12 - w, y = TOP + 12;
    glass_panel(c, x, y, w, h, 12, 1);
    hit_add(H_QS_PANEL, x, y, w, h);
    NetInfo ni; netinfo_read(&ni);
    float tx = x + 16, ty = y + 16;
    qs_tile(c, H_QS_NET, tx, ty, ni.up ? G_WIFI : G_WIFI_OFF, ni.up ? "Connected" : "Offline", ni.up ? ni.ip : "no DHCP lease", ni.up);
    qs_tile(c, H_QS_DARK, tx + 120, ty, G_MOON, "Dark mode", TH.dark ? "On" : "Off", TH.dark);
    qs_tile(c, H_QS_ACCENT, tx + 240, ty, G_PALETTE, "Accent", ACCENT_NAMES[TH.accent_i], 0);
    qs_tile(c, H_QS_WALL, tx, ty + 84, G_DISPLAY, "Wallpaper", WALL_NAMES[TH.wall], 0);
    qs_tile(c, H_QS_LOCK, tx + 120, ty + 84, G_LOCK, "Lock", "Win+L", 0);
    qs_tile(c, H_QS_SETTINGS, tx + 240, ty + 84, G_GEAR, "Settings", "All settings", 0);
    qs_slider(c, H_QS_BRIGHT, tx, ty + 180, w - 32, G_SUN, qs_bright);
    qs_slider(c, H_QS_VOL, tx, ty + 220, w - 32, G_VOLUME, qs_vol);
    g_rect(c, x, y + h - 52, w, 1, TH.dark ? 0x20FFFFFF : 0x18000000);
    icon_glyph(c, G_BATTERY, x + 30, y + h - 26, 20, TH.text);
    text(c, F_REG, 13, 0, x + 50, y + h - 21, TH.sub, "100% · plugged in (virtual machine)");
}
static void draw_desk_menu(Canvas *c) {
    if (!dm_open) return;
    const char *L[4] = { "Personalize", "Display settings", "Open in Terminal", "Refresh" };
    int G[4] = { G_PALETTE, G_DISPLAY, G_APPS, G_REFRESH };
    float w = 230, h = 4 * 38 + 10, x = fminf(dm_x, SCR_W - w - 8), y = fminf(dm_y, SCR_H - h - 8);
    glass_panel(c, x, y, w, h, 8, 1);
    hit_add(H_DM_PANEL, x, y, w, h);
    for (int i = 0; i < 4; i++) {
        float iy = y + 5 + i * 38;
        hit_add(H_DM_PERSONAL + i, x + 5, iy, w - 10, 36);
        if (hover_id == H_DM_PERSONAL + i) g_rrect(c, x + 5, iy, w - 10, 36, 5, 5, TH.hover);
        if (i == 2) icon_app(c, APP_TERMINAL, x + 16, iy + 9, 18); else icon_glyph(c, G[i], x + 25, iy + 18, 16, TH.text);
        text(c, F_REG, 14, 0, x + 46, iy + 23, TH.text, L[i]);
    }
}

// ---- alt+tab -------------------------------------------------------------------------------------
static void alttab_build(void) {
    alt_n = 0;
    for (int i = nz - 1; i >= 0; i--) { Win *w = &wins[zord[i]]; if (w->used && w->mapped && !w->closing) alt_list[alt_n++] = zord[i]; }
}
static void draw_alttab(Canvas *c) {
    if (!alttab_open || !alt_n) return;
    float cw = 260, ch = 190, gap = 16;
    int n = mini(alt_n, 6);
    float w = n * cw + (n + 1) * gap, h = ch + 2 * gap, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2 - 40;
    glass_panel(c, x, y, w, h, 14, 1);
    for (int i = 0; i < n; i++) {
        Win *wn = &wins[alt_list[i]];
        float cx = x + gap + i * (cw + gap), cy = y + gap;
        if (i == alt_sel) g_rrect_line(c, cx - 4, cy - 4, cw + 8, ch + 8, 10, 3, TH.accent);
        if (wn->app >= 0) icon_app(c, wn->app, cx + 6, cy + 4, 20);
        char *t = text_ellipsize(F_REG, 13, wn->title, cw - 40);
        text(c, F_REG, 13, 0, cx + 34, cy + 19, TH.text, t);
        free(t);
        float tw = cw, th = ch - 34, sc = fminf(tw / wn->bw, th / wn->bh);
        float pw = wn->bw * sc, ph = wn->bh * sc;
        if (wn->front >= 0 && wn->shm) {
            g_rrect(c, cx + (tw - pw) / 2, cy + 30 + (th - ph) / 2, pw, ph, 6, 6, TH.bg);
            g_buf(c, wn->shm + (size_t)wn->front * BUF_W * BUF_H, BUF_W, wn->bw, wn->bh, cx + (tw - pw) / 2, cy + 30 + (th - ph) / 2, pw, ph, 6, 6, 255, !wn->mica);
        }
    }
}

static void draw_tip(Canvas *c) {
    if (tip_id < H_TB_APP || tip_id >= H_TB_APP + 16 || now() - tip_t < 0.6 || start_open) return;
    Slot s[16];
    int n = slots(s), i = tip_id - H_TB_APP;
    if (i >= n) return;
    for (int k = 0; k < nhits; k++) if (hits[k].id == tip_id) {
        const char *t = s[i].app >= 0 ? APPS[s[i].app].title : s[i].exe;
        float tw = text_w(F_REG, 13, 0, t) + 24, tx = hits[k].x + hits[k].w / 2 - tw / 2, ty = TBY - 40;
        glass_panel(c, tx, ty, tw, 30, 6, 1);
        text(c, F_REG, 13, 0, tx + 12, ty + 20, TH.text, t);
    }
}

void shell_draw(Canvas *c) {
    if (phase == PH_DESK || (phase == PH_LOCK && (unlock_t >= 0 || lock_drag > 0))) {
        nhits = 0;
        draw_topbar(c);
        draw_taskbar(c);
        draw_tip(c);
        widgets_draw(c, widgets_p);
        copilot_draw(c, copilot_p);
        draw_start(c);
        draw_quick(c);
        draw_desk_menu(c);
        draw_alttab(c);
    }
    if (phase == PH_LOCK) draw_lock(c);
    if (phase == PH_BOOT) draw_boot(c, 1);
}

// ---- input ------------------------------------------------------------------------------------------
static void tb_click(int i) {
    Slot s[16];
    int n = slots(s);
    if (i >= n) return;
    Win *best = 0;
    for (int k = nz - 1; k >= 0; k--) { Win *w = &wins[zord[k]]; if (w->used && !w->closing && !strcmp(w->exe, s[i].exe)) { best = w; break; } }
    if (!best) { launch(s[i].exe, 0); return; }
    if (best == focus && !best->minimized) wm_minimize(best);
    else wm_restore(best);
}

int shell_mouse(IEv *e) {
    if (phase == PH_BOOT) return 1;
    if (phase == PH_LOCK) {
        if (unlock_t >= 0) return 1;
        if (e->type == IE_DOWN && e->btn == 1) { lock_down = 1; lock_y0 = e->y; }
        else if (e->type == IE_MOVE && lock_down) { lock_drag = fmaxf(0, lock_y0 - e->y); dirty = 1; }
        else if (e->type == IE_UP && lock_down) {
            lock_down = 0;
            if (lock_drag < 12 || lock_drag > 120) unlock(); else lock_drag = 0;
            dirty = 1;
        }
        return 1;
    }
    if (wm_grabbing()) return 0;
    if (alttab_open) return 1;
    int h = hit_at(e->x, e->y);
    if (qs_drag && (e->type == IE_MOVE || e->type == IE_UP)) {
        float x0 = SCR_W - 12 - 384 + 16 + 40, sw = 384 - 32 - 50;
        float v = clampf((e->x - x0) / sw, 0, 1);
        if (qs_drag == 1) qs_bright = v; else qs_vol = v;
        if (e->type == IE_UP) { MARK("QUICK %s %.0f", qs_drag == 1 ? "brightness" : "volume", v * 100); qs_drag = 0; }
        dirty = 1;
        return 1;
    }
    if (e->type == IE_MOVE) {
        if (h != hover_id) { hover_id = h; dirty = 1; tip_id = h; tip_t = now(); }
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        if (copilot_open && copilot_mouse(e, copilot_p)) return 1;
        return h >= 0 && !(h >= H_TB_APP && 0);
    }
    if (e->type == IE_DOWN) {
        dirty = 1;
        tip_id = -1;
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        if (copilot_open && copilot_mouse(e, copilot_p)) return 1;
        if (start_open && h != H_START_BTN && (h < H_START_PANEL || h > H_ALLAPPS) && h < H_START_APP) { close_panels(); return 1; }
        if (power_menu && h != H_POWER && (h < H_PWR_LOCK || h > H_PWR_SHUT)) power_menu = 0;
        if (qs_open && (h < H_QS_PANEL || h > H_QS_GEAR) && h != H_NET) { qs_open = 0; if (h < 0) return 1; }
        if (dm_open && (h < H_DM_PANEL || h > H_DM_REFRESH)) { dm_open = 0; if (h < 0) return 1; }
        if (h < 0 && e->btn == 2 && !win_at(e->x, e->y) && e->y > TOP && !start_open) { close_panels(); dm_open = 1; dm_x = e->x; dm_y = e->y; MARK("DESKTOP_MENU"); return 1; }
        if ((widgets_open && h != H_WIDGETS_BTN) || (copilot_open && h != H_COPILOT_BTN)) {
            if (h < 0) { close_panels(); return 1; }
        }
        switch (h) {
        case H_START_BTN: case H_SEARCH_BTN: case H_TOP_SEARCH: toggle_start(); return 1;
        case H_WIDGETS_BTN: { int o = !widgets_open; close_panels(); widgets_open = o; MARK("WIDGETS %s", o ? "open" : "close"); return 1; }
        case H_COPILOT_BTN: { int o = !copilot_open; close_panels(); copilot_open = o; MARK("COPILOT %s", o ? "open" : "close"); return 1; }
        case H_NET: { int o = !qs_open; close_panels(); qs_open = o; MARK("QUICK %s", o ? "open" : "close"); return 1; }
        case H_QS_NET: launch("settings", "network"); close_panels(); return 1;
        case H_QS_DARK: shell_theme(!TH.dark, TH.accent_i, TH.wall); return 1;
        case H_QS_ACCENT: shell_theme(TH.dark, (TH.accent_i + 1) % NACCENT, TH.wall); return 1;
        case H_QS_WALL: shell_theme(TH.dark, TH.accent_i, (TH.wall + 1) % NWALL); return 1;
        case H_QS_LOCK: lock_now(); return 1;
        case H_QS_SETTINGS: case H_QS_GEAR: launch("settings", 0); close_panels(); return 1;
        case H_QS_BRIGHT: qs_drag = 1; return 1;
        case H_QS_VOL: qs_drag = 2; return 1;
        case H_QS_PANEL: case H_DM_PANEL: return 1;
        case H_DM_PERSONAL: launch("settings", "personalization"); close_panels(); return 1;
        case H_DM_DISPLAY: launch("settings", 0); close_panels(); return 1;
        case H_DM_TERMINAL: launch("terminal", 0); close_panels(); return 1;
        case H_DM_REFRESH: close_panels(); load_wallpaper(); dirty = 1; return 1;
        case H_CLOCK: { int o = !widgets_open; close_panels(); widgets_open = o; return 1; }
        case H_POWER: power_menu = !power_menu; return 1;
        case H_PWR_LOCK: lock_now(); return 1;
        case H_PWR_RESTART: case H_PWR_SHUT:
            MARK("POWER %s", h == H_PWR_RESTART ? "restart" : "shutdown");
            settings_save(); sync();
            exit(h == H_PWR_RESTART ? 42 : 43);
        case H_USER: launch("settings", "accounts"); close_panels(); return 1;
        case H_ALLAPPS: all_apps = !all_apps; return 1;
        case H_START_PANEL: case H_START_SEARCH: return 1;
        }
        if (h >= H_TB_APP && h < H_TB_APP + 16) { tb_click(h - H_TB_APP); return 1; }
        if (h >= H_START_APP && h < H_START_APP + NAPPS) { launch(APPS[h - H_START_APP].exe, 0); close_panels(); return 1; }
        if (h >= H_RESULT && h < H_RESULT + 16) { int r[16]; int n = results(r); if (h - H_RESULT < n) open_result(r[h - H_RESULT]); return 1; }
        if (h >= H_RECENT && h < H_RECENT + 8) { launch("notepad", recent[h - H_RECENT]); close_panels(); return 1; }
        return h >= 0;
    }
    if (e->type == IE_UP || e->type == IE_WHEEL) {
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        if (copilot_open && copilot_mouse(e, copilot_p)) return 1;
        return h >= 0;
    }
    return 0;
}

int shell_key(IEv *e) {
    if (phase == PH_BOOT) return 1;
    if (phase == PH_LOCK) {
        if (e->type == IE_KEY && (e->key == K_ENTER || e->key == ' ' || e->key == K_UP)) unlock();
        return 1;
    }
    // Alt+Tab
    if (e->type == IE_KEY && e->key == K_TAB && (e->mods & MOD_ALT)) {
        if (!alttab_open) { alttab_build(); if (!alt_n) return 1; alttab_open = 1; alt_sel = alt_n > 1 ? 1 : 0; }
        else alt_sel = (alt_sel + ((e->mods & MOD_SHIFT) ? alt_n - 1 : 1)) % mini(alt_n, 6);
        dirty = 1;
        return 1;
    }
    if (alttab_open && !(e->mods & MOD_ALT)) {
        alttab_open = 0;
        Win *w = &wins[alt_list[alt_sel]];
        if (w->used) { wm_restore(w); MARK("ALTTAB %s", w->exe); }
        dirty = 1;
        if (e->type == IE_KEYUP) return 1;
    }
    if (e->key == K_SUPER) {
        if (e->type == IE_KEY) super_alone = 1;
        else if (super_alone) { toggle_start(); dirty = 1; }
        return 1;
    }
    if (e->type == IE_KEY && (e->mods & MOD_SUPER)) super_alone = 0;
    if (e->type != IE_KEY || !e->key) return 0;
    if (e->mods & MOD_SUPER) {
        int k = e->key;
        if (k == 'c') { int o = !copilot_open; close_panels(); copilot_open = o; MARK("COPILOT %s", o ? "open" : "close"); return 1; }
        if (k == 'w') { int o = !widgets_open; close_panels(); widgets_open = o; MARK("WIDGETS %s", o ? "open" : "close"); return 1; }
        if (k == 'e') { launch("explorer", 0); return 1; }
        if (k == 'l') { lock_now(); return 1; }
        if (k == 's') { toggle_start(); dirty = 1; return 1; }
        if (k == 'a') { int o = !qs_open; close_panels(); qs_open = o; MARK("QUICK %s", o ? "open" : "close"); return 1; }
        return 0;   // Win+arrows go to the window manager
    }
    if (e->key == K_ESC && (start_open || widgets_open || copilot_open || qs_open || dm_open)) { close_panels(); MARK("PANELS close"); return 1; }
    if (start_open) {
        int n = strlen(query);
        if (e->key == K_BACKSPACE) { if (n) query[n - 1] = 0; }
        else if (e->key == K_ENTER) { int r[16]; if (results(r)) open_result(r[0]); }
        else if (e->text[0] && n < (int)sizeof query - 2) { query[n] = e->text[0]; query[n + 1] = 0; }
        dirty = 1;
        return 1;
    }
    if (copilot_open && copilot_key(e)) { dirty = 1; return 1; }
    if (widgets_open && widgets_key(e)) { dirty = 1; return 1; }
    return 0;
}
