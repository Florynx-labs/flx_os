// Window manager + protocol server: accepts app connections on /run/w12.sock, maps their
// memfd buffers, composites windows with rounded corners, soft shadows and Mica titlebars,
// and implements focus, drag, resize, minimize/maximize, snap layouts and animations.
#include "comp.h"
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

Win wins[MAXWIN];
int zord[MAXWIN], nz;
Win *focus;
char *clipboard;
static int next_id = 1, cascade;
#define TOP 36             // top bar height
#define WORK_Y0 TOP
#define WORK_Y1 1000       // above the floating taskbar
#define GAP 8
enum { DR_NONE, DR_MOVE, DR_RESIZE, DR_CONTENT };
static int drag, rz_edges, drag_btn;
static Win *dw;            // window being dragged / grabbed
static float gx, gy, ox, oy, ow, oh;
static Win *hover_win;
static int snap_prev; static float spx, spy, spw, sph;   // drag-to-edge preview
static int flyout_open; static Win *flyout_win; static double flyout_t;
static Win *hover_max_win;   // mouse resting on a maximize button (opens the snap flyout)

static float rad(Win *w) { return w->state == 1 ? 0 : 8; }
static int idx(Win *w) { return (int)(w - wins); }

void wm_init(void) { (void)0; }

int wm_animating(void) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].anim) return 1;
    return snap_prev || flyout_open;
}
Win *wm_top(void) {
    for (int i = nz - 1; i >= 0; i--) { Win *w = &wins[zord[i]]; if (w->mapped && !w->minimized && !w->closing) return w; }
    return 0;
}

static void z_remove(Win *w) {
    int k = idx(w), j = 0;
    for (int i = 0; i < nz; i++) if (zord[i] != k) zord[j++] = zord[i];
    nz = j;
}
static void z_top(Win *w) { z_remove(w); zord[nz++] = idx(w); }

void wm_focus(Win *w) {
    if (focus == w) { if (w) z_top(w); return; }
    if (focus && focus->used && !focus->closing) msg_send(focus->fd, M_FOCUS, 0, 0, 0, 0, 0, 0, -1);
    focus = w;
    if (w) {
        z_top(w);
        msg_send(w->fd, M_FOCUS, 1, 0, 0, 0, 0, 0, -1);
        MARK("FOCUS %s %d", w->exe, w->id);
    }
    dirty = 1;
}
static void focus_next(void) {
    focus = 0;
    Win *t = wm_top();
    if (t) wm_focus(t);
}

static void configure(Win *w) { msg_send(w->fd, M_CONFIGURE, (int)w->w, (int)w->h, w->state, 0, 0, 0, -1); }
static void start_anim(Win *w, int kind) {
    w->ax = w->x; w->ay = w->y; w->aw = w->w; w->ah = w->h;
    w->anim = kind; w->anim_t = now(); dirty = 1;
}
static void set_rect(Win *w, float x, float y, float ww, float hh, int state) {
    start_anim(w, AN_MOVE);
    w->x = roundf(x); w->y = roundf(y); w->w = roundf(fmaxf(ww, 200)); w->h = roundf(fmaxf(hh, 120)); w->state = state;
    configure(w);
}

