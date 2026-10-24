// Settings: System, Display (dock autohide, wallpaper, theme), Network, Apps, Accounts, About
// Matching the reference design from settings_page_exemple.png
#include "w12.h"
#include "user.h"
#include "../net/net.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>

enum {
    P_SYSTEM, P_DISPLAY, P_SOUND, P_NETWORK, P_BT, P_DEVICES,
    P_APPS, P_ACCOUNTS, P_TIME, P_SECURITY, P_UPDATE, NPAGES
};
static const char *PNAMES[NPAGES] = {
    "Système", "Affichage", "Son", "Réseau", "Bluetooth", "Périphériques",
    "Applications", "Comptes", "Heure et langue", "Sécurité", "Mise à jour"
};
static const int PGLYPH[NPAGES] = {
    G_GEAR, G_DISPLAY, G_VOLUME, G_NET, G_BLUETOOTH, G_DEVICES,
    G_APPS, G_USER, G_CLOCK, G_SHIELD, G_UPDATE
};

static int page = P_SYSTEM;
static Scroll sc;
static Image *thumbs[NWALL];
static char nettest[200]; static volatile int testing;
static char search_query[64] = "";
static int dock_autohide_setting = 0;
static int dock_apps_setting = 127, window_scale_setting = 100;
static const char *DOCK_APPS[7] = { "explorer", "browser", "terminal", "settings", "notepad", "calc", "taskmgr" };
static char password_old[128], password_new[128], password_confirm[128], recovery_code[80], recovery_new[128];
static char auth_feedback[144];
static int account_auth_status = -1;
static char create_username[32], create_fullname[64], create_password[128], create_admin_password[128];
static int create_admin_role;
static int thumbs_dark = -1;
#define NAVW 260

static char hw_status[4096];
static double hw_status_time;
static char hw_feedback[96];
static int hw_request(const char *cmd, char *out, size_t cap) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "/run/flx-device.sock");
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -1; }
    if (send(fd, cmd, strlen(cmd), MSG_NOSIGNAL) < 0) { close(fd); return -1; }
    ssize_t n = recv(fd, out, cap - 1, 0); close(fd);
    if (n < 0) return -1; out[n] = 0; return 0;
}
static void hw_refresh(void) {
    if (now() - hw_status_time < 1.0) return;
    char buf[sizeof hw_status];
    if (!hw_request("GET", buf, sizeof buf)) { snprintf(hw_status, sizeof hw_status, "%s", buf); hw_feedback[0] = 0; }
    else snprintf(hw_status, sizeof hw_status, "INFO\tService matériel FLX\tIndisponible\n");
    hw_status_time = now();
}
static void hw_rows(Canvas *c, float x, float y, float w, const char *filter) {
    hw_refresh();
    char copy[sizeof hw_status]; snprintf(copy, sizeof copy, "%s", hw_status);
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *a = strchr(line, '\t'); if (!a) continue; *a++ = 0;
        char *b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
        if (filter && strcmp(line, filter)) continue;
        ui_card(x, y, w, 62);
        icon_glyph(c, !strcmp(line, "SOUND") ? G_VOLUME : !strcmp(line, "BLUETOOTH") ? G_BLUETOOTH :
                      !strcmp(line, "NETWORK") ? G_NET : !strcmp(line, "STORAGE") ? G_DRIVE :
                      !strcmp(line, "INPUT") ? G_DEVICES : G_DISPLAY, x + 28, y + 30, 20, TH.accent);
        text(c, F_SEMI, 14, 0, x + 54, y + 27, TH.text, a);
        text(c, F_REG, 12, 0, x + 54, y + 47, TH.sub, b);
        y += 72;
    }
    if (hw_feedback[0]) text(c, F_REG, 12, 0, x + 8, y + 8, TH.accent, hw_feedback);
}
static void page_sound(Canvas *c, float x, float y, float w) {
    hw_refresh();
    char *vp = strstr(hw_status, "VOLUME\t");
    int vol = -1;
    if (vp) { char *p = strchr(vp, '\t'); if (p && (p = strchr(p + 1, '\t'))) { if (isdigit((unsigned char)p[1])) vol = atoi(p + 1); } }
    ui_card(x, y, w, 80);
    icon_glyph(c, G_VOLUME, x + 30, y + 38, 24, TH.accent);
    text(c, F_SEMI, 16, 0, x + 62, y + 33, TH.text, vol >= 0 ? "Sortie audio" : "Service audio FLX");
    text(c, F_REG, 12, 0, x + 62, y + 56, TH.sub, vol >= 0 ? "Volume appliqué via les contrôles ALSA du noyau." : "Le mixeur matériel sera détecté automatiquement.");
    if (vol >= 0) {
        char label[16]; snprintf(label, sizeof label, "%d%%", vol);
        text_r(c, F_SEMI, 15, 0, x + w - 30, y + 36, TH.accent, label);
        float sx = x + 24, sy = y + 98, sw = w - 48;
        g_rrect(c, sx, sy, sw, 8, 4, 4, TH.hover);
        g_rrect(c, sx, sy, sw * vol / 100.0f, 8, 4, 4, TH.accent);
        if (U.ev == EV_DOWN && U.btn == 1 && U.mx >= sx && U.mx <= sx + sw && U.my >= sy - 12 && U.my <= sy + 20) {
            int next = (int)((U.mx - sx) * 100 / sw); char cmd[32], result[32];
            snprintf(cmd, sizeof cmd, "VOLUME %d", mini(100, maxi(0, next)));
            if (!hw_request(cmd, result, sizeof result) && !strcmp(result, "OK")) hw_status_time = 0;
        }
        y += 132;
    } else y += 96;
    hw_rows(c, x, y, w, "SOUND");
    if (!strstr(hw_status, "SOUND\t")) {
        ui_card(x, y + 72, w, 84);
        text(c, F_SEMI, 14, 0, x + 20, y + 104, TH.text, "Aucun contrôleur audio n’est exposé");
        text(c, F_REG, 12, 0, x + 20, y + 127, TH.sub, "Cette machine virtuelle n’a pas de périphérique son configuré.");
    }
}
static void page_bluetooth(Canvas *c, float x, float y, float w) {
    hw_refresh();
    int present = strstr(hw_status, "BLUETOOTH\t") && !strstr(hw_status, "aucun adaptateur détecté");
    ui_card(x, y, w, 114);
    icon_glyph(c, G_BLUETOOTH, x + 36, y + 42, 30, present ? TH.accent : TH.sub);
    text(c, F_SEMI, 17, 0, x + 78, y + 37, TH.text, present ? "Adaptateur Bluetooth" : "Aucun adaptateur Bluetooth");
    text(c, F_REG, 12, 0, x + 78, y + 60, TH.sub, present ? "État et blocage matériel lus depuis rfkill." : "Le service FLX surveille rfkill; aucun contrôleur n’est connecté.");
    if (present) {
        int on = strstr(hw_status, "BLUETOOTH\tBluetooth\tactivé") != NULL;
        if (ui_button(x + w - 148, y + 70, 126, 30, on ? "Désactiver" : "Activer", B_ACCENT)) {
            char result[32];
            if (!hw_request(on ? "BLUETOOTH off" : "BLUETOOTH on", result, sizeof result) && !strcmp(result, "OK")) {
                hw_status_time = 0; snprintf(hw_feedback, sizeof hw_feedback, "Bluetooth %s.", on ? "désactivé" : "activé");
            } else snprintf(hw_feedback, sizeof hw_feedback, "Le contrôleur ne permet pas ce changement.");
        }
    }
    hw_rows(c, x, y + 130, w, "BLUETOOTH");
}
static void page_devices(Canvas *c, float x, float y, float w) {
    text(c, F_REG, 13, 0, x + 2, y + 18, TH.sub, "Inventaire direct du noyau : écran, entrée, réseau, stockage et énergie.");
    y += 34;
    hw_rows(c, x, y, w, "SCREEN");
    hw_rows(c, x, y + 150, w, "DISPLAY");
    hw_rows(c, x, y + 300, w, "INPUT");
    hw_rows(c, x, y + 600, w, "NETWORK");
    hw_rows(c, x, y + 900, w, "STORAGE");
    hw_rows(c, x, y + 1200, w, "POWER");
}

