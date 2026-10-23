// flx-compositor: owns DRM/KMS and evdev, composites app windows, draws the shell.
#pragma once
#include "w12.h"
#include "../net/net.h"
#include <sys/types.h>

// drm.c
Canvas *drm_init(void);
Canvas *drm_flip(void);

// input.c
enum { IE_MOVE = 1, IE_DOWN, IE_UP, IE_WHEEL, IE_KEY, IE_KEYUP };
typedef struct { int type; float x, y; int btn; int key, mods; char text[8]; int wheel; } IEv;
void input_init(void);
int input_poll(IEv *out, int max);
int input_fds(int *fds, int max);
extern float mouse_x, mouse_y;
extern int mouse_buttons, mods_held;

// windows (wm.c)
#define TB 40
#define MAXWIN 32
typedef struct Win {
    int used, id, fd, app, mica;
    uid_t peer_uid;
    pid_t pid;
    char exe[32], title[128];
    uint32_t *shm;
    int front, bw, bh, mapped;
    float x, y, w, h;           // content rect (titlebar is above y)
    float rx, ry, rw, rh;       // restore rect
    int state;                  // 0 normal, 1 maximized, 2 snapped
    int minimized, closing, dead;
    double anim_t; int anim;    // AN_*
    float ax, ay, aw, ah;       // animation start rect
    double hover_max_t;
} Win;
enum { AN_NONE, AN_OPEN, AN_CLOSE, AN_MIN, AN_UNMIN, AN_MOVE };
extern Win wins[MAXWIN];
extern int zord[MAXWIN], nz;     // bottom..top indices into wins
extern Win *focus;
void wm_init(void);
void wm_accept(int lfd);
void wm_client(Win *w);
void wm_compose(Canvas *c);
int wm_mouse(IEv *e);           // returns 1 if handled
void wm_key(IEv *e);
void wm_focus(Win *w);
void wm_close(Win *w);
void wm_minimize(Win *w);
void wm_restore(Win *w);
void wm_maximize(Win *w);
void wm_snap(Win *w, float x, float y, float ww, float hh, const char *name);
void wm_broadcast_theme(void);
void wm_update_dock_autohide(void);
void wm_set_scale(int percent);
void wm_tick(void);
int wm_animating(void);
Win *wm_top(void);
Win *win_at(float x, float y);
void launch(const char *exe, const char *arg);
extern char *clipboard;

// shell.c
enum { PH_BOOT, PH_LOCK, PH_DESK };
extern int phase;
extern int dirty;
extern int dock_autohide;
extern int ui_scale_percent;
extern int dock_app_mask;
extern Canvas *wall, *wall_blur;
extern int start_open, widgets_open, alttab_open, clock_flyout_open;
void shell_init(void);
void shell_draw(Canvas *c);
int shell_mouse(IEv *e);
int shell_key(IEv *e);
void shell_tick(void);
void shell_suspend_after_draw(void);
int shell_animating(void);
float taskbar_icon_x(const char *exe);
void settings_save(void);
void recent_add(const char *path);
void close_panels(void);

// panels.c (widgets board)
void panels_init(void);
void widgets_draw(Canvas *c, float t);
int widgets_mouse(IEv *e, float t);
int widgets_key(IEv *e);
void panels_tick(void);

// data (data.c): weather, system stats
typedef struct { int ok; float temp, tmax, tmin, wind; int code; char place[32], cond[48]; double fetched; } Weather;
extern Weather weather;
extern float cpu_pct, mem_pct; extern long mem_total_kb, mem_used_kb;
void data_init(void);
void data_tick(void);
const char *weather_desc(int code);
int weather_glyph(int code);

// hit regions (shell.c): registered while drawing, queried by the mouse handlers
void hit_add(int id, float x, float y, float w, float h);
int hit_at(float x, float y);
extern int hover_id;
int wm_grabbing(void);
int wm_cursor(float x, float y);
void crossfade_begin(void);
int crossfading(void);
void lock_now(void);
// panel slide state 0..1
extern float start_p, widgets_p;