void wm_maximize(Win *w) {
    if (w->state == 1) { set_rect(w, w->rx, w->ry, w->rw, w->rh, 0); MARK("RESTORE %s", w->exe); return; }
    if (w->state == 0) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; }
    set_rect(w, 0, WORK_Y0 + TB, SCR_W, WORK_Y1 - WORK_Y0 - TB, 1);
    MARK("MAXIMIZE %s", w->exe);
}
void wm_snap(Win *w, float x, float y, float ww, float hh, const char *name) {
    if (w->state == 0) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; }
    set_rect(w, x, y + TB, ww, hh - TB, 2);
    MARK("SNAP %s %s %d %d %d %d", w->exe, name, (int)w->x, (int)w->y, (int)w->w, (int)w->h);
}
static void snap_frac(Win *w, float fx, float fy, float fw, float fh, const char *name) {
    float W = SCR_W - 2 * GAP, H = WORK_Y1 - WORK_Y0 - 2 * GAP;
    float x = GAP + fx * W, y = WORK_Y0 + GAP + fy * H, ww = fw * W, hh = fh * H;
    if (fx > 0) { x += GAP / 2; ww -= GAP / 2; }
    if (fx + fw < 0.999f) ww -= GAP / 2;
    if (fy > 0) { y += GAP / 2; hh -= GAP / 2; }
    if (fy + fh < 0.999f) hh -= GAP / 2;
    wm_snap(w, x, y, ww, hh, name);
}
void wm_minimize(Win *w) {
    if (w->minimized) return;
    w->minimized = 1;
    start_anim(w, AN_MIN);
    MARK("MINIMIZE %s", w->exe);
    if (focus == w) focus_next();
}
void wm_restore(Win *w) {
    if (!w->minimized) { wm_focus(w); return; }
    w->minimized = 0;
    start_anim(w, AN_UNMIN);
    wm_focus(w);
}
void wm_close(Win *w) { msg_send(w->fd, M_CLOSE_REQ, 0, 0, 0, 0, 0, 0, -1); }

void launch(const char *exe, const char *arg) {
    // single-instance apps: focus the running one
    if (!strcmp(exe, "settings") || !strcmp(exe, "taskmgr") || !strcmp(exe, "calc") || !strcmp(exe, "minesweeper")) {
        for (int i = 0; i < MAXWIN; i++) if (wins[i].used && !wins[i].closing && !strcmp(wins[i].exe, exe)) { wm_restore(&wins[i]); return; }
    }
    char path[64];
    snprintf(path, sizeof path, "/bin/%s", exe);
    MARK("LAUNCH %s", exe);
    pid_t p = fork();
    if (p == 0) {
        sigset_t s; sigemptyset(&s); sigprocmask(SIG_SETMASK, &s, 0);
        chdir("/home/user");
        if (arg && *arg) execl(path, exe, arg, (char *)0);
        else execl(path, exe, (char *)0);
        _exit(127);
    }
}

void wm_broadcast_theme(void) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && !wins[i].closing) msg_send(wins[i].fd, M_THEME, TH.dark, TH.accent_i, TH.wall, 0, 0, 0, -1);
    dirty = 1;
}

