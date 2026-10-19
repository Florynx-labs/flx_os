// Calculator: standard mode with keyboard input. Its own process.
#include "w12.h"

static double acc; static int op;          // pending operator: + - * /
static char entry[64] = "0";
static int fresh = 1, has_acc;
static char expr[128];
static char err[32];

static void fmt(double v, char *o, int n) {
    if (!isfinite(v)) { snprintf(o, n, "Cannot divide by zero"); return; }
    snprintf(o, n, "%.12g", v);
    if (strchr(o, 'e')) return;
    // thousands separators for the display only happen in draw
}
static double cur(void) { return atof(entry); }
static const char *opch(int o) { return o == '+' ? "+" : o == '-' ? "-" : o == '*' ? "×" : o == '/' ? "÷" : ""; }
static double apply(double a, int o, double b) {
    switch (o) { case '+': return a + b; case '-': return a - b; case '*': return a * b; case '/': return b == 0 ? NAN : a / b; }
    return b;
}
static void set_entry(double v) { fmt(v, entry, sizeof entry); if (!isfinite(v)) { snprintf(err, sizeof err, "Cannot divide by zero"); snprintf(entry, sizeof entry, "0"); } }
static void press(const char *k) {
    if (err[0] && strcmp(k, "C")) { err[0] = 0; }
    if (isdigit((unsigned char)k[0]) && !k[1]) {
        if (fresh || !strcmp(entry, "0")) { snprintf(entry, sizeof entry, "%s", k); fresh = 0; }
        else if (strlen(entry) < 16) strcat(entry, k);
    } else if (!strcmp(k, ".")) {
        if (fresh) { snprintf(entry, sizeof entry, "0."); fresh = 0; }
        else if (!strchr(entry, '.')) strcat(entry, ".");
    } else if (strchr("+-*/", k[0]) && !k[1]) {
        if (has_acc && !fresh) set_entry(acc = apply(acc, op, cur()));
        else if (!has_acc) acc = cur();
        op = k[0]; has_acc = 1; fresh = 1;
        char a[64]; fmt(acc, a, sizeof a);
        snprintf(expr, sizeof expr, "%s %s", a, opch(op));
    } else if (!strcmp(k, "=")) {
        if (!has_acc) return;
        double b = cur();
        char a[64], bb[64]; fmt(acc, a, sizeof a); fmt(b, bb, sizeof bb);
        snprintf(expr, sizeof expr, "%s %s %s =", a, opch(op), bb);
        double r = apply(acc, op, b);
        set_entry(r);
        has_acc = 0; fresh = 1;
        MARK("CALC %s %s", expr, err[0] ? err : entry);
    } else if (!strcmp(k, "C")) { snprintf(entry, sizeof entry, "0"); acc = 0; has_acc = 0; op = 0; expr[0] = 0; fresh = 1; err[0] = 0; }
    else if (!strcmp(k, "CE")) { snprintf(entry, sizeof entry, "0"); fresh = 1; }
    else if (!strcmp(k, "BS")) { int n = strlen(entry); if (!fresh && n > 1) entry[n - 1] = 0; else snprintf(entry, sizeof entry, "0"); }
    else if (!strcmp(k, "+/-")) { if (entry[0] == '-') memmove(entry, entry + 1, strlen(entry)); else if (strcmp(entry, "0")) { memmove(entry + 1, entry, strlen(entry) + 1); entry[0] = '-'; } }
    else if (!strcmp(k, "%")) set_entry(has_acc ? acc * cur() / 100 : cur() / 100);
    else if (!strcmp(k, "1/x")) { double v = cur(); snprintf(expr, sizeof expr, "1/(%s)", entry); set_entry(v == 0 ? NAN : 1 / v); fresh = 1; }
    else if (!strcmp(k, "x2")) { double v = cur(); snprintf(expr, sizeof expr, "sqr(%s)", entry); set_entry(v * v); fresh = 1; }
    else if (!strcmp(k, "sqrt")) { double v = cur(); snprintf(expr, sizeof expr, "√(%s)", entry); set_entry(v < 0 ? NAN : sqrt(v)); fresh = 1; }
}

