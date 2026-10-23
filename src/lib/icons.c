// Theme palette, app table, and every icon in FLX OS — all drawn in code, all original.
// By Florynx Labs. Built on Lumen OS 2 rasteriser.
#include "w12.h"
#include <strings.h>

// ---- theme (FLX OS dark palette: near-black surfaces, gold accent, teal highlights) -------
const uint32_t ACCENTS[NACCENT] = { 0xFFF5AD32, 0xFF35D6C0, 0xFF1F6FEB, 0xFF7A4FD6, 0xFFD13469, 0xFF2E8B57 };
const char *ACCENT_NAMES[NACCENT] = { "Gold", "Teal", "Glacier", "Violet", "Rose", "Fern" };
const char *WALL_NAMES[NWALL] = { "Aurora", "Eclipse", "Flow", "Glass", "Horizon", "Obsidian", "Alpine", "Coast", "Sahara", "Aurora", "Eclipse", "Flow", "Glass", "Horizon", "Obsidian" };
Theme TH;

void theme_set(int dark, int accent, int wall) {
    Theme t = {0};
    t.dark = dark; t.accent_i = (accent % NACCENT + NACCENT) % NACCENT; t.wall = (wall % NWALL + NWALL) % NWALL;
    uint32_t a = ACCENTS[t.accent_i];
    if (dark) {
        // FLX OS dark: #05090B base, graphite surfaces, gold+teal accents
        t.accent = a; t.accent_fg = 0xFF05090B;
        t.bg = 0xFF05090B; t.mica = 0xE00A1114; t.surface = 0xFF0A1114; t.card = 0xFF101A1D; t.card2 = 0xFF152025;
        t.hover = 0x18FFFFFF; t.press = 0x10FFFFFF; t.line = 0xFF203034; t.text = 0xFFF2F5F4; t.sub = 0xFF94A6AA;
        t.dim = 0xFF607176; t.sel = c_alpha(t.accent, 0.35f); t.danger = 0xFFFF6B6B;
    } else {
        // FLX OS light: clean whites, subtle gold accent
        t.accent = c_mix(a, 0xFF000000, 0.10f); t.accent_fg = 0xFFFFFFFF;
        t.bg = 0xFFF0F2F3; t.mica = 0xE6F4F5F7; t.surface = 0xFFF8FAFA; t.card = 0xFFFFFFFF; t.card2 = 0xFFF3F5F6;
        t.hover = 0x0F000000; t.press = 0x08000000; t.line = 0xFFDDE3E5; t.text = 0xFF0D1618; t.sub = 0xFF4A5C61;
        t.dim = 0xFF7A8E93; t.sel = c_alpha(a, 0.28f); t.danger = 0xFFC42B1C;
    }
    t.accent_soft = c_alpha(t.accent, 0.18f);
    TH = t;
}

// ---- app table (FLX OS) -----------------------------------------------------------------------
const AppInfo APPS[NAPPS] = {
    { "explorer", "Fichiers", "Gestionnaire de fichiers" },
    { "settings", "Param\u00e8tres", "Configuration de FLX OS" },
    { "terminal", "Terminal", "FLX Terminal" },
    { "notepad",  "\u00c9diteur de texte", "Code - OSS" },
    { "calc",     "Calculatrice", "Utilitaires" },
    { "browser",  "Navigateur Web", "Firefox" },
    { "taskmgr",  "Gestionnaire de t\u00e2ches", "Processus actifs" },
    { "minesweeper", "Minesweeper", "Jeu classique" },
    { "proof",    "Informations syst\u00e8me", "FLX System Info" },
    { "store",    "Centre d'applications", "FLX Store" },
};
int app_by_exe(const char *exe) {
    for (int i = 0; i < NAPPS; i++) if (!strcmp(APPS[i].exe, exe)) return i;
    return -1;
}

// ---- FLX OS logo mark: official golden fox mark -----------------------------------------------
static Image *cached_logo_mark = NULL;
static int logo_tried = 0;