// ---- connections ----------------------------------------------------------------------------
void wm_accept(int lfd) {
    int fd = accept4(lfd, 0, 0, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return;
    for (int i = 0; i < MAXWIN; i++) if (!wins[i].used) {
        memset(&wins[i], 0, sizeof wins[i]);
        wins[i].used = 1; wins[i].fd = fd; wins[i].front = -1; wins[i].id = next_id++;
        return;
    }
    close(fd);
}

static void destroy(Win *w) {
    if (w->shm) munmap(w->shm, (size_t)2 * BUF_W * BUF_H * 4);
    z_remove(w);
    if (hover_win == w) hover_win = 0;
    if (dw == w) { dw = 0; drag = DR_NONE; }
    if (flyout_win == w) { flyout_open = 0; flyout_win = 0; }
    if (hover_max_win == w) hover_max_win = 0;
    memset(w, 0, sizeof *w);
    dirty = 1;
}

static void gone(Win *w) {
    close(w->fd);
    w->fd = -1;
    if (!w->mapped) { if (w->exe[0]) MARK("WINDOW_CLOSE %s %d", w->exe, w->id); destroy(w); return; }
    MARK("WINDOW_CLOSE %s %d", w->exe, w->id);
    w->closing = 1;
    start_anim(w, AN_CLOSE);
    if (focus == w) focus_next();
}

void wm_client(Win *w) {
    Msg m;
    int r, mfd;
    while (w->fd >= 0 && (r = msg_recv(w->fd, &m, &mfd)) > 0) {
        switch (m.type) {
        case M_HELLO: {
            if (mfd >= 0) {
                void *p = mmap(0, (size_t)2 * BUF_W * BUF_H * 4, PROT_READ, MAP_SHARED, mfd, 0);
                close(mfd);
                if (p != MAP_FAILED) w->shm = p;
            }
            w->pid = m.a; w->mica = m.d & 1;
            snprintf(w->exe, sizeof w->exe, "%s", m.s);
            w->app = app_by_exe(w->exe);
            snprintf(w->title, sizeof w->title, "%s", w->app >= 0 ? APPS[w->app].title : w->exe);
            w->w = mini(maxi(m.b, 200), SCR_W - 40); w->h = mini(maxi(m.c, 120), WORK_Y1 - WORK_Y0 - TB - 40);
            w->x = roundf((SCR_W - w->w) / 2 + (cascade % 6) * 32 - 80);
            w->y = roundf(WORK_Y0 + TB + 40 + (cascade % 6) * 30);
            if (w->y + w->h > WORK_Y1 - 10) w->y = fmaxf(WORK_Y0 + TB + 8, WORK_Y1 - 10 - w->h);
            if (w->x < 8) w->x = 8;
            cascade++;
            msg_send(w->fd, M_THEME, TH.dark, TH.accent_i, TH.wall, 0, 0, 0, -1);
            configure(w);
            z_top(w);
            MARK("WINDOW_OPEN %s %d", w->exe, w->id);
            break;
        }
        case M_COMMIT: {
            if (!w->shm || m.a < 0 || m.a > 1) break;
            int old = w->front;
            w->front = m.a; w->bw = mini(m.b, BUF_W); w->bh = mini(m.c, BUF_H);
            if (old >= 0 && old != m.a) msg_send(w->fd, M_RELEASE, old, 0, 0, 0, 0, 0, -1);
            if (!w->mapped) { w->mapped = 1; start_anim(w, AN_OPEN); wm_focus(w); MARK("WINDOW_MAPPED %s %d %d %d %d %d", w->exe, w->id, (int)w->x, (int)w->y, (int)w->w, (int)w->h); }
            dirty = 1;
            break;
        }
        case M_TITLE: snprintf(w->title, sizeof w->title, "%s", m.s); dirty = 1; break;
        case M_LAUNCH: {
            char *tab = strchr(m.s, '\t');
            if (tab) *tab++ = 0;
            launch(m.s, tab);
            break;
        }
        case M_CLIP_SET: free(clipboard); clipboard = xstrdup(m.s); MARK("CLIPBOARD %zu", strlen(clipboard)); break;
        case M_CLIP_GET: msg_send(w->fd, M_CLIP_DATA, 0, 0, 0, 0, 0, clipboard ? clipboard : "", -1); break;
        case M_SET_THEME:
            theme_set(m.a, m.b, m.c);
            settings_save();
            wm_broadcast_theme();
            MARK("THEME dark=%d accent=%d wall=%d", TH.dark, TH.accent_i, TH.wall);
            break;
        case M_CLOSE_ME: wm_close(w); break;
        case M_POWER:
            MARK("POWER %s", m.a ? "restart" : "shutdown");
            settings_save();
            sync();
            exit(m.a ? 42 : 43);
        case M_RECENT: recent_add(m.s); break;
        }
    }
    if (w->fd >= 0 && r == 0) gone(w);
}

void wm_tick(void) {
    int st;
    while (waitpid(-1, &st, WNOHANG) > 0) {}
    double t = now();
    for (int i = 0; i < MAXWIN; i++) {
        Win *w = &wins[i];
        if (!w->used || !w->anim) continue;
        float dur = w->anim == AN_CLOSE ? 0.16f : w->anim == AN_MIN || w->anim == AN_UNMIN ? 0.24f : w->anim == AN_MOVE ? 0.18f : 0.2f;
        if (t - w->anim_t >= dur) {
            if (w->anim == AN_CLOSE) { destroy(w); continue; }
            w->anim = AN_NONE;
            dirty = 1;
        }
    }
    if (hover_max_win && !flyout_open && hover_max_win->used && hover_max_win->hover_max_t && t - hover_max_win->hover_max_t > 0.45) {
        flyout_open = 1; flyout_win = hover_max_win; flyout_t = t; dirty = 1;
        MARK("SNAP_FLYOUT %s", flyout_win->exe);
    }
    if (flyout_open && flyout_win) {
        Win *w = flyout_win;
        float fx = w->x + w->w - 92 - 120, fy = w->y, fw = 300, fh = 104;
        bool in_btn = mouse_x >= w->x + w->w - 92 && mouse_x < w->x + w->w - 46 && mouse_y >= w->y - TB && mouse_y < w->y;
        bool in_fly = mouse_x >= fx && mouse_x < fx + fw && mouse_y >= fy - 4 && mouse_y < fy + fh;
        if (in_btn || in_fly) flyout_t = t;
        else if (t - flyout_t > 0.35) { flyout_open = 0; dirty = 1; }
    }
}

// ---- hit testing -------------------------------------------------------------------------------
static int frame_hit(Win *w, float x, float y) {   // 0 none, 1 inside frame, edges bitmask << 1 for resize border
    float fx = w->x, fy = w->y - TB, fw = w->w, fh = w->h + TB;
    if (w->state == 1) return (x >= fx && x < fx + fw && y >= fy && y < fy + fh) ? 1 : 0;
    float b = 6;
    if (x < fx - b || x >= fx + fw + b || y < fy - b || y >= fy + fh + b) return 0;
    int e = 0;
    if (x < fx + 3) e |= 1;
    if (x >= fx + fw - 3) e |= 2;
    if (y < fy + 3) e |= 4;
    if (y >= fy + fh - 3) e |= 8;
    return e ? e << 1 : 1;
}
Win *win_at(float x, float y) {
    for (int i = nz - 1; i >= 0; i--) {
        Win *w = &wins[zord[i]];
        if (!w->mapped || w->minimized || w->closing) continue;
        if (frame_hit(w, x, y)) return w;
    }
    return 0;
}

static void send_mouse(Win *w, IEv *e, int ev) {
    int btn = e->type == IE_WHEEL ? e->wheel : e->btn;
    msg_send(w->fd, M_MOUSE, (int)(e->x - w->x), (int)(e->y - w->y), mouse_buttons, ev, btn, 0, -1);
}

static const float LAYOUTS[4][4][4] = {
    { { 0, 0, .5f, 1 }, { .5f, 0, .5f, 1 } },
    { { 0, 0, .66f, 1 }, { .66f, 0, .34f, 1 } },
    { { 0, 0, .333f, 1 }, { .333f, 0, .334f, 1 }, { .667f, 0, .333f, 1 } },
    { { 0, 0, .5f, .5f }, { .5f, 0, .5f, .5f }, { 0, .5f, .5f, .5f }, { .5f, .5f, .5f, .5f } },
};
static const int NZONES[4] = { 2, 2, 3, 4 };
static void zone_rect(int l, int z, float *x, float *y, float *w, float *h) {   // flyout geometry
    Win *fw = flyout_win;
    float fx = fw->x + fw->w - 92 - 120, fy = fw->y;
    float cx = fx + 12 + l * 72, cy = fy + 14, cw = 64, ch = 44;
    const float *L = LAYOUTS[l][z];
    *x = cx + L[0] * cw + 1.5f; *y = cy + L[1] * ch + 1.5f; *w = L[2] * cw - 3; *h = L[3] * ch - 3;
}

int wm_grabbing(void) { return drag != DR_NONE; }

// cursor shape: 0 arrow, 1 left-right, 2 up-down, 3 NW-SE, 4 NE-SW
int wm_cursor(float x, float y) {
    int e = 0;
    if (drag == DR_RESIZE) e = rz_edges;
    else if (drag == DR_NONE) { Win *w = win_at(x, y); int h = w ? frame_hit(w, x, y) : 0; if (h > 1) e = h >> 1; }
    if (!e) return 0;
    bool hz = e & 3, vt = e & 12;
    if (hz && vt) return ((e & 1) && (e & 4)) || ((e & 2) && (e & 8)) ? 3 : 4;
    return hz ? 1 : 2;
}

int wm_mouse(IEv *e) {
    float x = e->x, y = e->y;
    // snap flyout (drawn above windows)
    if (flyout_open && flyout_win && (e->type == IE_DOWN || e->type == IE_MOVE)) {
        for (int l = 0; l < 4; l++)
            for (int z = 0; z < NZONES[l]; z++) {
                float zx, zy, zw, zh;
                zone_rect(l, z, &zx, &zy, &zw, &zh);
                if (x >= zx && x < zx + zw && y >= zy && y < zy + zh) {
                    if (e->type == IE_DOWN) {
                        const float *L = LAYOUTS[l][z];
                        char nm[32];
                        snprintf(nm, sizeof nm, "layout%d.zone%d", l, z);
                        snap_frac(flyout_win, L[0], L[1], L[2], L[3], nm);
                        flyout_open = 0;
                    }
                    dirty = 1;
                    return 1;
                }
            }
    }
    if (drag == DR_CONTENT && dw) {
        if (e->type == IE_MOVE) send_mouse(dw, e, EV_MOVE);
        else if (e->type == IE_UP) { send_mouse(dw, e, EV_UP); if (!mouse_buttons) { drag = DR_NONE; dw = 0; } }
        else if (e->type == IE_DOWN) send_mouse(dw, e, EV_DOWN);
        return 1;
    }
    if (drag == DR_MOVE && dw) {
        if (e->type == IE_MOVE) {
            if (dw->state != 0) {   // pull a maximized/snapped window off: restore under the cursor
                float rel = (x - dw->x) / dw->w;
                dw->state = 0;
                dw->w = dw->rw; dw->h = dw->rh;
                dw->x = roundf(x - rel * dw->w);
                ox = dw->x; gx = x;
                configure(dw);
            }
            dw->x = roundf(ox + x - gx); dw->y = roundf(fmaxf(WORK_Y0 + TB, oy + y - gy));
            // drag-to-edge snap preview
            int p = 0;
            if (x <= 2 && y < WORK_Y0 + 160) p = 5; else if (x <= 2 && y > WORK_Y1 - 160) p = 7;
            else if (x >= SCR_W - 3 && y < WORK_Y0 + 160) p = 6; else if (x >= SCR_W - 3 && y > WORK_Y1 - 160) p = 8;
            else if (x <= 2) p = 1; else if (x >= SCR_W - 3) p = 2; else if (y <= WORK_Y0 + 2) p = 3;
            snap_prev = p;
            float W = SCR_W, H = WORK_Y1 - WORK_Y0;
            if (p == 1) { spx = GAP; spy = WORK_Y0 + GAP; spw = W / 2 - 1.5f * GAP; sph = H - 2 * GAP; }
            if (p == 2) { spx = W / 2 + GAP / 2; spy = WORK_Y0 + GAP; spw = W / 2 - 1.5f * GAP; sph = H - 2 * GAP; }
            if (p == 3) { spx = 0; spy = WORK_Y0; spw = W; sph = H; }
            if (p >= 5) { spx = (p == 5 || p == 7) ? GAP : W / 2 + GAP / 2; spy = (p <= 6) ? WORK_Y0 + GAP : WORK_Y0 + H / 2 + GAP / 2; spw = W / 2 - 1.5f * GAP; sph = H / 2 - 1.5f * GAP; }
            dirty = 1;
        } else if (e->type == IE_UP) {
            Win *w = dw;
            drag = DR_NONE; dw = 0;
            if (snap_prev == 3) { w->rx = ox; w->ry = oy; wm_maximize(w); }
            else if (snap_prev) {
                static const char *nm[] = { "", "left", "right", "max", "", "topleft", "topright", "bottomleft", "bottomright" };
                if (w->state == 0) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; }
                wm_snap(w, spx, spy, spw, sph, nm[snap_prev]);
            } else MARK("MOVE %s %d %d", w->exe, (int)w->x, (int)w->y);
            snap_prev = 0;
            dirty = 1;
        }
        return 1;
    }
    if (drag == DR_RESIZE && dw) {
        if (e->type == IE_MOVE) {
            float dx = x - gx, dy = y - gy, nx = ox, ny = oy, nw = ow, nh = oh;
            if (rz_edges & 1) { nx = ox + dx; nw = ow - dx; }
            if (rz_edges & 2) nw = ow + dx;
            if (rz_edges & 4) { ny = oy + dy; nh = oh - dy; }
            if (rz_edges & 8) nh = oh + dy;
            if (nw < 320) { if (rz_edges & 1) nx -= 320 - nw; nw = 320; }
            if (nh < 200) { if (rz_edges & 4) ny -= 200 - nh; nh = 200; }
            dw->x = roundf(nx); dw->y = roundf(ny); dw->w = roundf(nw); dw->h = roundf(nh);
            dw->state = 0;
            configure(dw);
            dirty = 1;
        } else if (e->type == IE_UP) { MARK("RESIZE %s %dx%d", dw->exe, (int)dw->w, (int)dw->h); drag = DR_NONE; dw = 0; }
        return 1;
    }

    Win *w = win_at(x, y);
    if (e->type == IE_MOVE) {
        if (hover_win && hover_win != w && hover_win->used && !hover_win->closing)
            msg_send(hover_win->fd, M_MOUSE, -10000, -10000, 0, EV_LEAVE, 0, 0, -1);
        hover_win = w;
        static int last_btn_hover;
        int bh = 0;
        if (w && y >= w->y - TB && y < w->y && x >= w->x + w->w - 138) bh = 1 + (int)((w->x + w->w - x) / 46);
        if (bh != last_btn_hover) { last_btn_hover = bh; dirty = 1; }
        if (w && bh == 2) { if (!w->hover_max_t) w->hover_max_t = now(); hover_max_win = w; }
        else { if (w) w->hover_max_t = 0; hover_max_win = 0; }
        if (w && y >= w->y && frame_hit(w, x, y) == 1) send_mouse(w, e, EV_MOVE);
        return w != 0;
    }
    if (!w) return 0;
    if (e->type == IE_WHEEL) { send_mouse(w, e, EV_WHEEL); return 1; }
    if (e->type != IE_DOWN) { if (e->type == IE_UP && y >= w->y) send_mouse(w, e, EV_UP); return 1; }
    if (focus != w) wm_focus(w);
    int fh = frame_hit(w, x, y);
    if (fh > 1 && e->btn == 1) {
        drag = DR_RESIZE; dw = w; rz_edges = fh >> 1; gx = x; gy = y; ox = w->x; oy = w->y; ow = w->w; oh = w->h;
        if (rz_edges & 4) oy = w->y;
        // resize edges work in content coordinates; the top edge moves the titlebar
        return 1;
    }
    if (y < w->y) {   // titlebar
        float rx = w->x + w->w - x;
        if (e->btn == 1 && rx < 46) { wm_close(w); return 1; }
        if (e->btn == 1 && rx < 92) { flyout_open = 0; wm_maximize(w); return 1; }
        if (e->btn == 1 && rx < 138) { wm_minimize(w); return 1; }
        static double last_t; static Win *last_w;
        if (e->btn == 1 && last_w == w && now() - last_t < 0.4) { last_t = 0; wm_maximize(w); return 1; }
        last_t = now(); last_w = w;
        if (e->btn == 1) { drag = DR_MOVE; dw = w; gx = x; gy = y; ox = w->x; oy = w->y; }
        return 1;
    }
    drag = DR_CONTENT; dw = w; drag_btn = e->btn;
    send_mouse(w, e, EV_DOWN);
    return 1;
}

