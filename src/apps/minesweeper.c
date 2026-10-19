// Minesweeper: beginner / intermediate / expert, flags on right-click, chording, timer,
// first click always safe. Original art drawn in code. Its own process.
#include "w12.h"

static const int LV[3][3] = { { 9, 9, 10 }, { 16, 16, 40 }, { 30, 16, 99 } };
static const char *LVN[3] = { "Beginner", "Intermediate", "Expert" };
static int lvl, cols, rows, mines;
static unsigned char mine[30][16], open_[30][16], flag[30][16], boom_x = 255, boom_y = 255;
static int state;          // 0 ready, 1 playing, 2 lost, 3 won
static double t0, t_end;
static int nflags, nopen;

static void reset(int l) {
    lvl = l; cols = LV[l][0]; rows = LV[l][1]; mines = LV[l][2];
    memset(mine, 0, sizeof mine); memset(open_, 0, sizeof open_); memset(flag, 0, sizeof flag);
    state = 0; nflags = 0; nopen = 0; boom_x = boom_y = 255;
    MARK("MINES new %s", LVN[l]);
}
static int count(int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && mine[nx][ny]) n++;
    }
    return n;
}
static void place(int sx, int sy) {
    unsigned seed = (unsigned)(now() * 1000);
    int left = mines;
    while (left) {
        seed = seed * 1103515245 + 12345;
        int x = (seed >> 8) % cols, y = (seed >> 20) % rows;
        if (mine[x][y] || (abs(x - sx) <= 1 && abs(y - sy) <= 1)) continue;
        mine[x][y] = 1; left--;
    }
}
static void reveal(int x, int y) {
    if (x < 0 || y < 0 || x >= cols || y >= rows || open_[x][y] || flag[x][y]) return;
    open_[x][y] = 1; nopen++;
    if (mine[x][y]) { state = 2; boom_x = x; boom_y = y; t_end = now(); MARK("MINES lost"); return; }
    if (!count(x, y)) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) reveal(x + dx, y + dy);
}
static void check_win(void) {
    if (state == 1 && nopen == cols * rows - mines) { state = 3; t_end = now(); MARK("MINES won %.0f", t_end - t0); }
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;
    // header
    float bx = 16;
    for (int i = 0; i < 3; i++) {
        float w = text_w(F_REG, 14, 0, LVN[i]) + 28;
        if (lvl == i) g_rrect(c, bx, 12, w, 32, 6, 6, TH.accent_soft);
        if (ui_button(bx, 12, w, 32, LVN[i], lvl == i ? B_SUBTLE : B_SUBTLE)) reset(i);
        if (lvl == i) g_rrect(c, bx + w / 2 - 8, 40, 16, 3, 1.5f, 1.5f, TH.accent);
        bx += w + 6;
    }
    char b[32];
    double el = state == 1 ? now() - t0 : state >= 2 ? t_end - t0 : 0;
    // counters
    float cx = W - 300;
    g_rrect(c, cx, 10, 90, 36, 6, 6, TH.dark ? 0xFF1A1A1A : 0xFF20242B);
    icon_glyph(c, G_FLAG, cx + 20, 28, 16, 0xFFFF5A4E);
    snprintf(b, sizeof b, "%03d", mines - nflags);
    text(c, F_BOLD, 20, 0, cx + 36, 36, 0xFFFF5A4E, b);
    // face button (original: a glass pane that cracks / shines)
    float fx = W - 194;
    bool hv = ui_hover(fx, 8, 40, 40);
    g_rrect(c, fx, 8, 40, 40, 8, 8, hv ? TH.card2 : TH.card);
    g_rrect_line(c, fx, 8, 40, 40, 8, 1, TH.line);
    if (state == 2) icon_glyph(c, G_CLOSE, fx + 20, 28, 18, 0xFFE5484D);
    else if (state == 3) icon_glyph(c, G_STAR, fx + 20, 28, 24, 0xFFFFB020);
    else icon_glyph(c, G_RESTART, fx + 20, 28, 18, TH.text);
    if (ui_click(fx, 8, 40, 40)) reset(lvl);
    g_rrect(c, W - 140, 10, 110, 36, 6, 6, TH.dark ? 0xFF1A1A1A : 0xFF20242B);
    icon_glyph(c, G_CLOCK, W - 120, 28, 16, 0xFF5AD1FF);
    snprintf(b, sizeof b, "%03d", (int)fmin(999, el));
    text(c, F_BOLD, 20, 0, W - 100, 36, 0xFF5AD1FF, b);
    // board
    float top = 60, cs = floorf(fminf((W - 32) / cols, (H - top - 40) / rows));
    float ox = floorf((W - cs * cols) / 2), oy = top + floorf((H - top - 30 - cs * rows) / 2);
    g_rrect(c, ox - 6, oy - 6, cs * cols + 12, cs * rows + 12, 8, 8, TH.dark ? 0xFF2A2E35 : 0xFFB8C4D3);
    static const uint32_t NUMC[9] = { 0, 0xFF1F6FEB, 0xFF2E9E44, 0xFFE5484D, 0xFF5B3CC4, 0xFFB5422A, 0xFF0E8C8C, 0xFF222222, 0xFF777777 };
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            float px = ox + x * cs, py = oy + y * cs;
            bool hov = state < 2 && ui_hover(px, py, cs, cs);
            if (!open_[x][y]) {
                g_vgrad(c, px + 1, py + 1, cs - 2, cs - 2, 4, hov ? 0xFF9DBBE8 : 0xFF8FA9CF, hov ? 0xFF7B9CD0 : 0xFF6F8BB5);
                g_rect(c, px + 3, py + 2, cs - 6, 1, 0x70FFFFFF);
                if (flag[x][y]) icon_glyph(c, G_FLAG, px + cs / 2, py + cs / 2, cs * 0.55f, 0xFFFF4D4D);
                if (state == 2 && mine[x][y] && !flag[x][y]) {
                    g_circle(c, px + cs / 2, py + cs / 2, cs * 0.2f, 0xFF20242B);
                }
            } else {
                bool b0 = boom_x == x && boom_y == y;
                g_rrect(c, px + 1, py + 1, cs - 2, cs - 2, 3, 3, b0 ? 0xFFFF6B6B : TH.dark ? 0xFF3A3F47 : 0xFFF7F9FC);
                if (mine[x][y]) {
                    for (int i = 0; i < 4; i++) { float a = i * (float)M_PI / 4; g_line(c, px + cs / 2 - cosf(a) * cs * .3f, py + cs / 2 - sinf(a) * cs * .3f, px + cs / 2 + cosf(a) * cs * .3f, py + cs / 2 + sinf(a) * cs * .3f, cs * .07f, 0xFF20242B); }
                    g_circle(c, px + cs / 2, py + cs / 2, cs * 0.2f, 0xFF20242B);
                    g_circle(c, px + cs / 2 - cs * .07f, py + cs / 2 - cs * .07f, cs * .05f, 0xFFFFFFFF);
                } else {
                    int n = count(x, y);
                    if (n) { snprintf(b, sizeof b, "%d", n); text_c(c, F_BOLD, cs * 0.55f, 0, px + cs / 2, py + cs * 0.7f, NUMC[n], b); }
                }
            }
            if (state >= 2) continue;
            if (ui_click(px, py, cs, cs)) {
                if (state == 0) { place(x, y); state = 1; t0 = now(); }
                if (open_[x][y]) {   // chord
                    int nf = 0;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int nx = x + dx, ny = y + dy; if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && flag[nx][ny]) nf++; }
                    if (nf == count(x, y)) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) reveal(x + dx, y + dy);
                } else reveal(x, y);
                MARK("MINES reveal %d %d open=%d", x, y, nopen);
                check_win();
            }
            if (ui_rclick(px, py, cs, cs) && !open_[x][y]) { flag[x][y] = !flag[x][y]; nflags += flag[x][y] ? 1 : -1; MARK("MINES flag %d %d", x, y); }
        }
    const char *st = state == 2 ? "Boom. Click the button to try again." : state == 3 ? "You cleared the field!" : state == 0 ? "Left-click to reveal, right-click to flag. First click is always safe." : "";
    text_c(c, F_REG, 13, 0, W / 2, H - 12, state == 2 ? TH.danger : TH.sub, st);
}
static void tick(void) { if (state == 1) app_redraw(); }
static void init(int argc, char **argv) { (void)argc; (void)argv; reset(0); }

int main(int argc, char **argv) {
    static AppDef a = { .exe = "minesweeper", .title = "Minesweeper", .w = 960, .h = 620, .mica = 1, .init = init, .draw = draw, .tick = tick, .tick_ms = 500 };
    return app_main(&a, argc, argv);
}
