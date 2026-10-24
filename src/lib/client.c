// libw12ui client side of the FLX OS window protocol.
// One window per app process: a Unix SOCK_SEQPACKET connection to the compositor for
// messages, and one memfd holding two BUF_W x BUF_H ARGB buffers (double buffering with
// explicit release, so the compositor never reads a buffer we are drawing into).
#include "w12.h"
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define HDR ((int)offsetof(Msg, s))

int msg_send(int fd, int type, int a, int b, int c, int d, int e, const char *s, int pass_fd) {
    Msg m;
    m.type = type; m.a = a; m.b = b; m.c = c; m.d = d; m.e = e;
    int sl = 0;
    if (s) { sl = strlen(s); if (sl > (int)sizeof m.s - 1) sl = sizeof m.s - 1; memcpy(m.s, s, sl); }
    m.s[sl] = 0;
    struct iovec iov = { &m, HDR + sl + 1 };
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    if (pass_fd >= 0) {
        memset(cbuf, 0, sizeof cbuf);
        mh.msg_control = cbuf; mh.msg_controllen = sizeof cbuf;
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &pass_fd, sizeof(int));
    }
    int r;
    do r = sendmsg(fd, &mh, MSG_NOSIGNAL); while (r < 0 && errno == EINTR);
    return r;
}

int msg_recv(int fd, Msg *m, int *fd_out) {
    struct iovec iov = { m, sizeof *m };
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
    if (fd_out) *fd_out = -1;
    int r;
    do r = recvmsg(fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC); while (r < 0 && errno == EINTR);
    if (r == 0) return 0;
    if (r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? -1 : 0;
    if (r < HDR + 1) { ((char *)m)[r < (int)sizeof *m ? r : (int)sizeof *m - 1] = 0; m->s[0] = 0; }
    else m->s[r - HDR - 1 < (int)sizeof m->s ? r - HDR - 1 : (int)sizeof m->s - 1] = 0;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            int f; memcpy(&f, CMSG_DATA(cm), sizeof f);
            if (fd_out) *fd_out = f; else close(f);
        }
    return r;
}

// ---- app runtime ------------------------------------------------------------------------------
UIState U;
Canvas *UC;
int app_focused;
static AppDef *A;
static int sock = -1;
static uint32_t *shm;
static int busy[2];
static Canvas scratch;
static int need;
static Msg pend[32];
static int npend;
static double last_down_t; static float last_down_x, last_down_y;

void app_redraw(void) { need = 1; }
void app_set_title(const char *t) { msg_send(sock, M_TITLE, 0, 0, 0, 0, 0, t, -1); }
void app_launch(const char *exe, const char *arg) {
    char b[1024];
    snprintf(b, sizeof b, "%s\t%s", exe, arg ? arg : "");
    msg_send(sock, M_LAUNCH, 0, 0, 0, 0, 0, b, -1);
}
void app_clip_set(const char *s) { msg_send(sock, M_CLIP_SET, 0, 0, 0, 0, 0, s, -1); }
void app_set_theme(int dark, int accent, int wall) { msg_send(sock, M_SET_THEME, dark, accent, wall, 0, 0, 0, -1); }
void app_set_dock(int autohide) { msg_send(sock, M_SET_DOCK, autohide, 0, 0, 0, 0, 0, -1); }
void app_set_font(int family) { msg_send(sock, M_SET_FONT, family, 0, 0, 0, 0, 0, -1); }
void app_set_scale(int percent) { msg_send(sock, M_SET_SCALE, percent, 0, 0, 0, 0, 0, -1); }
void app_set_dock_apps(int mask) { msg_send(sock, M_SET_DOCK_APPS, mask, 0, 0, 0, 0, 0, -1); }
void app_power(int restart) { msg_send(sock, M_POWER, restart, 0, 0, 0, 0, 0, -1); }
void app_recent(const char *path) { msg_send(sock, M_RECENT, 0, 0, 0, 0, 0, path, -1); }
void app_quit(void) { exit(0); }

static int wait_msg(Msg *m, int ms) {
    struct pollfd p = { sock, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) return -1;
    return msg_recv(sock, m, 0);
}
int app_auth_request(const char *request, char *response, size_t response_size) {
    if (!request || !response || response_size < 2 || msg_send(sock, M_AUTH_REQ, 0, 0, 0, 0, 0, request, -1) < 0) return -1;
    double deadline = now() + 10.0;
    while (now() < deadline) {
        Msg m; int r = wait_msg(&m, 250);
        if (r == 0) exit(0);
        if (r < 0) continue;
        if (m.type == M_AUTH_RES) { snprintf(response, response_size, "%s", m.s); memset(m.s, 0, sizeof m.s); return 0; }
        if (npend < 32) pend[npend++] = m;
    }
    return -1;
}
char *app_clip_get(void) {
    msg_send(sock, M_CLIP_GET, 0, 0, 0, 0, 0, 0, -1);
    double dl = now() + 0.5;
    while (now() < dl) {
        Msg m;
        int r = wait_msg(&m, 50);
        if (r == 0) exit(0);
        if (r < 0) continue;
        if (m.type == M_CLIP_DATA) return xstrdup(m.s);
        if (npend < 32) pend[npend++] = m;
    }
    return xstrdup("");
}

static void pass(void) {   // one immediate-mode pass into the private scratch canvas
    canvas_wrap(&scratch, scratch.px, U.w, U.h, BUF_W);
    UC = &scratch;
    U.t = now();
    U.consumed = 0;
    if (A->mica) {
        uint32_t m = TH.mica;
        for (int y = 0; y < U.h; y++) { uint32_t *r = scratch.px + (size_t)y * BUF_W; for (int x = 0; x < U.w; x++) r[x] = m; }
    } else g_clear(&scratch, TH.bg);
    A->draw(&scratch);
}

