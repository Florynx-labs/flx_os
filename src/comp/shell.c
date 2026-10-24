// FLX OS shell surfaces drawn by the compositor: boot animation, lock screen,
// floating glass taskbar, top status bar, Start/Launcher menu, Quick Settings panel
// and the Alt+Tab switcher. Settings persist on the home disk.
#include "comp.h"
#include "auth_client.h"
#include <fcntl.h>
#include <unistd.h>

int phase = PH_BOOT, dirty = 1;
int start_open, widgets_open, alttab_open, clock_flyout_open;
float start_p, widgets_p, clock_flyout_p;
int hover_id = -1;
static double boot_t0, unlock_t = -1, lock_shown_t;
static int session_authenticated, suspend_pending, resume_fast_unlock;
static float lock_drag; static float lock_x0; static int lock_down;
static int lock_power_open;
enum { H_LOCK_SHUT = 210, H_LOCK_RESTART, H_LOCK_SLEEP, H_LOCK_USER, H_LOCK_SUBMIT };
static int auth_status = -1, auth_dialog, auth_confirm;
static char auth_password[257], auth_first[257], auth_message[112];
static char lock_users[8][32], lock_user[32] = "flx";
static int lock_user_count;
static char query[128];
static int power_menu, all_apps, qs_open, dm_open, search_focus; static float dm_x, dm_y, qs_vol = 0.7f, qs_bright = 1.0f, app_scroll; static int qs_drag;
static int super_alone;
static int alt_sel; static int alt_list[MAXWIN], alt_n;
static char recent[8][256]; static int nrecent;
static int start_marked;
static double tip_t; static int tip_id = -1;
static void recent_load(void);
static void load_wallpaper(void);

static void lock_users_load(void) {
    char response[2048]; lock_user_count = 0;
    if (flx_auth_request("LIST", response, sizeof response) || strncmp(response, "USERS", 5)) return;
    char *save = 0;
    for (char *entry = strtok_r(response + 5, " ", &save); entry && lock_user_count < 8; entry = strtok_r(0, " ", &save)) {
        char *sep = strchr(entry, ':'); if (!sep) continue;
        size_t n = (size_t)(sep - entry); if (!n || n >= sizeof lock_users[0]) continue;
        memcpy(lock_users[lock_user_count], entry, n); lock_users[lock_user_count][n] = 0; lock_user_count++;
    }
    int found = 0; for (int i = 0; i < lock_user_count; i++) if (!strcmp(lock_users[i], lock_user)) found = 1;
    if (!found && lock_user_count) snprintf(lock_user, sizeof lock_user, "%s", lock_users[0]);
}
static void password_hex(const char *password, char *out, size_t cap) {
    size_t n = 0; for (const unsigned char *p = (const unsigned char *)password; *p && n + 2 < cap; p++) n += (size_t)snprintf(out + n, cap - n, "%02x", *p);
    if (cap) out[n] = 0;
}

int dock_autohide = 1;
int dock_app_mask = 127;
static float dock_p = 1.0f;
static int dock_revealed = 1;

#define TOP 36
#define TBH 56
#define TBY (SCR_H - 12 - TBH)
#define SM_W 680
#define SM_H 700
#define SM_X ((SCR_W - SM_W) / 2)
#define SM_Y (TBY - 12 - SM_H)
static const char *PINNED[] = { "explorer", "browser", "terminal", "settings", "notepad", "calc", "taskmgr" };
#define NPIN 7
typedef struct { const char *exe; int app; } Slot;
static int slots(Slot *s);
enum {
    H_NONE, H_START_BTN, H_SEARCH_BTN, H_WIDGETS_BTN, H_TOP_SEARCH, H_NET, H_CLOCK, H_VOL,
    H_START_PANEL, H_START_SEARCH, H_POWER, H_PWR_LOCK, H_PWR_SLEEP, H_PWR_RESTART, H_PWR_SHUT, H_USER, H_ALLAPPS,
    H_CLOCK_PANEL = 18,
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
    char *s = strfmt("dark=%d\naccent=%d\nwall=%d\nautohide=%d\nfont=%d\nfont_version=2\nscale=%d\ndockapps=%d\n", TH.dark, TH.accent_i, TH.wall, dock_autohide, font_get_family(), ui_scale_percent, dock_app_mask);
    wm_write_user_file("/home/user/.config/flx.conf", s, strlen(s));
    free(s);
    char ds[96]; snprintf(ds, sizeof ds, "autohide=%d\ndockapps=%d\nscale=%d\n", dock_autohide, dock_app_mask, ui_scale_percent);
    wm_write_user_file("/home/user/.config/dock.conf", ds, strlen(ds));
    sync();
}
void settings_load(void) {
    int d = 1, a = 0, w = 0;  // FLX OS defaults: dark graphite, gold accent
    char *s = wm_read_user_file("/home/user/.config/flx.conf", 65536);
    if (!s) s = wm_read_user_file("/home/user/.config/w12.conf", 65536);  // backward compat
    if (s) {
        char *p;
        if ((p = strstr(s, "dark="))) d = atoi(p + 5);
        if ((p = strstr(s, "accent="))) a = atoi(p + 7);
        if ((p = strstr(s, "wall="))) w = atoi(p + 5);
        if ((p = strstr(s, "autohide="))) dock_autohide = atoi(p + 9);
        if ((p = strstr(s, "scale="))) ui_scale_percent = mini(140, maxi(80, atoi(p + 6)));
        if ((p = strstr(s, "font="))) {
            int family = atoi(p + 5);
            if (!strstr(s, "font_version=2")) family = family == 0 ? 1 : family == 1 ? 0 : family;
            font_set_family(family);
        }
        if ((p = strstr(s, "dockapps="))) {
            dock_app_mask = atoi(p + 9) & ((1 << NPIN) - 1);
            // 63 was the former six-app default; keep the newly installed
            // browser pinned when migrating an untouched pre-browser profile.
            if (dock_app_mask == 63) dock_app_mask = 127;
        }
        free(s);
    }
    char *ds = wm_read_user_file("/home/user/.config/dock.conf", 4096);
    if (ds) {
        char *p = strstr(ds, "autohide=");
        if (p) dock_autohide = atoi(p + 9);
        if ((p = strstr(ds, "scale="))) ui_scale_percent = mini(140, maxi(80, atoi(p + 6)));
        if ((p = strstr(ds, "dockapps="))) dock_app_mask = atoi(p + 9) & 255;
        free(ds);
    }
    theme_set(d, a, w);
}
void shell_reload_session(void) { settings_load(); load_wallpaper(); recent_load(); dirty = 1; }
static void recent_load(void) {
    nrecent = 0;
    char *s = wm_read_user_file("/home/user/.config/recent", 4096);
    if (!s) return;
    for (char *l = strtok(s, "\n"); l && nrecent < 8; l = strtok(0, "\n")) snprintf(recent[nrecent++], 256, "%s", l);
    free(s);
}
void recent_add(const char *path) {
    char out[8 * 260] = "";
    int n = snprintf(out, sizeof out, "%s\n", path);
    for (int i = 0, k = 1; i < nrecent && k < 8; i++) if (strcmp(recent[i], path)) { n += snprintf(out + n, sizeof out - n, "%s\n", recent[i]); k++; }
    wm_write_user_file("/home/user/.config/recent", out, (size_t)n);
    recent_load();
    dirty = 1;
}