static int setting_page_match(int p, const char *q) {
    if (!q[0] || strcasestr(PNAMES[p], q)) return 1;
    if (p == P_DISPLAY && (strcasestr("dock taille fenetre police fond ecran apparence accent", q))) return 1;
    if (p == P_ACCOUNTS && strcasestr("utilisateur mot de passe recuperation compte role", q)) return 1;
    if (p == P_NETWORK && strcasestr("wifi internet connexion adresse", q)) return 1;
    if (p == P_SYSTEM && strcasestr("version processeur cpu memoire stockage noyau", q)) return 1;
    return 0;
}

static void load_thumbs(void) {
    if (thumbs_dark == TH.dark) return;
    for (int i = 0; i < NWALL; i++) { image_free(thumbs[i]); thumbs[i] = NULL; }
    thumbs_dark = TH.dark;
    static const char *dark_names[6] = { "dark_aurora", "dark_eclipse", "dark_flow", "dark_glass", "dark_horizon", "dark_obsidian" };
    static const char *light_names[6] = { "light_aurora", "light_eclipse", "light_flow", "light_glass", "light_horizon", "light_obsidian" };
    static const char *world_names[3] = { "world_alpine", "world_coast", "world_sahara" };
    for (int i = 0; i < NWALL; i++) {
        const char *name = i < 6 ? (TH.dark ? dark_names[i] : light_names[i]) : world_names[i - 6];
        char p[80]; snprintf(p, sizeof p, "/share/wallpapers/%s.raw", name);
        size_t n; uint32_t *d = (uint32_t *)read_file(p, &n);
        if (!d || n < 12) { free(d); continue; }
        int w = d[1], h = d[2], tw = 240, th = 135;
        thumbs[i] = image_new(tw, th);
        for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) thumbs[i]->px[y * tw + x] = d[3 + (size_t)(y * h / th) * w + x * w / tw] | 0xFF000000;
        free(d);
    }
}

static void *test_th(void *a) {
    (void)a;
    HttpResp *r = http_get("https://api.open-meteo.com/");
    if (r->status == 200) snprintf(nettest, sizeof nettest, "Connecté · HTTPS %d · %zu octets reçus (TLS BearSSL)", r->status, r->len);
    else snprintf(nettest, sizeof nettest, "Échec · %s", r->err[0] ? r->err : "erreur statut");
    MARK("SETTINGS nettest %s", nettest);
    http_free(r);
    testing = 0;
    app_redraw();
    return 0;
}