static void present(void) {
    int b = !busy[0] ? 0 : !busy[1] ? 1 : -1;
    if (b < 0) return;   // both in use: wait for a release, need stays set
    U.ev = EV_NONE;
    pass();
    uint32_t *dst = shm + (size_t)b * BUF_W * BUF_H;
    for (int y = 0; y < U.h; y++) memcpy(dst + (size_t)y * BUF_W, scratch.px + (size_t)y * BUF_W, (size_t)U.w * 4);
    busy[b] = 1;
    msg_send(sock, M_COMMIT, b, U.w, U.h, 0, 0, 0, -1);
    need = 0;
}

static void handle(Msg *m) {
    switch (m->type) {
    case M_CONFIGURE:
        if (m->a > 0 && m->b > 0 && m->a <= BUF_W && m->b <= BUF_H && (m->a != U.w || m->b != U.h)) { U.w = m->a; U.h = m->b; need = 1; }
        break;
    case M_RELEASE: if (m->a == 0 || m->a == 1) busy[m->a] = 0; break;
    case M_THEME: theme_set(m->a, m->b, m->c); need = 1; break;
    case M_FONT: font_set_family(m->a); need = 1; break;
    case M_FOCUS: app_focused = m->a; need = 1; break;
    case M_CLOSE_REQ:
        if (!A->close || A->close()) { LOG("%s: closing", A->exe); exit(0); }
        need = 1;
        break;
    case M_MOUSE: {
        U.mx = m->a; U.my = m->b; U.buttons = m->c; U.ev = m->d; U.btn = m->e; U.wheel = 0;
        if (U.ev == EV_WHEEL) { U.wheel = m->e / 120.0f; U.btn = 0; }
        if (U.ev == EV_DOWN) {
            U.px = U.mx; U.py = U.my;
            double t = now();
            U.clicks = (U.btn == 1 && t - last_down_t < 0.45 && fabsf(U.mx - last_down_x) < 6 && fabsf(U.my - last_down_y) < 6) ? 2 : 1;
            if (U.btn == 1) { last_down_t = U.clicks == 2 ? 0 : t; last_down_x = U.mx; last_down_y = U.my; }
        }
        if (U.ev != EV_MOVE || U.buttons) pass();   // plain hovering is handled by the present pass
        need = 1;
        break;
    }
    case M_KEY:
        U.ev = EV_KEY; U.key = m->a; U.mods = m->b;
        snprintf(U.text, sizeof U.text, "%s", m->s);
        pass();
        U.key = 0; U.text[0] = 0;
        need = 1;
        break;
    }
    U.ev = EV_NONE;
}

int app_main(AppDef *a, int argc, char **argv) {
    A = a;
    signal(SIGPIPE, SIG_IGN);
    font_init();
    theme_set(0, 0, 0);
    for (int i = 0; i < 100 && sock < 0; i++) {
        int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        struct sockaddr_un ad = { .sun_family = AF_UNIX };
        snprintf(ad.sun_path, sizeof ad.sun_path, "%s", W12_SOCK);
        if (connect(s, (struct sockaddr *)&ad, sizeof ad) == 0) sock = s;
        else { close(s); usleep(50000); }
    }
    if (sock < 0) { LOG("%s: no compositor", a->exe); return 1; }
    int mfd = memfd_create("w12-window", MFD_CLOEXEC);
    size_t sz = (size_t)2 * BUF_W * BUF_H * 4;
    if (mfd < 0 || ftruncate(mfd, sz)) { LOG("%s: memfd failed (%s)", a->exe, strerror(errno)); return 1; }
    shm = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    scratch.px = malloc((size_t)BUF_W * BUF_H * 4);
    U.w = a->w; U.h = a->h;
    msg_send(sock, M_HELLO, getpid(), a->w, a->h, a->mica, 0, a->exe, mfd);
    close(mfd);
    if (a->init) a->init(argc, argv);
    need = 1;
    double next_tick = now() + (a->tick_ms ? a->tick_ms / 1000.0 : 1e9);
    for (;;) {
        while (npend) { Msg m = pend[0]; memmove(pend, pend + 1, --npend * sizeof(Msg)); handle(&m); }
        if (need) present();
        int timeout = -1;
        if (a->tick_ms) timeout = maxi(0, (int)((next_tick - now()) * 1000));
        struct pollfd p[2] = { { sock, POLLIN, 0 }, { a->extra_fd > 0 ? a->extra_fd : -1, POLLIN, 0 } };
        int r = poll(p, 2, timeout);
        if (r > 0 && (p[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            Msg m;
            int k;
            // drain everything queued; coalesce pure motion so a fast mouse costs one pass
            while ((k = msg_recv(sock, &m, 0)) > 0) {
                if (m.type == M_MOUSE && m.d == EV_MOVE && !m.c) { U.mx = m.a; U.my = m.b; U.buttons = 0; need = 1; continue; }
                handle(&m);
            }
            if (k == 0) { LOG("%s: compositor went away", a->exe); exit(0); }
        }
        if (r > 0 && a->extra_fd > 0 && (p[1].revents & (POLLIN | POLLHUP))) { if (a->on_fd) a->on_fd(); }
        if (a->tick_ms && now() >= next_tick) {
            next_tick = now() + a->tick_ms / 1000.0;
            if (a->tick) a->tick();
        }
    }
}
