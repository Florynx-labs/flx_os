// libw12ui widgets: immediate-mode Fluent-style controls drawn with gfx.c + font.c.
// Every widget both draws itself and reacts to the event in U (see client.c).
#include "w12.h"

static bool in(float px, float py, float x, float y, float w, float h) { return px >= x && py >= y && px < x + w && py < y + h; }
bool ui_in(float x, float y, float w, float h) { return in(U.mx, U.my, x, y, w, h); }
bool ui_hover(float x, float y, float w, float h) { return !U.consumed && in(U.mx, U.my, x, y, w, h) && (!U.buttons || in(U.px, U.py, x, y, w, h)); }
bool ui_click(float x, float y, float w, float h) {
    if (U.consumed || U.ev != EV_UP || U.btn != 1) return false;
    if (in(U.mx, U.my, x, y, w, h) && in(U.px, U.py, x, y, w, h)) { U.consumed = 1; return true; }
    return false;
}
bool ui_rclick(float x, float y, float w, float h) {
    if (U.consumed || U.ev != EV_UP || U.btn != 2 || !in(U.mx, U.my, x, y, w, h)) return false;
    U.consumed = 1;
    return true;
}
bool ui_dblclick(float x, float y, float w, float h) {
    if (U.consumed || U.ev != EV_DOWN || U.btn != 1 || U.clicks != 2 || !in(U.mx, U.my, x, y, w, h)) return false;
    U.consumed = 1;
    return true;
}
bool ui_downin(float x, float y, float w, float h) { return !U.consumed && U.ev == EV_DOWN && U.btn == 1 && in(U.mx, U.my, x, y, w, h); }
bool ui_pressing(float x, float y, float w, float h) { return (U.buttons & 1) && in(U.px, U.py, x, y, w, h) && in(U.mx, U.my, x, y, w, h); }
bool ui_key(int k, int mods) {
    if (U.consumed || U.ev != EV_KEY || U.key != k || (U.mods & ~MOD_SHIFT) != (mods & ~MOD_SHIFT)) return false;
    U.consumed = 1;
    return true;
}
void ui_consume(void) { U.consumed = 1; }

bool ui_button(float x, float y, float w, float h, const char *lbl, int kind) {
    bool hov = ui_hover(x, y, w, h), prs = ui_pressing(x, y, w, h);
    uint32_t bg, fg = TH.text;
    if (kind == B_ACCENT) { bg = prs ? c_mix(TH.accent, TH.bg, 0.2f) : hov ? c_mix(TH.accent, 0xFFFFFFFF, 0.12f) : TH.accent; fg = TH.accent_fg; }
    else if (kind == B_SUBTLE) bg = prs ? TH.press : hov ? TH.hover : 0;
    else bg = prs ? TH.card2 : hov ? c_mix(TH.card, TH.card2, 0.6f) : TH.card;
    if (bg >> 24) g_rrect(UC, x, y, w, h, 5, 5, bg);
    if (kind == B_STD) g_rrect_line(UC, x, y, w, h, 5, 1, TH.line);
    float tw = text_w(F_REG, 15, 0, lbl);
    text(UC, F_REG, 15, 0, x + (w - tw) / 2, y + h / 2 + 5.5f, prs && kind == B_STD ? TH.sub : fg, lbl);
    return ui_click(x, y, w, h);
}

bool ui_icon_btn(float x, float y, float w, float h, int glyph, int active) {
    bool hov = ui_hover(x, y, w, h), prs = ui_pressing(x, y, w, h);
    if (active) g_rrect(UC, x, y, w, h, 5, 5, TH.accent_soft);
    else if (hov || prs) g_rrect(UC, x, y, w, h, 5, 5, prs ? TH.press : TH.hover);
    icon_glyph(UC, glyph, x + w / 2, y + h / 2, fminf(w, h) * 0.5f, active ? TH.accent : TH.text);
    return ui_click(x, y, w, h);
}

bool ui_toggle(float x, float y, int *on) {
    float w = 44, h = 22;
    bool hov = ui_hover(x, y, w, h);
    if (*on) g_rrect(UC, x, y, w, h, 11, 11, hov ? c_mix(TH.accent, 0xFFFFFFFF, 0.1f) : TH.accent);
    else { g_rrect(UC, x, y, w, h, 11, 11, hov ? TH.hover : 0); g_rrect_line(UC, x, y, w, h, 11, 1.2f, TH.sub); }
    g_circle(UC, *on ? x + w - 11 : x + 11, y + 11, hov ? 7 : 6, *on ? TH.accent_fg : TH.sub);
    if (ui_click(x, y, w, h)) { *on = !*on; return true; }
    return false;
}