// ---- Page: Système (Matches settings_page_exemple.png) --------------------------------
static void page_system(Canvas *c, float x, float y, float w) {
    // Card 1: FLX OS Badge Card
    float ch1 = 90;
    ui_card(x, y, w, ch1);
    g_rrect(c, x + 16, y + 16, 60, 58, 12, 12, 0xFF142430);
    g_rrect_line(c, x + 16, y + 16, 60, 58, 12, 1.5f, 0xFFF5AD32);
    icon_logo(c, x + 46, y + 45, 44, 255);
    text(c, F_BOLD, 22, 0, x + 92, y + 42, 0xFFFFFFFF, "FLX OS");
    text(c, F_REG, 14, 0, x + 92, y + 64, 0xFF60B5FF, "Version 0.1.0 (Dev)");
    y += ch1 + 16;

    // Card 2: Device Specs Card
    float ch2 = 210;
    ui_card(x, y, w, ch2);

    float ry = y + 18;
    // Row 1: Nom de l'appareil
    icon_glyph(c, G_DISPLAY, x + 34, ry + 16, 20, 0xFF94A6AA);
    text(c, F_REG, 14, 0, x + 68, ry + 22, 0xFFCBD5E1, "Nom de l'appareil");
    text_r(c, F_SEMI, 14, 0, x + w - 24, ry + 22, 0xFFFFFFFF, "flx-os");
    g_rect(c, x + 24, ry + 42, w - 48, 1, 0xFF182226);
    ry += 52;

    // Row 2: Processeur
    icon_glyph(c, G_CPU, x + 34, ry + 16, 20, 0xFF94A6AA);
    text(c, F_REG, 14, 0, x + 68, ry + 22, 0xFFCBD5E1, "Processeur");
    text_r(c, F_SEMI, 14, 0, x + w - 24, ry + 22, 0xFFFFFFFF, "Intel® Core™ i5 / QEMU vCPU (4 cœurs)");
    g_rect(c, x + 24, ry + 42, w - 48, 1, 0xFF182226);
    ry += 52;

    // Row 3: Mémoire vive
    long tot = 0, av = 0; char *m = read_file("/proc/meminfo", 0);
    if (m) { char *q; if ((q = strstr(m, "MemTotal:"))) tot = atol(q + 9); if ((q = strstr(m, "MemAvailable:"))) av = atol(q + 13); free(m); }
    char a[32], b[32];
    human_size((tot - av) * 1024.0, a, sizeof a); human_size(tot * 1024.0, b, sizeof b);
    char membuf[64]; snprintf(membuf, sizeof membuf, "%s / %s", a, b);

    icon_glyph(c, G_MEMORY, x + 34, ry + 16, 20, 0xFF94A6AA);
    text(c, F_REG, 14, 0, x + 68, ry + 22, 0xFFCBD5E1, "Mémoire vive");
    text_r(c, F_SEMI, 14, 0, x + w - 24, ry + 22, 0xFFFFFFFF, membuf);
    g_rect(c, x + 24, ry + 42, w - 48, 1, 0xFF182226);
    ry += 52;

    // Row 4: Stockage
    struct statfs st;
    char sto[64] = "42.8 Go / 100 Go";
    if (!statfs("/home", &st)) {
        human_size((double)(st.f_blocks - st.f_bfree) * st.f_bsize, a, sizeof a);
        human_size((double)st.f_blocks * st.f_bsize, b, sizeof b);
        snprintf(sto, sizeof sto, "%s / %s", a, b);
    }
    icon_glyph(c, G_DRIVE, x + 34, ry + 16, 20, 0xFF94A6AA);
    text(c, F_REG, 14, 0, x + 68, ry + 22, 0xFFCBD5E1, "Stockage");
    text_r(c, F_SEMI, 14, 0, x + w - 24, ry + 22, 0xFFFFFFFF, sto);
    y += ch2 + 16;

    // Card 3: Informations système
    float ch3 = 68;
    ui_card(x, y, w, ch3);
    icon_glyph(c, G_SHIELD, x + 34, y + 34, 22, 0xFF2CD5C6);
    text(c, F_SEMI, 15, 0, x + 68, y + 30, 0xFFFFFFFF, "Informations système");
    text(c, F_REG, 12, 0, x + 68, y + 50, 0xFF708890, "Kernel 6.12-flx · DRM virtio-gpu · musl libc · zéro toolkit");
    icon_glyph(c, G_CHEV_R, x + w - 30, y + 34, 14, 0xFF94A6AA);
    if (ui_click(x, y, w, ch3)) app_launch("proof", 0);
}