void icon_logo(Canvas *c, float cx, float cy, float s, int alpha) {
    if (!logo_tried) {
        cached_logo_mark = image_load("/share/icons/flx_os_logo.png");
        if (!cached_logo_mark) cached_logo_mark = image_load("/share/icons/flx_logo_mark.png");
        logo_tried = 1;
    }
    if (cached_logo_mark) {
        g_image(c, cached_logo_mark, cx - s / 2, cy - s / 2, s, s, 0, alpha);
        return;
    }
    float A = alpha / 255.0f;
    g_circle(c, cx, cy, s * 0.65f, c_alpha(0x28F5AD32, A));
    float h = s * 0.50f, hw = s * 0.30f;
    float up[8] = { cx, cy - h, cx + hw, cy - h*0.08f, cx, cy, cx - hw, cy - h*0.08f };
    g_poly(c, up, 4, c_alpha(0xFFFFC24B, A), c_alpha(0xFFF5AD32, 0.7f*A), cx, cy-h, cx, cy);
    float dn[8] = { cx - hw, cy - h*0.08f, cx, cy, cx + hw, cy - h*0.08f, cx, cy + h*0.52f };
    g_poly(c, dn, 4, c_alpha(0xFFF5AD32, A), c_alpha(0xFFB87E1A, 0.9f*A), cx, cy, cx, cy+h*0.52f);
    float lw[6] = { cx - hw*0.55f, cy - h*0.55f, cx - hw*1.62f, cy - h*0.10f, cx - hw*0.5f, cy - h*0.05f };
    g_poly(c, lw, 3, c_alpha(0xFFFFD477, 0.85f*A), c_alpha(0xFFF5AD32, 0.35f*A), cx-hw*1.6f, cy, cx, cy-h);
    float rw[6] = { cx + hw*0.55f, cy - h*0.55f, cx + hw*1.62f, cy - h*0.10f, cx + hw*0.5f, cy - h*0.05f };
    g_poly(c, rw, 3, c_alpha(0xFFFFD477, 0.85f*A), c_alpha(0xFFF5AD32, 0.35f*A), cx, cy-h, cx+hw*1.6f, cy);
    g_line(c, cx, cy - h, cx, cy + h * 0.28f, fmaxf(1.0f, s * 0.03f), c_alpha(0xFFFFEEBB, 0.60f * A));
}

// ---- FLX User avatar --------------------------------------------------------------------------
static Image *cached_avatar = NULL;
static int avatar_tried = 0;

void icon_user_avatar(Canvas *c, float x, float y, float size, float r, int alpha) {
    if (!avatar_tried) {
        cached_avatar = image_load("/share/icons/user_avatar.png");
        avatar_tried = 1;
    }
    if (cached_avatar) {
        g_image_cover(c, cached_avatar, x, y, size, size, r, alpha);
    } else {
        g_circle(c, x + size / 2, y + size / 2, size / 2, TH.accent);
        text_c(c, F_SEMI, size * 0.5f, 0, x + size / 2, y + size * 0.6f, TH.accent_fg, "F");
    }
}



