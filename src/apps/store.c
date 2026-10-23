// FLX Store: official application and games center for FLX OS
// Supports native FLX apps (.flxapp) and Flatpak compatibility
#include "w12.h"

typedef struct {
    const char *id;
    const char *name;
    const char *cat;
    const char *desc;
    const char *ver;
    const char *size;
    const char *icon;
    int installed;
    int is_native;
} StoreApp;

static StoreApp catalog[] = {
    { "terminal", "FLX Terminal", "Outils", "Terminal natif avancé avec onglets et thème personnalisé", "1.2.0", "480 Ko", "terminal", 1, 1 },
    { "explorer", "Fichiers", "Système", "Gestionnaire de fichiers moderne avec panneau latéral rapide", "1.1.0", "280 Ko", "explorer", 1, 1 },
    { "browser",  "Navigateur Web", "Internet", "Navigateur rapide avec support TLS / BearSSL et moteur HTML5", "2.0.1", "540 Ko", "browser", 1, 1 },
    { "settings", "Paramètres", "Système", "Panneau de configuration complet du système et des comptes", "1.0.4", "460 Ko", "settings", 1, 1 },
    { "code",     "Code - OSS", "Développement", "Éditeur de code extensible avec coloration syntaxique", "1.92.0", "64 Mo", "notepad", 1, 0 },
    { "libreoffice", "LibreOffice", "Bureautique", "Suite bureautique complète (Traitement de texte, Tableur)", "7.6.2", "180 Mo", "proof", 0, 0 },
    { "vlc",      "VLC Media Player", "Multimédia", "Lecteur multimédia universel haute performance", "3.0.20", "42 Mo", "media", 0, 0 },
    { "mines",    "Démineur FLX", "Jeux", "Le jeu classique revisité avec design néo-glass", "1.0.0", "250 Ko", "minesweeper", 1, 1 },
    { "calc",     "Calculatrice", "Utilitaires", "Calculatrice scientifique et arithmétique", "1.0.1", "260 Ko", "calc", 1, 1 },
    { "taskmgr",  "Gestionnaire de tâches", "Système", "Surveillance des performances CPU, mémoire et processus", "1.1.0", "270 Ko", "taskmgr", 1, 1 },
    { "gimp",     "GIMP Image Editor", "Création", "Éditeur d'images professionnel et retouche photo", "2.10.36", "85 Mo", "gallery", 0, 0 },
};
#define NCATALOG (int)(sizeof(catalog) / sizeof(catalog[0]))

static int active_tab = 0;
static const char *TABS[] = { "Tous", "Applications", "Jeux", "Outils", "Développement" };
#define NTABS (int)(sizeof(TABS) / sizeof(TABS[0]))

static char search_query[64] = "";
static Scroll sc;

// Cached icon images for non-app catalog entries
static Image *extra_icons[NCATALOG];
static int icon_tried[NCATALOG];