static void pretty(const char *e, char *o, int n) {   // 1234567.5 -> 1,234,567.5
    const char *dot = strchr(e, '.');
    int ilen = dot ? (int)(dot - e) : (int)strlen(e), neg = e[0] == '-', k = 0;
    if (strchr(e, 'e')) { snprintf(o, n, "%s", e); return; }
    if (neg) o[k++] = '-';
    for (int i = neg; i < ilen && k < n - 2; i++) {
        o[k++] = e[i];
        int left = ilen - i - 1;
        if (left > 0 && left % 3 == 0) o[k++] = ',';
    }
    snprintf(o + k, n - k, "%s", dot ? dot : "");
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;
    text(c, F_SEMI, 18, 0, 22, 36, TH.text, "Standard");
    // keyboard
    if (U.ev == EV_KEY) {
        int k = U.key;
        char s[2] = { (char)k, 0 };
        if ((k >= '0' && k <= '9') || k == '.' || k == '+' || k == '-' || k == '*' || k == '/') press(s);
        else if (k == K_ENTER || k == '=') press("=");
        else if (k == K_ESC) press("C");
        else if (k == K_BACKSPACE) press("BS");
        else if (k == K_DEL) press("CE");
        else if (k == '%') press("%");
    }
    text_r(c, F_REG, 15, 0, W - 22, 92, TH.sub, expr);
    char disp[96];
    pretty(err[0] ? "0" : entry, disp, sizeof disp);
    float fs = 52;
    while (fs > 22 && text_w(F_SEMI, fs, 0, err[0] ? err : disp) > W - 44) fs -= 2;
    text_r(c, F_SEMI, fs, 0, W - 22, 160, TH.text, err[0] ? err : disp);
    static const char *K[6][4] = {
        { "%", "CE", "C", "BS" }, { "1/x", "x2", "sqrt", "/" }, { "7", "8", "9", "*" },
        { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "+/-", "0", ".", "=" } };
    static const char *L[6][4] = {
        { "%", "CE", "C", "⌫" }, { "¹⁄ₓ", "x²", "√x", "÷" }, { "7", "8", "9", "×" },
        { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "+/-", "0", ".", "=" } };
    float gx = 8, gy = 190, gw = W - 16, gh = H - gy - 8, bw = gw / 4, bh = gh / 6;
    for (int r = 0; r < 6; r++)
        for (int k = 0; k < 4; k++) {
            float x = gx + k * bw + 2, y = gy + r * bh + 2, w = bw - 4, h = bh - 4;
            bool digit = r >= 2 && k < 3, eq = r == 5 && k == 3;
            bool hov = ui_hover(x, y, w, h), prs = ui_pressing(x, y, w, h);
            uint32_t bg = eq ? TH.accent : digit ? (TH.dark ? 0xFF3B3B3B : 0xFFFFFFFF) : (TH.dark ? 0xFF323232 : 0xFFF6F6F6);
            if (hov) bg = eq ? c_mix(TH.accent, 0xFFFFFFFF, 0.12f) : c_mix(bg, TH.dark ? 0xFFFFFFFF : 0xFF000000, 0.05f);
            if (prs) bg = eq ? c_mix(TH.accent, 0xFF000000, 0.12f) : c_mix(bg, TH.dark ? 0xFF000000 : 0xFF000000, 0.08f);
            g_rrect(c, x, y, w, h, 5, 5, bg);
            g_rrect_line(c, x, y, w, h, 5, 1, TH.dark ? 0x30FFFFFF : 0x14000000);
            const char *lbl = L[r][k];
            if (!strcmp(K[r][k], "BS")) icon_glyph(c, G_BACK, x + w / 2, y + h / 2, 18, TH.text);
            else if (!strcmp(K[r][k], "1/x")) text_c(c, F_REG, 17, 0, x + w / 2, y + h / 2 + 6, TH.text, "1/x");
            else if (!strcmp(K[r][k], "x2")) { text_c(c, F_REG, 17, 0, x + w / 2 - 3, y + h / 2 + 6, TH.text, "x"); text(c, F_REG, 11, 0, x + w / 2 + 3, y + h / 2 - 2, TH.text, "2"); }
            else if (!strcmp(K[r][k], "sqrt")) { text_c(c, F_REG, 17, 0, x + w / 2, y + h / 2 + 6, TH.text, "sqrt"); }
            else text_c(c, digit ? F_SEMI : F_REG, digit ? 20 : 18, 0, x + w / 2, y + h / 2 + 7, eq ? TH.accent_fg : TH.text, lbl);
            if (ui_click(x, y, w, h)) press(K[r][k]);
        }
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "calc", .title = "Calculator", .w = 380, .h = 600, .mica = 1, .draw = draw };
    return app_main(&a, argc, argv);
}