// ---- app icons ----------------------------------------------------------------------------------------
void icon_folder(Canvas *c, float x, float y, float s) {
    g_rrect(c, x + s * .06f, y + s * .16f, s * .40f, s * .2f, s * .06f, 0, 0xFFE0A21B);
    g_rrect(c, x + s * .06f, y + s * .24f, s * .88f, s * .62f, s * .07f, s * .07f, 0xFFE8A91F);
    g_vgrad(c, x + s * .06f, y + s * .33f, s * .88f, s * .55f, s * .07f, 0xFFFFD86A, 0xFFF6B828);
}
void icon_file(Canvas *c, const char *name, float x, float y, float s) {
    const char *ext = name ? strrchr(name, '.') : 0;
    int image = ext && (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".gif") || !strcasecmp(ext, ".webp") || !strcasecmp(ext, ".svg"));
    int audio = ext && (!strcasecmp(ext, ".mp3") || !strcasecmp(ext, ".wav") || !strcasecmp(ext, ".ogg") || !strcasecmp(ext, ".flac"));
    int video = ext && (!strcasecmp(ext, ".mp4") || !strcasecmp(ext, ".mkv") || !strcasecmp(ext, ".webm") || !strcasecmp(ext, ".mov"));
    int pdf = ext && !strcasecmp(ext, ".pdf");
    int source = ext && (!strcasecmp(ext, ".c") || !strcasecmp(ext, ".h") || !strcasecmp(ext, ".rs") || !strcasecmp(ext, ".py") || !strcasecmp(ext, ".js") || !strcasecmp(ext, ".ts") || !strcasecmp(ext, ".sh"));
    int archive = ext && (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".tar") || !strcasecmp(ext, ".gz") || !strcasecmp(ext, ".xz") || !strcasecmp(ext, ".7z"));
    uint32_t band = 0xFF8796A5;
    if (image) band = 0xFF16B9C8;
    else if (source) band = 0xFF6E73E8;
    else if (ext && (!strcasecmp(ext, ".md") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".log") || !strcasecmp(ext, ".csv"))) band = 0xFF3E9BE8;
    else if (pdf) band = 0xFFE94B52;
    else if (audio) band = 0xFFAA55D9;
    else if (video) band = 0xFF3988F6;
    else if (archive) band = 0xFFF39A35;
    else if (ext && (!strcasecmp(ext, ".toml") || !strcasecmp(ext, ".json") || !strcasecmp(ext, ".yaml") || !strcasecmp(ext, ".yml") || !strcasecmp(ext, ".ini") || !strcasecmp(ext, ".conf"))) band = 0xFF28A780;
    else if (ext && (!strcasecmp(ext, ".bin") || !strcasecmp(ext, ".elf") || !strcasecmp(ext, ".app"))) band = 0xFFF5AD32;
    g_rrect(c, x + s * .18f, y + s * .06f, s * .64f, s * .88f, s * .07f, s * .07f, 0xFFB9BEC7);
    g_rrect(c, x + s * .19f, y + s * .07f, s * .62f, s * .86f, s * .06f, s * .06f, 0xFFFFFFFF);
    if (image) {
        g_rrect(c, x+s*.25f,y+s*.34f,s*.50f,s*.40f,s*.04f,s*.04f,0xFF145464);
        g_circle(c,x+s*.62f,y+s*.44f,s*.055f,0xFFFFD36C);
        float m[12]={x+s*.28f,y+s*.68f,x+s*.43f,y+s*.51f,x+s*.53f,y+s*.62f,x+s*.63f,y+s*.48f,x+s*.73f,y+s*.68f,x+s*.73f,y+s*.72f};
        g_poly(c,m,6,0xFF51CFBC,0xFF1789A0,x+s*.28f,y+s*.48f,x+s*.73f,y+s*.72f);
    } else if (audio) {
        g_line(c,x+s*.52f,y+s*.36f,x+s*.52f,y+s*.68f,s*.07f,band);
        g_line(c,x+s*.52f,y+s*.38f,x+s*.72f,y+s*.33f,s*.07f,band);
        g_line(c,x+s*.72f,y+s*.33f,x+s*.72f,y+s*.59f,s*.07f,band);
        g_circle(c,x+s*.43f,y+s*.70f,s*.09f,band); g_circle(c,x+s*.63f,y+s*.61f,s*.09f,band);
    } else if (video) {
        g_rrect(c,x+s*.25f,y+s*.37f,s*.50f,s*.34f,s*.05f,s*.05f,0xFF1D62B9);
        float p[6]={x+s*.43f,y+s*.41f,x+s*.43f,y+s*.67f,x+s*.64f,y+s*.54f};
        g_poly(c,p,3,0xFFFFFFFF,0xFFFFFFFF,x+s*.43f,y+s*.41f,x+s*.64f,y+s*.67f);
    } else if (pdf) {
        text_c(c,F_SEMI,s*.22f,0,x+s*.50f,y+s*.69f,band,"PDF");
    } else if (archive) {
        g_rect(c,x+s*.32f,y+s*.36f,s*.36f,s*.37f,0xFFE6A849);
        for(int i=0;i<3;i++) g_rect(c,x+s*.47f,y+s*(.40f+i*.09f),s*.08f,s*.045f,0xFF70451A);
    } else if (source) {
        text_c(c,F_SEMI,s*.30f,0,x+s*.50f,y+s*.68f,band,"<>");
    } else {
        for (int i = 0; i < 3; i++) g_rect(c, x + s * .3f, y + s * (.46f + i * .10f), s * .40f, fmaxf(1, s * .04f), 0xFFC4C8D0);
    }
    g_rrect(c, x + s * .19f, y + s * .07f, s * .62f, s * .16f, s * .06f, 0, band);
}

static Image *app_pngs[NAPPS] = {0};
static int app_pngs_tried[NAPPS] = {0};