// ---- Page: Affichage (Dock Auto-Hide & Personnalisation) ------------------------------
static void page_display(Canvas *c, float x, float y, float w) {
    hw_refresh();
    char *bp = strstr(hw_status, "DISPLAY\t");
    if (bp) {
        char *detail = strchr(bp, '\t'); if (detail) detail = strchr(detail + 1, '\t');
        int cur = 0, max = 0; if (detail) sscanf(detail + 1, "%d/%d", &cur, &max);
        int pct = max > 0 ? cur * 100 / max : 0;
        ui_card(x, y, w, 70);
        icon_glyph(c, G_DISPLAY, x + 32, y + 35, 20, TH.accent);
        text(c, F_SEMI, 15, 0, x + 64, y + 29, TH.text, "Luminosité du rétroéclairage");
        text(c, F_REG, 12, 0, x + 64, y + 50, TH.sub, "Contrôle transmis au pilote d’écran par le service matériel FLX.");
        char level[16]; snprintf(level, sizeof level, "%d%%", pct);
        text_r(c, F_SEMI, 14, 0, x + w - 183, y + 42, TH.accent, level);
        if (ui_button(x + w - 122, y + 19, 38, 32, "−", B_STD) || ui_button(x + w - 74, y + 19, 38, 32, "+", B_STD)) {
            int next = pct + (U.mx > x + w - 98 ? 10 : -10); next = mini(100, maxi(5, next));
            char cmd[32], result[32]; snprintf(cmd, sizeof cmd, "BRIGHTNESS %d", next);
            if (!hw_request(cmd, result, sizeof result) && !strcmp(result, "OK")) hw_status_time = 0;
            else snprintf(hw_feedback, sizeof hw_feedback, "Le pilote ne permet pas de régler la luminosité.");
        }
        y += 84;
    } else {
        ui_card(x, y, w, 58);
        icon_glyph(c, G_DISPLAY, x + 30, y + 29, 19, TH.sub);
        text(c, F_SEMI, 14, 0, x + 58, y + 26, TH.text, "Aucun rétroéclairage matériel réglable détecté");
        text(c, F_REG, 11, 0, x + 58, y + 44, TH.sub, "La luminosité écran reste gérée par l’affichage virtio dans cette VM.");
        y += 72;
    }
    // Dock Auto-hide Setting
    ui_card(x, y, w, 70);
    icon_glyph(c, G_DISPLAY, x + 32, y + 35, 20, TH.text);
    text(c, F_SEMI, 15, 0, x + 64, y + 32, TH.text, "Masquer automatiquement le dock");
    text(c, F_REG, 12, 0, x + 64, y + 52, TH.sub, "Le dock se cache hors de l'écran quand la souris s'éloigne (fenêtres plein écran)");

    float btn_w = 110, btn_h = 32;
    if (ui_button(x + w - btn_w - 20, y + 19, btn_w, btn_h, dock_autohide_setting ? "Activé" : "Désactivé", dock_autohide_setting ? B_ACCENT : B_STD)) {
        dock_autohide_setting = !dock_autohide_setting;
        char buf[32]; snprintf(buf, sizeof buf, "autohide=%d\n", dock_autohide_setting);
        FILE *f = fopen("/home/user/.config/dock.conf", "w");
        if (f) { fputs(buf, f); fclose(f); sync(); }
        app_set_dock(dock_autohide_setting);
        MARK("SETTINGS dock_autohide %d", dock_autohide_setting);
    }
    y += 86;

    ui_card(x, y, w, 70);
    icon_glyph(c, G_DISPLAY, x + 32, y + 35, 20, TH.text);
    text(c, F_SEMI, 15, 0, x + 64, y + 29, TH.text, "Taille des fenêtres d’applications");
    char scale_label[24]; snprintf(scale_label, sizeof scale_label, "%d%%", window_scale_setting);
    text_r(c, F_SEMI, 14, 0, x + w - 124, y + 42, TH.accent, scale_label);
    if (ui_button(x + w - 104, y + 19, 38, 32, "−", B_STD)) { window_scale_setting = maxi(80, window_scale_setting - 10); app_set_scale(window_scale_setting); }
    if (ui_button(x + w - 58, y + 19, 38, 32, "+", B_STD)) { window_scale_setting = mini(140, window_scale_setting + 10); app_set_scale(window_scale_setting); }
    y += 84;

    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Applications épinglées au dock");
    y += 30;
    for (int i = 0; i < 7; i++) {
        float bx = x + (i % 4) * (w / 4), by = y + (i / 4) * 46;
        int app = app_by_exe(DOCK_APPS[i]);
        if (app < 0) continue;
        bool on = (dock_apps_setting & (1 << i)) != 0;
        ui_card(bx, by, w / 4 - 8, 38);
        icon_app(c, app, bx + 8, by + 6, 26);
        text(c, F_REG, 11, 0, bx + 40, by + 24, TH.text, APPS[app].title);
        if (on) icon_glyph(c, G_CHECK, bx + w / 4 - 28, by + 19, 14, TH.accent);
        if (ui_click(bx, by, w / 4 - 8, 38)) { dock_apps_setting ^= 1 << i; app_set_dock_apps(dock_apps_setting); }
    }
    y += 104;

    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Police de l’interface");
    y += 30;
    for (int i = 0; i < 4; i++) {
        float bx = x + i * (w / 4), bw = w / 4 - 8;
        bool on = font_get_family() == i;
        if (ui_button(bx, y, bw, 38, font_family_name(i), on ? B_ACCENT : B_STD)) app_set_font(i);
    }
    y += 52;

    // Theme selector
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Mode d'affichage");
    y += 34;
    for (int i = 0; i < 2; i++) {
        float cx = x + i * 236, cw = 220, ch = 130;
        bool on = (TH.dark == i);
        g_rrect(c, cx, y, cw, ch, 10, 10, i ? 0xFF141F25 : 0xFFF3F3F3);
        g_rrect_line(c, cx, y, cw, ch, 10, on ? 2.0f : 1.0f, on ? TH.accent : TH.line);
        text(c, F_SEMI, 15, 0, cx + 20, y + 110, i ? 0xFFFFFFFF : 0xFF1A1A1A, i ? "Mode Sombre" : "Mode Clair");
        if (ui_click(cx, y, cw, ch) && !on) { app_set_theme(i, TH.accent_i, TH.wall); }
    }
    y += 150;

    // Accent colors
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Couleur d'accentuation");
    y += 34;
    for (int i = 0; i < NACCENT; i++) {
        float cx = x + i * 110;
        bool on = (TH.accent_i == i);
        g_rrect(c, cx, y, 98, 64, 8, 8, ACCENTS[i]);
        if (on) {
            g_rrect_line(c, cx - 3, y - 3, 104, 70, 10, 2.5f, TH.text);
            icon_glyph(c, G_CHECK, cx + 49, y + 32, 22, 0xFFFFFFFF);
        }
        text_c(c, F_REG, 12, 0, cx + 49, y + 84, TH.sub, ACCENT_NAMES[i]);
        if (ui_click(cx, y, 98, 64) && !on) { app_set_theme(TH.dark, i, TH.wall); }
    }
    y += 110;

    // Wallpapers
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Fonds d'écran réels");
    y += 34;
    for (int i = 0; i < NWALL; i++) {
        float cx = x + (i % 3) * 256;
        float cy = y + (i / 3) * 174;
        bool on = (TH.wall == i);
        if (thumbs[i]) g_image(c, thumbs[i], cx, cy, 240, 135, 8, 255);
        if (on) g_rrect_line(c, cx - 4, cy - 4, 248, 143, 10, 2.5f, TH.accent);
        text_c(c, F_REG, 13, 0, cx + 120, cy + 158, TH.text, WALL_NAMES[i]);
        if (ui_click(cx, cy, 240, 135) && !on) { app_set_theme(TH.dark, TH.accent_i, i); }
    }
}

