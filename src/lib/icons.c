// Theme palette, app table, and every icon in Windows 12 — all drawn in code, all original.
#include "w12.h"

// ---- theme --------------------------------------------------------------------------------
const uint32_t ACCENTS[NACCENT] = { 0xFF1F6FEB, 0xFF7A4FD6, 0xFF038387, 0xFFD13469, 0xFFCA5010, 0xFF2E8B57 };
const char *ACCENT_NAMES[NACCENT] = { "Glacier", "Violet", "Lagoon", "Rose", "Ember", "Fern" };
const char *WALL_NAMES[NWALL] = { "Bloom", "Aurora", "Dune" };
Theme TH;

void theme_set(int dark, int accent, int wall) {
    Theme t = {0};
    t.dark = dark; t.accent_i = (accent % NACCENT + NACCENT) % NACCENT; t.wall = (wall % NWALL + NWALL) % NWALL;
    uint32_t a = ACCENTS[t.accent_i];
    if (dark) {
        t.accent = c_mix(a, 0xFFFFFFFF, 0.30f); t.accent_fg = 0xFF101010;
        t.bg = 0xFF202020; t.mica = 0xE0202024; t.surface = 0xFF272727; t.card = 0xFF2D2D2D; t.card2 = 0xFF343434;
        t.hover = 0x16FFFFFF; t.press = 0x0CFFFFFF; t.line = 0xFF3B3B3B; t.text = 0xFFFFFFFF; t.sub = 0xFFC8C8C8;
        t.dim = 0xFF8C8C8C; t.sel = c_alpha(t.accent, 0.40f); t.danger = 0xFFFF6B6B;
    } else {
        t.accent = a; t.accent_fg = 0xFFFFFFFF;
        t.bg = 0xFFF3F3F3; t.mica = 0xE6F4F4F6; t.surface = 0xFFF9F9F9; t.card = 0xFFFFFFFF; t.card2 = 0xFFF5F5F5;
        t.hover = 0x0F000000; t.press = 0x08000000; t.line = 0xFFE3E3E3; t.text = 0xFF1A1A1A; t.sub = 0xFF5C5C5C;
        t.dim = 0xFF8E8E8E; t.sel = c_alpha(a, 0.28f); t.danger = 0xFFC42B1C;
    }
    t.accent_soft = c_alpha(t.accent, 0.16f);
    TH = t;
}

// ---- app table --------------------------------------------------------------------------------
const AppInfo APPS[NAPPS] = {
    { "explorer", "File Explorer", "Browse your files" },
    { "settings", "Settings", "Personalize Windows 12" },
    { "terminal", "Terminal", "Command line" },
    { "notepad", "Notepad", "Write and edit text" },
    { "calc", "Calculator", "Standard calculator" },
    { "browser", "Browser", "Browse the web" },
    { "taskmgr", "Task Manager", "Processes and performance" },
    { "minesweeper", "Minesweeper", "Classic puzzle game" },
    { "proof", "Proof", "What runs on this PC" },
};
int app_by_exe(const char *exe) {
    for (int i = 0; i < NAPPS; i++) if (!strcmp(APPS[i].exe, exe)) return i;
    return -1;
}