void icon_app(Canvas *c, int app, float x, float y, float s) {
    if (app < 0 || app >= NAPPS) return;
    if (!app_pngs_tried[app]) {
        char path[64];
        snprintf(path, sizeof path, "/share/icons/%s.png", APPS[app].exe);
        app_pngs[app] = image_load(path);
        app_pngs_tried[app] = 1;
    }
    if (app_pngs[app]) {
        g_image(c, app_pngs[app], x, y, s, s, 0, 255);
        return;
    }
    float cx = x + s / 2, cy = y + s / 2;
    switch (app) {
    case APP_EXPLORER: icon_folder(c, x, y, s); break;
    case APP_STORE: {
        g_vgrad(c, x + s * .10f, y + s * .12f, s * .80f, s * .76f, s * .14f, 0xFFE5A823, 0xFFBA7610);
        g_rrect_line(c, x + s * .10f, y + s * .12f, s * .80f, s * .76f, s * .14f, 1.5f, 0xFFFFE28A);
        icon_logo(c, cx, cy + s * .04f, s * .42f, 255);
        break;
    }
    case APP_SETTINGS: {
        for (int i = 0; i < 8; i++) {
            float a = i * (float)M_PI / 4;
            g_line(c, cx + cosf(a) * s * .18f, cy + sinf(a) * s * .18f, cx + cosf(a) * s * .40f, cy + sinf(a) * s * .40f, s * .16f, 0xFF6B7380);
        }
        g_circle(c, cx, cy, s * .33f, 0xFF7E8794);
        g_circle(c, cx, cy, s * .30f, 0xFF98A1AE);
        g_circle(c, cx, cy, s * .13f, 0xFF3D434C);
        break;
    }
    case APP_TERMINAL:
        g_vgrad(c, x + s * .06f, y + s * .12f, s * .88f, s * .76f, s * .12f, 0xFF3A3F4B, 0xFF1C1F26);
        g_rrect(c, x + s * .06f, y + s * .12f, s * .88f, s * .12f, s * .12f, 0, 0xFF4B5262);
        g_line(c, cx - s * .24f, cy - s * .06f, cx - s * .10f, cy + s * .06f, s * .07f, 0xFFFFFFFF);
        g_line(c, cx - s * .10f, cy + s * .06f, cx - s * .24f, cy + s * .18f, s * .07f, 0xFFFFFFFF);
        g_rrect(c, cx - s * .02f, cy + s * .15f, s * .24f, s * .065f, s * .03f, s * .03f, 0xFF5CE1E6);
        break;
    case APP_NOTEPAD:
        g_rrect(c, x + s * .16f, y + s * .08f, s * .68f, s * .84f, s * .1f, s * .1f, 0xFFD5DAE2);
        g_rrect(c, x + s * .17f, y + s * .09f, s * .66f, s * .82f, s * .09f, s * .09f, 0xFFFFFFFF);
        g_rrect(c, x + s * .17f, y + s * .09f, s * .66f, s * .18f, s * .09f, 0, 0xFF3D8BFD);
        for (int i = 0; i < 4; i++) g_rect(c, x + s * .27f, y + s * (.38f + i * .13f), s * (i == 3 ? .3f : .46f), fmaxf(1.5f, s * .045f), 0xFFB7C0CC);
        g_line(c, x + s * .58f, y + s * .78f, x + s * .80f, y + s * .52f, s * .07f, 0xFFFFB02E);
        break;
    case APP_CALC:
        g_vgrad(c, x + s * .14f, y + s * .06f, s * .72f, s * .88f, s * .12f, 0xFF4D5562, 0xFF30353E);
        g_rrect(c, x + s * .22f, y + s * .14f, s * .56f, s * .2f, s * .05f, s * .05f, 0xFFB9F3E4);
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++)
                g_rrect(c, x + s * (.22f + k * .2f), y + s * (.42f + r * .17f), s * .15f, s * .12f, s * .03f, s * .03f,
                        r == 2 && k == 2 ? 0xFFFF8A3D : 0xFF8B95A3);
        break;
    case APP_BROWSER: {
        g_circle(c, cx, cy, s * .42f, 0xFF1A73E8);
        g_vgrad(c, cx - s * .42f, cy - s * .42f, s * .84f, s * .84f, s * .42f, 0xFF35C8FF, 0xFF1D4FD8);
        g_arc(c, cx, cy + s * .55f, s * .62f, -2.25f, -0.89f, s * .07f, 0xCCFFFFFF);
        g_arc(c, cx, cy + s * .80f, s * .62f, -2.05f, -1.09f, s * .06f, 0x99FFFFFF);
        g_circle(c, cx + s * .16f, cy - s * .16f, s * .08f, 0xEEFFFFFF);
        break;
    }
    case APP_TASKMGR:
        g_vgrad(c, x + s * .08f, y + s * .12f, s * .84f, s * .76f, s * .12f, 0xFFFFFFFF, 0xFFE8EEF5);
        g_rrect_line(c, x + s * .08f, y + s * .12f, s * .84f, s * .76f, s * .12f, fmaxf(1, s * .03f), 0xFFC7D0DC);
        {
            float pts[] = { .16f, .70f, .32f, .52f, .44f, .62f, .58f, .32f, .70f, .46f, .84f, .26f };
            for (int i = 0; i < 5; i++) g_line(c, x + s * pts[2 * i], y + s * pts[2 * i + 1], x + s * pts[2 * i + 2], y + s * pts[2 * i + 3], s * .07f, 0xFF0FA968);
        }
        break;
    case APP_MINES:
        g_rrect(c, x + s * .06f, y + s * .06f, s * .88f, s * .88f, s * .14f, s * .14f, 0xFFB8C4D3);
        g_rrect(c, x + s * .10f, y + s * .10f, s * .38f, s * .38f, s * .07f, s * .07f, 0xFFDDE5EF);
        g_rrect(c, x + s * .52f, y + s * .52f, s * .38f, s * .38f, s * .07f, s * .07f, 0xFFDDE5EF);
        for (int i = 0; i < 4; i++) {
            float a = i * (float)M_PI / 4;
            g_line(c, cx - cosf(a) * s * .3f, cy - sinf(a) * s * .3f, cx + cosf(a) * s * .3f, cy + sinf(a) * s * .3f, s * .06f, 0xFF20242B);
        }
        g_circle(c, cx, cy, s * .21f, 0xFF20242B);
        g_circle(c, cx - s * .07f, cy - s * .07f, s * .06f, 0xFFFFFFFF);
        break;
    case APP_PROOF: {
        float sh[] = { cx, y + s * .06f, x + s * .86f, y + s * .2f, x + s * .80f, y + s * .62f, cx, y + s * .94f, x + s * .20f, y + s * .62f, x + s * .14f, y + s * .2f };
        g_poly(c, sh, 6, 0xFF4ADE80, 0xFF0F8A4C, cx, y, cx, y + s);
        g_line(c, cx - s * .18f, cy, cx - s * .04f, cy + s * .14f, s * .08f, 0xFFFFFFFF);
        g_line(c, cx - s * .04f, cy + s * .14f, cx + s * .20f, cy - s * .14f, s * .08f, 0xFFFFFFFF);
        break;
    }
    }
}