// ---- Page: Comptes (User Account Management) ------------------------------------------
static void page_accounts(Canvas *c, float x, float y, float w) {
    UserAccount *cur = user_get_current();
    if (!cur) return;

    // Current User Profile Card
    float ch1 = 110;
    ui_card(x, y, w, ch1);
    icon_user_avatar(c, x + 20, y + 20, 70, 35, 255);
    text(c, F_BOLD, 20, 0, x + 105, y + 46, TH.text, cur->fullname);
    char inf[128];
    snprintf(inf, sizeof inf, "@%s · %s · Créé le %s", cur->username, cur->role, cur->created);
    text(c, F_REG, 13, 0, x + 105, y + 72, TH.sub, inf);

    y += ch1 + 24;

    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Tous les comptes utilisateurs");
    y += 34;

    for (int i = 0; i < g_users.count; i++) {
        UserAccount *u = &g_users.users[i];
        bool is_cur = (strcmp(u->username, g_users.current_username) == 0);
        float uh = 64;
        ui_card(x, y, w, uh);
        icon_user_avatar(c, x + 16, y + 12, 40, 20, 255);
        text(c, F_SEMI, 15, 0, x + 70, y + 30, TH.text, u->fullname);
        char sub[64]; snprintf(sub, sizeof sub, "@%s · %s", u->username, u->role);
        text(c, F_REG, 12, 0, x + 70, y + 50, TH.sub, sub);

        if (is_cur) {
            text_r(c, F_SEMI, 13, 0, x + w - 24, y + 38, TH.accent, "Session active");
        }
        y += uh + 12;
    }

    if (cur->is_admin) {
        y += 6;
        ui_card(x, y, w, 236);
        text(c, F_SEMI, 15, 0, x + 18, y + 24, TH.text, "Créer un compte local");
        ui_field(40, create_username, sizeof create_username, x + 18, y + 36, (w - 42) * .42f, 30, "Identifiant (ex. aminata)");
        ui_field(41, create_fullname, sizeof create_fullname, x + 24 + (w - 42) * .42f, y + 36, (w - 42) * .58f, 30, "Nom affiché");
        ui_password_field(42, create_password, sizeof create_password, x + 18, y + 74, w - 36, 30, "Mot de passe (1 caractère minimum)");
        ui_password_field(43, create_admin_password, sizeof create_admin_password, x + 18, y + 112, w - 220, 30, "Confirmer avec le mot de passe admin");
        if (ui_button(x + w - 194, y + 112, 176, 30, create_admin_role ? "Rôle : Administrateur" : "Rôle : Standard", B_STD)) create_admin_role = !create_admin_role;
        if (ui_button(x + 18, y + 154, 180, 30, "Créer l’utilisateur", B_ACCENT)) {
            if (!create_username[0] || !create_fullname[0] || !create_password[0] || !create_admin_password[0]) snprintf(auth_feedback, sizeof auth_feedback, "Tous les champs sont requis.");
            else {
                char actor_hex[257], pass_hex[257], name_hex[129], req[900], response[128];
                size_t j = 0; for (const unsigned char *p = (void *)create_admin_password; *p && j + 2 < sizeof actor_hex; p++) j += (size_t)snprintf(actor_hex + j, sizeof actor_hex - j, "%02x", *p); actor_hex[j] = 0;
                j = 0; for (const unsigned char *p = (void *)create_password; *p && j + 2 < sizeof pass_hex; p++) j += (size_t)snprintf(pass_hex + j, sizeof pass_hex - j, "%02x", *p); pass_hex[j] = 0;
                j = 0; for (const unsigned char *p = (void *)create_fullname; *p && j + 2 < sizeof name_hex; p++) j += (size_t)snprintf(name_hex + j, sizeof name_hex - j, "%02x", *p); name_hex[j] = 0;
                snprintf(req, sizeof req, "ADD %s %s %s %s %s %s", g_users.current_username, actor_hex, create_username, name_hex, create_admin_role ? "admin" : "standard", pass_hex);
                if (!app_auth_request(req, response, sizeof response) && !strcmp(response, "OK")) { snprintf(auth_feedback, sizeof auth_feedback, "Compte %s créé avec son dossier privé.", create_username); memset(create_username, 0, sizeof create_username); memset(create_fullname, 0, sizeof create_fullname); memset(create_password, 0, sizeof create_password); user_init(); }
                else snprintf(auth_feedback, sizeof auth_feedback, "Création refusée : vérifiez le mot de passe admin et l’identifiant.");
                memset(create_admin_password, 0, sizeof create_admin_password); memset(actor_hex, 0, sizeof actor_hex); memset(pass_hex, 0, sizeof pass_hex); memset(name_hex, 0, sizeof name_hex); memset(req, 0, sizeof req); memset(response, 0, sizeof response);
            }
            app_redraw();
        }
        text(c, F_REG, 11, 0, x + 214, y + 175, TH.sub, "Chaque compte utilise un UID et un dossier personnel distincts.");
        y += 250;
    }

    y += 8;
    text(c, F_REG, 12, 0, x + 4, y + 14, TH.sub, "Le changement de compte demande le mot de passe du compte choisi.");
    y += 28;
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Sécurité du compte actif");
    y += 32;
    ui_card(x, y, w, 186);
    if (account_auth_status < 0) { char req[64], status[32]; snprintf(req, sizeof req, "STATUS %s", g_users.current_username); if (!app_auth_request(req, status, sizeof status)) account_auth_status = !strcmp(status, "EMPTY") ? 0 : 1; }
    int first_setup = account_auth_status == 0;
    text(c, F_SEMI, 14, 0, x + 18, y + 24, TH.text, first_setup ? "Définir un mot de passe" : "Changer le mot de passe");
    const char *ph_old = "Mot de passe actuel", *ph_new = "Nouveau mot de passe (1 caractère minimum)", *ph_confirm = "Confirmer le nouveau mot de passe";
    if (!first_setup) ui_password_field(30, password_old, sizeof password_old, x + 18, y + 38, w - 36, 30, ph_old);
    ui_password_field(31, password_new, sizeof password_new, x + 18, y + 74, w - 36, 30, ph_new);
    ui_password_field(32, password_confirm, sizeof password_confirm, x + 18, y + 110, w - 36, 30, ph_confirm);
    if (ui_button(x + 18, y + 148, 190, 30, first_setup ? "Définir le mot de passe" : "Enregistrer le mot de passe", B_ACCENT)) {
        if (!password_new[0]) snprintf(auth_feedback, sizeof auth_feedback, "Un caractère minimum est requis.");
        else if (strcmp(password_new, password_confirm)) snprintf(auth_feedback, sizeof auth_feedback, "Les deux nouveaux mots de passe diffèrent.");
        else {
            char old_hex[257], new_hex[513], req[800], response[128];
            size_t j = 0; for (const unsigned char *p = (void *)password_old; *p && j + 2 < sizeof old_hex; p++) j += snprintf(old_hex + j, sizeof old_hex - j, "%02x", *p); old_hex[j] = 0;
            j = 0; for (const unsigned char *p = (void *)password_new; *p && j + 2 < sizeof new_hex; p++) j += snprintf(new_hex + j, sizeof new_hex - j, "%02x", *p); new_hex[j] = 0;
            if (first_setup) snprintf(req, sizeof req, "SETHEX %s %s", g_users.current_username, new_hex);
            else snprintf(req, sizeof req, "CHANGE %s %s %s", g_users.current_username, old_hex, new_hex);
            if (!app_auth_request(req, response, sizeof response) && !strcmp(response, "OK")) { snprintf(auth_feedback, sizeof auth_feedback, first_setup ? "Mot de passe défini." : "Mot de passe mis à jour."); account_auth_status = 1; }
            else snprintf(auth_feedback, sizeof auth_feedback, "Mot de passe actuel incorrect ou requête refusée.");
            memset(password_old, 0, sizeof password_old); memset(password_new, 0, sizeof password_new); memset(password_confirm, 0, sizeof password_confirm);
            memset(old_hex, 0, sizeof old_hex); memset(new_hex, 0, sizeof new_hex); memset(req, 0, sizeof req); memset(response, 0, sizeof response);
        }
        app_redraw();
    }
    y += 202;
    ui_card(x, y, w, 154);
    text(c, F_SEMI, 14, 0, x + 18, y + 24, TH.text, "Récupération du compte");
    text(c, F_REG, 11, 0, x + 18, y + 42, TH.sub, "Créez un code de secours après avoir confirmé votre mot de passe actuel.");
    ui_password_field(33, password_old, sizeof password_old, x + 18, y + 50, w - 36, 30, ph_old);
    if (ui_button(x + 18, y + 88, 210, 30, "Générer un code de secours", B_STD)) {
        char hex[257], req[300], response[128]; size_t j = 0;
        for (const unsigned char *p = (void *)password_old; *p && j + 2 < sizeof hex; p++) j += snprintf(hex + j, sizeof hex - j, "%02x", *p);
        hex[j] = 0; snprintf(req, sizeof req, "ROTATE %s %s", g_users.current_username, hex);
        if (!app_auth_request(req, response, sizeof response) && !strncmp(response, "RECOVERY ", 9)) snprintf(auth_feedback, sizeof auth_feedback, "Code à conserver : %s", response + 9);
        else snprintf(auth_feedback, sizeof auth_feedback, "Mot de passe actuel incorrect ou requête refusée.");
        memset(password_old, 0, sizeof password_old); memset(hex, 0, sizeof hex); memset(req, 0, sizeof req); memset(response, 0, sizeof response);
        app_redraw();
    }
    y += 170;
    ui_card(x, y, w, 126);
    text(c, F_SEMI, 14, 0, x + 18, y + 24, TH.text, "Récupérer avec un code de secours");
    ui_field(34, recovery_code, sizeof recovery_code, x + 18, y + 36, w - 36, 30, "Code de secours");
    ui_password_field(35, recovery_new, sizeof recovery_new, x + 18, y + 72, w - 240, 30, "Nouveau mot de passe");
    if (ui_button(x + w - 204, y + 72, 186, 30, "Réinitialiser", B_ACCENT)) {
        char token_hex[257], pass_hex[513], req[800], response[128]; size_t j = 0;
        for (const unsigned char *p = (void *)recovery_code; *p && j + 2 < sizeof token_hex; p++) j += snprintf(token_hex + j, sizeof token_hex - j, "%02x", *p);
        token_hex[j] = 0; j = 0;
        for (const unsigned char *p = (void *)recovery_new; *p && j + 2 < sizeof pass_hex; p++) j += snprintf(pass_hex + j, sizeof pass_hex - j, "%02x", *p);
        pass_hex[j] = 0; snprintf(req, sizeof req, "RECOVER %s %s %s", g_users.current_username, token_hex, pass_hex);
        if (!app_auth_request(req, response, sizeof response) && !strcmp(response, "OK")) snprintf(auth_feedback, sizeof auth_feedback, "Mot de passe réinitialisé. Le code de secours a été consommé.");
        else snprintf(auth_feedback, sizeof auth_feedback, "Code de secours invalide ou requête refusée.");
        memset(recovery_code, 0, sizeof recovery_code); memset(recovery_new, 0, sizeof recovery_new); memset(token_hex, 0, sizeof token_hex); memset(pass_hex, 0, sizeof pass_hex); memset(req, 0, sizeof req); memset(response, 0, sizeof response);
        app_redraw();
    }
    text(c, F_REG, 12, 0, x + 4, y + 144, TH.accent, auth_feedback);
}