// ---- wallpaper -------------------------------------------------------------------------------------
Canvas *wall, *wall_blur;
static int wall_loaded = -1;
static void load_wallpaper(void) {
    int key = TH.wall + TH.dark * NWALL;
    if (wall_loaded == key) return;
    static const char *dark_names[6] = { "dark_aurora", "dark_eclipse", "dark_flow", "dark_glass", "dark_horizon", "dark_obsidian" };
    static const char *light_names[6] = { "light_aurora", "light_eclipse", "light_flow", "light_glass", "light_horizon", "light_obsidian" };
    static const char *world_names[3] = { "world_alpine", "world_coast", "world_sahara" };
    char p[64];
    int index = TH.wall;
    if (index < 0 || index >= NWALL) index = ((index % NWALL) + NWALL) % NWALL;
    const char *name = index < 6 ? (TH.dark ? dark_names[index] : light_names[index]) : world_names[index - 6];
    snprintf(p, sizeof p, "/share/wallpapers/%s.raw", name);
    size_t n;
    uint32_t *d = (uint32_t *)read_file(p, &n);
    if (!wall) { wall = canvas_new(SCR_W, SCR_H); wall_blur = canvas_new(SCR_W, SCR_H); }
    if (d && n >= 12 + (size_t)SCR_W * SCR_H * 4 && d[1] == SCR_W && d[2] == SCR_H) memcpy(wall->px, d + 3, (size_t)SCR_W * SCR_H * 4);
    else {
        LOG("shell: wallpaper %s missing", p);
        if (TH.dark) g_vgrad(wall, 0, 0, SCR_W, SCR_H, 0, 0xFF0A151B, 0xFF152A35);
        else g_vgrad(wall, 0, 0, SCR_W, SCR_H, 0, 0xFFD5E4F0, 0xFFF5F7F8);
    }
    free(d);
    double t = now();
    g_blur_full(wall_blur, wall, 2);
    LOG("shell: wallpaper %s, blur cache built in %.1f ms", name, (now() - t) * 1000);
    wall_loaded = key;
}

void shell_init(void) {
    settings_load();
    load_wallpaper();
    recent_load();
    snprintf(lock_user, sizeof lock_user, "%s", wm_session_name());
    lock_users_load();
    auth_status = flx_auth_status_user(lock_user);
    if (auth_status < 0) snprintf(auth_message, sizeof auth_message, "Service d’authentification indisponible.");
    if (auth_status == 0) { phase = PH_DESK; session_authenticated = 1; dirty = 1; MARK("READY desktop autologin"); }
    else boot_t0 = now();
    MARK("READY boot");
}

void lock_now(void) { phase = PH_LOCK; unlock_t = -1; lock_drag = 0; auth_dialog = auth_confirm = 0; auth_password[0] = auth_first[0] = auth_message[0] = 0; snprintf(lock_user, sizeof lock_user, "%s", wm_session_name()); lock_users_load(); auth_status = flx_auth_status_user(lock_user); lock_shown_t = now(); close_panels(); dirty = 1; MARK("READY lock"); }
static void unlock(void) { if (unlock_t < 0) { session_authenticated = 1; resume_fast_unlock = 0; auth_dialog = 0; auth_password[0] = auth_first[0] = 0; unlock_t = now(); MARK("UNLOCK authenticated"); } }

static void request_suspend(void) {
    resume_fast_unlock = session_authenticated;
    suspend_pending = 1;
    phase = PH_LOCK; unlock_t = -1; lock_drag = 0; auth_dialog = auth_confirm = 0;
    auth_password[0] = auth_first[0] = auth_message[0] = 0;
    snprintf(lock_user, sizeof lock_user, "%s", wm_session_name()); lock_users_load(); auth_status = flx_auth_status_user(lock_user); lock_shown_t = now(); close_panels(); dirty = 1;
    MARK("POWER suspend requested; authenticated=%d", resume_fast_unlock);
}

void shell_suspend_after_draw(void) {
    if (!suspend_pending) return;
    suspend_pending = 0;
    int fd = open("/sys/power/state", O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, "mem\n", 4) != 4) {
        if (fd >= 0) close(fd);
        resume_fast_unlock = 0;
        snprintf(auth_message, sizeof auth_message, "La veille matérielle est indisponible.");
        dirty = 1; MARK("POWER suspend unavailable"); return;
    }
    close(fd); lock_shown_t = now(); dirty = 1; MARK("POWER resumed from suspend");
}

static void lock_submit(void) {
    size_t n = strlen(auth_password);
    if (auth_status < 0) { snprintf(auth_message, sizeof auth_message, "Service d’authentification indisponible."); return; }
    if (!n) { snprintf(auth_message, sizeof auth_message, "Saisissez votre mot de passe."); return; }
    char request[320], response[48];
    if (auth_status == 0) {
        if (n < 1) { snprintf(auth_message, sizeof auth_message, "Un caractère minimum."); return; }
        if (!auth_confirm) {
            memcpy(auth_first, auth_password, n + 1); auth_password[0] = 0; auth_confirm = 1;
            snprintf(auth_message, sizeof auth_message, "Confirmez le nouveau mot de passe."); return;
        }
        if (strcmp(auth_first, auth_password)) {
            memset(auth_first, 0, sizeof auth_first); memset(auth_password, 0, sizeof auth_password); auth_confirm = 0;
            snprintf(auth_message, sizeof auth_message, "Les mots de passe ne correspondent pas."); return;
        }
        char passhex[513]; password_hex(auth_password, passhex, sizeof passhex);
        snprintf(request, sizeof request, "SETHEX %s %s", lock_user, passhex);
        int ok = !flx_auth_request(request, response, sizeof response) && !strcmp(response, "OK");
        memset(passhex, 0, sizeof passhex); memset(request, 0, sizeof request); memset(response, 0, sizeof response);
        if (ok) { auth_status = 1; unlock(); return; }
        memset(auth_first, 0, sizeof auth_first); memset(auth_password, 0, sizeof auth_password); auth_confirm = 0;
        snprintf(auth_message, sizeof auth_message, "Création refusée. Réessayez."); return;
    }
    char passhex[513]; password_hex(auth_password, passhex, sizeof passhex);
    snprintf(request, sizeof request, "AUTH %s %s", lock_user, passhex);
    int ok = !flx_auth_request(request, response, sizeof response) && !strncmp(response, "OK ", 3);
    if (ok) {
        unsigned uid = 0, gid = 0; char role[16] = "standard";
        if (sscanf(response, "OK %u %u %15s", &uid, &gid, role) != 3 || wm_session_set((uid_t)uid, (gid_t)gid, lock_user, role)) ok = 0;
    }
    memset(passhex, 0, sizeof passhex);
    memset(request, 0, sizeof request); memset(response, 0, sizeof response);
    if (ok) { unlock(); return; }
    memset(auth_password, 0, sizeof auth_password);
    snprintf(auth_message, sizeof auth_message, "Mot de passe incorrect.");
}
void close_panels(void) {
    if (start_open || widgets_open || qs_open || dm_open || clock_flyout_open) dirty = 1;
    start_open = widgets_open = clock_flyout_open = 0; power_menu = 0; qs_open = 0; dm_open = 0;
}
static void toggle_start(void) {
    int o = !start_open;
    close_panels();
    start_open = o; query[0] = 0; start_marked = 0; all_apps = 0; app_scroll = 0; search_focus = o;
    MARK("START %s", o ? "open" : "close");
}
static void open_shell_search(void) { if (!start_open) toggle_start(); search_focus = 1; dirty = 1; }

