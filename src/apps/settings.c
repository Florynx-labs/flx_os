// Settings: System, Personalization (theme, accent, wallpaper — applied live everywhere via
// the compositor), Network (real values from w12-netd + an HTTPS test), Apps, About + Proof.
#include "w12.h"
#include "../net/net.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>

enum { P_SYSTEM, P_PERSONAL, P_NETWORK, P_APPS, P_ABOUT, NPAGES };
static const char *PNAMES[NPAGES] = { "System", "Personalization", "Network & internet", "Apps", "About" };
static const int PGLYPH[NPAGES] = { G_DISPLAY, G_PALETTE, G_NET, G_APPS, G_INFO };
static int page = P_SYSTEM;
static Scroll sc;
static Image *thumbs[NWALL];
static char nettest[200]; static volatile int testing;
#define NAVW 290

static void load_thumbs(void) {
    static const char *names[NWALL] = { "bloom", "aurora", "dune" };
    for (int i = 0; i < NWALL; i++) {
        char p[64]; snprintf(p, sizeof p, "/share/wallpapers/%s.raw", names[i]);
        size_t n; uint32_t *d = (uint32_t *)read_file(p, &n);
        if (!d || n < 12) { free(d); continue; }
        int w = d[1], h = d[2], tw = 240, th = 135;
        thumbs[i] = image_new(tw, th);
        for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) thumbs[i]->px[y * tw + x] = d[3 + (size_t)(y * h / th) * w + x * w / tw] | 0xFF000000;
        free(d);
    }
}

static void *test_th(void *a) {
    (void)a;
    HttpResp *r = http_get("https://example.com/");
    if (r->status == 200) snprintf(nettest, sizeof nettest, "Connected · HTTPS %d · %zu bytes in %.0f ms (own TLS client)", r->status, r->len, r->ms);
    else snprintf(nettest, sizeof nettest, "Failed · %s", r->err[0] ? r->err : "bad status");
    MARK("SETTINGS nettest %s", nettest);
    http_free(r);
    testing = 0;
    app_redraw();
    return 0;
}

static float row(Canvas *c, float x, float y, float w, int glyph, const char *title, const char *value) {
    ui_card(x, y, w, 64);
    icon_glyph(c, glyph, x + 30, y + 32, 20, TH.text);
    text(c, F_REG, 15, 0, x + 60, y + 38, TH.text, title);
    if (value) text_r(c, F_REG, 14, 0, x + w - 24, y + 38, TH.sub, value);
    return y + 70;
}

static void kv(Canvas *c, float x, float *y, const char *k, const char *v) {
    text(c, F_REG, 14, 0, x, *y, TH.sub, k);
    text(c, F_REG, 14, 0, x + 190, *y, TH.text, v);
    *y += 30;
}

static void page_system(Canvas *c, float x, float y, float w) {
    struct statfs st; char a[32], b[32], buf[128];
    y = row(c, x, y, w, G_DISPLAY, "Display", "1920 × 1080 · virtio-gpu · DRM/KMS dumb buffers");
    long tot = 0, av = 0; char *m = read_file("/proc/meminfo", 0);
    if (m) { char *q; if ((q = strstr(m, "MemTotal:"))) tot = atol(q + 9); if ((q = strstr(m, "MemAvailable:"))) av = atol(q + 13); free(m); }
    human_size((tot - av) * 1024.0, a, sizeof a); human_size(tot * 1024.0, b, sizeof b);
    snprintf(buf, sizeof buf, "%s of %s in use", a, b);
    y = row(c, x, y, w, G_APPS, "Memory", buf);
    if (!statfs("/home", &st)) {
        human_size((double)(st.f_blocks - st.f_bfree) * st.f_bsize, a, sizeof a); human_size((double)st.f_blocks * st.f_bsize, b, sizeof b);
        snprintf(buf, sizeof buf, "%s used of %s · /dev/vda (ext2, persistent)", a, b);
        y = row(c, x, y, w, G_SAVE, "Storage", buf);
    }
    y = row(c, x, y, w, G_VOLUME, "Sound", "No audio device in this build");
    y = row(c, x, y, w, G_BATTERY, "Power", "Plugged in (virtual machine)");
    double up = 0; char *u = read_file("/proc/uptime", 0); if (u) { up = atof(u); free(u); }
    snprintf(buf, sizeof buf, "%dh %02dm since boot", (int)(up / 3600), (int)fmod(up / 60, 60));
    y = row(c, x, y, w, G_CLOCK, "Uptime", buf);
}