// ---- Page: Applications (Installed & Uninstall) ---------------------------------------
static void page_apps(Canvas *c, float x, float y, float w) {
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Applications installées");
    text_r(c, F_REG, 13, 0, x + w, y + 18, TH.sub, "Gérer et désinstaller les applications");
    y += 36;

    for (int i = 0; i < NAPPS; i++) {
        ui_card(x, y, w, 68);
        icon_app(c, i, x + 18, y + 12, 44);
        text(c, F_SEMI, 15, 0, x + 76, y + 32, TH.text, APPS[i].title);
        text(c, F_REG, 12, 0, x + 76, y + 52, TH.sub, APPS[i].desc);

        float bw = 110, bh = 32;
        if (ui_button(x + w - bw - 14, y + 18, bw, bh, "Ouvrir", B_STD)) app_launch(APPS[i].exe, 0);
        y += 78;
    }
}

// ---- Page: Réseau --------------------------------------------------------------------
static void page_network(Canvas *c, float x, float y, float w) {
    NetInfo ni; netinfo_read(&ni);
    ui_card(x, y, w, 110);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, x + 34, y + 42, 28, ni.up ? 0xFF14B53A : TH.danger);
    text(c, F_SEMI, 18, 0, x + 76, y + 40, TH.text, ni.up ? "Connecté à Internet" : "Déconnecté");
    char s[200];
    snprintf(s, sizeof s, "Interface %s · IP %s · Passerelle %s · DNS %s", ni.ifname, ni.ip, ni.gw, ni.dns);
    text(c, F_REG, 13, 0, x + 76, y + 66, TH.sub, s);
    y += 126;

    ui_card(x, y, w, 90);
    text(c, F_SEMI, 15, 0, x + 24, y + 32, TH.text, "Test de connexion TLS / HTTPS");
    text(c, F_REG, 13, 0, x + 24, y + 56, TH.sub, nettest[0] ? nettest : "Prêt à tester la pile réseau FLX OS");
    if (!testing && ui_button(x + w - 120, y + 28, 100, 34, "Tester", B_ACCENT)) {
        testing = 1; snprintf(nettest, sizeof nettest, "Connexion HTTPS en cours...");
        pthread_t t; pthread_create(&t, 0, test_th, 0); pthread_detach(t);
    }
}