// ---- the Windows 12 mark: one glass pane in perspective -------------------------------------------
void icon_logo(Canvas *c, float cx, float cy, float s, int alpha) {
    float x0 = cx - s / 2, y0 = cy - s / 2;
    float q[8] = { x0 + 0.16f * s, y0 + 0.20f * s, x0 + 0.86f * s, y0 + 0.04f * s,
                   x0 + 0.86f * s, y0 + 0.96f * s, x0 + 0.16f * s, y0 + 0.80f * s };
    float A = alpha / 255.0f;
    // soft glow behind
    g_poly(c, (float[]){ q[0] - s * .02f, q[1] + s * .06f, q[2], q[3] + s * .08f, q[4], q[5] + s * .04f, q[6] - s * .02f, q[7] + s * .06f }, 4,
           c_alpha(0xFF2B6CFF, 0.18f * A), c_alpha(0xFF6A3CFF, 0.10f * A), x0, y0, x0 + s, y0 + s);
    g_poly(c, q, 4, c_alpha(0xFF6FE7FF, A), c_alpha(0xFF1F3FD6, A), x0 + 0.1f * s, y0, x0 + 0.9f * s, y0 + s);
    // glossy diagonal sheen across the upper half of the pane
    float sh[8] = { q[0], q[1], q[2], q[3], q[2], q[3] + 0.30f * s, q[0], q[1] + 0.40f * s };
    g_poly(c, sh, 4, c_alpha(0xFFFFFFFF, 0.38f * A), c_alpha(0xFFFFFFFF, 0.04f * A), x0, y0, x0 + 0.3f * s, y0 + 0.6f * s);
    // thin bright leading edge on the near (right) side
    g_poly(c, (float[]){ q[2] - s * 0.035f, q[3] + s * 0.012f, q[2], q[3], q[4], q[5], q[4] - s * 0.035f, q[5] - s * 0.012f }, 4,
           c_alpha(0xFFE9FCFF, 0.9f * A), c_alpha(0xFF8FB8FF, 0.6f * A), 0, y0, 0, y0 + s);
}

void icon_copilot(Canvas *c, float cx, float cy, float s) {
    // a four-point spark made of two lens shapes, cyan to magenta
    float r = s * 0.48f, k = s * 0.13f;
    g_poly(c, (float[]){ cx, cy - r, cx + k, cy, cx, cy + r, cx - k, cy }, 4, 0xFF39D5FF, 0xFFB44CFF, cx, cy - r, cx, cy + r);
    g_poly(c, (float[]){ cx - r, cy, cx, cy - k, cx + r, cy, cx, cy + k }, 4, 0xFF39D5FF, 0xFFFF5FA8, cx - r, cy, cx + r, cy);
    g_circle(c, cx + r * 0.62f, cy - r * 0.62f, s * 0.08f, 0xFFFFC24B);
}

// ---- app icons ----------------------------------------------------------------------------------------
void icon_folder(Canvas *c, float x, float y, float s) {
    g_rrect(c, x + s * .06f, y + s * .16f, s * .40f, s * .2f, s * .06f, 0, 0xFFE0A21B);
    g_rrect(c, x + s * .06f, y + s * .24f, s * .88f, s * .62f, s * .07f, s * .07f, 0xFFE8A91F);
    g_vgrad(c, x + s * .06f, y + s * .33f, s * .88f, s * .55f, s * .07f, 0xFFFFD86A, 0xFFF6B828);
}
void icon_file(Canvas *c, const char *name, float x, float y, float s) {
    const char *ext = name ? strrchr(name, '.') : 0;
    uint32_t band = 0xFF2F7CF6;
    if (ext && (!strcmp(ext, ".png") || !strcmp(ext, ".jpg"))) band = 0xFF13A10E;
    else if (ext && (!strcmp(ext, ".c") || !strcmp(ext, ".h") || !strcmp(ext, ".sh"))) band = 0xFF8250DF;
    else if (ext && !strcmp(ext, ".md")) band = 0xFFE8590C;
    g_rrect(c, x + s * .18f, y + s * .06f, s * .64f, s * .88f, s * .07f, s * .07f, 0xFFB9BEC7);
    g_rrect(c, x + s * .19f, y + s * .07f, s * .62f, s * .86f, s * .06f, s * .06f, 0xFFFFFFFF);
    for (int i = 0; i < 4; i++) g_rect(c, x + s * .3f, y + s * (.36f + i * .12f), s * (i == 3 ? .26f : .40f), fmaxf(1, s * .04f), 0xFFC4C8D0);
    g_rrect(c, x + s * .19f, y + s * .07f, s * .62f, s * .16f, s * .06f, 0, band);
}

void icon_app(Canvas *c, int app, float x, float y, float s) {
    float cx = x + s / 2, cy = y + s / 2;
    switch (app) {
    case APP_EXPLORER: icon_folder(c, x, y, s); break;
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
