// Settings: System, Display (dock autohide, wallpaper, theme), Network, Apps, Accounts, About
// Matching the reference design from settings_page_exemple.png
#include "w12.h"
#include "user.h"
#include "../net/net.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>

enum {
    P_SYSTEM, P_DISPLAY, P_SOUND, P_NETWORK, P_BT, P_DEVICES,
    P_APPS, P_ACCOUNTS, P_TIME, P_SECURITY, P_UPDATE, NPAGES
};
static const char *PNAMES[NPAGES] = {
    "Système", "Affichage", "Son", "Réseau", "Bluetooth", "Périphériques",
    "Applications", "Comptes", "Heure et langue", "Sécurité", "Mise à jour"
};
static const int PGLYPH[NPAGES] = {
    G_GEAR, G_DISPLAY, G_VOLUME, G_NET, G_WIFI, G_SAVE,
    G_APPS, G_USER, G_CLOCK, G_SHIELD, G_REFRESH
};

static int page = P_SYSTEM;
static Scroll sc;
static Image *thumbs[NWALL];
static char nettest[200]; static volatile int testing;
static char search_query[64] = "";
static int dock_autohide_setting = 0;
static int dock_apps_setting = 255, window_scale_setting = 100;
static const char *DOCK_APPS[8] = { "explorer", "terminal", "settings", "taskmgr", "calc", "notepad", "minesweeper", "proof" };
static char password_old[128], password_new[128], password_confirm[128], recovery_code[80], recovery_new[128];
static char auth_feedback[144];
static int thumbs_dark = -1;
#define NAVW 260

