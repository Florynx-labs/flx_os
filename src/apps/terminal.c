// Terminal: our own terminal emulator. Runs /bin/flxsh on a pseudo-terminal, parses the
// output (UTF-8, CR/LF/BS/TAB, CSI cursor moves, erase, SGR colours) into a cell grid with
// scrollback, and draws it in fixed-width cells with Selawik.
#include "w12.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

#define FS 15
#define CWD 8.6f
#define CHT 21
#define PAD 12
#define TABH 38
#define SB 1000
typedef struct { uint32_t cp; uint8_t fg, bold; } Cell;
typedef struct {
    Cell *grid; int cols, rows, total_lines, cx, cy, cur_fg, cur_bold, mfd, view_off;
    pid_t child; char esc[64]; int esc_n, in_esc; unsigned utf_cp; int utf_left; char title[48];
} TermTab;
#define MAX_TABS 8
static TermTab tabs[MAX_TABS]; static int ntab, active_tab;
static AppDef A;
static Cell *grid;            // SB + rows lines, ring-free: we memmove on scroll
static int cols = 80, rows = 24, total_lines;   // total_lines = scrollback lines in use above the screen
static int cx, cy;            // cursor within the screen
static int cur_fg = 7, cur_bold;
static int mfd = -1; static pid_t child;
static int view_off;          // lines scrolled back
static char esc[64]; static int esc_n, in_esc;
static unsigned utf_cp; static int utf_left;
static const uint32_t PAL[16] = { 0xFF0C0C0C, 0xFFE74856, 0xFF16C60C, 0xFFF9F1A5, 0xFF3B78FF, 0xFFB4009E, 0xFF61D6D6, 0xFFCCCCCC,
                                  0xFF767676, 0xFFE74856, 0xFF16C60C, 0xFFF9F1A5, 0xFF3B78FF, 0xFFB4009E, 0xFF61D6D6, 0xFFF2F2F2 };

static void tab_save(int i) {
    char title[48] = {0};
    snprintf(title, sizeof title, "%s", tabs[i].title);
    tabs[i] = (TermTab){ .grid=grid, .cols=cols, .rows=rows, .total_lines=total_lines, .cx=cx, .cy=cy,
        .cur_fg=cur_fg, .cur_bold=cur_bold, .mfd=mfd, .view_off=view_off, .child=child,
        .esc_n=esc_n, .in_esc=in_esc, .utf_cp=utf_cp, .utf_left=utf_left };
    memcpy(tabs[i].esc, esc, sizeof esc);
    snprintf(tabs[i].title, sizeof tabs[i].title, "%s", title[0] ? title : "flx@flx-os: ~");
}
static void tab_load(int i) {
    active_tab=i; grid=tabs[i].grid; cols=tabs[i].cols; rows=tabs[i].rows; total_lines=tabs[i].total_lines;
    cx=tabs[i].cx; cy=tabs[i].cy; cur_fg=tabs[i].cur_fg; cur_bold=tabs[i].cur_bold; mfd=tabs[i].mfd;
    view_off=tabs[i].view_off; child=tabs[i].child; esc_n=tabs[i].esc_n; in_esc=tabs[i].in_esc;
    memcpy(esc,tabs[i].esc,sizeof esc); utf_cp=tabs[i].utf_cp; utf_left=tabs[i].utf_left; A.extra_fd=mfd;
}

static Cell *line(int screen_y) { return grid + (size_t)(SB + screen_y) * 200; }
static void clear_line(Cell *l, int from) { for (int i = from; i < 200; i++) l[i] = (Cell){ ' ', 7, 0 }; }
static void scroll_up(void) {
    memmove(grid, grid + 200, (size_t)(SB + rows - 1) * 200 * sizeof(Cell));
    clear_line(line(rows - 1), 0);
    if (total_lines < SB) total_lines++;
}
static void newline(void) { if (++cy >= rows) { cy = rows - 1; scroll_up(); } }
static void put(uint32_t cp) {
    if (cx >= cols) { cx = 0; newline(); }
    line(cy)[cx++] = (Cell){ cp, (uint8_t)cur_fg, (uint8_t)cur_bold };
}