void wm_key(IEv *e) {
    if (e->type != IE_KEY || !e->key) return;
    Win *w = focus;
    if (!w || w->closing || w->minimized) return;
    if ((e->mods & MOD_SUPER) && e->key == K_LEFT) { snap_frac(w, 0, 0, .5f, 1, "left"); return; }
    if ((e->mods & MOD_SUPER) && e->key == K_RIGHT) { snap_frac(w, .5f, 0, .5f, 1, "right"); return; }
    if ((e->mods & MOD_SUPER) && e->key == K_UP) { if (w->state != 1) wm_maximize(w); return; }
    if ((e->mods & MOD_SUPER) && e->key == K_DOWN) { if (w->state) set_rect(w, w->rx, w->ry, w->rw, w->rh, 0); else wm_minimize(w); return; }
    if ((e->mods & MOD_ALT) && e->key == K_F4) { wm_close(w); return; }
    msg_send(w->fd, M_KEY, e->key, e->mods, 0, 0, 0, e->text, -1);
}

// ---- compositing ------------------------------------------------------------------------------------
static void anim_rect(Win *w, float *x, float *y, float *ww, float *hh, int *alpha) {
    *x = w->x; *y = w->y - TB; *ww = w->w; *hh = w->h + TB; *alpha = 255;
    if (!w->anim) return;
    float dur = w->anim == AN_CLOSE ? 0.16f : w->anim == AN_MIN || w->anim == AN_UNMIN ? 0.24f : w->anim == AN_MOVE ? 0.18f : 0.2f;
    float t = clampf((now() - w->anim_t) / dur, 0, 1), k;
    float cx = *x + *ww / 2, cy = *y + *hh / 2;
    switch (w->anim) {
    case AN_OPEN: case AN_CLOSE: {
        k = w->anim == AN_OPEN ? ease_out(t) : 1 - t;
        float s = 0.92f + 0.08f * k;
        *ww *= s; *hh *= s; *x = cx - *ww / 2; *y = cy - *hh / 2 + (1 - k) * 18; *alpha = (int)(255 * k);
        break;
    }
    case AN_MIN: case AN_UNMIN: {
        k = w->anim == AN_MIN ? ease_inout(t) : 1 - ease_inout(t);
        float tx = taskbar_icon_x(w->exe), ty = 1040;
        float s = 1 - 0.85f * k;
        float nw = *ww * s, nh = *hh * s;
        *x = lerpf(*x, tx - nw / 2, k); *y = lerpf(*y, ty - nh / 2, k); *ww = nw; *hh = nh; *alpha = (int)(255 * (1 - k * 0.9f));
        break;
    }
    case AN_MOVE:
        k = ease_out(t);
        *x = lerpf(w->ax, w->x, k); *y = lerpf(w->ay - TB, w->y - TB, k); *ww = lerpf(w->aw, w->w, k); *hh = lerpf(w->ah + TB, w->h + TB, k);
        break;
    }
}