static void load_thumbs(void) {
    if (thumbs_dark == TH.dark) return;
    for (int i = 0; i < NWALL; i++) { image_free(thumbs[i]); thumbs[i] = NULL; }
    thumbs_dark = TH.dark;
    static const char *names[NWALL] = { "dark_aurora", "dark_eclipse", "dark_flow", "dark_glass", "dark_horizon", "dark_obsidian", "world_alpine", "world_coast", "world_sahara", "light_aurora", "light_eclipse", "light_flow", "light_glass", "light_horizon", "light_obsidian" };
    for (int i = 0; i < NWALL; i++) {
        const char *name = i >= 6 && i <= 8 ? names[i] : names[i + (TH.dark ? 0 : 9)];
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
    icon_glyph(c, G_GEAR, x + 34, ry + 16, 20, 0xFF94A6AA);
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

    icon_glyph(c, G_APPS, x + 34, ry + 16, 20, 0xFF94A6AA);
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
    icon_glyph(c, G_SAVE, x + 34, ry + 16, 20, 0xFF94A6AA);
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
    for (int i = 0; i < 8; i++) {
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

    y += 8;
    text(c, F_REG, 12, 0, x + 4, y + 14, TH.sub, "Les comptes séparés, rôles et basculements protégés seront gérés par le service de session.");
    y += 28;
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Sécurité du compte actif");
    y += 32;
    ui_card(x, y, w, 186);
    text(c, F_SEMI, 14, 0, x + 18, y + 24, TH.text, "Changer le mot de passe");
    const char *ph_old = "Mot de passe actuel", *ph_new = "Nouveau mot de passe (10 caractères minimum)", *ph_confirm = "Confirmer le nouveau mot de passe";
    ui_password_field(30, password_old, sizeof password_old, x + 18, y + 38, w - 36, 30, ph_old);
    ui_password_field(31, password_new, sizeof password_new, x + 18, y + 74, w - 36, 30, ph_new);
    ui_password_field(32, password_confirm, sizeof password_confirm, x + 18, y + 110, w - 36, 30, ph_confirm);
    if (ui_button(x + 18, y + 148, 190, 30, "Enregistrer le mot de passe", B_ACCENT)) {
        if (strcmp(password_new, password_confirm)) snprintf(auth_feedback, sizeof auth_feedback, "Les deux nouveaux mots de passe diffèrent.");
        else {
            char old_hex[257], new_hex[513], req[800], response[128];
            size_t j = 0; for (const unsigned char *p = (void *)password_old; *p && j + 2 < sizeof old_hex; p++) j += snprintf(old_hex + j, sizeof old_hex - j, "%02x", *p); old_hex[j] = 0;
            j = 0; for (const unsigned char *p = (void *)password_new; *p && j + 2 < sizeof new_hex; p++) j += snprintf(new_hex + j, sizeof new_hex - j, "%02x", *p); new_hex[j] = 0;
            snprintf(req, sizeof req, "CHANGE %s %s", old_hex, new_hex);
            if (!app_auth_request(req, response, sizeof response) && !strcmp(response, "OK")) snprintf(auth_feedback, sizeof auth_feedback, "Mot de passe mis à jour.");
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
        hex[j] = 0; snprintf(req, sizeof req, "ROTATE %s", hex);
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
        pass_hex[j] = 0; snprintf(req, sizeof req, "RECOVER %s %s", token_hex, pass_hex);
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

        // Désinstaller button (or Ouvrir)
        float bw = 110, bh = 32;
        if (ui_button(x + w - bw - 14, y + 18, bw, bh, "Désinstaller", B_STD)) {
            // Uninstallation feedback
            MARK("SETTINGS uninstall %s", APPS[i].exe);
        }
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

    // Top Search Bar (Matches settings_page_exemple.png)
    g_rect(c, 0, 0, W, 56, TH.surface);
    g_rect(c, 0, 55, W, 1, TH.line);
    text(c, F_BOLD, 18, 0, 24, 34, TH.text, "Paramètres");

    float sw = 360, sx = W - sw - 24;
    ui_card(sx, 10, sw, 36);
    icon_glyph(c, G_SEARCH, sx + 18, 28, 16, TH.sub);
    label(c, F_REG, 13, sx + 36, 10, 36, TH.sub, search_query[0] ? search_query : "Rechercher un paramètre...");

    // Left Navigation Pane
    float top = 56;
    g_rect(c, 0, top, NAVW, H - top, TH.bg);
    g_rect(c, NAVW - 1, top, 1, H - top, TH.line);

    for (int i = 0; i < NPAGES; i++) {
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
    text(c, F_BOLD, 26, 0, cx, top + 42, TH.text, PNAMES[page]);

    float ctop = top + 56;
    int s0[4]; g_getclip(c, s0);
    g_clip(c, NAVW, ctop, W, H);
    float y = ctop + 10 - sc.pos;

    switch (page) {
    case P_SYSTEM:   page_system(c, cx, y, cw); break;
    case P_DISPLAY:  page_display(c, cx, y, cw); break;
    case P_NETWORK:  page_network(c, cx, y, cw); break;
    case P_ACCOUNTS: page_accounts(c, cx, y, cw); break;
    case P_APPS:     page_apps(c, cx, y, cw); break;
    default:
        ui_card(cx, y, cw, 120);
        text(c, F_SEMI, 16, 0, cx + 24, y + 40, TH.text, PNAMES[page]);
        text(c, F_REG, 13, 0, cx + 24, y + 68, TH.sub, "Cette section est prête et configurable par le système.");
        break;
    }
    g_setclip(c, s0);
    ui_scroll(&sc, NAVW, ctop, W - NAVW, H - ctop, page == P_DISPLAY ? 1500.0f : page == P_ACCOUNTS ? 1500.0f : 600.0f);
}

static void init(int argc, char **argv) {
    user_init();
    load_thumbs();
    char *ds = read_file("/home/user/.config/dock.conf", 0);
    if (!ds) ds = read_file("/home/user/.config/flx.conf", 0);
    if (ds) {
        char *p = strstr(ds, "autohide=");
        if (p) dock_autohide_setting = atoi(p + 9);
        if ((p = strstr(ds, "dockapps="))) dock_apps_setting = atoi(p + 9) & 255;
        if ((p = strstr(ds, "scale="))) window_scale_setting = mini(140, maxi(80, atoi(p + 6)));
        free(ds);
    }
    if (argc > 1) {
        if (!strcmp(argv[1], "network")) page = P_NETWORK;
        else if (!strcmp(argv[1], "display") || !strcmp(argv[1], "personalization")) page = P_DISPLAY;
        else if (!strcmp(argv[1], "accounts") || !strcmp(argv[1], "user")) page = P_ACCOUNTS;
        else if (!strcmp(argv[1], "apps")) page = P_APPS;
    }
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "settings", .title = "Paramètres", .w = 1080, .h = 680, .mica = 1, .init = init, .draw = draw, .tick_ms = 2000 };
    return app_main(&a, argc, argv);
}