static void csi(char fin) {
    int a[8] = { 0 }, n = 0;
    char *p = esc;
    if (*p == '?') p++;
    while (*p && n < 8) { a[n++] = atoi(p); while (isdigit((unsigned char)*p)) p++; if (*p == ';') p++; else break; }
    int a0 = n ? a[0] : 0;
    switch (fin) {
    case 'm':
        if (!n) { cur_fg = 7; cur_bold = 0; }
        for (int i = 0; i < n; i++) {
            if (a[i] == 0) { cur_fg = 7; cur_bold = 0; }
            else if (a[i] == 1) cur_bold = 1;
            else if (a[i] == 22) cur_bold = 0;
            else if (a[i] >= 30 && a[i] <= 37) cur_fg = a[i] - 30;
            else if (a[i] >= 90 && a[i] <= 97) cur_fg = a[i] - 90 + 8;
            else if (a[i] == 39) cur_fg = 7;
        }
        break;
    case 'K': { Cell *l = line(cy); if (a0 == 0) clear_line(l, cx); else if (a0 == 2) clear_line(l, 0); else for (int i = 0; i <= cx && i < 200; i++) l[i] = (Cell){ ' ', 7, 0 }; break; }
    case 'J':
        if (a0 == 2 || a0 == 3) { for (int y = 0; y < rows; y++) clear_line(line(y), 0); if (a0 == 3) total_lines = 0; }
        else if (a0 == 0) { clear_line(line(cy), cx); for (int y = cy + 1; y < rows; y++) clear_line(line(y), 0); }
        break;
    case 'H': case 'f': cy = mini(maxi((n > 0 ? a[0] : 1) - 1, 0), rows - 1); cx = mini(maxi((n > 1 ? a[1] : 1) - 1, 0), cols - 1); break;
    case 'A': cy = maxi(0, cy - maxi(a0, 1)); break;
    case 'B': cy = mini(rows - 1, cy + maxi(a0, 1)); break;
    case 'C': cx = mini(cols - 1, cx + maxi(a0, 1)); break;
    case 'D': cx = maxi(0, cx - maxi(a0, 1)); break;
    case 'G': cx = mini(cols - 1, maxi(0, a0 - 1)); break;
    }
}

static void feed(const unsigned char *b, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char ch = b[i];
        if (in_esc == 1) { in_esc = ch == '[' ? 2 : 0; esc_n = 0; continue; }
        if (in_esc == 2) {
            if (ch >= 0x40 && ch <= 0x7e) { esc[esc_n] = 0; csi(ch); in_esc = 0; }
            else if (esc_n < 63) esc[esc_n++] = ch;
            continue;
        }
        if (utf_left) { utf_cp = utf_cp << 6 | (ch & 63); if (--utf_left == 0) put(utf_cp); continue; }
        if (ch >= 0xc0) { utf_left = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : 1; utf_cp = ch & (0x3f >> utf_left); continue; }
        switch (ch) {
        case 27: in_esc = 1; break;
        case '\r': cx = 0; break;
        case '\n': newline(); break;
        case '\b': if (cx > 0) cx--; break;
        case '\t': cx = mini(cols - 1, (cx / 8 + 1) * 8); break;
        case 7: break;
        default: if (ch >= 32) put(ch);
        }
    }
    view_off = 0;
}

static void resize_pty(void) {
    int nc = mini(199, maxi(20, (int)((U.w - 2 * PAD - 10) / CWD))), nr = maxi(5, (int)((U.h - TABH - 2 * PAD) / CHT));
    if (nc == cols && nr == rows) return;
    if (nr < rows && cy >= nr) { int d = cy - nr + 1; for (int i = 0; i < d; i++) scroll_up(); cy -= d; }
    cols = nc; rows = nr;
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    if (mfd >= 0) ioctl(mfd, TIOCSWINSZ, &ws);
}