int shell_animating(void) {
    if (phase == PH_BOOT || unlock_t >= 0) return 1;
    if (phase == PH_LOCK) return now() - lock_shown_t < 0.35; // render the fade, then sleep until input
    float ts = start_open, tw = widgets_open, tk = clock_flyout_open;
    float td = (dock_autohide && !dock_revealed && !start_open) ? 0.0f : 1.0f;
    return fabsf(start_p - ts) > 0.001f || fabsf(widgets_p - tw) > 0.001f || fabsf(clock_flyout_p - tk) > 0.001f || fabsf(dock_p - td) > 0.001f;
}

void shell_tick(void) {
    static double last;
    double t = now();
    float dt = (float)fmin(t - last, 0.05); last = t;
    float k = dt / 0.25f;   // panel slides: 250 ms (reads well on 30 fps video)
#define STEP(p, tgt) do { float T_ = (tgt); if (p < T_) p = fminf(T_, p + k); else p = fmaxf(T_, p - k); } while (0)
    float before = start_p + widgets_p + dock_p + clock_flyout_p;
    STEP(start_p, start_open);
    STEP(widgets_p, widgets_open);
    STEP(clock_flyout_p, clock_flyout_open);

    float dock_target = 1.0f;
    if (dock_autohide) {
        if (mouse_y >= SCR_H - 14 || (mouse_y >= TBY - 10 && hover_id >= H_START_BTN && hover_id <= H_TB_APP + 20) || start_open) {
            dock_revealed = 1;
        } else if (mouse_y < TBY - 30 && !start_open) {
            dock_revealed = 0;
        }
        dock_target = dock_revealed ? 1.0f : 0.0f;
    } else {
        dock_revealed = 1;
        dock_target = 1.0f;
    }
    STEP(dock_p, dock_target);

    if (start_p + widgets_p + dock_p + clock_flyout_p != before) dirty = 1;
    if (phase == PH_BOOT && t - boot_t0 > 3.0) { phase = PH_LOCK; lock_shown_t = t; MARK("READY lock"); dirty = 1; }
    if (phase == PH_LOCK && unlock_t >= 0 && t - unlock_t > 0.42) { phase = PH_DESK; unlock_t = -1; lock_drag = 0; MARK("READY desktop"); dirty = 1; }
    static double auth_retry;
    if (phase == PH_LOCK && auth_status < 0 && t - auth_retry > 1.0) { auth_retry = t; auth_status = flx_auth_status(); dirty = 1; }
    if (wall_loaded != TH.wall + TH.dark * NWALL) { load_wallpaper(); dirty = 1; }
    static int last_min = -1;
    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    if (tm.tm_min != last_min) { last_min = tm.tm_min; dirty = 1; }
    if (tip_id >= 0 && t - tip_t > 0.6 && t - tip_t < 0.7) dirty = 1;
}