// pre-blurred wallpaper under a screen rect (Mica), clipped to the screen
static void mica_under(Canvas *c, float x, float y, float w, float h, float rt, float rb, int al) {
    int x0 = maxi(0, (int)floorf(x)), y0 = maxi(0, (int)floorf(y));
    int x1 = mini(SCR_W, (int)ceilf(x + w)), y1 = mini(SCR_H, (int)ceilf(y + h));
    if (x1 <= x0 || y1 <= y0) return;
    bool clipped = x0 > x || y0 > y || x1 < x + w || y1 < y + h;
    g_buf(c, wall_blur->px + (size_t)y0 * SCR_W + x0, SCR_W, x1 - x0, y1 - y0, x0, y0, x1 - x0, y1 - y0, clipped ? 0 : rt, clipped ? 0 : rb, al, 1);
}

static void draw_window(Canvas *c, Win *w) {
    float fx, fy, fw, fh;
    int al;
    anim_rect(w, &fx, &fy, &fw, &fh, &al);
    if (w->minimized && !w->anim) return;
    if (al <= 2) return;
    bool foc = w == focus;
    float r = rad(w), sc = fw / w->w, tb = TB * sc;
    if (w->state != 1) g_shadow(c, fx, fy, fw, fh, r, foc ? 34 : 20, (int)((foc ? 95 : 55) * al / 255), foc ? 8 : 4);
    // Mica titlebar: the pre-blurred wallpaper under it, tinted with the theme
    mica_under(c, fx, fy, fw, tb, r, 0, al);
    uint32_t tint = TH.dark ? (foc ? 0xD8202024 : 0xEC2A2A2C) : (foc ? 0xD4EEF0F4 : 0xEAF6F6F6);
    g_rrect(c, fx, fy, fw, tb, r, 0, c_alpha(tint, (tint >> 24) / 255.0f * al / 255.0f));
    // content
    float cx = fx, cy = fy + tb, cw = fw, ch = fh - tb;
    if (w->front >= 0 && w->shm) {
        const uint32_t *buf = w->shm + (size_t)w->front * BUF_W * BUF_H;
        if (w->mica)
            mica_under(c, cx, cy, cw, ch, 0, r, al);
        if (w->anim == AN_NONE || w->anim == AN_MOVE) {
            if (w->anim == AN_MOVE) g_buf(c, buf, BUF_W, w->bw, w->bh, cx, cy, cw, ch, 0, r, al, !w->mica);
            else {
                int dw_ = mini(w->bw, (int)cw), dh_ = mini(w->bh, (int)ch);
                if (dw_ < cw || dh_ < ch) g_rrect(c, cx, cy, cw, ch, 0, r, TH.bg);
                g_buf(c, buf, BUF_W, dw_, dh_, cx, cy, dw_, dh_, 0, (dw_ == (int)cw && dh_ == (int)ch) ? r : 0, al, !w->mica);
            }
        } else g_buf(c, buf, BUF_W, w->bw, w->bh, cx, cy, cw, ch, 0, r, al, !w->mica);
    } else g_rrect(c, cx, cy, cw, ch, 0, r, c_alpha(TH.bg, al / 255.0f));
    g_rrect_line(c, fx, fy, fw, fh, r, 1, TH.dark ? c_alpha(0xFF5A5A5A, al / 400.0f) : c_alpha(0xFF000000, al / 1400.0f));
    if (sc < 0.98f || al < 250) return;   // no chrome text while animating
    // title + icon
    uint32_t tc = foc ? TH.text : TH.dim;
    if (w->app >= 0) icon_app(c, w->app, fx + 14, fy + 11, 18);
    char *t = text_ellipsize(F_REG, 14, w->title, fw - 200);
    text(c, F_REG, 14, 0, fx + 42, fy + 25, tc, t);
    free(t);
    // caption buttons
    float bx = fx + fw - 138;
    for (int i = 0; i < 3; i++) {
        float x0 = bx + i * 46;
        bool hov = mouse_x >= x0 && mouse_x < x0 + 46 && mouse_y >= fy && mouse_y < fy + TB && win_at(mouse_x, mouse_y) == w;
        if (hov) {
            if (i == 2) g_rrect(c, x0, fy, 46, TB, 0, 0, 0xFFC42B1C);
            else g_rect(c, x0, fy, 46, TB, TH.hover);
            if (i == 2 && r > 0) g_rrect(c, x0, fy, 46, TB, r, 0, 0xFFC42B1C);
        }
        int g = i == 0 ? G_MIN : i == 1 ? (w->state == 1 ? G_RESTORE : G_MAX) : G_CLOSE;
        icon_glyph(c, g, x0 + 23, fy + TB / 2, 16, hov && i == 2 ? 0xFFFFFFFF : tc);
    }
}