static int field_impl(int id, char *buf, int cap, float x, float y, float w, float h, const char *ph, int hidden) {
    bool foc = U.focus == id, hov = ui_hover(x, y, w, h);
    int ret = 0;
    if (ui_downin(x, y, w, h)) { U.focus = id; foc = true; }
    else if (U.ev == EV_DOWN && U.btn == 1 && foc && !in(U.mx, U.my, x, y, w, h)) { U.focus = 0; foc = false; }
    if (foc && U.ev == EV_KEY && !U.consumed) {
        int n = strlen(buf);
        if (U.key == K_BACKSPACE) {
            if (U.mods & MOD_CTRL) { while (n > 0 && buf[n - 1] == ' ') n--; while (n > 0 && buf[n - 1] != ' ') n--; buf[n] = 0; }
            else { while (n > 0 && ((unsigned char)buf[n - 1] & 0xc0) == 0x80) n--; if (n > 0) buf[--n] = 0; }
            ret = 2; U.consumed = 1;
        } else if (U.key == K_ENTER) { ret = 1; U.consumed = 1; }
        else if (U.key == 'a' && (U.mods & MOD_CTRL)) { buf[0] = 0; ret = 2; U.consumed = 1; }
        else if (U.key == 'v' && (U.mods & MOD_CTRL)) {
            char *cl = app_clip_get();
            for (char *p = cl; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
            snprintf(buf + n, cap - n, "%s", cl);
            free(cl);
            ret = 2; U.consumed = 1;
        } else if (U.text[0] && !(U.mods & (MOD_CTRL | MOD_ALT)) && (unsigned char)U.text[0] >= 32) {
            int l = strlen(U.text);
            if (n + l < cap) { memcpy(buf + n, U.text, l + 1); ret = 2; }
            U.consumed = 1;
        }
    }
    g_rrect(UC, x, y, w, h, 5, 5, foc ? (TH.dark ? 0xFF1F1F1F : 0xFFFFFFFF) : hov ? c_mix(TH.card, TH.card2, 0.7f) : TH.card);
    g_rrect_line(UC, x, y, w, h, 5, 1, TH.line);
    if (foc) g_rrect(UC, x + 1, y + h - 2, w - 2, 2, 0, 1, TH.accent);
    else g_rect(UC, x + 4, y + h - 1, w - 8, 1, TH.dim);
    int sc[4];
    g_getclip(UC, sc);
    g_clip(UC, x + 8, y, x + w - 8, y + h);
    char masked[512] = "";
    const char *shown = buf;
    if (hidden) {
        const char *p = buf; int count = 0;
        while (*p && count < 100) { utf8_next(&p); strncat(masked, "•", sizeof masked - strlen(masked) - 1); count++; }
        shown = masked;
    }
    float tw = text_w(F_REG, 15, 0, shown), tx = x + 11;
    if (tw > w - 26) tx = x + w - 15 - tw;
    float base = y + h / 2 + 5.5f;
    if (!buf[0] && ph) text(UC, F_REG, 15, 0, x + 11, base, TH.dim, ph);
    else text(UC, F_REG, 15, 0, tx, base, TH.text, shown);
    if (foc) g_rect(UC, tx + tw + 1, y + h / 2 - 9, 1.5f, 18, TH.text);
    g_setclip(UC, sc);
    return ret;
}

int ui_field(int id, char *buf, int cap, float x, float y, float w, float h, const char *ph) {
    return field_impl(id, buf, cap, x, y, w, h, ph, 0);
}
int ui_password_field(int id, char *buf, int cap, float x, float y, float w, float h, const char *ph) {
    return field_impl(id, buf, cap, x, y, w, h, ph, 1);
}

void ui_scroll(Scroll *s, float x, float y, float w, float h, float content_h) {
    s->max = fmaxf(0, content_h - h);
    if (U.ev == EV_WHEEL && !U.consumed && in(U.mx, U.my, x, y, w, h)) { s->pos -= U.wheel * 72; U.consumed = 1; }
    float bx = x + w - 8, th = content_h > 0 ? fmaxf(36, h * h / content_h) : h;
    if (s->max > 0) {
        float ty = y + (h - th) * (s->pos / s->max);
        if (U.ev == EV_DOWN && U.btn == 1 && in(U.mx, U.my, bx - 4, y, 12, h)) {
            s->drag = 1; s->grab = in(U.mx, U.my, bx - 4, ty, 12, th) ? U.my - ty : th / 2; U.consumed = 1;
        }
        if (s->drag && (U.buttons & 1)) s->pos = (U.my - s->grab - y) / fmaxf(1, h - th) * s->max;
        if (!(U.buttons & 1)) s->drag = 0;
        s->pos = clampf(s->pos, 0, s->max);
        ty = y + (h - th) * (s->pos / s->max);
        bool hv = in(U.mx, U.my, bx - 6, y, 14, h) || s->drag;
        g_rrect(UC, bx + (hv ? -1 : 1), ty + 2, hv ? 6 : 3, th - 4, 3, 3, c_alpha(TH.text, hv ? 0.45f : 0.3f));
    } else s->pos = 0;
}

void ui_card(float x, float y, float w, float h) {
    g_rrect(UC, x, y, w, h, 7, 7, TH.dark ? 0xFF2B2B2B : 0xFFFFFFFF);
    g_rrect_line(UC, x, y, w, h, 7, 1, TH.dark ? 0xFF353535 : 0xFFE6E6E6);
}
void ui_divider(float x, float y, float w) { g_rect(UC, x, y, w, 1, TH.line); }
