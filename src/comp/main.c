// w12-compositor main loop: one process owns the display (DRM/KMS dumb buffers, page
// flips) and input (evdev). It composites app windows from shared memory, draws the
// shell on top, then a software cursor, at up to 60 fps and only when something changed.
#include "comp.h"
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void draw_cursor(Canvas *c, float x, float y) {
    float p[] = { x, y, x, y + 21, x + 5, y + 16.5f, x + 9, y + 25, x + 12.5f, y + 23.5f, x + 8.8f, y + 15.4f, x + 15, y + 15 };
    // outline first (slightly larger), then the fill; two convex halves keep g_poly convex
    float o = 1.3f;
    float a1[] = { x - o, y - o * 2, x - o, y + 21 + o * 1.5f, x + 5, y + 16.5f + o, x + 15 + o * 2, y + 15 + o * .5f };
    float a2[] = { x + 5 - o, y + 16.5f - o, x + 9 - o, y + 25 + o, x + 12.5f + o, y + 23.5f + o, x + 8.8f + o, y + 15.4f - o };
    g_poly(c, a1, 4, 0xFF000000, 0xFF000000, 0, 0, 1, 1);
    g_poly(c, a2, 4, 0xFF000000, 0xFF000000, 0, 0, 1, 1);
    float f1[] = { p[0], p[1], p[2], p[3], p[4], p[5], p[12], p[13] };
    float f2[] = { p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11] };
    g_poly(c, f1, 4, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 1, 1);
    g_poly(c, f2, 4, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 1, 1);
}

static void draw_resize_cursor(Canvas *c, float x, float y, int kind) {
    float dx = kind == 1 ? 1 : kind == 2 ? 0 : 0.7071f, dy = kind == 1 ? 0 : kind == 2 ? 1 : (kind == 3 ? 0.7071f : -0.7071f);
    float L = 11, hx = -dy, hy = dx;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t col = pass ? 0xFFFFFFFF : 0xFF000000;
        float w = pass ? 1.6f : 4.2f, a = pass ? 0 : 1.4f;
        g_line(c, x - dx * L, y - dy * L, x + dx * L, y + dy * L, w, col);
        for (int s = -1; s <= 1; s += 2) {
            float tx = x + s * dx * (L + a), ty = y + s * dy * (L + a), bx = x + s * dx * (L - 6), by = y + s * dy * (L - 6);
            float tri[6] = { tx, ty, bx + hx * (5 + a), by + hy * (5 + a), bx - hx * (5 + a), by - hy * (5 + a) };
            g_poly(c, tri, 3, col, col, 0, 0, 1, 1);
        }
    }
}

// theme crossfade: snapshot the old frame, fade it out over the new one
static Canvas *scene, *fade_from;
static double fade_t = -1;
void crossfade_begin(void) {
    if (!scene) return;
    if (!fade_from) fade_from = canvas_new(SCR_W, SCR_H);
    memcpy(fade_from->px, scene->px, (size_t)SCR_W * SCR_H * 4);
    fade_t = now();
}
int crossfading(void) { return fade_t >= 0; }

static int listen_sock(void) {
    int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", W12_SOCK);
    unlink(W12_SOCK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 16)) { LOG("comp: socket failed (%s)", strerror(errno)); return -1; }
    chmod(W12_SOCK, 0666);
    return s;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    LOG("comp: Windows 12 compositor starting");
    Canvas *scr = drm_init();
    if (!scr) { MARK("ERROR no display"); sleep(3); return 1; }
    scene = canvas_new(SCR_W, SCR_H);
    font_init();
    input_init();
    int lfd = listen_sock();
    shell_init();
    data_init();
    panels_init();
    wm_init();
    double stat_t = now(), worst = 0, work_sum = 0;
    int frames = 0, composes = 0;
    float cx = -1, cy = -1;
    double next = now();
    for (;;) {
        // wait for input / clients, but never past the next frame deadline while animating
        struct pollfd pf[MAXWIN + 12];
        int np = 0;
        int ifd[8], ni = input_fds(ifd, 8);
        for (int i = 0; i < ni; i++) pf[np++] = (struct pollfd){ ifd[i], POLLIN, 0 };
        if (lfd >= 0) pf[np++] = (struct pollfd){ lfd, POLLIN, 0 };
        int wmap[MAXWIN + 12];
        for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].fd >= 0) { wmap[np] = i; pf[np++] = (struct pollfd){ wins[i].fd, POLLIN, 0 }; }
        bool busy = dirty || wm_animating() || shell_animating() || crossfading();
        int timeout = busy ? maxi(0, (int)((next - now()) * 1000)) : 50;
        poll(pf, np, timeout);
        if (lfd >= 0 && pf[ni].revents & POLLIN) wm_accept(lfd);
        for (int i = ni + (lfd >= 0); i < np; i++) if (pf[i].revents) wm_client(&wins[wmap[i]]);
        IEv ev[64];
        int n = input_poll(ev, 64);
        for (int i = 0; i < n; i++) {
            IEv *e = &ev[i];
            if (e->type == IE_KEY || e->type == IE_KEYUP) { if (!shell_key(e)) wm_key(e); continue; }
            if (wm_grabbing()) { wm_mouse(e); continue; }
            if (!shell_mouse(e)) wm_mouse(e);
        }
        wm_tick();
        shell_tick();
        data_tick();
        panels_tick();
        double t = now();
        if (t < next && !(n > 0)) continue;   // pace frames to 60 Hz; input still wakes us
        if (t < next) continue;
        next = t + 1.0 / 60;
        bool moved = mouse_x != cx || mouse_y != cy;
        if (!(dirty || wm_animating() || shell_animating() || crossfading() || moved)) continue;
        double t0 = now();
        if (dirty || wm_animating() || shell_animating() || crossfading()) {
            dirty = 0;
            if (phase != PH_BOOT && !(phase == PH_LOCK && shell_animating() == 0)) wm_compose(scene);
            else if (phase == PH_LOCK) wm_compose(scene);
            shell_draw(scene);
            if (fade_t >= 0) {
                float k = (float)(now() - fade_t) / 0.4f;
                if (k >= 1) fade_t = -1;
                else g_buf(scene, fade_from->px, SCR_W, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0, 0, (int)(255 * (1 - ease_out(k))), 1);
            }
            composes++;
        }
        memcpy(scr->px, scene->px, (size_t)SCR_W * SCR_H * 4);
        if (phase != PH_BOOT) {
            int ck = phase == PH_DESK ? wm_cursor(mouse_x, mouse_y) : 0;
            if (ck) draw_resize_cursor(scr, mouse_x, mouse_y, ck); else draw_cursor(scr, mouse_x, mouse_y);
        }
        cx = mouse_x; cy = mouse_y;
        double work = now() - t0;
        work_sum += work;
        if (work > worst) worst = work;
        scr = drm_flip();
        frames++;
        if (t - stat_t >= 10) {
            LOG("perf: %d frames (%d composed) in %.0f s, avg work %.1f ms, worst %.1f ms", frames, composes, t - stat_t, work_sum * 1000 / maxi(frames, 1), worst * 1000);
            frames = composes = 0; worst = work_sum = 0; stat_t = t;
        }
    }
}