// ---- monoline glyphs ------------------------------------------------------------------------------------
void icon_glyph(Canvas *c, int g, float cx, float cy, float s, uint32_t col) {
    float w = fmaxf(1.4f, s * 0.085f), h = s / 2;
    switch (g) {
    case G_SEARCH: g_ring(c, cx - s * .08f, cy - s * .08f, s * .3f, w, col); g_line(c, cx + s * .14f, cy + s * .14f, cx + s * .4f, cy + s * .4f, w * 1.2f, col); break;
    case G_WIFI: case G_WIFI_OFF: {
        uint32_t cc = g == G_WIFI_OFF ? c_alpha(col, 0.35f) : col;
        for (int i = 0; i < 3; i++) g_arc(c, cx, cy + s * .32f, s * (.18f + i * .17f), -2.36f, -0.785f, w, cc);
        g_circle(c, cx, cy + s * .3f, w * .9f, cc);
        if (g == G_WIFI_OFF) g_line(c, cx - s * .4f, cy - s * .38f, cx + s * .4f, cy + s * .42f, w, col);
        break;
    }
    case G_NET:
        g_rrect_line(c, cx - s * .42f, cy - s * .32f, s * .84f, s * .52f, s * .06f, w, col);
        g_line(c, cx, cy + s * .2f, cx, cy + s * .36f, w, col); g_line(c, cx - s * .2f, cy + s * .38f, cx + s * .2f, cy + s * .38f, w, col);
        break;
    case G_VOLUME:
        g_poly(c, (float[]){ cx - s * .42f, cy - s * .14f, cx - s * .24f, cy - s * .14f, cx - s * .02f, cy - s * .36f, cx - s * .02f, cy + s * .36f, cx - s * .24f, cy + s * .14f, cx - s * .42f, cy + s * .14f }, 6, col, col, 0, 0, 1, 1);
        g_arc(c, cx - s * .02f, cy, s * .2f, -0.9f, 0.9f, w, col); g_arc(c, cx - s * .02f, cy, s * .36f, -0.9f, 0.9f, w, col);
        break;
    case G_BATTERY:
        g_rrect_line(c, cx - s * .44f, cy - s * .2f, s * .8f, s * .4f, s * .08f, w, col);
        g_rrect(c, cx + s * .38f, cy - s * .09f, s * .07f, s * .18f, s * .03f, s * .03f, col);
        g_rrect(c, cx - s * .36f, cy - s * .12f, s * .52f, s * .24f, s * .03f, s * .03f, col);
        break;
    case G_POWER: g_arc(c, cx, cy + s * .04f, s * .34f, -1.0f, 4.14f, w, col); g_line(c, cx, cy - s * .42f, cx, cy - s * .02f, w, col); break;
    case G_CLOSE: g_line(c, cx - s * .3f, cy - s * .3f, cx + s * .3f, cy + s * .3f, w, col); g_line(c, cx + s * .3f, cy - s * .3f, cx - s * .3f, cy + s * .3f, w, col); break;
    case G_MIN: g_line(c, cx - s * .32f, cy, cx + s * .32f, cy, w * .9f, col); break;
    case G_MAX: g_rrect_line(c, cx - s * .3f, cy - s * .3f, s * .6f, s * .6f, s * .08f, w * .9f, col); break;
    case G_RESTORE:
        g_rrect_line(c, cx - s * .32f, cy - s * .18f, s * .5f, s * .5f, s * .07f, w * .9f, col);
        g_line(c, cx - s * .14f, cy - s * .32f, cx + s * .22f, cy - s * .32f, w * .9f, col);
        g_line(c, cx + s * .32f, cy - s * .22f, cx + s * .32f, cy + s * .14f, w * .9f, col);
        break;
    case G_CHEV_R: g_line(c, cx - s * .12f, cy - s * .28f, cx + s * .16f, cy, w, col); g_line(c, cx + s * .16f, cy, cx - s * .12f, cy + s * .28f, w, col); break;
    case G_CHEV_L: g_line(c, cx + s * .12f, cy - s * .28f, cx - s * .16f, cy, w, col); g_line(c, cx - s * .16f, cy, cx + s * .12f, cy + s * .28f, w, col); break;
    case G_CHEV_D: g_line(c, cx - s * .28f, cy - s * .12f, cx, cy + s * .16f, w, col); g_line(c, cx, cy + s * .16f, cx + s * .28f, cy - s * .12f, w, col); break;
    case G_CHEV_U: g_line(c, cx - s * .28f, cy + s * .12f, cx, cy - s * .16f, w, col); g_line(c, cx, cy - s * .16f, cx + s * .28f, cy + s * .12f, w, col); break;
    case G_BACK: case G_FWD: {
        float d = g == G_BACK ? -1 : 1;
        g_line(c, cx - d * s * .34f, cy, cx + d * s * .34f, cy, w, col);
        g_line(c, cx + d * s * .34f, cy, cx + d * s * .06f, cy - s * .28f, w, col);
        g_line(c, cx + d * s * .34f, cy, cx + d * s * .06f, cy + s * .28f, w, col);
        break;
    }
    case G_UP:
        g_line(c, cx, cy + s * .34f, cx, cy - s * .34f, w, col);
        g_line(c, cx, cy - s * .34f, cx - s * .28f, cy - s * .06f, w, col); g_line(c, cx, cy - s * .34f, cx + s * .28f, cy - s * .06f, w, col);
        break;
    case G_REFRESH: case G_RESTART:
        g_arc(c, cx, cy, s * .32f, -1.2f, 4.1f, w, col);
        g_poly(c, (float[]){ cx + s * .06f, cy - s * .48f, cx + s * .30f, cy - s * .32f, cx + s * .06f, cy - s * .16f }, 3, col, col, 0, 0, 1, 1);
        break;
    case G_PLUS: g_line(c, cx - s * .34f, cy, cx + s * .34f, cy, w, col); g_line(c, cx, cy - s * .34f, cx, cy + s * .34f, w, col); break;
    case G_TRASH:
        g_line(c, cx - s * .36f, cy - s * .26f, cx + s * .36f, cy - s * .26f, w, col);
        g_line(c, cx - s * .1f, cy - s * .38f, cx + s * .1f, cy - s * .38f, w, col);
        g_rrect_line(c, cx - s * .26f, cy - s * .2f, s * .52f, s * .6f, s * .06f, w, col);
        break;
    case G_EDIT:
        g_line(c, cx - s * .3f, cy + s * .3f, cx + s * .26f, cy - s * .26f, w * 1.6f, col);
        g_line(c, cx - s * .38f, cy + s * .38f, cx - s * .3f, cy + s * .3f, w, col);
        break;
    case G_FOLDER:
        g_rrect_line(c, cx - s * .42f, cy - s * .28f, s * .84f, s * .6f, s * .07f, w, col);
        g_line(c, cx - s * .42f, cy - s * .12f, cx + s * .42f, cy - s * .12f, w, col);
        break;
    case G_FILE:
        g_rrect_line(c, cx - s * .3f, cy - s * .4f, s * .6f, s * .8f, s * .07f, w, col);
        g_line(c, cx - s * .16f, cy - s * .06f, cx + s * .16f, cy - s * .06f, w, col); g_line(c, cx - s * .16f, cy + s * .12f, cx + s * .16f, cy + s * .12f, w, col);
        break;
    case G_GEAR:
        for (int i = 0; i < 8; i++) { float a = i * (float)M_PI / 4; g_line(c, cx + cosf(a) * s * .26f, cy + sinf(a) * s * .26f, cx + cosf(a) * s * .42f, cy + sinf(a) * s * .42f, w * 1.6f, col); }
        g_ring(c, cx, cy, s * .27f, s * .12f, col); break;
    case G_USER: g_ring(c, cx, cy - s * .14f, s * .18f, w, col); g_arc(c, cx, cy + s * .44f, s * .34f, -2.9f, -0.24f, w, col); break;
    case G_WIDGETS:
        g_rrect(c, cx - s * .4f, cy - s * .4f, s * .36f, s * .48f, s * .08f, s * .08f, col);
        g_rrect_line(c, cx + s * .04f, cy - s * .4f, s * .36f, s * .3f, s * .08f, w, col);
        g_rrect_line(c, cx - s * .4f, cy + s * .14f, s * .36f, s * .26f, s * .08f, w, col);
        g_rrect(c, cx + s * .04f, cy - s * .04f, s * .36f, s * .44f, s * .08f, s * .08f, c_alpha(col, 0.6f));
        break;
    case G_HOME:
        g_line(c, cx - s * .38f, cy - s * .02f, cx, cy - s * .38f, w, col); g_line(c, cx, cy - s * .38f, cx + s * .38f, cy - s * .02f, w, col);
        g_rrect_line(c, cx - s * .28f, cy - s * .1f, s * .56f, s * .48f, s * .05f, w, col);
        break;
    case G_LIST: for (int i = -1; i <= 1; i++) { g_circle(c, cx - s * .32f, cy + i * s * .26f, w * .8f, col); g_line(c, cx - s * .16f, cy + i * s * .26f, cx + s * .38f, cy + i * s * .26f, w, col); } break;
    case G_GRID: for (int i = 0; i < 4; i++) g_rrect_line(c, cx + (i % 2 ? s * .04f : -s * .38f), cy + (i / 2 ? s * .04f : -s * .38f), s * .34f, s * .34f, s * .06f, w, col); break;
    case G_SUN:
        g_circle(c, cx, cy, s * .2f, col);
        for (int i = 0; i < 8; i++) { float a = i * (float)M_PI / 4; g_line(c, cx + cosf(a) * s * .3f, cy + sinf(a) * s * .3f, cx + cosf(a) * s * .42f, cy + sinf(a) * s * .42f, w, col); }
        break;
    case G_MOON: g_arc(c, cx - s * .04f, cy, s * .2f, -1.75f, 1.75f, s * .22f, col); break;
    case G_CLOUD: case G_RAIN:
        g_circle(c, cx - s * .16f, cy + s * .02f, s * .2f, col); g_circle(c, cx + s * .08f, cy - s * .08f, s * .26f, col);
        g_rrect(c, cx - s * .38f, cy + s * .02f, s * .76f, s * .22f, s * .11f, s * .11f, col);
        if (g == G_RAIN) for (int i = -1; i <= 1; i++) g_line(c, cx + i * s * .2f, cy + s * .32f, cx + i * s * .2f - s * .06f, cy + s * .46f, w, col);
        break;
    case G_STAR: {
        float p[20];
        for (int i = 0; i < 10; i++) { float a = -(float)M_PI / 2 + i * (float)M_PI / 5, r = i % 2 ? s * .18f : s * .44f; p[2 * i] = cx + cosf(a) * r; p[2 * i + 1] = cy + sinf(a) * r; }
        for (int i = 0; i < 10; i += 2) g_poly(c, (float[]){ cx, cy, p[2 * i], p[2 * i + 1], p[(2 * i + 2) % 20], p[(2 * i + 3) % 20] }, 3, col, col, 0, 0, 1, 1);
        for (int i = 1; i < 10; i += 2) g_poly(c, (float[]){ cx, cy, p[2 * i], p[2 * i + 1], p[(2 * i + 2) % 20], p[(2 * i + 3) % 20] }, 3, col, col, 0, 0, 1, 1);
        break;
    }
    case G_SEND: g_poly(c, (float[]){ cx - s * .38f, cy - s * .36f, cx + s * .42f, cy, cx - s * .38f, cy + s * .36f, cx - s * .24f, cy }, 4, col, col, 0, 0, 1, 1); break;
    case G_FLAG:
        g_line(c, cx - s * .22f, cy - s * .38f, cx - s * .22f, cy + s * .4f, w, col);
        g_poly(c, (float[]){ cx - s * .22f, cy - s * .38f, cx + s * .34f, cy - s * .2f, cx - s * .22f, cy - s * .02f }, 3, col, col, 0, 0, 1, 1);
        break;
    case G_CLOCK: g_ring(c, cx, cy, s * .4f, w, col); g_line(c, cx, cy, cx, cy - s * .24f, w, col); g_line(c, cx, cy, cx + s * .18f, cy + s * .1f, w, col); break;
    case G_LOCK: g_rrect(c, cx - s * .3f, cy - s * .06f, s * .6f, s * .46f, s * .08f, s * .08f, col); g_arc(c, cx, cy - s * .08f, s * .2f, -(float)M_PI, 0, w, col);
        g_line(c, cx - s * .2f, cy - s * .08f, cx - s * .2f, cy - s * .02f, w, col); g_line(c, cx + s * .2f, cy - s * .08f, cx + s * .2f, cy - s * .02f, w, col); break;
    case G_INFO: g_ring(c, cx, cy, s * .4f, w, col); g_line(c, cx, cy - s * .04f, cx, cy + s * .22f, w * 1.1f, col); g_circle(c, cx, cy - s * .2f, w * .8f, col); break;
    case G_PALETTE:
        g_ring(c, cx, cy, s * .4f, w, col);
        g_circle(c, cx - s * .16f, cy - s * .12f, s * .07f, col); g_circle(c, cx + s * .12f, cy - s * .18f, s * .07f, col); g_circle(c, cx + s * .2f, cy + s * .08f, s * .07f, col);
        break;
    case G_APPS: for (int i = 0; i < 4; i++) g_rrect(c, cx + (i % 2 ? s * .06f : -s * .38f), cy + (i / 2 ? s * .06f : -s * .38f), s * .32f, s * .32f, s * .07f, s * .07f, col); break;
    case G_DISPLAY: g_rrect_line(c, cx - s * .42f, cy - s * .32f, s * .84f, s * .54f, s * .06f, w, col); g_line(c, cx - s * .18f, cy + s * .38f, cx + s * .18f, cy + s * .38f, w, col); break;
    case G_SHIELD:
        g_poly(c, (float[]){ cx, cy - s * .42f, cx + s * .36f, cy - s * .28f, cx + s * .3f, cy + s * .16f, cx, cy + s * .44f, cx - s * .3f, cy + s * .16f, cx - s * .36f, cy - s * .28f }, 6, col, col, 0, 0, 1, 1);
        break;
    case G_CHECK: g_line(c, cx - s * .32f, cy, cx - s * .08f, cy + s * .24f, w * 1.2f, col); g_line(c, cx - s * .08f, cy + s * .24f, cx + s * .34f, cy - s * .24f, w * 1.2f, col); break;
    case G_DOTS: for (int i = -1; i <= 1; i++) g_circle(c, cx + i * s * .28f, cy, w * .9f, col); break;
    case G_COPY: g_rrect_line(c, cx - s * .3f, cy - s * .2f, s * .44f, s * .56f, s * .06f, w, col); g_rrect_line(c, cx - s * .12f, cy - s * .38f, s * .44f, s * .56f, s * .06f, w, col); break;
    case G_PASTE: g_rrect_line(c, cx - s * .3f, cy - s * .32f, s * .6f, s * .72f, s * .06f, w, col); g_rrect(c, cx - s * .14f, cy - s * .42f, s * .28f, s * .16f, s * .04f, s * .04f, col); break;
    case G_SAVE:
        g_rrect_line(c, cx - s * .38f, cy - s * .38f, s * .76f, s * .76f, s * .08f, w, col);
        g_rect(c, cx - s * .2f, cy - s * .38f, s * .36f, s * .22f, col); g_rrect_line(c, cx - s * .22f, cy + s * .06f, s * .44f, s * .3f, s * .03f, w, col);
        break;
    }
    (void)h;
}