float taskbar_icon_x(const char *exe) {
    Slot ordered[16]; int n = slots(ordered), slot = -1;
    for (int i = 0; i < n; i++) if (!strcmp(ordered[i].exe, exe)) { slot = i; break; }
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
    static const char *DAYS[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
    static const char *MON[]  = { "Janvier", "F\u00e9vrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Ao\u00fbt", "Septembre", "Octobre", "Novembre", "D\u00e9cembre" };
    static const char *DAYS_SHORT[] = { "Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam" };
    static const char *MON_SHORT[]  = { "Janv.", "F\u00e9vr.", "Mars", "Avr.", "Mai", "Juin", "Juil.", "Ao\u00fbt", "Sept.", "Oct.", "Nov.", "D\u00e9c." };
    if (hm) snprintf(hm, n1, "%d:%02d", tm.tm_hour, tm.tm_min);
    if (date) snprintf(date, n2, "%s %d %s %d", DAYS[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon], tm.tm_year + 1900);
    if (dshort) snprintf(dshort, n3, "%s %d %s %d", DAYS_SHORT[tm.tm_wday], tm.tm_mday, MON_SHORT[tm.tm_mon], tm.tm_year + 1900);
}

static Image *boot_logo = NULL;
static int boot_logo_tried = 0;

static void draw_boot(Canvas *c, float alpha) {
    double t = now() - boot_t0;
    g_rect(c, 0, 0, SCR_W, SCR_H, c_alpha(0xFF000000, alpha));
    float la = clampf((float)t / 0.6f, 0, 1) * alpha;
    if (!boot_logo_tried) {
        boot_logo = image_load("/share/icons/flx_logo_mark.png");
        if (!boot_logo) boot_logo = image_load("/share/icons/flx_os_logo.png");
        boot_logo_tried = 1;
    }
    if (boot_logo) {
        g_image(c, boot_logo, SCR_W / 2 - 90, SCR_H / 2 - 160, 180, 180, 0, (int)(255 * la));
    } else {
        icon_logo(c, SCR_W / 2, SCR_H / 2 - 70, 150, (int)(255 * la));
    }
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
    float slide = unlock_t >= 0 ? SCR_H * ease_inout((float)(now() - unlock_t) / 0.42f) : 0;
    g_buf(c, wall->px, SCR_W, SCR_W, SCR_H, 0, -(int)slide, SCR_W, SCR_H, 0, 0, 255, 1);
    g_rect(c, 0, 0, SCR_W, SCR_H, 0x18000000);
    char hm[16], date[64]; clock_strings(hm, sizeof hm, date, sizeof date, 0, 0);
    float fade = clampf((float)(now() - lock_shown_t) / 0.5f, 0, 1);
    uint32_t white = c_alpha(0xFFF4F5F6, fade), gold = c_alpha(TH.accent, fade);
    // Compact system status: only report Wi-Fi from the network service; battery is omitted
    // on systems where no readable power-supply device is present.
    NetInfo ni; netinfo_read(&ni);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, SCR_W - 96, 30, 20, white);
    FILE *bf = fopen("/sys/class/power_supply/BAT0/capacity", "r"); int pct = -1;
    if (bf) { if (fscanf(bf, "%d", &pct) != 1 || pct < 0 || pct > 100) pct = -1; fclose(bf); }
    if (pct >= 0) { char b[16]; snprintf(b, sizeof b, "%d%%", pct); icon_glyph(c, G_BATTERY, SCR_W - 54, 30, 19, white); text(c, F_REG, 12, 0, SCR_W - 34, 34, white, b); }

    text_c(c, F_LIGHT, SCR_H * 0.105f, 0, SCR_W / 2, SCR_H * 0.30f, white, hm);
    text_c(c, F_SEMILIGHT, 25, 0, SCR_W / 2, SCR_H * 0.355f, white, date);
    text_c(c, F_REG, 17, 0, SCR_W / 2, SCR_H * 0.405f, c_alpha(0xFFD7D9DC, fade), "« Les grandes choses naissent d'une vision claire. »");
    text_c(c, F_SEMI, 14, 0, SCR_W / 2, SCR_H * 0.438f, gold, "— FLX OS —");

    float sw = fminf(390, SCR_W - 48), sh = 76, sx = (SCR_W - sw) / 2, sy = SCR_H * 0.79f;
    float travel = sw - sh, dx = fminf(lock_drag, travel);
    g_rrect(c, sx, sy, sw, sh, sh/2, sh/2, 0xB910171C);
    g_rrect_line(c, sx, sy, sw, sh, sh/2, 1, 0x5085A0A8);
    g_circle(c, sx + sh/2 + dx, sy + sh/2, sh/2 - 4, 0xFF10191D);
    g_ring(c, sx + sh/2 + dx, sy + sh/2, sh/2 - 4, 2, gold);
    icon_glyph(c, G_LOCK, sx + sh/2 + dx, sy + sh/2, 24, gold);
    text_c(c, F_REG, 16, 0, SCR_W/2 + 28, sy + 43, white, "Faire glisser pour déverrouiller");
    icon_glyph(c, G_CHEV_R, sx + sw - 28, sy + sh/2, 20, white);
    hit_add(H_POWER, SCR_W - 56, 8, 42, 42);
    icon_glyph(c, G_POWER, SCR_W - 35, 29, 20, white);
    if (lock_power_open) {
        float mw = 230, mh = 132, mx = SCR_W - mw - 18, my = 58;
        g_rrect(c, mx, my, mw, mh, 12, 12, 0xEA11191D);
        const char *items[] = { "Éteindre", "Redémarrer", "Veille indisponible", "Changer d’utilisateur indisponible" };
        int ids[] = { H_LOCK_SHUT, H_LOCK_RESTART, H_LOCK_SLEEP, H_LOCK_USER };
        for (int i=0;i<4;i++) { float yy=my+8+i*29; hit_add(ids[i],mx,yy,mw,29); text(c,F_REG,13,0,mx+14,yy+19,white,items[i]); }
    }
    if (auth_dialog) {
        float dw=fminf(440, SCR_W-40), dh=324, dx=(SCR_W-dw)/2, dy=(SCR_H-dh)/2;
        g_rrect(c,dx,dy,dw,dh,16,16,0xF20A1419); g_rrect_line(c,dx,dy,dw,dh,16,1,0x5085A0A8);
        text(c,F_SEMI,22,0,dx+28,dy+38,white,auth_status==0 ? (auth_confirm ? "Confirmer le mot de passe" : "Créer votre mot de passe") : "Déverrouiller FLX OS");
        text(c,F_REG,12,0,dx+28,dy+61,0xFFB7C5CA,"Choisissez un compte");
        int cols = lock_user_count > 4 ? 4 : (lock_user_count ? lock_user_count : 1);
        float gap = 6, cellw = (dw - 48 - gap * (cols - 1)) / cols;
        for (int i = 0; i < lock_user_count; i++) {
            float bx = dx + 24 + (i % cols) * (cellw + gap), by = dy + 70 + (i / cols) * 30;
            int active = !strcmp(lock_users[i], lock_user);
            hit_add(230 + i, bx, by, cellw, 26);
            g_rrect(c, bx, by, cellw, 26, 7, 7, active ? 0xFF34402D : 0xFF101F25);
            if (active) g_rrect_line(c, bx, by, cellw, 26, 7, 1, TH.accent);
            text_c(c, F_SEMI, 11, 0, bx + cellw / 2, by + 17, active ? TH.accent : white, lock_users[i]);
        }
        float fy=dy+138; g_rrect(c,dx+24,fy,dw-48,48,8,8,0xFF101F25); g_rrect_line(c,dx+24,fy,dw-48,48,8,1,TH.accent);
        char masked[520]; size_t count = strlen(auth_password); if (count > 256) count = 256;
        for (size_t i = 0; i < count; i++) { masked[i * 3] = (char)0xE2; masked[i * 3 + 1] = (char)0x80; masked[i * 3 + 2] = (char)0xA2; }
        masked[count * 3] = 0;
        text(c,F_REG,16,0,dx+40,fy+32,white,auth_password[0] ? masked : "Mot de passe");
        if(auth_message[0]) text(c,F_REG,12,0,dx+28,dy+222,auth_status==0 ? TH.accent : 0xFFFF8C82,auth_message);
        float by=dy+dh-56; hit_add(H_LOCK_SUBMIT,dx+24,by,dw-48,40); g_rrect(c,dx+24,by,dw-48,40,8,8,TH.accent); text_c(c,F_SEMI,14,0,SCR_W/2,by+25,0xFF171411,auth_status==0 ? (auth_confirm ? "Enregistrer" : "Continuer") : "Déverrouiller");
        text_c(c,F_REG,11,0,SCR_W/2,dy+dh-8,0xFF92A4AB,"Entrée pour valider · Échap pour annuler");
    }
}

