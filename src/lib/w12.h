// FLX OS — libflxui: shared declarations (gfx, text, theme, icons, window protocol, toolkit).
// Rasteriser and text code are ported from Lumen OS 2 (MIT, Jakub Dostál).
#pragma once
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#define SCR_W 1920
#define SCR_H 1080
#define W12_VERSION "0.1.0"
#define W12_NAME "FLX OS"
#define W12_TAGLINE "Plus qu'un syst\u00e8me. Une vision."
#define W12_SOCK "/run/flx.sock"
#define FLX_SOCK  W12_SOCK   /* preferred alias */

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline float ease_out(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }
static inline float ease_inout(float t) { t = clampf(t, 0, 1); return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

// ---- sys.c --------------------------------------------------------------------------------
extern int log_fd;               // where LOG/MARK go (default stderr = serial console)
double now(void);
void LOG(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void MARK(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   // "W12:..." serial marker
char *xstrdup(const char *s);
char *strfmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *read_file(const char *path, size_t *len);           // malloc'd, NUL terminated
int write_file(const char *path, const void *d, size_t n);
void mkdir_p(const char *path);
void human_size(double b, char *out, int n);
typedef struct { uint64_t total_bytes, available_bytes, used_bytes; } FlxFsStats;
int flx_fs_stats(const char *path, FlxFsStats *out);  // 0 on success, -1 with errno preserved

// ---- gfx.c --------------------------------------------------------------------------------
typedef struct { int w, h; uint32_t *px; int cx0, cy0, cx1, cy1; int stride; } Canvas;
typedef struct { int w, h; uint32_t *px; } Image;

Canvas *canvas_new(int w, int h);
void canvas_wrap(Canvas *c, uint32_t *px, int w, int h, int stride);
void g_clip(Canvas *c, float x0, float y0, float x1, float y1);
void g_noclip(Canvas *c);
void g_getclip(Canvas *c, int s[4]);
void g_setclip(Canvas *c, const int s[4]);
void g_clear(Canvas *c, uint32_t col);
void g_rect(Canvas *c, float x, float y, float w, float h, uint32_t col);
void g_rrect(Canvas *c, float x, float y, float w, float h, float rtop, float rbot, uint32_t col);
void g_rrect_line(Canvas *c, float x, float y, float w, float h, float r, float lw, uint32_t col);
void g_circle(Canvas *c, float cx, float cy, float r, uint32_t col);
void g_ring(Canvas *c, float cx, float cy, float r, float lw, uint32_t col);
void g_line(Canvas *c, float x0, float y0, float x1, float y1, float w, uint32_t col);
void g_arc(Canvas *c, float cx, float cy, float r, float a0, float a1, float w, uint32_t col);
void g_vgrad(Canvas *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot);
// convex polygon, anti-aliased, linear gradient from (gx0,gy0) col0 to (gx1,gy1) col1
void g_poly(Canvas *c, const float *xy, int n, uint32_t col0, uint32_t col1, float gx0, float gy0, float gx1, float gy1);
void g_shadow(Canvas *c, float x, float y, float w, float h, float r, float blur, int alpha, float dy);
// frosted glass: blurs what is already in c under the rounded rect, then tints it
void g_glass(Canvas *c, float x, float y, float w, float h, float r, uint32_t tint);
void g_blur_full(Canvas *dst, const Canvas *src, int passes);   // whole-canvas heavy blur (wallpaper cache)
void g_copy_from(Canvas *dst, const Canvas *src, float x, float y, float w, float h, float rt, float rb);  // same coords
// composite a raw ARGB buffer (premultiplied-free straight alpha) with rounded corners and global alpha
void g_buf(Canvas *c, const uint32_t *px, int stride, int sw, int sh, float x, float y, float w, float h,
           float rt, float rb, int alpha, int opaque);
void g_image(Canvas *c, const Image *im, float x, float y, float w, float h, float radius, int alpha);
void g_image_cover(Canvas *c, const Image *im, float x, float y, float w, float h, float radius, int alpha);
void g_layer_rect(Canvas *dst, const Canvas *src, float x, float y, float w, float h, float radius, int alpha);
uint32_t c_alpha(uint32_t col, float a);
uint32_t c_mix(uint32_t a, uint32_t b, float t);
Image *image_decode(const void *data, int len);
Image *image_load(const char *path);
Image *image_new(int w, int h);
void image_free(Image *im);

// ---- font.c (Selawik) ---------------------------------------------------------------------
enum { F_LIGHT, F_SEMILIGHT, F_REG, F_SEMI, F_BOLD, NFONT };
#define W400 F_REG
#define W600 F_SEMI
#define W800 F_BOLD
void font_init(void);
int font_get_family(void);
void font_set_family(int family);
const char *font_family_name(int family);
float text_w(int f, float px, float track, const char *s);
float text(Canvas *c, int f, float px, float track, float x, float base, uint32_t col, const char *s);
float text_n(Canvas *c, int f, float px, float track, float x, float base, uint32_t col, const char *s, int n);
float text_wn(int f, float px, float track, const char *s, int n);
void text_r(Canvas *c, int f, float px, float track, float xr, float base, uint32_t col, const char *s);
void text_c(Canvas *c, int f, float px, float track, float xc, float base, uint32_t col, const char *s);
float font_asc(int f, float px);
float font_cap(int f, float px);
int utf8_next(const char **s);
int utf8_put(char *o, int cp);
char *text_ellipsize(int f, float px, const char *s, float maxw);
int text_wrap(int f, float px, const char *s, float maxw, int *st, int *ln, int max);
// convenience: text vertically centred in a box of height h starting at y
float label(Canvas *c, int f, float px, float x, float y, float h, uint32_t col, const char *s);
void labelf(Canvas *c, int f, float px, float x, float y, float h, uint32_t col, const char *fmt, ...) __attribute__((format(printf, 8, 9)));

// ---- theme ----------------------------------------------------------------------------------
#define NACCENT 6
#define NWALL 9  // Six dark/light pairs plus three theme-neutral world wallpapers
// Shared libflxui design tokens. Geometry is expressed on the 4 px grid.
#define FLX_SPACE_UNIT 4.0f
#define FLX_RADIUS_CONTROL 8.0f
#define FLX_RADIUS_CARD 12.0f
#define FLX_RADIUS_PILL 16.0f
#define FLX_BORDER_WIDTH 1.0f
#define FLX_ANIMATION_MS 180
extern const uint32_t ACCENTS[NACCENT];
extern const char *ACCENT_NAMES[NACCENT];
extern const char *WALL_NAMES[NWALL];
typedef struct {
    int dark, accent_i, wall;
    uint32_t accent, accent_fg, accent_soft;
    uint32_t bg, mica, surface, card, card2, hover, press, line, text, sub, dim, sel, danger;
} Theme;
extern Theme TH;
void theme_set(int dark, int accent, int wall);

// ---- icons.c --------------------------------------------------------------------------------
enum { APP_EXPLORER, APP_SETTINGS, APP_TERMINAL, APP_NOTEPAD, APP_CALC, APP_TASKMGR, APP_MINES, APP_PROOF, APP_BROWSER, NAPPS };
typedef struct { const char *exe, *title, *desc; } AppInfo;
extern const AppInfo APPS[NAPPS];
int app_by_exe(const char *exe);
void icon_app(Canvas *c, int app, float x, float y, float size);
enum { ICON_NORMAL, ICON_HOVER, ICON_ACTIVE, ICON_DISABLED };
void icon_draw(Canvas *c, const char *name, float cx, float cy, int size, uint32_t color, int state);
void icon_logo(Canvas *c, float cx, float cy, float size, int alpha);   // the official FLX OS golden fox mark
void icon_user_avatar(Canvas *c, float x, float y, float size, float r, int alpha);
enum {
    G_SEARCH, G_WIFI, G_WIFI_OFF, G_VOLUME, G_BATTERY, G_POWER, G_CLOSE, G_MIN, G_MAX, G_RESTORE, G_CHEV_R, G_CHEV_L,
    G_CHEV_D, G_CHEV_U, G_BACK, G_FWD, G_UP, G_REFRESH, G_PLUS, G_TRASH, G_EDIT, G_FOLDER, G_FILE, G_GEAR, G_USER,
    G_WIDGETS, G_HOME, G_LIST, G_GRID, G_SUN, G_MOON, G_CLOUD, G_RAIN, G_STAR, G_SEND, G_FLAG, G_CLOCK, G_RESTART,
    G_LOCK, G_INFO, G_PALETTE, G_NET, G_APPS, G_DISPLAY, G_SHIELD, G_CHECK, G_DOTS, G_COPY, G_PASTE, G_SAVE,
    G_BLUETOOTH, G_DEVICES, G_CPU, G_MEMORY, G_DRIVE, G_UPDATE,
};
void icon_glyph(Canvas *c, int g, float cx, float cy, float size, uint32_t col);
void icon_folder(Canvas *c, float x, float y, float size);
void icon_file(Canvas *c, const char *name, float x, float y, float size);

// ---- window protocol (client.c / compositor) --------------------------------------------------
#define BUF_W SCR_W
#define BUF_H SCR_H
enum {
    M_HELLO = 1,   // a=pid b=w c=h d=flags(1=mica) s=exe ; memfd attached (2 buffers BUF_W*BUF_H)
    M_COMMIT,      // a=buffer b=w c=h
    M_TITLE,       // s
    M_LAUNCH,      // s="exe\targ"
    M_CLIP_SET,    // s
    M_CLIP_GET,
    M_SET_THEME,   // a=dark b=accent c=wallpaper
    M_CLOSE_ME,
    M_POWER,       // a: 0 shutdown 1 restart
    M_RECENT,      // s=path (Start > Recommended)
    M_SET_DOCK,    // a=autohide
    M_SET_FONT,    // a=font family index
    M_SET_SCALE,   // a=window scale percentage
    M_SET_DOCK_APPS, // a=bit mask of pinned app ids
    M_AUTH_REQ,    // s=restricted authd command
    M_AUTH_RES,    // s=authd result
    M_CONFIGURE = 100,  // a=w b=h c=state(0 normal 1 max 2 snapped)
    M_MOUSE,       // a=x b=y c=buttons d=ev e=button/wheel
    M_KEY,         // a=key b=mods s=utf8
    M_FOCUS,       // a=focused
    M_CLOSE_REQ,
    M_THEME,       // a=dark b=accent c=wallpaper
    M_CLIP_DATA,   // s
    M_RELEASE,     // a=buffer
    M_FONT,        // a=font family index
};
enum { EV_NONE, EV_DOWN, EV_UP, EV_MOVE, EV_WHEEL, EV_LEAVE, EV_KEY };
enum { K_BACKSPACE = 8, K_TAB = 9, K_ENTER = 10, K_ESC = 27, K_DEL = 127,
       K_LEFT = 0x110000, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END, K_PGUP, K_PGDN, K_SUPER, K_F1, K_F4 = K_F1 + 3, K_F5 };
enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_SUPER = 8 };
typedef struct { int32_t type, a, b, c, d, e; char s[4072]; } Msg;
int msg_send(int fd, int type, int a, int b, int c, int d, int e, const char *s, int pass_fd);
int msg_recv(int fd, Msg *m, int *fd_out);   // >0 ok, 0 closed, <0 would block / error

// ---- toolkit (client.c + ui.c) -----------------------------------------------------------------
typedef struct {
    const char *exe;         // matches APPS[].exe
    const char *title;
    int w, h, mica;
    void (*init)(int argc, char **argv);
    void (*draw)(Canvas *c);            // immediate mode: draw + react to U
    void (*tick)(void);                 // every tick_ms (if set)
    int tick_ms;
    int (*close)(void);                 // return 0 to keep the window open
    int extra_fd;                       // optional fd to poll (terminal pty)
    void (*on_fd)(void);
} AppDef;
int app_main(AppDef *a, int argc, char **argv);
void app_redraw(void);
void app_set_title(const char *t);
void app_launch(const char *exe, const char *arg);
void app_clip_set(const char *s);
char *app_clip_get(void);   // malloc'd (blocks briefly for the compositor's answer)
void app_set_theme(int dark, int accent, int wall);
void app_set_dock(int autohide);
void app_set_font(int family);
void app_set_scale(int percent);
void app_set_dock_apps(int mask);
int app_auth_request(const char *request, char *response, size_t response_size);
void app_power(int restart);
void app_recent(const char *path);
void app_quit(void);
extern int app_focused;

typedef struct {
    int w, h;
    float mx, my;            // mouse in content coords
    int buttons;             // held
    int ev, btn;             // current event
    float px, py;            // where the last press started
    int clicks;              // 2 on a double click press
    float wheel;
    int key, mods; char text[8];
    int focus;               // focused text field id
    int consumed;
    double t;
} UIState;
extern UIState U;
bool ui_in(float x, float y, float w, float h);       // mouse inside
bool ui_hover(float x, float y, float w, float h);
bool ui_click(float x, float y, float w, float h);    // left press+release inside
bool ui_rclick(float x, float y, float w, float h);
bool ui_dblclick(float x, float y, float w, float h);
bool ui_pressing(float x, float y, float w, float h);
bool ui_downin(float x, float y, float w, float h);   // left press event inside
bool ui_key(int k, int mods);
void ui_consume(void);
enum { B_STD, B_ACCENT, B_SUBTLE };
bool ui_button(float x, float y, float w, float h, const char *label, int kind);
bool ui_icon_btn(float x, float y, float w, float h, int glyph, int active);
bool ui_toggle(float x, float y, int *on);
// single-line field: returns 1 on Enter, 2 when text changed
int ui_field(int id, char *buf, int cap, float x, float y, float w, float h, const char *ph);
int ui_password_field(int id, char *buf, int cap, float x, float y, float w, float h, const char *ph);
typedef struct { float pos, max; int drag; float grab; } Scroll;
void ui_scroll(Scroll *s, float x, float y, float w, float h, float content_h);   // wheel + bar
void ui_card(float x, float y, float w, float h);
void ui_divider(float x, float y, float w);
extern Canvas *UC;   // canvas being drawn

// ---- json.c (own parser) ---------------------------------------------------------------------
typedef struct Json Json;
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
struct Json { int type; double num; char *str; Json *kids; char *key; Json *next; };
Json *json_parse(const char *s);
void json_free(Json *j);
Json *json_get(Json *o, const char *key);     // object member
Json *json_at(Json *a, int i);
double json_num(Json *j, double def);
const char *json_str(Json *j, const char *def);
char *json_escape(const char *s);             // malloc'd "quoted"

// ---- netinfo (written by flx-netd) ------------------------------------------------------------
typedef struct { int up; char ifname[16], ip[20], mask[20], gw[20], dns[20], mac[20]; double since; } NetInfo;
int netinfo_read(NetInfo *ni);