static void page_personal(Canvas *c, float x, float y, float w) {
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Theme");
    y += 34;
    for (int i = 0; i < 2; i++) {
        float cx = x + i * 236, cw = 220, ch = 150;
        bool on = TH.dark == i;
        g_rrect(c, cx, y, cw, ch, 10, 10, i ? 0xFF202020 : 0xFFF3F3F3);
        g_rrect(c, cx + 20, y + 22, cw - 40, 70, 6, 6, i ? 0xFF2D2D2D : 0xFFFFFFFF);
        g_rrect(c, cx + 34, y + 36, 70, 10, 5, 5, TH.accent);
        g_rrect(c, cx + 34, y + 56, 120, 8, 4, 4, i ? 0xFF555555 : 0xFFD0D0D0);
        g_rrect_line(c, cx, y, cw, ch, 10, on ? 2.5f : 1, on ? TH.accent : TH.line);
        text(c, F_SEMI, 15, 0, cx + 20, y + 128, i ? 0xFFFFFFFF : 0xFF1A1A1A, i ? "Dark" : "Light");
        if (ui_click(cx, y, cw, ch) && !on) { app_set_theme(i, TH.accent_i, TH.wall); MARK("SETTINGS theme %s", i ? "dark" : "light"); }
    }
    y += 176;
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Accent color");
    y += 34;
    for (int i = 0; i < NACCENT; i++) {
        float cx = x + i * 112;
        bool on = TH.accent_i == i;
        g_rrect(c, cx, y, 100, 70, 8, 8, ACCENTS[i]);
        if (on) { g_rrect_line(c, cx - 4, y - 4, 108, 78, 11, 2.5f, TH.text); icon_glyph(c, G_CHECK, cx + 50, y + 30, 22, 0xFFFFFFFF); }
        text_c(c, F_REG, 13, 0, cx + 50, y + 92, TH.sub, ACCENT_NAMES[i]);
        if (ui_click(cx, y, 100, 70) && !on) { app_set_theme(TH.dark, i, TH.wall); MARK("SETTINGS accent %s", ACCENT_NAMES[i]); }
    }
    y += 120;
    text(c, F_SEMI, 16, 0, x, y + 18, TH.text, "Wallpaper");
    text(c, F_REG, 13, 0, x + 110, y + 18, TH.sub, "Procedurally generated at build time");
    y += 34;
    for (int i = 0; i < NWALL; i++) {
        float cx = x + i * 256;
        bool on = TH.wall == i;
        if (thumbs[i]) g_image(c, thumbs[i], cx, y, 240, 135, 8, 255);
        g_rrect_line(c, cx - (on ? 3 : 0), y - (on ? 3 : 0), 240 + (on ? 6 : 0), 135 + (on ? 6 : 0), 10, on ? 2.5f : 1, on ? TH.accent : TH.line);
        text(c, on ? F_SEMI : F_REG, 14, 0, cx + 2, y + 160, TH.text, WALL_NAMES[i]);
        if (ui_click(cx, y, 240, 135) && !on) { app_set_theme(TH.dark, TH.accent_i, i); MARK("SETTINGS wallpaper %s", WALL_NAMES[i]); }
    }
}