// ---- top bar -----------------------------------------------------------------------------------------
static void draw_topbar(Canvas *c) {
    g_glass(c, 0, 0, SCR_W, TOP, 0, glass_tint());
    g_rect(c, 0, TOP - 1, SCR_W, 1, TH.dark ? 0x20FFFFFF : 0x20000000);
    uint32_t fg = TH.text;
    // Search takes the former brand position; the desktop logo remains on the dock.
    float sw = 340, sx = 18;
    hit_add(H_TOP_SEARCH, sx, 4, sw, 28);
    g_rrect(c, sx, 4, sw, 28, 14, 14, hover_id == H_TOP_SEARCH ? (TH.dark ? 0x30FFFFFF : 0xB0FFFFFF) : (TH.dark ? 0x18FFFFFF : 0x70FFFFFF));
    g_rrect_line(c, sx, 4, sw, 28, 14, 1, TH.dark ? 0x18FFFFFF : 0x14000000);
    icon_glyph(c, G_SEARCH, sx + 20, 18, 14, TH.sub);
    label(c, F_REG, 13, sx + 36, 4, 28, TH.sub, "Rechercher...");
    // right: network, battery and clock
    NetInfo ni; netinfo_read(&ni);
    float rx = SCR_W - 10;
    rx -= 40;
    char hm[16], ds[32];
    clock_strings(hm, sizeof hm, 0, 0, ds, sizeof ds);
    float wt = text_w(F_SEMI, 13, 0, hm), wd = text_w(F_REG, 13, 0, ds), tw = wt + wd + 32;
    rx -= tw + 6; hit_add(H_CLOCK, rx, 3, tw, 30);
    if (hover_id == H_CLOCK || clock_flyout_open) g_rrect(c, rx, 3, tw, 30, 6, 6, TH.hover);
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
static int slots(Slot *s) {
    int n = 0;
    for (int i = 0; i < NPIN; i++) if (dock_app_mask & (1 << i)) s[n++] = (Slot){ PINNED[i], app_by_exe(PINNED[i]) };
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
    // Keep one clear entry point for the overview; search lives inside it.
    float slot = 52, w = 20 + (n + 1) * slot + 14, x = (SCR_W - w) / 2;
    float y = lerpf(SCR_H + 10, TBY, ease_out(dock_p));
    if (dock_autohide && dock_p < 0.02f) {
        hit_add(H_START_BTN, (SCR_W - w) / 2, SCR_H - 12, w, 12);
        return;
    }
    glass_panel(c, x, y, w, TBH, 14, 1);
    float ix = x + 10;
    // start
    hit_add(H_START_BTN, ix, y + 4, slot - 4, TBH - 8);
    if (hover_id == H_START_BTN || start_open) g_rrect(c, ix, y + 6, slot - 4, TBH - 12, 8, 8, TH.hover);
    icon_logo(c, ix + slot / 2 - 2, y + TBH / 2, 28, 255);
    ix += slot;
    g_rect(c, ix + slot - 4, y + 16, 1, TBH - 32, TH.dark ? 0x40FFFFFF : 0x30000000);
    ix += slot + 4;
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
    for (int i = 0; i < NAPPS && n < 16; i++) if (matches(APPS[i].title, query) || matches(APPS[i].exe, query)) out[n++] = i;
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
    // FLX Overview: one quiet surface for search, apps, and open windows.
    g_rect(c, 0, 0, SCR_W, SCR_H, c_alpha(TH.dark ? 0xE6081115 : 0xE6E8ECEF, p));
    hit_add(H_START_PANEL, 0, 0, SCR_W, SCR_H);
    float alpha = clampf((p - 0.35f) / 0.65f, 0, 1);
    uint32_t fg = c_alpha(TH.text, alpha), sub = c_alpha(TH.sub, alpha);
    float fw = fminf(660, SCR_W - 64), fx = (SCR_W - fw) / 2, fy = 54, fh = 54;
    hit_add(H_START_SEARCH, fx, fy, fw, fh);
    g_rrect(c, fx, fy, fw, fh, 18, 18, TH.dark ? 0xFF182226 : 0xFFF9FAFB);
    g_rrect_line(c, fx, fy, fw, fh, 18, 1, TH.line);
    icon_glyph(c, G_SEARCH, fx + 28, fy + fh / 2, 19, sub);
    if (query[0]) {
        float tw = text(c, F_REG, 19, 0, fx + 58, fy + 34, fg, query);
        g_rect(c, tw + 3, fy + 15, 1.5f, 24, TH.accent);
    } else {
        text(c, F_REG, 17, 0, fx + 58, fy + 33, sub, "Rechercher applications et fichiers");
    }

    if (query[0]) {
        int r[16], n = results(r);
        text(c, F_SEMI, 15, 0, fx, fy + 92, fg, n ? "Résultats" : "Aucun résultat");
        for (int i = 0; i < n && i < 8; i++) {
            float ry = fy + 108 + i * 58;
            int id = H_RESULT + i;
            hit_add(id, fx, ry, fw, 54);
            if (hover_id == id || i == 0) g_rrect(c, fx, ry, fw, 54, 12, 12, i == 0 ? TH.accent_soft : TH.hover);
            if (r[i] < 1000) {
                icon_app(c, r[i], fx + 14, ry + 9, 36);
                text(c, F_SEMI, 15, 0, fx + 64, ry + 24, fg, APPS[r[i]].title);
                text(c, F_REG, 12, 0, fx + 64, ry + 42, sub, APPS[r[i]].desc);
            } else {
                const char *path = recent[r[i] - 1000], *base = strrchr(path, '/');
                icon_file(c, base ? base + 1 : path, fx + 14, ry + 9, 36);
                text(c, F_SEMI, 15, 0, fx + 64, ry + 24, fg, base ? base + 1 : path);
                text(c, F_REG, 12, 0, fx + 64, ry + 42, sub, path);
            }
        }
        return;
    }

    float content_w = fminf(980, SCR_W - 80), left = (SCR_W - content_w) / 2;
    float cell = 100, gap = 12, gy = fy + 112;
    if (!all_apps) {
        text(c, F_SEMI, 15, 0, left, fy + 96, fg, "Favoris");
        float gridw = fminf(content_w, NPIN * (cell + gap) - gap);
        float gx = (SCR_W - gridw) / 2;
        Slot pinned[16]; int n = slots(pinned);
        for (int i = 0; i < n; i++) {
            float x = gx + i * (cell + gap);
            int id = H_START_APP + (pinned[i].app >= 0 ? pinned[i].app : i);
            hit_add(id, x, gy, cell, 96);
            if (hover_id == id) g_rrect(c, x, gy, cell, 96, 15, 15, TH.hover);
            if (pinned[i].app >= 0) icon_app(c, pinned[i].app, x + 32, gy + 10, 36);
            text_c(c, F_REG, 12, 0, x + cell / 2, gy + 72, fg, pinned[i].app >= 0 ? APPS[pinned[i].app].title : pinned[i].exe);
        }
        gy += 116;
    } else {
        text(c, F_SEMI, 15, 0, left, fy + 96, fg, "Toutes les applications");
    }
    // A discoverable application grid; scrolling keeps the overview usable on shorter displays.
    int cols = mini(8, maxi(3, (int)(content_w / (cell + gap))));
    int rows = (NAPPS + cols - 1) / cols;
    float gridw = cols * cell + (cols - 1) * gap, gx = (SCR_W - gridw) / 2;
    int clip[4]; g_getclip(c, clip); g_clip(c, 0, gy, SCR_W, SCR_H - 38);
    for (int i = 0; i < NAPPS; i++) {
        int row = i / cols, col = i % cols;
        float x = gx + col * (cell + gap), y = gy + row * 80 - app_scroll;
        if (all_apps && y + 72 > gy && y < SCR_H - 38) {
            int id = H_START_APP + i;
            hit_add(id, x, y, cell, 72);
            if (hover_id == id) g_rrect(c, x, y, cell, 72, 12, 12, TH.hover);
            icon_app(c, i, x + 32, y + 4, 36);
            text_c(c, F_REG, 11, 0, x + cell / 2, y + 62, fg, APPS[i].title);
        }
    }
    g_setclip(c, clip);
    float toggle_w = 172, toggle_x = fx + fw - toggle_w, toggle_y = fy + 78;
    hit_add(H_ALLAPPS, toggle_x, toggle_y, toggle_w, 28);
    g_rrect(c, toggle_x, toggle_y, toggle_w, 26, 13, 13, TH.dark ? 0x202ED9C3 : 0x18000000);
    text_c(c, F_SEMI, 12, 0, SCR_W / 2, toggle_y + 18, TH.accent, all_apps ? "Voir les favoris" : "Toutes les applications  ⌄");
    float max_scroll = fmaxf(0, rows * 80 - (SCR_H - 38 - gy));
    app_scroll = clampf(app_scroll, 0, max_scroll);

    int open_count = 0;
    for (int i = 0; i < nz; i++) { Win *w = &wins[zord[i]]; if (w->used && w->mapped && !w->closing && !w->minimized) open_count++; }
    if (open_count) {
        float wy = gy + 142;
        text(c, F_SEMI, 15, 0, left, wy, fg, "Fenêtres ouvertes");
        float ww = fminf(260, (content_w - 24) / 3), wh = 136;
        int shown = 0;
        for (int i = nz - 1; i >= 0 && shown < 6; i--) {
            Win *w = &wins[zord[i]]; if (!w->used || !w->mapped || w->closing || w->minimized) continue;
            float x = left + (shown % 3) * (ww + 12), y = wy + 18 + (shown / 3) * (wh + 12);
            hit_add(H_TB_APP + 16 + shown, x, y, ww, wh);
            glass_panel(c, x, y, ww, wh, 14, 1);
            if (w->app >= 0) icon_app(c, w->app, x + 14, y + 12, 26);
            text(c, F_SEMI, 13, 0, x + 48, y + 30, fg, w->title);
            g_rrect(c, x + 12, y + 48, ww - 24, wh - 60, 8, 8, TH.dark ? 0x80101418 : 0x80FFFFFF);
            text_c(c, F_REG, 11, 0, x + ww / 2, y + 102, sub, "Continuer");
            shown++;
        }
    }
    text_c(c, F_REG, 12, 0, SCR_W / 2, SCR_H - 30, sub, "Échap pour fermer · commence à taper pour rechercher");
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
    float w = 356, h = 404, x = SCR_W - 12 - w, y = TOP + 12;
    glass_panel(c, x, y, w, h, 20, 1);
    hit_add(H_QS_PANEL, x, y, w, h);
    NetInfo ni; netinfo_read(&ni);
    text(c, F_SEMI, 18, 0, x + 22, y + 32, TH.text, "Centre de contrôle");
    text(c, F_REG, 11, 0, x + 22, y + 51, TH.sub, "FLX  ·  ÉTAT DU SYSTÈME");
    float row = y + 68;
    g_rrect(c, x + 16, row, w - 32, 54, 13, 13, TH.dark ? 0xFF101B20 : 0xA0FFFFFF);
    hit_add(H_QS_NET, x + 16, row, w - 32, 54);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, x + 39, row + 26, 19, TH.accent);
    text(c, F_SEMI, 13, 0, x + 61, row + 23, TH.text, ni.up ? "Réseau connecté" : "Réseau hors ligne");
    text(c, F_REG, 11, 0, x + 61, row + 41, TH.sub, ni.up ? ni.ip : "Configurer la connexion");
    text(c, F_SEMI, 12, 0, x + 22, row + 80, TH.sub, "APPARENCE FLX");
    const int ids[3] = { H_QS_DARK, H_QS_ACCENT, H_QS_WALL };
    const int glyphs[3] = { G_MOON, G_PALETTE, G_DISPLAY };
    const char *names[3] = { TH.dark ? "Sombre" : "Clair", ACCENT_NAMES[TH.accent_i], "Fond" };
    for (int i = 0; i < 3; i++) {
        float bx = x + 16 + i * 108, by = row + 91;
        hit_add(ids[i], bx, by, 100, 45);
        g_rrect(c, bx, by, 100, 45, 13, 13, (i == 0 && TH.dark) ? TH.accent_soft : (TH.dark ? 0xFF101B20 : 0xA0FFFFFF));
        icon_glyph(c, glyphs[i], bx + 17, by + 22, 15, TH.accent);
        text(c, F_SEMI, 11, 0, bx + 32, by + 26, TH.text, names[i]);
    }
    qs_slider(c, H_QS_BRIGHT, x + 14, row + 151, w - 28, G_SUN, qs_bright);
    qs_slider(c, H_QS_VOL, x + 14, row + 191, w - 28, G_VOLUME, qs_vol);
    g_rect(c, x + 16, row + 230, w - 32, 1, TH.line);
    FILE *bf = fopen("/sys/class/power_supply/BAT0/capacity", "r"); int pct = -1;
    if (bf) { if (fscanf(bf, "%d", &pct) != 1 || pct < 0 || pct > 100) pct = -1; fclose(bf); }
    icon_glyph(c, G_BATTERY, x + 30, row + 260, 17, TH.accent);
    char power[80];
    if (pct >= 0) snprintf(power, sizeof power, "Batterie · %d%%", pct);
    else snprintf(power, sizeof power, "Alimentation secteur");
    text(c, F_REG, 12, 0, x + 49, row + 264, TH.sub, power);
    hit_add(H_QS_LOCK, x + w - 125, row + 245, 104, 34);
    icon_glyph(c, G_LOCK, x + w - 112, row + 261, 14, TH.text);
    text(c, F_SEMI, 11, 0, x + w - 94, row + 265, TH.text, "Verrouiller");
    hit_add(H_QS_SETTINGS, x + 16, y + h - 38, w - 32, 28);
    text(c, F_SEMI, 12, 0, x + 22, y + h - 20, TH.accent, "Ouvrir les paramètres");
}
static void draw_desk_menu(Canvas *c) {
    if (!dm_open) return;
    const char *L[4] = { "Personnaliser", "Param\u00e8tres d'affichage", "Ouvrir dans le Terminal", "Actualiser" };
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

static void draw_clock_flyout(Canvas *c) {
    float p = ease_out(clock_flyout_p);
    if (p <= 0.01f) return;
    float fw = 340, fh = 424;
    float fx = SCR_W - 12 - fw;
    float fy = TOP + 8 + (1.0f - p) * (-14);
    float alpha = p;

    // Outer shadow and frosted glass card
    g_shadow(c, fx, fy, fw, fh, 16, 28, (int)(110 * alpha), 8);
    g_glass(c, fx, fy, fw, fh, 16, c_alpha(TH.dark ? 0xDC081115 : 0xEFF3F4F7, alpha));
    g_rrect_line(c, fx, fy, fw, fh, 16, 1.2f, c_alpha(TH.dark ? 0x30FFFFFF : 0x30000000, alpha));
    g_rrect_line(c, fx + 1, fy + 1, fw - 2, fh - 2, 15, 1.0f, c_alpha(0x18000000, alpha));
    hit_add(H_CLOCK_PANEL, fx, fy, fw, fh);

    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    char hm[16], dstr[64];
    snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);

    static const char *JOURS_FR[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
    static const char *MOIS_FR[]  = { "Janvier", "F\u00e9vrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Ao\u00fbt", "Septembre", "Octobre", "Novembre", "D\u00e9cembre" };
    snprintf(dstr, sizeof dstr, "%s %d %s %d", JOURS_FR[tm.tm_wday], tm.tm_mday, MOIS_FR[tm.tm_mon], tm.tm_year + 1900);

    text(c, F_SEMI, 12, 0, fx + 24, fy + 30, c_alpha(TH.sub, alpha), "VOTRE JOURNÉE");
    text(c, F_LIGHT, 48, 0, fx + 22, fy + 91, c_alpha(TH.text, alpha), hm);
    text(c, F_REG, 14, 0, fx + 24, fy + 116, c_alpha(TH.sub, alpha), dstr);
    g_rect(c, fx + 20, fy + 139, fw - 40, 1, c_alpha(TH.line, alpha));
    icon_glyph(c, weather.ok ? weather_glyph(weather.code) : G_SUN, fx + 40, fy + 184, 31, c_alpha(TH.accent, alpha));
    text(c, F_SEMI, 14, 0, fx + 72, fy + 174, c_alpha(TH.text, alpha), weather.ok ? weather.place : "Bamako");
    if (weather.ok) labelf(c, F_LIGHT, 28, fx + 72, fy + 204, 34, c_alpha(TH.text, alpha), "%.0f°C", weather.temp);
    else text(c, F_REG, 13, 0, fx + 72, fy + 200, c_alpha(TH.sub, alpha), "Météo indisponible");
    text(c, F_REG, 12, 0, fx + 72, fy + 224, c_alpha(TH.sub, alpha), weather.ok ? weather.cond : "Connexion en attente");
    g_rect(c, fx + 20, fy + 251, fw - 40, 1, c_alpha(TH.line, alpha));
    icon_glyph(c, G_CLOCK, fx + 31, fy + 280, 17, c_alpha(TH.accent, alpha));
    text(c, F_REG, 13, 0, fx + 56, fy + 285, c_alpha(TH.text, alpha), "Heure locale · Bamako");
    FILE *bf = fopen("/sys/class/power_supply/BAT0/capacity", "r"); int pct = -1;
    if (bf) { if (fscanf(bf, "%d", &pct) != 1 || pct < 0 || pct > 100) pct = -1; fclose(bf); }
    icon_glyph(c, G_BATTERY, fx + 31, fy + 318, 18, c_alpha(TH.accent, alpha));
    if (pct >= 0) { char b[40]; snprintf(b, sizeof b, "Batterie · %d%%", pct); text(c, F_REG, 13, 0, fx + 56, fy + 323, c_alpha(TH.text, alpha), b); }
    else text(c, F_REG, 13, 0, fx + 56, fy + 323, c_alpha(TH.sub, alpha), "Sur secteur");
    icon_glyph(c, TH.dark ? G_MOON : G_SUN, fx + 31, fy + 358, 18, c_alpha(TH.accent, alpha));
    text(c, F_REG, 13, 0, fx + 56, fy + 363, c_alpha(TH.text, alpha), TH.dark ? "Ambiance nuit" : "Ambiance jour");
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
        Win *maximized = wm_top();
        if (!maximized || maximized->state != 1) draw_topbar(c);
        draw_start(c);
        draw_taskbar(c);
        draw_tip(c);
        widgets_draw(c, widgets_p);
        draw_quick(c);
        draw_desk_menu(c);
        draw_clock_flyout(c);
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
        int h = hit_at(e->x, e->y);
        if (e->type == IE_DOWN && e->btn == 1 && auth_dialog) {
            if (h >= 230 && h < 230 + lock_user_count) {
                snprintf(lock_user, sizeof lock_user, "%s", lock_users[h - 230]);
                auth_status = flx_auth_status_user(lock_user);
                auth_confirm = 0; auth_password[0] = auth_first[0] = auth_message[0] = 0;
            } else if (h == H_LOCK_SUBMIT) lock_submit();
            dirty = 1; return 1;
        }
        if (e->type == IE_DOWN && e->btn == 1 && h == H_POWER) { lock_power_open = !lock_power_open; dirty = 1; return 1; }
        if (e->type == IE_DOWN && e->btn == 1 && h >= H_LOCK_SHUT && h <= H_LOCK_USER) {
            lock_power_open = 0;
            if (h == H_LOCK_SHUT || h == H_LOCK_RESTART) {
                MARK("POWER %s", h == H_LOCK_RESTART ? "restart" : "shutdown");
                settings_save(); sync(); exit(h == H_LOCK_RESTART ? 42 : 43);
            }
            if (h == H_LOCK_SLEEP) request_suspend();
            if (h == H_LOCK_USER) { lock_users_load(); auth_dialog = 1; auth_confirm = 0; auth_password[0] = auth_first[0] = auth_message[0] = 0; auth_status = flx_auth_status_user(lock_user); }
            dirty = 1; return 1;
        }
        if (e->type == IE_DOWN && e->btn == 1 && !lock_power_open && e->y >= SCR_H * .79f && e->y <= SCR_H * .79f + 76) { lock_down = 1; lock_x0 = e->x; lock_drag = 0; }
        else if (e->type == IE_MOVE && lock_down) { lock_drag = clampf(e->x - lock_x0, 0, fminf(390, SCR_W - 48) - 76); dirty = 1; }
        else if (e->type == IE_UP && lock_down) {
            lock_down = 0;
            if (lock_drag >= (fminf(390, SCR_W - 48) - 76) * .78f) {
                auth_status = flx_auth_status_user(lock_user); lock_users_load(); auth_dialog = 1; auth_confirm = 0;
                auth_password[0] = auth_first[0] = auth_message[0] = 0;
                if (resume_fast_unlock && auth_status >= 0) { unlock(); dirty = 1; return 1; }
                if (auth_status == 0) { unlock(); dirty = 1; return 1; }
                if (auth_status < 0) snprintf(auth_message, sizeof auth_message, "Service d’authentification indisponible.");
            } else lock_drag = 0;
            dirty = 1;
        }
        return 1;
    }
    if (wm_grabbing()) return 0;
    if (alttab_open) return 1;
    int h = hit_at(e->x, e->y);
    if (qs_drag && (e->type == IE_MOVE || e->type == IE_UP)) {
        float x0 = SCR_W - 12 - 356 + 14 + 40, sw = 356 - 28 - 50;
        float v = clampf((e->x - x0) / sw, 0, 1);
        if (qs_drag == 1) qs_bright = v; else qs_vol = v;
        if (e->type == IE_UP) { MARK("QUICK %s %.0f", qs_drag == 1 ? "brightness" : "volume", v * 100); qs_drag = 0; }
        dirty = 1;
        return 1;
    }
    if (e->type == IE_MOVE) {
        if (h != hover_id) { hover_id = h; dirty = 1; tip_id = h; tip_t = now(); }
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        return h >= 0 && !(h >= H_TB_APP && 0);
    }
    if (e->type == IE_DOWN) {
        dirty = 1;
        tip_id = -1;
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        if (start_open && h != H_START_BTN && (h < H_START_PANEL || h > H_ALLAPPS) && h < H_START_APP) { close_panels(); return 1; }
        if (clock_flyout_open && h != H_CLOCK && h != H_CLOCK_PANEL) { close_panels(); if (h < 0) return 1; }
        if (power_menu && h != H_POWER && (h < H_PWR_LOCK || h > H_PWR_SHUT)) power_menu = 0;
        if (qs_open && (h < H_QS_PANEL || h > H_QS_GEAR) && h != H_NET) { qs_open = 0; if (h < 0) return 1; }
        if (dm_open && (h < H_DM_PANEL || h > H_DM_REFRESH)) { dm_open = 0; if (h < 0) return 1; }
        if (h < 0 && e->btn == 2 && !win_at(e->x, e->y) && e->y > TOP && !start_open) { close_panels(); dm_open = 1; dm_x = e->x; dm_y = e->y; MARK("DESKTOP_MENU"); return 1; }
        if ((widgets_open && h != H_WIDGETS_BTN)) {
            if (h < 0) { close_panels(); return 1; }
        }
        switch (h) {
        case H_START_BTN: case H_SEARCH_BTN: toggle_start(); return 1;
        case H_TOP_SEARCH: open_shell_search(); return 1;
        case H_WIDGETS_BTN: { int o = !widgets_open; close_panels(); widgets_open = o; MARK("WIDGETS %s", o ? "open" : "close"); return 1; }
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
        case H_CLOCK: { int o = !clock_flyout_open; close_panels(); clock_flyout_open = o; MARK("CLOCK_FLYOUT %s", o ? "open" : "close"); return 1; }
        case H_CLOCK_PANEL: return 1;
        case H_POWER: power_menu = !power_menu; return 1;
        case H_PWR_LOCK: lock_now(); return 1;
        case H_PWR_SLEEP: request_suspend(); return 1;
        case H_PWR_RESTART: case H_PWR_SHUT:
            MARK("POWER %s", h == H_PWR_RESTART ? "restart" : "shutdown");
            settings_save(); sync();
            exit(h == H_PWR_RESTART ? 42 : 43);
        case H_USER: launch("settings", "accounts"); close_panels(); return 1;
        case H_ALLAPPS: all_apps = !all_apps; app_scroll = 0; dirty = 1; return 1;
        case H_START_PANEL: return 1;
        case H_START_SEARCH: search_focus = 1; dirty = 1; return 1;
        }
        if (h >= H_TB_APP && h < H_TB_APP + 16) { tb_click(h - H_TB_APP); return 1; }
        if (h >= H_TB_APP + 16 && h < H_TB_APP + 22) {
            int nth = h - H_TB_APP - 16, seen = 0;
            for (int i = nz - 1; i >= 0; i--) { Win *w = &wins[zord[i]]; if (w->used && w->mapped && !w->closing && !w->minimized && seen++ == nth) { wm_restore(w); close_panels(); return 1; } }
            return 1;
        }
        if (h >= H_START_APP && h < H_START_APP + NAPPS) { launch(APPS[h - H_START_APP].exe, 0); close_panels(); return 1; }
        if (h >= H_RESULT && h < H_RESULT + 16) { int r[16]; int n = results(r); if (h - H_RESULT < n) open_result(r[h - H_RESULT]); return 1; }
        if (h >= H_RECENT && h < H_RECENT + 8) { launch("notepad", recent[h - H_RECENT]); close_panels(); return 1; }
        return h >= 0;
    }
    if (e->type == IE_UP || e->type == IE_WHEEL) {
        if (e->type == IE_WHEEL && start_open && all_apps) { app_scroll = fmaxf(0, app_scroll + e->wheel * 54); dirty = 1; return 1; }
        if (widgets_open && widgets_mouse(e, widgets_p)) return 1;
        return h >= 0;
    }
    return 0;
}