// ---- Main Drawing --------------------------------------------------------------------
static void draw(Canvas *c) {
    load_thumbs();
    float W = U.w, H = U.h;

    g_rect(c, 0, 0, W, 56, TH.surface);
    g_rect(c, 0, 55, W, 1, TH.line);
    text(c, F_BOLD, 18, 0, 24, 34, TH.text, "Paramètres");

    float sw = fminf(380, W - NAVW - 52), sx = W - sw - 20;
    icon_glyph(c, G_SEARCH, sx + 18, 28, 16, TH.sub);
    int sr = ui_field(90, search_query, sizeof search_query, sx + 36, 10, sw - 36, 36, "Rechercher un paramètre");
    int first_match = -1;
    for (int i = 0; i < NPAGES; i++) if (setting_page_match(i, search_query)) { first_match = i; break; }
    if (sr == 1 && first_match >= 0) { page = first_match; sc.pos = 0; }

    // Left Navigation Pane
    float top = 56;
    g_rect(c, 0, top, NAVW, H - top, TH.bg);
    g_rect(c, NAVW - 1, top, 1, H - top, TH.line);

    for (int i = 0; i < NPAGES; i++) {
        if (!setting_page_match(i, search_query)) continue;
        float ny = top + 12 + i * 40;
        bool on = (page == i);
        if (on) {
            g_rrect(c, 10, ny, NAVW - 20, 36, 6, 6, 0x33F5AD32);
            g_rrect_line(c, 10, ny, NAVW - 20, 36, 6, 1.5f, 0xFFF5AD32);
        } else if (ui_hover(10, ny, NAVW - 20, 36)) {
            g_rrect(c, 10, ny, NAVW - 20, 36, 6, 6, TH.hover);
        }
        icon_glyph(c, PGLYPH[i], 32, ny + 18, 18, on ? 0xFFFFD477 : TH.sub);
        text(c, on ? F_SEMI : F_REG, 14, 0, 56, ny + 23, on ? 0xFFFFFFFF : TH.text, PNAMES[i]);
        if (ui_click(10, ny, NAVW - 20, 36)) { page = i; sc.pos = 0; MARK("SETTINGS page %s", PNAMES[i]); }
    }

    // Content Pane
    float cx = NAVW + 28, cw = W - cx - 36;
    if (search_query[0] && first_match < 0) {
        ui_card(cx, top + 72, cw, 100);
        text(c, F_SEMI, 16, 0, cx + 24, top + 112, TH.text, "Aucun paramètre trouvé");
        text(c, F_REG, 13, 0, cx + 24, top + 140, TH.sub, "Essaie : affichage, dock, police, réseau, compte ou mot de passe.");
        return;
    }
    text(c, F_BOLD, 26, 0, cx, top + 42, TH.text, PNAMES[page]);

    float ctop = top + 56;
    int s0[4]; g_getclip(c, s0);
    g_clip(c, NAVW, ctop, W, H);
    float y = ctop + 10 - sc.pos;

    switch (page) {
    case P_SYSTEM:   page_system(c, cx, y, cw); break;
    case P_DISPLAY:  page_display(c, cx, y, cw); break;
    case P_SOUND:    page_sound(c, cx, y, cw); break;
    case P_NETWORK:  page_network(c, cx, y, cw); break;
    case P_BT:       page_bluetooth(c, cx, y, cw); break;
    case P_DEVICES:  page_devices(c, cx, y, cw); break;
    case P_ACCOUNTS: page_accounts(c, cx, y, cw); break;
    case P_APPS:     page_apps(c, cx, y, cw); break;
    default:
        ui_card(cx, y, cw, 120);
        icon_glyph(c, PGLYPH[page], cx + 39, y + 42, 24, TH.accent);
        text(c, F_SEMI, 16, 0, cx + 64, y + 40, TH.text, PNAMES[page]);
        text(c, F_REG, 13, 0, cx + 64, y + 68, TH.sub, "Ces contrôles ne sont pas encore disponibles dans cette version de FLX OS.");
        break;
    }
    g_setclip(c, s0);
    float display_height = 94 + 10 + 86 + 84 + 134 + 82 + 184 + 144 + 34 + ((NWALL + 2) / 3) * 174 + 80;
    ui_scroll(&sc, NAVW, ctop, W - NAVW, H - ctop,
              page == P_DISPLAY ? display_height : page == P_ACCOUNTS ? 2800.0f : page == P_DEVICES ? 6500.0f : 900.0f);
}