static void page_network(Canvas *c, float x, float y, float w) {
    NetInfo ni; netinfo_read(&ni);
    ui_card(x, y, w, 110);
    icon_glyph(c, ni.up ? G_WIFI : G_WIFI_OFF, x + 50, y + 55, 40, ni.up ? TH.accent : TH.dim);
    text(c, F_SEMI, 20, 0, x + 100, y + 48, TH.text, ni.up ? "Connected" : "Not connected");
    text(c, F_REG, 14, 0, x + 100, y + 74, TH.sub, ni.up ? "Ethernet (virtio-net) · configured by our own DHCP client" : "Waiting for DHCP…");
    y += 126;
    ui_card(x, y, w, 230);
    float ky = y + 36;
    kv(c, x + 24, &ky, "Interface", ni.ifname);
    kv(c, x + 24, &ky, "IPv4 address", ni.ip[0] ? ni.ip : "-");
    kv(c, x + 24, &ky, "Subnet mask", ni.mask[0] ? ni.mask : "-");
    kv(c, x + 24, &ky, "Default gateway", ni.gw[0] ? ni.gw : "-");
    kv(c, x + 24, &ky, "DNS server", ni.dns[0] ? ni.dns : "-");
    kv(c, x + 24, &ky, "Physical address", ni.mac);
    kv(c, x + 24, &ky, "Source", "/run/net.conf (written by w12-netd)");
    y += 246;
    if (ui_button(x, y, 200, 36, testing ? "Testing…" : "Test connection", B_ACCENT) && !testing) {
        testing = 1; nettest[0] = 0;
        pthread_t t; pthread_create(&t, 0, test_th, 0); pthread_detach(t);
    }
    if (nettest[0]) text(c, F_REG, 14, 0, x + 216, y + 24, TH.text, nettest);
}

static void page_apps(Canvas *c, float x, float y, float w) {
    text(c, F_REG, 14, 0, x, y + 16, TH.sub, "Every app is its own process, statically linked against musl, written for Windows 12.");
    y += 36;
    for (int i = 0; i < NAPPS; i++) {
        ui_card(x, y, w, 62);
        icon_app(c, i, x + 18, y + 15, 32);
        text(c, F_SEMI, 15, 0, x + 66, y + 28, TH.text, APPS[i].title);
        text(c, F_REG, 13, 0, x + 66, y + 47, TH.sub, APPS[i].desc);
        char p[64], sz[32]; struct stat st; snprintf(p, sizeof p, "/bin/%s", APPS[i].exe);
        if (!stat(p, &st)) { human_size(st.st_size, sz, sizeof sz); char v[128]; snprintf(v, sizeof v, "%s · %s", p, sz); text_r(c, F_REG, 13, 0, x + w - 120, y + 37, TH.sub, v); }
        if (ui_button(x + w - 100, y + 14, 80, 34, "Open", B_STD)) app_launch(APPS[i].exe, 0);
        y += 68;
    }
}