int shell_key(IEv *e) {
    if (phase == PH_BOOT) return 1;
    if (phase == PH_LOCK) {
        if (e->type == IE_KEY && auth_dialog) {
            if (e->key == K_ESC) { auth_dialog = auth_confirm = 0; auth_password[0] = auth_first[0] = 0; }
            else if (e->key == K_ENTER) lock_submit();
            else if (e->key == K_BACKSPACE) {
                size_t n = strlen(auth_password);
                if (n) { do { n--; } while (n && ((unsigned char)auth_password[n] & 0xC0) == 0x80); auth_password[n] = 0; }
            } else if (e->text[0] && (unsigned char)e->text[0] >= 0x20) {
                size_t n = strlen(auth_password), add = strlen(e->text);
                if (add && add < sizeof e->text && n + add < sizeof auth_password - 1) memcpy(auth_password + n, e->text, add + 1);
            }
            dirty = 1; return 1;
        }
        if (e->type == IE_KEY && e->key == K_ESC) { lock_power_open = 0; lock_drag = 0; dirty = 1; }
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
        if (k == 'w') { int o = !widgets_open; close_panels(); widgets_open = o; MARK("WIDGETS %s", o ? "open" : "close"); return 1; }
        if (k == 'e') { launch("explorer", 0); return 1; }
        if (k == 'l') { lock_now(); return 1; }
        if (k == 's') { toggle_start(); dirty = 1; return 1; }
        if (k == 'a') { int o = !qs_open; close_panels(); qs_open = o; MARK("QUICK %s", o ? "open" : "close"); return 1; }
        return 0;   // Win+arrows go to the window manager
    }
    if (e->key == K_ESC && (start_open || widgets_open || qs_open || dm_open || clock_flyout_open)) { close_panels(); search_focus = 0; MARK("PANELS close"); return 1; }
    if (start_open && search_focus) {
        int n = strlen(query);
        if (e->key == K_BACKSPACE) { if (n) { do { n--; } while (n && ((unsigned char)query[n] & 0xC0) == 0x80); query[n] = 0; } }
        else if (e->key == K_ENTER) { int r[16]; if (results(r)) open_result(r[0]); }
        else if (e->text[0] && n + strlen(e->text) < sizeof query - 1) strcat(query, e->text);
        dirty = 1;
        return 1;
    }
    if (widgets_open && widgets_key(e)) { dirty = 1; return 1; }
    return 0;
}