static int session_start(void) {
    grid = calloc((size_t)(SB + 200) * 200, sizeof(Cell));
    if (!grid) return -1;
    for (int y = -SB; y < 200; y++) clear_line(grid + (size_t)(SB + y) * 200, 0);
    cols = 0; rows = 0; total_lines = cx = cy = view_off = 0; cur_fg = 7; cur_bold = 0;
    esc_n = in_esc = utf_left = 0; utf_cp = 0; memset(esc, 0, sizeof esc);
    resize_pty();
    mfd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (mfd < 0 || grantpt(mfd) || unlockpt(mfd)) { if (mfd >= 0) close(mfd); mfd = -1; free(grid); grid = 0; return -1; }
    char *sn = ptsname(mfd);
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    ioctl(mfd, TIOCSWINSZ, &ws);
    child = fork();
    if (child == 0) {
        setsid();
        int s = open(sn, O_RDWR);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        if (s > 2) close(s);
        signal(SIGPIPE, SIG_DFL);
        setenv("TERM", "xterm-256color", 1);
        execl("/bin/flxsh", "flxsh", (char *)0);
        _exit(127);
    }
    if (child < 0) { close(mfd); mfd = -1; free(grid); grid = 0; return -1; }
    A.extra_fd = mfd;
    return 0;
}
static void close_tab(int i) {
    if (i < 0 || i >= ntab) return;
    if (ntab == 1) { app_quit(); return; }
    if (i == active_tab) tab_save(i);
    if (tabs[i].child > 0) kill(-tabs[i].child, SIGHUP);
    if (tabs[i].mfd >= 0) close(tabs[i].mfd);
    free(tabs[i].grid);
    for (int k = i; k + 1 < ntab; k++) tabs[k] = tabs[k + 1];
    memset(&tabs[--ntab], 0, sizeof tabs[0]);
    if (i < active_tab) active_tab--;
    else if (i == active_tab) active_tab = i < ntab ? i : ntab - 1;
    tab_load(active_tab); app_redraw();
}
static void create_tab(void) {
    if (ntab >= MAX_TABS) return;
    tab_save(active_tab);
    int i = ntab++;
    if (session_start()) { ntab--; tab_load(active_tab); return; }
    snprintf(tabs[i].title, sizeof tabs[i].title, "Session %d", i + 1);
    tab_save(i); tab_load(i); app_redraw();
}
static void select_tab(int i) {
    if (i < 0 || i >= ntab || i == active_tab) return;
    tab_save(active_tab); tab_load(i); app_redraw();
}

static void on_fd(void) {
    unsigned char b[8192];
    int n = read(mfd, b, sizeof b);
    if (n <= 0) { LOG("terminal: shell exited"); close_tab(active_tab); return; }
    feed(b, n);
    app_redraw();
}

static void send_str(const char *s) { if (mfd >= 0) write(mfd, s, strlen(s)); }

