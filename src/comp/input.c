// evdev input: virtio-tablet (absolute pointer + buttons + wheel) and virtio-keyboard.
// Pattern from Lumen OS 2 (MIT); extended with mouse buttons, wheel and modifiers.
#include "comp.h"
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAXDEV 8
static int fds[MAXDEV], nfd;
static int absmax_x[MAXDEV], absmax_y[MAXDEV];
static int ax, ay, moved, caps;
float mouse_x = SCR_W / 2, mouse_y = SCR_H / 2;
int mouse_buttons, mods_held;
static int pend_btn[8][2], npb;   // button changes waiting for SYN

void input_init(void) {
    for (int i = 0; i < 16 && nfd < MAXDEV; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[64] = "";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        struct input_absinfo ai = {0};
        absmax_x[nfd] = ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0 && ai.maximum ? ai.maximum : 32767;
        absmax_y[nfd] = ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0 && ai.maximum ? ai.maximum : 32767;
        LOG("input: %s = %s", p, name);
        fds[nfd++] = fd;
    }
}
int input_fds(int *out, int max) { int n = mini(nfd, max); memcpy(out, fds, n * sizeof(int)); return n; }

static const char km_lo[] = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char km_hi[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static int keymap(int code, int shift) {
    switch (code) {
    case KEY_LEFT: return K_LEFT;
    case KEY_RIGHT: return K_RIGHT;
    case KEY_UP: return K_UP;
    case KEY_DOWN: return K_DOWN;
    case KEY_HOME: return K_HOME;
    case KEY_END: return K_END;
    case KEY_PAGEUP: return K_PGUP;
    case KEY_PAGEDOWN: return K_PGDN;
    case KEY_DELETE: return K_DEL;
    case KEY_KPENTER: return K_ENTER;
    case KEY_LEFTMETA: case KEY_RIGHTMETA: return K_SUPER;
    case KEY_F1: return K_F1;
    case KEY_F4: return K_F4;
    case KEY_F5: return K_F5;
    }
    if (code < (int)sizeof km_lo - 1) {
        int c = (unsigned char)(shift ? km_hi : km_lo)[code];
        if (caps && c >= 'a' && c <= 'z') c -= 32;
        else if (caps && c >= 'A' && c <= 'Z') c += 32;
        return c;
    }
    return 0;
}

int input_poll(IEv *out, int max) {
    int n = 0;
    struct input_event ev[64];
    for (int d = 0; d < nfd; d++) {
        int r;
        while ((r = read(fds[d], ev, sizeof ev)) > 0) {
            for (int i = 0; i < r / (int)sizeof ev[0]; i++) {
                struct input_event *e = &ev[i];
                if (e->type == EV_ABS && e->code == ABS_X) { ax = (int)((long)e->value * (SCR_W - 1) / absmax_x[d]); moved = 1; }
                else if (e->type == EV_ABS && e->code == ABS_Y) { ay = (int)((long)e->value * (SCR_H - 1) / absmax_y[d]); moved = 1; }
                else if (e->type == EV_REL && e->code == REL_WHEEL && n < max) out[n++] = (IEv){ .type = IE_WHEEL, .x = mouse_x, .y = mouse_y, .wheel = e->value * 120, .mods = mods_held };
                else if (e->type == EV_KEY && (e->code == BTN_LEFT || e->code == BTN_RIGHT || e->code == BTN_MIDDLE || e->code == BTN_TOUCH)) {
                    if (npb < 8) { pend_btn[npb][0] = e->code == BTN_RIGHT ? 2 : e->code == BTN_MIDDLE ? 3 : 1; pend_btn[npb][1] = e->value != 0; npb++; }
                } else if (e->type == EV_KEY && e->code < 256) {
                    int m = 0;
                    switch (e->code) {
                    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: m = MOD_SHIFT; break;
                    case KEY_LEFTCTRL: case KEY_RIGHTCTRL: m = MOD_CTRL; break;
                    case KEY_LEFTALT: case KEY_RIGHTALT: m = MOD_ALT; break;
                    case KEY_LEFTMETA: case KEY_RIGHTMETA: m = MOD_SUPER; break;
                    }
                    if (e->code == KEY_CAPSLOCK && e->value == 1) caps = !caps;
                    if (m && m != MOD_SUPER) { if (e->value) mods_held |= m; else mods_held &= ~m; if (e->value != 2 && n < max) out[n++] = (IEv){ .type = e->value ? IE_KEY : IE_KEYUP, .key = 0, .mods = mods_held }; continue; }
                    if (m == MOD_SUPER) { if (e->value) mods_held |= m; else mods_held &= ~m; }
                    int k = keymap(e->code, mods_held & MOD_SHIFT);
                    if (!k || n >= max) continue;
                    IEv o = { .type = e->value ? IE_KEY : IE_KEYUP, .x = mouse_x, .y = mouse_y, .key = k, .mods = mods_held };
                    if (k >= 32 && k < 127 && !(mods_held & (MOD_CTRL | MOD_ALT | MOD_SUPER))) { o.text[0] = k; o.text[1] = 0; }
                    if ((mods_held & MOD_CTRL) && k >= 'A' && k <= 'Z') o.key = k + 32;
                    out[n++] = o;
                } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                    if (moved && n < max) { mouse_x = ax; mouse_y = ay; out[n++] = (IEv){ .type = IE_MOVE, .x = ax, .y = ay, .mods = mods_held }; }
                    moved = 0;
                    for (int b = 0; b < npb && n < max; b++) {
                        int bit = 1 << (pend_btn[b][0] - 1);
                        if (pend_btn[b][1]) mouse_buttons |= bit; else mouse_buttons &= ~bit;
                        out[n++] = (IEv){ .type = pend_btn[b][1] ? IE_DOWN : IE_UP, .x = mouse_x, .y = mouse_y, .btn = pend_btn[b][0], .mods = mods_held };
                    }
                    npb = 0;
                }
            }
        }
    }
    return n;
}
