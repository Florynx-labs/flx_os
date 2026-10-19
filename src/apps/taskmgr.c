// Task Manager: real processes from /proc with per-process CPU and memory, live CPU and
// memory graphs, and End task (SIGKILL). Its own process.
#include "w12.h"
#include <dirent.h>
#include <signal.h>

typedef struct { int pid, ppid, threads, kthread, app; char name[64], exe[256], state; long rss_kb; unsigned long long ticks; float cpu; } Proc;
static Proc pr[512]; static int np;
static unsigned long long prev_ticks[32768];
static unsigned long long tot_prev;
static float cpu_hist[90], mem_hist[90]; static int nh;
static float cpu_now, mem_now; static long mem_tot, mem_used;
static int sel_pid = -1, page, show_k;
static Scroll sc;
static char msg[160]; static double msg_t;
#define NAVW 64

static int cmpp(const void *a, const void *b) {
    const Proc *x = a, *y = b;
    if (x->kthread != y->kthread) return x->kthread - y->kthread;
    if ((x->app >= 0) != (y->app >= 0)) return (y->app >= 0) - (x->app >= 0);
    if (x->rss_kb != y->rss_kb) return y->rss_kb > x->rss_kb ? 1 : -1;
    return x->pid - y->pid;
}

static void sample(void) {
    char *s = read_file("/proc/stat", 0);
    unsigned long long tot = 0, idle = 0;
    static unsigned long long idle_prev;
    if (s) {
        unsigned long long v[8] = { 0 };
        sscanf(s, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        for (int i = 0; i < 8; i++) tot += v[i];
        idle = v[3] + v[4];
        free(s);
    }
    unsigned long long dt = tot - tot_prev;
    if (tot_prev && dt) cpu_now = 100.0f * (1 - (float)(idle - idle_prev) / dt);
    int ncpu = 0; char *ci = read_file("/proc/cpuinfo", 0);
    for (char *q = ci; q && (q = strstr(q, "processor")); q++) ncpu++;
    free(ci);
    if (!ncpu) ncpu = 1;
    np = 0;
    DIR *d = opendir("/proc"); struct dirent *e;
    while (d && (e = readdir(d)) && np < 512) {
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;
        char p[64]; snprintf(p, sizeof p, "/proc/%d/stat", pid);
        char *st = read_file(p, 0);
        if (!st) continue;
        Proc *x = &pr[np];
        memset(x, 0, sizeof *x);
        x->pid = pid;
        char *a = strchr(st, '('), *b = strrchr(st, ')');
        snprintf(x->name, sizeof x->name, "%.*s", a && b ? (int)(b - a - 1) : 0, a ? a + 1 : "?");
        if (b) {
            unsigned long long ut = 0, stt = 0; long rss = 0; int thr = 0, ppid = 0;
            // fields after ')': state ppid ... utime(14) stime(15) ... num_threads(20) ... rss(24)
            char *q = b + 2;
            x->state = *q;
            char *f[24]; int nf = 0;
            for (char *t = strtok(q, " "); t && nf < 24; t = strtok(0, " ")) f[nf++] = t;
            if (nf >= 22) { ppid = atoi(f[1]); ut = strtoull(f[11], 0, 10); stt = strtoull(f[12], 0, 10); thr = atoi(f[17]); rss = atol(f[21]); }
            x->ppid = ppid; x->threads = thr; x->rss_kb = rss * 4; x->ticks = ut + stt;
        }
        free(st);
        snprintf(p, sizeof p, "/proc/%d/exe", pid);
        int k = readlink(p, x->exe, sizeof x->exe - 1);
        if (k > 0) x->exe[k] = 0; else { x->exe[0] = 0; x->kthread = 1; }
        const char *bn = strrchr(x->exe, '/');
        x->app = bn ? app_by_exe(bn + 1) : -1;
        unsigned long long prev = pid < 32768 ? prev_ticks[pid] : 0;
        if (prev && dt && x->ticks >= prev) x->cpu = 100.0f * (x->ticks - prev) / ((float)dt / ncpu) / ncpu;
        if (pid < 32768) prev_ticks[pid] = x->ticks ? x->ticks : 1;
        np++;
    }
    if (d) closedir(d);
    tot_prev = tot; idle_prev = idle;
    qsort(pr, np, sizeof *pr, cmpp);
    s = read_file("/proc/meminfo", 0);
    if (s) {
        long t = 0, av = 0; char *q;
        if ((q = strstr(s, "MemTotal:"))) t = atol(q + 9);
        if ((q = strstr(s, "MemAvailable:"))) av = atol(q + 13);
        mem_tot = t; mem_used = t - av; mem_now = t ? 100.0f * mem_used / t : 0;
        free(s);
    }
    if (nh < 90) { cpu_hist[nh] = cpu_now; mem_hist[nh] = mem_now; nh++; }
    else { memmove(cpu_hist, cpu_hist + 1, 89 * sizeof(float)); memmove(mem_hist, mem_hist + 1, 89 * sizeof(float)); cpu_hist[89] = cpu_now; mem_hist[89] = mem_now; }
}

static void graph(Canvas *c, float x, float y, float w, float h, float *hist, uint32_t col, const char *title, const char *big, const char *sub) {
    ui_card(x, y, w, h);
    text(c, F_SEMI, 16, 0, x + 20, y + 32, TH.text, title);
    text_r(c, F_SEMI, 26, 0, x + w - 20, y + 40, col, big);
    text(c, F_REG, 13, 0, x + 20, y + 54, TH.sub, sub);
    float gx = x + 20, gy = y + 70, gw = w - 40, gh = h - 90;
    g_rect(c, gx, gy, gw, gh, c_alpha(col, 0.05f));
    for (int i = 1; i < 4; i++) g_rect(c, gx, gy + gh * i / 4, gw, 1, c_alpha(TH.text, 0.06f));
    for (int i = 1; i < 6; i++) g_rect(c, gx + gw * i / 6, gy, 1, gh, c_alpha(TH.text, 0.06f));
    g_rrect_line(c, gx, gy, gw, gh, 0, 1, c_alpha(col, 0.5f));
    int n = nh;
    for (int i = 0; i < n; i++) {
        float x0 = gx + gw - (n - i) * gw / 89.0f;
        float v = clampf(hist[i], 0, 100) / 100 * gh;
        g_rect(c, roundf(x0), roundf(gy + gh - v), ceilf(gw / 89.0f) + 1, roundf(v), c_alpha(col, 0.18f));
        if (i) {
            float xp = gx + gw - (n - i + 1) * gw / 89.0f, vp = clampf(hist[i - 1], 0, 100) / 100 * gh;
            g_line(c, xp, gy + gh - vp, x0, gy + gh - v, 1.8f, col);
        }
    }
}

static void draw_processes(Canvas *c, float x, float y, float w, float h) {
    Proc *selp = 0;
    for (int i = 0; i < np; i++) if (pr[i].pid == sel_pid) selp = &pr[i];
    text(c, F_SEMI, 24, 0, x, y + 30, TH.text, "Processes");
    if (ui_button(x + w - 140, y + 6, 130, 34, "End task", selp && !selp->kthread ? B_ACCENT : B_STD) && selp && !selp->kthread) {
        if (selp->pid == 1) snprintf(msg, sizeof msg, "init (PID 1) can't be ended");
        else if (kill(selp->pid, SIGKILL) == 0) { snprintf(msg, sizeof msg, "Ended %s (PID %d)", selp->name, selp->pid); MARK("TASKMGR kill %d %s", selp->pid, selp->name); sel_pid = -1; }
        else snprintf(msg, sizeof msg, "Couldn't end %s: %s", selp->name, strerror(errno));
        msg_t = now();
        sample();
    }
    if (msg[0] && now() - msg_t < 4) text_r(c, F_REG, 13, 0, x + w - 156, y + 28, TH.accent, msg);
    float hy = y + 56;
    float cx[6] = { x + 12, x + w * 0.36f, x + w * 0.44f, x + w * 0.53f, x + w * 0.62f, x + w * 0.73f };
    char hc[32], hm[32];
    snprintf(hc, sizeof hc, "%.0f%%", cpu_now); snprintf(hm, sizeof hm, "%.0f%%", mem_now);
    text(c, F_REG, 13, 0, cx[0] + 34, hy + 18, TH.sub, "Name");
    text(c, F_REG, 13, 0, cx[1], hy + 18, TH.sub, "PID");
    text(c, F_REG, 13, 0, cx[2], hy + 18, TH.sub, "Status");
    text(c, F_SEMI, 15, 0, cx[3], hy + 12, TH.text, hc); text(c, F_REG, 12, 0, cx[3], hy + 28, TH.sub, "CPU");
    text(c, F_SEMI, 15, 0, cx[4], hy + 12, TH.text, hm); text(c, F_REG, 12, 0, cx[4], hy + 28, TH.sub, "Memory");
    text(c, F_REG, 13, 0, cx[5], hy + 18, TH.sub, "Executable (/proc/PID/exe)");
    g_rect(c, x, hy + 36, w, 1, TH.line);
    float ly = hy + 40, lh = 32, vh = y + h - ly;
    int sc0[4]; g_getclip(c, sc0);
    g_clip(c, x, ly, x + w, ly + vh);
    float ry = ly - sc.pos;
    int nk = 0, napps = 0, nbg = 0;
    for (int i = 0; i < np; i++) { if (pr[i].kthread) nk++; else if (pr[i].app >= 0) napps++; else nbg++; }
    int group = -1;
    for (int i = 0; i < np; i++) {
        Proc *p = &pr[i];
        int g = p->kthread ? 2 : p->app >= 0 ? 0 : 1;
        if (g != group) {
            group = g;
            char gt[64];
            snprintf(gt, sizeof gt, "%s (%d)", g == 0 ? "Apps" : g == 1 ? "Windows 12 processes" : "Kernel threads", g == 0 ? napps : g == 1 ? nbg : nk);
            ry += 6;
            if (g == 2) {
                icon_glyph(c, show_k ? G_CHEV_D : G_CHEV_R, x + 18, ry + 14, 12, TH.sub);
                if (ui_click(x, ry, w, 28)) show_k = !show_k;
            }
            text(c, F_SEMI, 14, 0, x + (g == 2 ? 32 : 12), ry + 19, TH.accent, gt);
            ry += 30;
        }
        if (g == 2 && !show_k) continue;
        if (ry > ly - lh && ry < ly + vh) {
            bool s = p->pid == sel_pid;
            if (s) g_rrect(c, x, ry, w - 12, lh - 2, 5, 5, TH.accent_soft);
            else if (ui_hover(x, ry, w - 12, lh - 2) && ui_in(x, ly, w, vh)) g_rrect(c, x, ry, w - 12, lh - 2, 5, 5, TH.hover);
            if (p->app >= 0) icon_app(c, p->app, cx[0] + 4, ry + 5, 20);
            else icon_glyph(c, p->kthread ? G_GEAR : G_APPS, cx[0] + 14, ry + 15, 14, TH.sub);
            text(c, F_REG, 14, 0, cx[0] + 34, ry + 21, TH.text, p->app >= 0 ? APPS[p->app].title : p->name);
            char b[64];
            snprintf(b, sizeof b, "%d", p->pid); text(c, F_REG, 13, 0, cx[1], ry + 21, TH.sub, b);
            text(c, F_REG, 13, 0, cx[2], ry + 21, TH.sub, p->state == 'R' ? "Running" : p->state == 'S' ? "Sleeping" : p->state == 'I' ? "Idle" : p->state == 'Z' ? "Zombie" : "Waiting");
            if (p->cpu >= 0.05f) g_rect(c, cx[3] - 6, ry, w * 0.09f, lh - 2, c_alpha(0xFFFFB020, clampf(p->cpu / 40, 0.08f, 0.5f)));
            snprintf(b, sizeof b, "%.1f%%", p->cpu); text(c, F_REG, 13, 0, cx[3], ry + 21, TH.text, b);
            human_size(p->rss_kb * 1024.0, b, sizeof b); if (p->kthread) snprintf(b, sizeof b, "-"); else if (!p->rss_kb) snprintf(b, sizeof b, "< 4 KB");
            text(c, F_REG, 13, 0, cx[4], ry + 21, TH.text, b);
            char *ex = text_ellipsize(F_REG, 13, p->kthread ? "[kernel]" : p->exe, x + w - cx[5] - 16);
            text(c, F_REG, 13, 0, cx[5], ry + 21, p->kthread ? TH.dim : TH.text, ex); free(ex);
            if (ui_downin(x, ry, w - 12, lh - 2) && ui_in(x, ly, w, vh)) { sel_pid = p->pid; MARK("TASKMGR select %d %s", p->pid, p->name); }
        }
        ry += lh;
    }
    g_setclip(c, sc0);
    ui_scroll(&sc, x, ly, w, vh, ry + sc.pos - ly + 10);
    if (U.ev == EV_KEY && U.key == K_DEL && selp && !selp->kthread) { kill(selp->pid, SIGKILL); MARK("TASKMGR kill %d %s", selp->pid, selp->name); }
}

static void draw(Canvas *c) {
    float W = U.w, H = U.h;
    // narrow nav rail
    const int G[2] = { G_LIST, G_CLOCK };
    for (int i = 0; i < 2; i++) {
        if (ui_icon_btn(12, 16 + i * 48, 40, 40, G[i], page == i)) { page = i; MARK("TASKMGR page %d", i); }
    }
    float x = NAVW + 16, y = 10, w = W - x - 20, h = H - y - 10;
    if (page == 0) draw_processes(c, x, y, w, h);
    else {
        text(c, F_SEMI, 24, 0, x, y + 30, TH.text, "Performance");
        char big[32], sub[128], a[32], b[32];
        snprintf(big, sizeof big, "%.0f%%", cpu_now);
        int nthr = 0, nproc = 0; for (int i = 0; i < np; i++) { nthr += pr[i].threads; nproc++; }
        snprintf(sub, sizeof sub, "Utilization over the last 90 seconds · %d processes · %d threads", nproc, nthr);
        graph(c, x, y + 52, w, (h - 64) / 2, cpu_hist, TH.accent, "CPU", big, sub);
        human_size(mem_used * 1024.0, a, sizeof a); human_size(mem_tot * 1024.0, b, sizeof b);
        snprintf(big, sizeof big, "%s", a);
        snprintf(sub, sizeof sub, "In use of %s (%.0f%%) · from /proc/meminfo", b, mem_now);
        graph(c, x, y + 64 + (h - 64) / 2, w, (h - 64) / 2, mem_hist, 0xFF8B5CF6, "Memory", big, sub);
    }
}
static void tick(void) { sample(); app_redraw(); }
static void init(int argc, char **argv) { (void)argc; (void)argv; sample(); }

int main(int argc, char **argv) {
    static AppDef a = { .exe = "taskmgr", .title = "Task Manager", .w = 1080, .h = 700, .mica = 1, .init = init, .draw = draw, .tick = tick, .tick_ms = 1000 };
    return app_main(&a, argc, argv);
}