static void draw_flyout(Canvas *c) {
    Win *w = flyout_win;
    float fx = w->x + w->w - 92 - 120, fy = w->y, fw = 300, fh = 72;
    g_shadow(c, fx, fy, fw, fh, 8, 24, 80, 6);
    g_glass(c, fx, fy, fw, fh, 8, TH.dark ? 0xC8282828 : 0xC8F6F6F6);
    g_rrect_line(c, fx, fy, fw, fh, 8, 1, TH.dark ? 0x40FFFFFF : 0x20000000);
    for (int l = 0; l < 4; l++)
        for (int z = 0; z < NZONES[l]; z++) {
            float zx, zy, zw, zh;
            zone_rect(l, z, &zx, &zy, &zw, &zh);
            bool hov = mouse_x >= zx && mouse_x < zx + zw && mouse_y >= zy && mouse_y < zy + zh;
            g_rrect(c, zx, zy, zw, zh, 3, 3, hov ? TH.accent : (TH.dark ? 0xFF4A4A4A : 0xFFD8D8D8));
        }
}

void wm_compose(Canvas *c) {
    g_buf(c, wall->px, SCR_W, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0, 0, 255, 1);
    if (snap_prev) {   // drag-to-edge preview, behind the window being dragged
        g_glass(c, spx, spy, spw, sph, snap_prev == 3 ? 0 : 8, c_alpha(TH.accent, 0.25f));
        g_rrect_line(c, spx, spy, spw, sph, snap_prev == 3 ? 0 : 8, 1.5f, c_alpha(TH.accent, 0.8f));
    }
    for (int i = 0; i < nz; i++) {
        Win *w = &wins[zord[i]];
        if (w->used && w->mapped) draw_window(c, w);
    }
    if (flyout_open && flyout_win && flyout_win->used && !flyout_win->closing) draw_flyout(c);
}