static void draw(Canvas *c) {
    resize_pty();
    g_rect(c, 0, 0, U.w, U.h, 0xFF060A0C);
    // In-window PTY tabs; the plus button always creates a session here.
    g_rect(c, 0, 0, U.w, TABH, 0xFF0A1114);
    float tabw = fminf(190, (U.w - 164) / maxi(1, ntab)), tabx = 8;
    for (int i = 0; i < ntab; i++) {
        float x = tabx + i * (tabw + 5);
        bool active = i == active_tab;
        g_rrect(c, x, 6, tabw, TABH - 6, 8, 0, active ? 0xFF060A0C : 0xFF10191D);
        if (active) g_rrect_line(c, x, 6, tabw, TABH - 6, 8, 1, 0x28F5AD32);
        icon_app(c, APP_TERMINAL, x + 10, 13, 17);
        char *name = text_ellipsize(F_REG, 12, tabs[i].title, tabw - 54);
        text(c, F_REG, 12, 0, x + 34, 27, 0xFFF2F5F4, name); free(name);
        icon_glyph(c, G_CLOSE, x + tabw - 13, 21, 9, 0xFF9A9A9A);
        if (ui_click(x, 6, tabw - 20, 28)) select_tab(i);
        if (ui_click(x + tabw - 20, 6, 18, 28)) close_tab(i);
    }
    float plus_x = tabx + ntab * (tabw + 5) + 2;
    icon_glyph(c, G_PLUS, plus_x + 11, 22, 13, 0xFFB7C5C7);
    if (ui_click(plus_x, 5, 28, 30)) create_tab();
    // Right action buttons (command palette and settings)
    float rx = U.w - 80;
    ui_card(rx, 6, 32, 26);
    icon_glyph(c, G_APPS, rx + 16, 19, 14, 0xFF94A6AA);
    ui_card(rx + 38, 6, 32, 26);
    icon_glyph(c, G_GEAR, rx + 54, 19, 14, 0xFF94A6AA);
    if (ui_click(rx + 38, 6, 32, 26)) app_launch("settings", 0);
    if (U.ev == EV_WHEEL) { view_off = mini(total_lines, maxi(0, view_off + (int)(U.wheel * 3))); }
    if (U.ev == EV_KEY) {
        int k = U.key, m = U.mods;
        char b[8] = { 0 };
        if ((m & MOD_CTRL) && k == 'v') { char *s = app_clip_get(); send_str(s); free(s); }
        else if ((m & MOD_CTRL) && k >= 'a' && k <= 'z') { b[0] = k - 'a' + 1; send_str(b); }
        else if (k == K_ENTER) send_str("\r");
        else if (k == K_BACKSPACE) send_str("\x7f");
        else if (k == K_TAB) send_str("\t");
        else if (k == K_ESC) send_str("\x1b");
        else if (k == K_UP) send_str("\x1b[A");
        else if (k == K_DOWN) send_str("\x1b[B");
        else if (k == K_RIGHT) send_str("\x1b[C");
        else if (k == K_LEFT) send_str("\x1b[D");
        else if (k == K_PGUP) view_off = mini(total_lines, view_off + rows / 2);
        else if (k == K_PGDN) view_off = maxi(0, view_off - rows / 2);
        else if (U.text[0]) send_str(U.text);
    }
    float y0 = TABH + PAD;
    for (int y = 0; y < rows; y++) {
        Cell *l = grid + (size_t)(SB + y - view_off) * 200;
        if (SB + y - view_off < 0) continue;
        float by = y0 + y * CHT + 16;
        for (int x = 0; x < cols; x++) {
            Cell *ce = &l[x];
            if (ce->cp == ' ' || !ce->cp) continue;
            char u[5]; int k = utf8_put(u, ce->cp); u[k] = 0;
            float gw = text_w(ce->bold ? F_SEMI : F_REG, FS, 0, u);
            text(c, ce->bold ? F_SEMI : F_REG, FS, 0, PAD + x * CWD + (CWD - gw) / 2, by, PAL[ce->fg & 15], u);
        }
    }
    if (!view_off && app_focused) g_rect(c, PAD + cx * CWD, y0 + cy * CHT + 2, CWD, CHT - 3, 0xC0CCCCCC);
    else if (!view_off) g_rrect_line(c, PAD + cx * CWD, y0 + cy * CHT + 2, CWD, CHT - 3, 0, 1, 0xC0CCCCCC);
    if (view_off) {
        char s[64]; snprintf(s, sizeof s, "Scrolled back %d lines", view_off);
        g_rrect(c, U.w - 230, TABH + 8, 214, 28, 6, 6, 0xE0303030);
        text(c, F_REG, 13, 0, U.w - 218, TABH + 27, 0xFFDDDDDD, s);
    }
}

static void init(int argc, char **argv) {
    (void)argc; (void)argv;
    U.w = A.w; U.h = A.h;
    ntab = 1; active_tab = 0; mfd = -1; child = -1;
    if (session_start()) { app_quit(); return; }
    snprintf(tabs[0].title, sizeof tabs[0].title, "flx@flx-os: ~");
    tab_save(0); tab_load(0);
    MARK("TERMINAL tabs ready");
}

int main(int argc, char **argv) {
    A = (AppDef){ .exe = "terminal", .title = "FLX Terminal", .w = 960, .h = 600, .draw = draw, .init = init, .on_fd = on_fd };
    return app_main(&A, argc, argv);
}