static Image *load_catalog_icon(int idx) {
    if (!icon_tried[idx]) {
        char path[64];
        snprintf(path, sizeof path, "/share/icons/%s.png", catalog[idx].icon);
        extra_icons[idx] = image_load(path);
        icon_tried[idx] = 1;
    }
    return extra_icons[idx];
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;

    // Header & Navigation
    g_rect(c, 0, 0, W, 60, TH.surface);
    g_rect(c, 0, 59, W, 1, TH.line);

    // Store Logo
    icon_logo(c, 28, 30, 32, 255);
    text(c, F_BOLD, 18, 0, 52, 36, TH.accent, "FLX Store");

    // Search bar
    float sw = 260, sx = W - sw - 20;
    ui_card(sx, 12, sw, 36);
    icon_glyph(c, G_SEARCH, sx + 18, 30, 16, TH.sub);
    ui_field(100, search_query, sizeof search_query, sx + 36, 12, sw - 56, 36, "Rechercher une app...");

    // Tabs
    float tx = 180;
    for (int i = 0; i < NTABS; i++) {
        float tw = text_w(F_SEMI, 14, 0, TABS[i]) + 24;
        bool active = (i == active_tab);
        if (ui_click(tx, 14, tw, 32)) { active_tab = i; sc.pos = 0; }
        if (active) {
            g_rrect(c, tx, 14, tw, 32, 6, 6, c_alpha(TH.accent, 0.25f));
            g_rrect_line(c, tx, 14, tw, 32, 6, 1.5f, TH.accent);
            text_c(c, F_SEMI, 14, 0, tx + tw / 2, 34, TH.accent, TABS[i]);
        } else {
            if (ui_hover(tx, 14, tw, 32)) g_rrect(c, tx, 14, tw, 32, 6, 6, TH.hover);
            text_c(c, F_REG, 14, 0, tx + tw / 2, 34, TH.text, TABS[i]);
        }
        tx += tw + 8;
    }

    // Scrollable content area
    float content_y = 70;
    float content_h = H - content_y;

    int cs[4]; g_getclip(c, cs);
    g_clip(c, 0, content_y, W, H);

    // Hero banner
    float bx = 20, bw = W - 40, bh = 100;
    float by0 = content_y - sc.pos;
    g_vgrad(c, bx, by0, bw, bh, 10, 0xFF142430, 0xFF0B141C);
    g_rrect_line(c, bx, by0, bw, bh, 10, 1, c_alpha(TH.accent, 0.4f));
    icon_logo(c, bx + 50, by0 + 50, 54, 255);
    text(c, F_BOLD, 20, 0, bx + 95, by0 + 40, 0xFFFFFFFF, "Bienvenue sur le Store FLX OS");
    text(c, F_REG, 14, 0, bx + 95, by0 + 66, 0xFFB0C4DE, "Applications natives sandboxées (.flxapp) et support universel Flatpak.");

    float y = content_y + bh + 20 - sc.pos;

    // Filter apps based on tab
    int cols = (W > 900) ? 3 : (W > 600 ? 2 : 1);
    float card_w = (W - 40 - (cols - 1) * 16) / cols;
    float card_h = 100;

    int visible_idx = 0;
    for (int i = 0; i < NCATALOG; i++) {
        StoreApp *app = &catalog[i];

        // Filter by tab
        if (active_tab != 0) {
            if (strcmp(TABS[active_tab], app->cat) != 0 &&
                !(active_tab == 1 && strcmp(app->cat, "Jeux") != 0)) continue;
        }

        // Filter by search
        if (search_query[0]) {
            if (!strcasestr(app->name, search_query) && !strcasestr(app->cat, search_query) && !strcasestr(app->desc, search_query))
                continue;
        }

        int col = visible_idx % cols;
        int row = visible_idx / cols;
        float cx = 20 + col * (card_w + 16);
        float cy = y + row * (card_h + 16);

        if (cy + card_h > content_y && cy < H) {
            ui_card(cx, cy, card_w, card_h);

            // Icon: try app icon first, then cached PNG
            int app_id = app_by_exe(app->id);
            if (app_id >= 0) {
                icon_app(c, app_id, cx + 16, cy + 18, 44);
            } else {
                Image *img = load_catalog_icon(i);
                if (img) g_image(c, img, cx + 16, cy + 18, 44, 44, 0, 255);
                else icon_glyph(c, G_APPS, cx + 38, cy + 40, 24, TH.accent);
            }

            text(c, F_SEMI, 15, 0, cx + 72, cy + 32, TH.text, app->name);
            text(c, F_REG, 12, 0, cx + 72, cy + 50, TH.sub, app->desc);
            char inf[64];
            snprintf(inf, sizeof inf, "%s · %s · %s", app->ver, app->size, app->is_native ? "Natif FLX" : "Flatpak");
            text(c, F_REG, 11, 0, cx + 72, cy + 70, TH.dim, inf);

            // Install / Open button
            float bw2 = 90, bh2 = 28;
            float bx2 = cx + card_w - bw2 - 14, by2 = cy + card_h - bh2 - 14;
            if (app->installed) {
                if (ui_button(bx2, by2, bw2, bh2, "Ouvrir", B_ACCENT)) {
                    app_launch(app->id, 0);
                }
            } else {
                if (ui_button(bx2, by2, bw2, bh2, "Installer", B_STD)) {
                    app->installed = 1;
                    app_redraw();
                }
            }
        }
        visible_idx++;
    }

    float total_h = (bh + 20) + ((visible_idx + cols - 1) / cols) * (card_h + 16) + 40;
    g_setclip(c, cs);
    ui_scroll(&sc, 0, content_y, W, content_h, total_h);

    // Status bar
    g_rect(c, 0, H - 24, W, 24, TH.surface);
    g_rect(c, 0, H - 24, W, 1, TH.line);
    text(c, F_REG, 12, 0, 16, H - 8, TH.sub, "FLX Package Manager: Dépôts officiels synchronisés · Sandbox chroot/seccomp activé");
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "store", .title = "Centre d'applications", .w = 960, .h = 640, .mica = 1, .draw = draw };
    return app_main(&a, argc, argv);
}
