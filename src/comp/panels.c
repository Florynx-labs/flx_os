// FLX OS Widgets board and system panels.
#include "comp.h"
#include <unistd.h>

#define PY 48
#define PH (SCR_H - 12 - 56 - 12 - PY)
#define WX0 12
#define WW 420

enum { HW_PANEL = 300, HW_NOTES, HW_REFRESH };

// ---- widgets ------------------------------------------------------------------------------------
static char notes[2048];
static int notes_focus;
static float cpu_hist[60]; static int nh;
static Image *cached_art_landscape = NULL;

void panels_init(void) {
    char *n = read_file("/home/.config/notes.txt", 0);
    snprintf(notes, sizeof notes, "%s", n ? n : "Bienvenue sur FLX OS\nUne vision sobre et puissante.\n");
    free(n);
}

static void card(Canvas *c, float x, float y, float w, float h, float a) {
    g_rrect(c, x, y, w, h, 12, 12, c_alpha(TH.dark ? 0xDD0D161A : 0xFFFFFFFF, a));
    g_rrect_line(c, x, y, w, h, 12, 1.2f, c_alpha(TH.dark ? 0x28FFFFFF : 0x20000000, a));
}

void widgets_draw(Canvas *c, float p) {
    p = ease_out(p);
    if (p <= 0.01f) return;
    float x = WX0 - (1 - p) * (WW + 24), y = PY;
    g_shadow(c, x, y, WW, PH, 14, 32, (int)(100 * p), 8);
    g_glass(c, x, y, WW, PH, 14, TH.dark ? 0xDC081115 : 0xB8F3F4F7);
    g_rrect_line(c, x, y, WW, PH, 14, 1.2f, TH.dark ? 0x28FFFFFF : 0x40FFFFFF);
    hit_add(HW_PANEL, x, y, WW, PH);

    uint32_t fg = TH.text, sub = TH.sub;
    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    char b[128];

    // Minimalist Header with FLX Logo
    icon_logo(c, x + 34, y + 36, 24, 255);
    text(c, F_SEMI, 16, 0.5f, x + 54, y + 42, fg, "Tableau de bord");

    static const char *JOURS[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
    static const char *MOIS[] = { "Janv.", "F\u00e9vr.", "Mars", "Avr.", "Mai", "Juin", "Juil.", "Ao\u00fbt", "Sept.", "Oct.", "Nov.", "D\u00e9c." };
    snprintf(b, sizeof b, "%s %d %s %d", JOURS[tm.tm_wday], tm.tm_mday, MOIS[tm.tm_mon], tm.tm_year + 1900);
    text_r(c, F_REG, 13, 0, x + WW - 20, y + 42, sub, b);

    float cy = y + 68, cx = x + 16, fw = WW - 32;

    // Card 1: Météo
    card(c, cx, cy, fw, 108, 1);
    if (weather.ok) {
        icon_glyph(c, weather_glyph(weather.code), cx + 46, cy + 54, 44, 0xFFF5AD32);
        snprintf(b, sizeof b, "%.0f°", weather.temp);
        text(c, F_LIGHT, 44, 0, cx + 86, cy + 68, fg, b);
        text(c, F_SEMI, 15, 0, cx + 180, cy + 48, fg, weather.cond);
        snprintf(b, sizeof b, "Max %.0f° · Min %.0f° · Vent %.0f km/h", weather.tmax, weather.tmin, weather.wind);
        text(c, F_REG, 12, 0, cx + 180, cy + 70, sub, b);
        text(c, F_REG, 12, 0, cx + 180, cy + 90, TH.accent, weather.place);
    } else {
        icon_glyph(c, G_CLOUD, cx + 46, cy + 54, 40, TH.accent);
        text(c, F_SEMI, 15, 0, cx + 86, cy + 48, fg, "M\u00e9t\u00e9o FLX");
        text(c, F_REG, 12, 0, cx + 86, cy + 72, sub, "Actualisation en cours...");
    }

    // Card 2: Performances Système
    cy += 120;
    card(c, cx, cy, fw, 124, 1);
    text(c, F_SEMI, 14, 0, cx + 18, cy + 26, fg, "Performances syst\u00e8me");

    snprintf(b, sizeof b, "Processeur  %.0f%%", cpu_pct);
    text(c, F_REG, 12, 0, cx + 18, cy + 52, sub, b);
    g_rrect(c, cx + 18, cy + 58, fw - 36, 5, 2.5f, 2.5f, 0x30FFFFFF);
    g_rrect(c, cx + 18, cy + 58, (fw - 36) * clampf(cpu_pct / 100, 0.02f, 1), 5, 2.5f, 2.5f, TH.accent);

    char used[32], tot[32];
    human_size(mem_used_kb * 1024.0, used, sizeof used); human_size(mem_total_kb * 1024.0, tot, sizeof tot);
    snprintf(b, sizeof b, "M\u00e9moire vive  %s / %s", used, tot);
    text(c, F_REG, 12, 0, cx + 18, cy + 82, sub, b);
    g_rrect(c, cx + 18, cy + 88, fw - 36, 5, 2.5f, 2.5f, 0x30FFFFFF);
    g_rrect(c, cx + 18, cy + 88, (fw - 36) * clampf(mem_pct / 100, 0.02f, 1), 5, 2.5f, 2.5f, 0xFF2FD5C6);

    double up = 0; char *u = read_file("/proc/uptime", 0); if (u) { up = atof(u); free(u); }
    snprintf(b, sizeof b, "En fonction depuis %dh %02dm", (int)(up / 3600), (int)fmod(up / 60, 60));
    text(c, F_REG, 11, 0, cx + 18, cy + 110, TH.dim, b);

    // Card 3: Philosophie FLX OS
    cy += 136;
    card(c, cx, cy, fw, 78, 1);
    text(c, F_REG, 13, 0, cx + 18, cy + 32, 0xFFCBD5E1, "\u00ab Les grandes choses naissent d'une vision claire. \u00bb");
    text(c, F_SEMI, 12, 0, cx + 18, cy + 56, TH.accent, "\u2014 Philosophie FLX OS");

    // Card 4: Notes Rapides
    cy += 90;
    card(c, cx, cy, fw, 120, 1);
    hit_add(HW_NOTES, cx, cy, fw, 120);
    text(c, F_SEMI, 14, 0, cx + 18, cy + 24, fg, "Notes rapides");
    text_r(c, F_REG, 11, 0, cx + fw - 18, cy + 24, TH.dim, notes_focus ? "Enregistr\u00e9 auto." : "Cliquer pour modifier");
    if (notes_focus) g_rrect_line(c, cx + 1, cy + 1, fw - 2, 118, 11, 1.5f, TH.accent);
    int st[12], ln[12];
    int nl = text_wrap(F_REG, 13, notes, fw - 36, st, ln, 12);
    int show0 = maxi(0, nl - 3);
    for (int i = show0; i < nl; i++) text_n(c, F_REG, 13, 0, cx + 18, cy + 48 + (i - show0) * 19, fg, notes + st[i], ln[i]);
    if (notes_focus && nl) {
        float lx = cx + 18 + text_wn(F_REG, 13, 0, notes + st[nl - 1], ln[nl - 1]);
        g_rect(c, lx + 1, cy + 36 + (nl - 1 - show0) * 19, 1.5f, 16, TH.text);
    }

    // Card 5: Visuel FLX Horizon
    cy += 132;
    float ph = fminf(140, y + PH - 16 - cy);
    if (ph > 50) {
        if (!cached_art_landscape) {
            cached_art_landscape = image_load("/share/icons/card_landscape.png");
        }
        if (cached_art_landscape) {
            g_image(c, cached_art_landscape, cx, cy, fw, ph, 10, 255);
        } else {
            g_vgrad(c, cx, cy, fw, ph, 10, 0xFF352018, 0xFF0B141C);
        }
        g_rrect_line(c, cx, cy, fw, ph, 10, 1.0f, 0x30FFFFFF);
        text(c, F_SEMI, 12, 0, cx + 16, cy + ph - 12, 0xFFFFFFFF, "FLX Horizon \u00b7 Florynx Labs");
    }
}

int widgets_mouse(IEv *e, float p) {
    (void)p;
    int h = hit_at(e->x, e->y);
    if (e->type == IE_DOWN) {
        if (h == HW_NOTES) { notes_focus = 1; dirty = 1; return 1; }
        if (h == HW_PANEL) { notes_focus = 0; dirty = 1; return 1; }
    }
    return h >= HW_PANEL && h < HW_PANEL + 100;
}
int widgets_key(IEv *e) {
    if (!notes_focus) return 0;
    int n = strlen(notes);
    if (e->key == K_BACKSPACE) { if (n) notes[n - 1] = 0; }
    else if (e->key == K_ENTER) { if (n < (int)sizeof notes - 2) { notes[n] = '\n'; notes[n + 1] = 0; } }
    else if (e->text[0] && n < (int)sizeof notes - 2) { notes[n] = e->text[0]; notes[n + 1] = 0; }
    else return 0;
    write_file("/home/.config/notes.txt", notes, strlen(notes));
    return 1;
}

void panels_tick(void) {
    static double last;
    if (now() - last >= 1) {
        last = now();
        if (nh < 60) cpu_hist[nh++] = cpu_pct;
        else { memmove(cpu_hist, cpu_hist + 1, 59 * sizeof(float)); cpu_hist[59] = cpu_pct; }
        if (widgets_open) dirty = 1;
    }
}