static void page_about(Canvas *c, float x, float y, float w) {
    ui_card(x, y, w, 170);
    icon_logo(c, x + 90, y + 85, 110, 255);
    text(c, F_SEMI, 30, 0, x + 170, y + 70, TH.text, "Windows 12");
    char b[256];
    char *bd = read_file("/share/proof/build.txt", 0);
    if (bd) bd[strcspn(bd, "\n")] = 0;
    snprintf(b, sizeof b, "Version " W12_VERSION " · build %s", bd ? bd : "?");
    free(bd);
    text(c, F_REG, 15, 0, x + 172, y + 100, TH.sub, b);
    text(c, F_SEMI, 14, 0, x + 172, y + 132, TH.danger, "Unofficial fan project, not affiliated with Microsoft.");
    y += 186;
    ui_card(x, y, w, 220);
    float ky = y + 36;
    struct utsname u; uname(&u);
    snprintf(b, sizeof b, "%s %s (%s)", u.sysname, u.release, u.machine);
    kv(c, x + 24, &ky, "Kernel", b);
    int ncpu = 0; char *ci = read_file("/proc/cpuinfo", 0);
    for (char *q = ci; q && (q = strstr(q, "processor")); q++) ncpu++;
    free(ci);
    snprintf(b, sizeof b, "%d × ARM64 core (Apple silicon via HVF)", ncpu);
    kv(c, x + 24, &ky, "Processor", b);
    long tot = 0; char *m = read_file("/proc/meminfo", 0); if (m) { tot = atol(strstr(m, "MemTotal:") + 9); free(m); }
    char sz[32]; human_size(tot * 1024.0, sz, sizeof sz);
    kv(c, x + 24, &ky, "Installed RAM", sz);
    kv(c, x + 24, &ky, "Display", "1920 × 1080, 32-bit, software rendered");
    kv(c, x + 24, &ky, "Font", "Selawik (SIL Open Font License)");
    kv(c, x + 24, &ky, "Built on", "Lumen OS 2 code (MIT) — DRM, input, text, net stack, browser");
    y += 236;
    ui_card(x, y, w, 120);
    icon_app(c, APP_PROOF, x + 24, y + 28, 44);
    text(c, F_SEMI, 16, 0, x + 86, y + 46, TH.text, "Proof");
    text(c, F_REG, 13, 0, x + 86, y + 70, TH.sub, "See every running process, lines of code and the toolkit scan.");
    text(c, F_REG, 13, 0, x + 86, y + 90, TH.sub, "Linux kernel below. Everything above /dev/dri and /dev/input is written in this repo.");
    if (ui_button(x + w - 150, y + 42, 126, 36, "Open Proof", B_ACCENT)) { app_launch("proof", 0); MARK("SETTINGS proof"); }
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;
    // user card + nav
    g_circle(c, 46, 50, 26, TH.accent);
    text_c(c, F_SEMI, 22, 0, 46, 58, TH.accent_fg, "U");
    text(c, F_SEMI, 15, 0, 84, 46, TH.text, "User");
    text(c, F_REG, 13, 0, 84, 66, TH.sub, "Local account");
    for (int i = 0; i < NPAGES; i++) {
        float ny = 110 + i * 42;
        bool on = page == i;
        if (on) g_rrect(c, 10, ny, NAVW - 20, 38, 5, 5, TH.dark ? 0x22FFFFFF : 0x0F000000);
        else if (ui_hover(10, ny, NAVW - 20, 38)) g_rrect(c, 10, ny, NAVW - 20, 38, 5, 5, TH.hover);
        if (on) g_rrect(c, 10, ny + 10, 3, 18, 1.5f, 1.5f, TH.accent);
        icon_glyph(c, PGLYPH[i], 38, ny + 19, 17, on ? TH.accent : TH.text);
        text(c, F_REG, 14, 0, 62, ny + 25, TH.text, PNAMES[i]);
        if (ui_click(10, ny, NAVW - 20, 38)) { page = i; sc.pos = 0; MARK("SETTINGS page %s", PNAMES[i]); }
    }
    float x = NAVW + 20, w = W - x - 40;
    text(c, F_SEMI, 28, 0, x, 58, TH.text, PNAMES[page]);
    float top = 84;
    int s0[4]; g_getclip(c, s0);
    g_clip(c, NAVW, top, W, H);
    float y = top + 16 - sc.pos;
    switch (page) {
    case P_SYSTEM: page_system(c, x, y, w); break;
    case P_PERSONAL: page_personal(c, x, y, w); break;
    case P_NETWORK: page_network(c, x, y, w); break;
    case P_APPS: page_apps(c, x, y, w); break;
    case P_ABOUT: page_about(c, x, y, w); break;
    }
    g_setclip(c, s0);
    float ch = page == P_APPS ? 36 + NAPPS * 68 + 20 : page == P_ABOUT ? 560 : page == P_PERSONAL ? 590 : 480;
    ui_scroll(&sc, NAVW, top, W - NAVW, H - top, ch + 16);
}

static void init(int argc, char **argv) {
    load_thumbs();
    if (argc > 1) {
        if (!strcmp(argv[1], "network")) page = P_NETWORK;
        else if (!strcmp(argv[1], "about")) page = P_ABOUT;
        else if (!strcmp(argv[1], "personalization")) page = P_PERSONAL;
        else if (!strcmp(argv[1], "apps")) page = P_APPS;
    }
}
static void tick(void) { if (page == P_SYSTEM || page == P_NETWORK) app_redraw(); }

int main(int argc, char **argv) {
    static AppDef a = { .exe = "settings", .title = "Settings", .w = 1120, .h = 720, .mica = 1, .init = init, .draw = draw, .tick = tick, .tick_ms = 2000 };
    return app_main(&a, argc, argv);
}
