// Proof: what is running on this PC and where it came from. Live processes with their
// executable paths, lines of code per component (computed at build time), the result of
// the build-time toolkit scan, and a full listing of the root filesystem.
#include "w12.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>

typedef struct { int pid; char name[48], exe[200]; } P;
static P ps[512]; static int nps, nk;
static char loc_name[16][32]; static int loc_n[16], nloc, loc_total;
static char scan[512];
static char files[256][200]; static long fsize[256]; static int nfiles; static long ftotal;
static Scroll s1, s2;

static void walk(const char *d) {
    DIR *dd = opendir(d); struct dirent *e;
    while (dd && (e = readdir(dd)) && nfiles < 256) {
        if (e->d_name[0] == '.') continue;
        char p[400]; snprintf(p, sizeof p, "%s/%s", strcmp(d, "/") ? d : "", e->d_name);
        if (!strcmp(p, "/proc") || !strcmp(p, "/sys") || !strcmp(p, "/dev") || !strcmp(p, "/home") || !strcmp(p, "/run") || !strcmp(p, "/tmp")) {
            snprintf(files[nfiles], 200, "%s/  (virtual or disk mount)", p); fsize[nfiles++] = -1; continue;
        }
        struct stat st;
        if (lstat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) walk(p);
        else { snprintf(files[nfiles], 200, "%s%s", p, S_ISLNK(st.st_mode) ? " -> /sbin/init" : ""); fsize[nfiles++] = st.st_size; ftotal += st.st_size; }
    }
    if (dd) closedir(dd);
}
static void refresh(void) {
    nps = 0; nk = 0;
    DIR *d = opendir("/proc"); struct dirent *e;
    while (d && (e = readdir(d)) && nps < 512) {
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;
        char p[64], buf[256]; snprintf(p, sizeof p, "/proc/%d/exe", pid);
        int k = readlink(p, buf, sizeof buf - 1);
        if (k <= 0) { nk++; continue; }
        buf[k] = 0;
        P *x = &ps[nps++];
        x->pid = pid; snprintf(x->exe, sizeof x->exe, "%s", buf);
        snprintf(p, sizeof p, "/proc/%d/comm", pid);
        char *c = read_file(p, 0);
        snprintf(x->name, sizeof x->name, "%s", c ? c : "?");
        x->name[strcspn(x->name, "\n")] = 0;
        free(c);
    }
    if (d) closedir(d);
}
static void init(int argc, char **argv) {
    (void)argc; (void)argv;
    char *l = read_file("/share/proof/loc.txt", 0);
    for (char *t = l ? strtok(l, "\n") : 0; t && nloc < 16; t = strtok(0, "\n")) {
        char *sp = strrchr(t, ' ');
        if (!sp) continue;
        *sp = 0;
        if (!strcmp(t, "total")) { loc_total = atoi(sp + 1); continue; }
        snprintf(loc_name[nloc], 32, "%s", t); loc_n[nloc++] = atoi(sp + 1);
    }
    free(l);
    char *s = read_file("/share/proof/scan.txt", 0);
    snprintf(scan, sizeof scan, "%s", s ? s : "scan result missing");
    free(s);
    walk("/");
    refresh();
    MARK("PROOF window %d processes %d files", nps, nfiles);
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;
    struct utsname u; uname(&u);
    icon_app(c, APP_PROOF, 24, 18, 52);
    text(c, F_SEMI, 26, 0, 92, 44, TH.text, "Proof: what runs on this PC");
    char kv[200]; snprintf(kv, sizeof kv, "Kernel: %s %s (%s)", u.sysname, u.release, u.machine);
    text(c, F_REG, 14, 0, 92, 68, TH.sub, kv);
    g_rrect(c, 24, 86, W - 48, 40, 8, 8, TH.accent_soft);
    text(c, F_SEMI, 15, 0, 40, 112, TH.text, "Linux kernel below. Everything above /dev/dri and /dev/input is written from scratch in this repo.");
    float top = 140, colw = (W - 72) / 2, h = H - top - 16;
    // left: processes
    ui_card(24, top, colw, h);
    char t[64]; snprintf(t, sizeof t, "Running processes (%d ours + %d kernel threads)", nps, nk);
    text(c, F_SEMI, 15, 0, 40, top + 28, TH.text, t);
    float ly = top + 44, lh = 26;
    int sc[4]; g_getclip(c, sc);
    g_clip(c, 24, ly, 24 + colw, top + h - 8);
    for (int i = 0; i < nps; i++) {
        float y = ly + i * lh - s1.pos;
        char b[16]; snprintf(b, sizeof b, "%d", ps[i].pid);
        text_r(c, F_REG, 13, 0, 80, y + 18, TH.sub, b);
        text(c, F_SEMI, 13, 0, 94, y + 18, TH.text, ps[i].name);
        text(c, F_REG, 13, 0, 250, y + 18, 0xFF2EA043, ps[i].exe);
    }
    g_setclip(c, sc);
    ui_scroll(&s1, 24, ly, colw, top + h - 8 - ly, nps * lh + 8);
    // right: LOC, scan, files
    float rx = 48 + colw;
    ui_card(rx, top, colw, 250);
    snprintf(t, sizeof t, "Lines of code: %d (computed at build time)", loc_total);
    text(c, F_SEMI, 15, 0, rx + 16, top + 28, TH.text, t);
    int mx = 1; for (int i = 0; i < nloc; i++) mx = maxi(mx, loc_n[i]);
    for (int i = 0; i < nloc; i++) {
        float y = top + 42 + i * 18;
        text(c, F_REG, 12, 0, rx + 16, y + 13, TH.sub, loc_name[i]);
        float bw = (colw - 230) * loc_n[i] / mx;
        g_rrect(c, rx + 120, y + 4, fmaxf(2, bw), 11, 3, 3, ACCENTS[i % NACCENT]);
        char b[16]; snprintf(b, sizeof b, "%d", loc_n[i]);
        text(c, F_REG, 12, 0, rx + 126 + bw, y + 13, TH.text, b);
    }
    float sy = top + 262;
    bool pass = !strncmp(scan, "PASS", 4);
    ui_card(rx, sy, colw, 100);
    g_rrect(c, rx + 16, sy + 16, 64, 28, 14, 14, pass ? 0xFF2EA043 : 0xFFE5484D);
    text_c(c, F_BOLD, 14, 0, rx + 48, sy + 35, 0xFFFFFFFF, pass ? "PASS" : "FAIL");
    text(c, F_SEMI, 15, 0, rx + 92, sy + 36, TH.text, "Toolkit scan (scripts/verify_no_toolkits.sh)");
    int st[6], ln[6];
    int nl = text_wrap(F_REG, 12, scan, colw - 32, st, ln, 6);
    for (int i = 0; i < nl && i < 3; i++) text_n(c, F_REG, 12, 0, rx + 16, sy + 62 + i * 16, TH.sub, scan + st[i], ln[i]);
    float fy = sy + 112, fh = top + h - fy;
    ui_card(rx, fy, colw, fh);
    char fs[32]; human_size(ftotal, fs, sizeof fs);
    snprintf(t, sizeof t, "Root filesystem: %d entries, %s", nfiles, fs);
    text(c, F_SEMI, 15, 0, rx + 16, fy + 28, TH.text, t);
    g_clip(c, rx, fy + 40, rx + colw, fy + fh - 6);
    for (int i = 0; i < nfiles; i++) {
        float y = fy + 40 + i * 22 - s2.pos;
        if (fsize[i] >= 0) { char b[32]; human_size(fsize[i], b, sizeof b); text_r(c, F_REG, 12, 0, rx + 90, y + 16, TH.sub, b); }
        text(c, F_REG, 12, 0, rx + 104, y + 16, TH.text, files[i]);
    }
    g_setclip(c, sc);
    ui_scroll(&s2, rx, fy + 40, colw, fh - 46, nfiles * 22 + 8);
}
static void tick(void) { refresh(); app_redraw(); }

int main(int argc, char **argv) {
    static AppDef a = { .exe = "proof", .title = "Proof", .w = 1280, .h = 760, .mica = 1, .init = init, .draw = draw, .tick = tick, .tick_ms = 2000 };
    return app_main(&a, argc, argv);
}