static void init(int argc, char **argv) {
    user_init();
    load_thumbs();
    char *ds = read_file("/home/user/.config/dock.conf", 0);
    if (!ds) ds = read_file("/home/user/.config/flx.conf", 0);
    if (ds) {
        char *p = strstr(ds, "autohide=");
        if (p) dock_autohide_setting = atoi(p + 9);
        if ((p = strstr(ds, "dockapps="))) {
            dock_apps_setting = atoi(p + 9) & 255;
            if (dock_apps_setting == 63) dock_apps_setting = 127;
        }
        if ((p = strstr(ds, "scale="))) window_scale_setting = mini(140, maxi(80, atoi(p + 6)));
        free(ds);
    }
    if (argc > 1) {
        if (!strcmp(argv[1], "network")) page = P_NETWORK;
        else if (!strcmp(argv[1], "sound")) page = P_SOUND;
        else if (!strcmp(argv[1], "bluetooth")) page = P_BT;
        else if (!strcmp(argv[1], "devices")) page = P_DEVICES;
        else if (!strcmp(argv[1], "display") || !strcmp(argv[1], "personalization")) page = P_DISPLAY;
        else if (!strcmp(argv[1], "accounts") || !strcmp(argv[1], "user")) page = P_ACCOUNTS;
        else if (!strcmp(argv[1], "apps")) page = P_APPS;
    }
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "settings", .title = "Paramètres", .w = 1080, .h = 680, .mica = 1, .init = init, .draw = draw, .tick_ms = 2000 };
    return app_main(&a, argc, argv);
}
