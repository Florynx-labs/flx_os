// w12sh: the Windows 12 shell. Runs on the Terminal's pty. All commands are built in:
// ls cd pwd cat echo mkdir rm touch ps kill uname free df ifconfig ping curl clear help
// proof open date whoami hostname fetch exit. ping uses raw ICMP; curl uses our own
// HTTP/TLS stack; open asks the compositor to launch an app.
#include "w12.h"
#include "../net/net.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <net/if.h>
#include <termios.h>

#define C_RESET "\x1b[0m"
#define C_BLUE "\x1b[94m"
#define C_CYAN "\x1b[96m"
#define C_GREEN "\x1b[92m"
#define C_YEL "\x1b[93m"
#define C_RED "\x1b[91m"
#define C_DIM "\x1b[90m"
#define C_BOLD "\x1b[1m"
static volatile int interrupted;

static void p(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void p(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

static char *home(void) { return "/home/user"; }
static void pretty_cwd(char *o, int n) {
    char c[512];
    if (!getcwd(c, sizeof c)) snprintf(c, sizeof c, "?");
    if (!strncmp(c, home(), strlen(home()))) snprintf(o, n, "~%s", c + strlen(home()));
    else snprintf(o, n, "%s", c);
}

// ---- commands ---------------------------------------------------------------------------------------
static int cmp_str(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }
static void cmd_ls(int argc, char **argv) {
    int lng = 0, all = 0;
    const char *dir = ".";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { if (strchr(argv[i], 'l')) lng = 1; if (strchr(argv[i], 'a')) all = 1; }
        else dir = argv[i];
    }
    DIR *d = opendir(dir);
    if (!d) { struct stat st; if (!stat(dir, &st)) { p("%s\n", dir); return; } p(C_RED "ls: %s: %s" C_RESET "\n", dir, strerror(errno)); return; }
    char *names[1024]; int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 1024) if (all || e->d_name[0] != '.') names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    int col = 0;
    for (int i = 0; i < n; i++) {
        char path[1024]; struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        int ok = !lstat(path, &st);
        const char *c = ok && S_ISDIR(st.st_mode) ? C_BLUE C_BOLD : ok && (st.st_mode & 0111) ? C_GREEN : "";
        if (lng) {
            char sz[32]; human_size(ok ? st.st_size : 0, sz, sizeof sz);
            char tm[32]; struct tm t; localtime_r(&st.st_mtime, &t); strftime(tm, sizeof tm, "%d %b %H:%M", &t);
            p("%c%c%c%c %8s  %s  %s%s" C_RESET "%s\n", S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(st.st_mode) ? 'l' : '-',
              st.st_mode & 0400 ? 'r' : '-', st.st_mode & 0200 ? 'w' : '-', st.st_mode & 0100 ? 'x' : '-', sz, tm, c, names[i], S_ISDIR(st.st_mode) ? "/" : "");
        } else {
            p("%s%-22s" C_RESET, c, names[i]);
            if (++col == 4) { p("\n"); col = 0; }
        }
        free(names[i]);
    }
    if (!lng && col) p("\n");
}
static void cmd_cat(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        size_t n; char *d = read_file(argv[i], &n);
        if (!d) { p(C_RED "cat: %s: %s" C_RESET "\n", argv[i], strerror(errno)); continue; }
        fwrite(d, 1, n, stdout);
        if (n && d[n - 1] != '\n') p("\n");
        free(d);
    }
}
static int rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path); struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[1024]; snprintf(c, sizeof c, "%s/%s", path, e->d_name); rm_rf(c);
        }
        if (d) closedir(d);
        return rmdir(path);
    }
    return unlink(path);
}
static int proc_name(int pid, char *name, int n, char *exe, int en, long *rss_kb) {
    char pth[64], buf[512];
    snprintf(pth, sizeof pth, "/proc/%d/stat", pid);
    char *s = read_file(pth, 0);
    if (!s) return -1;
    char *a = strchr(s, '('), *b = strrchr(s, ')');
    snprintf(name, n, "%.*s", a && b ? (int)(b - a - 1) : 0, a ? a + 1 : "");
    long rss = 0;
    if (b) { char *q = b + 2; for (int f = 3; f < 24 && q; f++) { q = strchr(q, ' '); if (q) q++; } if (q) rss = atol(q) * 4; }
    if (rss_kb) *rss_kb = rss;
    free(s);
    snprintf(pth, sizeof pth, "/proc/%d/exe", pid);
    int k = readlink(pth, buf, sizeof buf - 1);
    snprintf(exe, en, "%.*s", k > 0 ? k : 0, buf);
    if (k <= 0) snprintf(exe, en, "[kernel thread]");
    return 0;
}
static void cmd_ps(void) {
    p(C_BOLD "  PID  %-18s %8s  %s" C_RESET "\n", "NAME", "RSS", "EXECUTABLE");
    DIR *d = opendir("/proc"); struct dirent *e;
    while (d && (e = readdir(d))) {
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;
        char name[64], exe[256]; long rss;
        if (proc_name(pid, name, sizeof name, exe, sizeof exe, &rss)) continue;
        if (exe[0] == '[') continue;    // ps hides kernel threads; proof shows them
        char sz[32]; human_size(rss * 1024.0, sz, sizeof sz);
        p("%5d  %-18s %8s  %s\n", pid, name, sz, exe);
    }
    if (d) closedir(d);
}
static void cmd_free(void) {
    char *s = read_file("/proc/meminfo", 0);
    if (!s) return;
    long tot = 0, fr = 0, av = 0, buf = 0, cach = 0; char *q;
    if ((q = strstr(s, "MemTotal:"))) tot = atol(q + 9);
    if ((q = strstr(s, "MemFree:"))) fr = atol(q + 8);
    if ((q = strstr(s, "MemAvailable:"))) av = atol(q + 13);
    if ((q = strstr(s, "Buffers:"))) buf = atol(q + 8);
    if ((q = strstr(s, "\nCached:"))) cach = atol(q + 8);
    free(s);
    p(C_BOLD "%14s %10s %10s %10s %10s" C_RESET "\n", "total", "used", "free", "buff/cache", "available");
    p("Mem: %9ld MB %7ld MB %7ld MB %7ld MB %7ld MB\n", tot / 1024, (tot - av) / 1024, fr / 1024, (buf + cach) / 1024, av / 1024);
}
static void cmd_df(void) {
    const char *mp[] = { "/", "/home", "/run", "/tmp" };
    const char *fs[] = { "rootfs (initramfs)", "/dev/vda (ext2)", "tmpfs", "tmpfs" };
    p(C_BOLD "%-20s %10s %10s %10s  %s" C_RESET "\n", "Filesystem", "Size", "Used", "Avail", "Mounted on");
    for (int i = 0; i < 4; i++) {
        struct statfs st;
        if (statfs(mp[i], &st)) continue;
        double sz = (double)st.f_blocks * st.f_bsize, av = (double)st.f_bavail * st.f_bsize, us = sz - (double)st.f_bfree * st.f_bsize;
        char a[32], b[32], c[32];
        human_size(sz, a, sizeof a); human_size(us, b, sizeof b); human_size(av, c, sizeof c);
        p("%-20s %10s %10s %10s  %s\n", fs[i], sz > 0 ? a : "-", b, c, mp[i]);
    }
}
static void cmd_ifconfig(void) {
    NetInfo ni;
    if (netinfo_read(&ni)) { p("no network info\n"); return; }
    p(C_BOLD "%s" C_RESET ": %s\n", ni.ifname, ni.up ? C_GREEN "UP, configured by w12-netd (own DHCP client)" C_RESET : C_RED "DOWN" C_RESET);
    p("    inet %s  netmask %s\n    gateway %s  dns %s\n    ether %s\n", ni.ip, ni.mask, ni.gw, ni.dns, ni.mac);
    p(C_BOLD "lo" C_RESET ": UP\n    inet 127.0.0.1  netmask 255.0.0.0\n");
}
static uint16_t csum(const void *d, int n) {
    const uint8_t *b = d; uint32_t s = 0;
    for (int i = 0; i + 1 < n; i += 2) s += b[i] << 8 | b[i + 1];
    if (n & 1) s += b[n - 1] << 8;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return htons(~s);
}
static void cmd_ping(int argc, char **argv) {
    const char *host = 0; int count = 4;
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-c") && i + 1 < argc) count = atoi(argv[++i]); else host = argv[i]; }
    if (!host) { p("usage: ping [-c N] host\n"); return; }
    uint32_t ip;
    if (!net_up()) { p(C_RED "ping: network is down" C_RESET "\n"); return; }
    if (dns_resolve(host, &ip)) { p(C_RED "ping: cannot resolve %s" C_RESET "\n", host); return; }
    struct in_addr ia = { ip };
    int raw = 1;
    int s = socket(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP);
    if (s < 0) { s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_ICMP); raw = 0; }
    if (s < 0) { p(C_RED "ping: socket: %s" C_RESET "\n", strerror(errno)); return; }
    p("PING %s (%s): 56 data bytes\n", host, inet_ntoa(ia));
    int ok = 0; double sum = 0, mn = 1e9, mx = 0;
    uint16_t id = getpid() & 0xffff;
    for (int seq = 1; seq <= count && !interrupted; seq++) {
        unsigned char pkt[64] = { 8, 0 };
        pkt[4] = id >> 8; pkt[5] = id; pkt[6] = seq >> 8; pkt[7] = seq;
        for (int i = 8; i < 64; i++) pkt[i] = i;
        uint16_t c = csum(pkt, 64); memcpy(pkt + 2, &c, 2);
        struct sockaddr_in to = { .sin_family = AF_INET, .sin_addr.s_addr = ip };
        double t0 = now();
        sendto(s, pkt, 64, 0, (struct sockaddr *)&to, sizeof to);
        int got = 0;
        while (now() - t0 < 1.0) {
            struct pollfd pf = { s, POLLIN, 0 };
            if (poll(&pf, 1, (int)((1.0 - (now() - t0)) * 1000) + 1) <= 0) break;
            unsigned char r[1500];
            int n = recv(s, r, sizeof r, 0);
            int off = raw ? (r[0] & 15) * 4 : 0;
            if (n < off + 8) continue;
            unsigned char *ic = r + off;
            if (ic[0] != 0 || ((ic[6] << 8) | ic[7]) != seq) continue;
            double ms = (now() - t0) * 1000;
            p("64 bytes from %s: icmp_seq=%d ttl=%d time=%.1f ms\n", inet_ntoa(ia), seq, raw ? r[8] : 64, ms);
            sum += ms; mn = fmin(mn, ms); mx = fmax(mx, ms); ok++; got = 1;
            break;
        }
        if (!got) p("Request timeout for icmp_seq %d\n", seq);
        fflush(stdout);
        double wait = 1.0 - (now() - t0);
        if (seq < count && wait > 0) usleep((useconds_t)(wait * 1e6));
    }
    close(s);
    p("--- %s ping statistics ---\n%d packets transmitted, %d received, %.0f%% packet loss\n", host, count, ok, 100.0 * (count - ok) / count);
    if (ok) p("round-trip min/avg/max = %.1f/%.1f/%.1f ms\n", mn, sum / ok, mx);
}
static void cmd_curl(int argc, char **argv) {
    const char *url = 0; int head = 0;
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-I") || !strcmp(argv[i], "-i")) head = 1; else url = argv[i]; }
    if (!url) { p("usage: curl [-I] URL\n"); return; }
    char *u = strstr(url, "://") ? xstrdup(url) : strfmt("https://%s", url);
    HttpResp *r = http_request("GET", u, 0, 0, 2 << 20);
    if (!r->status) p(C_RED "curl: %s" C_RESET "\n", r->err[0] ? r->err : "failed");
    else {
        p(C_DIM "HTTP %d · %s · %zu bytes · %.0f ms · %s" C_RESET "\n", r->status, r->content_type ? r->content_type : "?", r->len, r->ms,
          !strncmp(r->url, "https", 5) ? "TLS by BearSSL, HTTP by w12" : "HTTP by w12");
        if (!head && r->body) {
            size_t n = r->len > 6000 ? 6000 : r->len;
            fwrite(r->body, 1, n, stdout);
            if (n < r->len) p(C_DIM "\n... (%zu more bytes)" C_RESET, r->len - n);
            p("\n");
        }
    }
    http_free(r); free(u);
}
static int open_app(const char *exe, const char *arg) {
    int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", W12_SOCK);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
    char b[600]; snprintf(b, sizeof b, "%s\t%s", exe, arg ? arg : "");
    msg_send(s, M_LAUNCH, 0, 0, 0, 0, 0, b, -1);
    usleep(50000);
    close(s);
    return 0;
}
static void kernel_version(char *o, int n) { struct utsname u; uname(&u); snprintf(o, n, "%s %s %s", u.sysname, u.release, u.machine); }

static void cmd_proof(int argc, char **argv) {
    char kv[128]; kernel_version(kv, sizeof kv);
    p(C_CYAN C_BOLD "Windows 12 proof" C_RESET "  (unofficial fan project, not affiliated with Microsoft)\n");
    p("Kernel: " C_BOLD "%s" C_RESET "\n", kv);
    p(C_YEL "Linux kernel below. Everything above /dev/dri and /dev/input is written from scratch in this repo." C_RESET "\n\n");
    p(C_BOLD "Running processes (/proc):" C_RESET "\n");
    DIR *d = opendir("/proc"); struct dirent *e;
    int nk = 0, nu = 0;
    char kthreads[2048] = "";
    while (d && (e = readdir(d))) {
        int pid = atoi(e->d_name);
        if (pid <= 0) continue;
        char name[64], exe[256]; long rss;
        if (proc_name(pid, name, sizeof name, exe, sizeof exe, &rss)) continue;
        if (exe[0] == '[') { nk++; if (strlen(kthreads) < 1900) { strcat(kthreads, name); strcat(kthreads, " "); } continue; }
        nu++;
        p("  %5d  %-16s " C_GREEN "%s" C_RESET "\n", pid, name, exe);
    }
    if (d) closedir(d);
    p("  + %d kernel threads: " C_DIM "%.120s..." C_RESET "\n\n", nk, kthreads);
    p(C_BOLD "Lines of code (computed at build time):" C_RESET "\n");
    char *loc = read_file("/share/proof/loc.txt", 0);
    if (loc) {
        for (char *l = strtok(loc, "\n"); l; l = strtok(0, "\n")) { char *sp = strrchr(l, ' '); if (sp) { *sp = 0; p("  %-14s %7s\n", l, sp + 1); } }
        free(loc);
    }
    p("\n" C_BOLD "Toolkit scan (scripts/verify_no_toolkits.sh, at build time):" C_RESET "\n");
    char *scan = read_file("/share/proof/scan.txt", 0);
    if (scan) { p(C_GREEN "  %s" C_RESET, scan); free(scan); }
    p("\n" C_BOLD "Root filesystem:" C_RESET "\n");
    const char *roots[] = { "/sbin", "/bin", "/share/fonts", "/share/wallpapers", "/share/proof", "/etc" };
    for (int i = 0; i < 6; i++) {
        DIR *dd = opendir(roots[i]); struct dirent *f;
        while (dd && (f = readdir(dd))) {
            if (f->d_name[0] == '.') continue;
            char pth[512]; struct stat st; snprintf(pth, sizeof pth, "%s/%s", roots[i], f->d_name);
            if (stat(pth, &st)) continue;
            char sz[32]; human_size(st.st_size, sz, sizeof sz);
            p("  %9s  %s\n", sz, pth);
        }
        if (dd) closedir(dd);
    }
    p(C_DIM "  (/init -> /sbin/init; /proc /sys /dev /run /tmp are virtual; /home is the ext2 disk)" C_RESET "\n");
    p("\n" C_GREEN C_BOLD "%d user processes, all ours. No X11, no Wayland, no toolkit, no browser engine." C_RESET "\n", nu);
    if (argc > 1 && !strcmp(argv[1], "--gui")) open_app("proof", 0);
    MARK("PROOF terminal %d processes", nu);
}

static void cmd_fetch(void) {
    char kv[128]; kernel_version(kv, sizeof kv);
    NetInfo ni; netinfo_read(&ni);
    char *m = read_file("/proc/meminfo", 0); long tot = m ? atol(strstr(m, "MemTotal:") + 9) : 0; free(m);
    double up = 0; char *u = read_file("/proc/uptime", 0); if (u) { up = atof(u); free(u); }
    p(C_CYAN C_BOLD "   user@windows12" C_RESET "\n   ---------------\n");
    p(C_CYAN "   OS" C_RESET "       Windows 12 " W12_VERSION " (unofficial fan project)\n");
    p(C_CYAN "   Kernel" C_RESET "   %s\n", kv);
    p(C_CYAN "   Uptime" C_RESET "   %dm %ds\n", (int)(up / 60), (int)fmod(up, 60));
    p(C_CYAN "   Shell" C_RESET "    w12sh (built in, no busybox)\n");
    p(C_CYAN "   Display" C_RESET "  1920x1080, DRM/KMS dumb buffers (virtio-gpu)\n");
    p(C_CYAN "   UI" C_RESET "       w12-compositor + libw12ui (own code)\n");
    p(C_CYAN "   Font" C_RESET "     Selawik via stb_truetype\n");
    p(C_CYAN "   Memory" C_RESET "   %ld MB\n", tot / 1024);
    p(C_CYAN "   Network" C_RESET "  %s %s\n", ni.up ? ni.ip : "down", ni.up ? "(own DHCP)" : "");
}

static void help(void) {
    p(C_BOLD "w12sh built-in commands" C_RESET "\n");
    const char *h[][2] = {
        { "ls [-l] [dir]", "list files" }, { "cd [dir]", "change directory" }, { "pwd", "print working directory" },
        { "cat FILE...", "print files" }, { "echo TEXT [> FILE]", "print text (or write a file)" }, { "mkdir DIR", "make a directory" },
        { "rm [-r] PATH", "remove files" }, { "touch FILE", "create an empty file" }, { "ps", "list processes" },
        { "kill [-9] PID", "end a process" }, { "uname [-a]", "kernel info" }, { "free", "memory usage" }, { "df", "disk usage" },
        { "ifconfig", "network configuration" }, { "ping [-c N] HOST", "ICMP echo (raw socket)" }, { "curl [-I] URL", "HTTP(S) with our own stack" },
        { "open APP [ARG]", "open an app (notepad, browser, ...)" }, { "proof [--gui]", "show what is running and why it's ours" },
        { "fetch", "system summary" }, { "date, whoami, hostname, clear, exit", "" } };
    for (size_t i = 0; i < sizeof h / sizeof h[0]; i++) p("  " C_CYAN "%-22s" C_RESET " %s\n", h[i][0], h[i][1]);
}

// ---- parsing ----------------------------------------------------------------------------------------
static int split(char *s, char **argv, int max, char **redir) {
    int n = 0;
    *redir = 0;
    while (*s && n < max - 1) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (*s == '>') { s++; while (*s == ' ') s++; *redir = s; while (*s && *s != ' ') s++; if (*s) *s++ = 0; continue; }
        char q = (*s == '"' || *s == '\'') ? *s++ : 0;
        argv[n++] = s;
        while (*s && (q ? *s != q : (*s != ' ' && *s != '\t' && *s != '>'))) s++;
        if (*s == '>') { *s++ = 0; while (*s == ' ') s++; *redir = s; while (*s && *s != ' ') s++; if (*s) *s++ = 0; continue; }
        if (*s) *s++ = 0;
    }
    argv[n] = 0;
    return n;
}

static void run(char *line) {
    char *argv[64], *redir;
    int argc = split(line, argv, 64, &redir);
    if (!argc) return;
    int saved = -1;
    if (redir && *redir) {
        int fd = open(redir, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { p(C_RED "w12sh: %s: %s" C_RESET "\n", redir, strerror(errno)); return; }
        fflush(stdout); saved = dup(1); dup2(fd, 1); close(fd);
    }
    const char *c = argv[0];
    interrupted = 0;
    if (!strcmp(c, "ls") || !strcmp(c, "dir")) cmd_ls(argc, argv);
    else if (!strcmp(c, "cd")) { const char *d = argc > 1 ? argv[1] : home(); if (!strcmp(d, "~")) d = home(); if (chdir(d)) p(C_RED "cd: %s: %s" C_RESET "\n", d, strerror(errno)); }
    else if (!strcmp(c, "pwd")) { char b[512]; p("%s\n", getcwd(b, sizeof b) ? b : "?"); }
    else if (!strcmp(c, "cat")) cmd_cat(argc, argv);
    else if (!strcmp(c, "echo")) { for (int i = 1; i < argc; i++) p("%s%s", argv[i], i + 1 < argc ? " " : ""); p("\n"); }
    else if (!strcmp(c, "mkdir")) { for (int i = 1; i < argc; i++) if (mkdir(argv[i], 0755)) p(C_RED "mkdir: %s: %s" C_RESET "\n", argv[i], strerror(errno)); }
    else if (!strcmp(c, "rm")) {
        int r = 0;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') { r = strchr(argv[i], 'r') != 0; continue; }
            struct stat st;
            if (!r && !stat(argv[i], &st) && S_ISDIR(st.st_mode)) { p(C_RED "rm: %s is a directory (use rm -r)" C_RESET "\n", argv[i]); continue; }
            if (rm_rf(argv[i])) p(C_RED "rm: %s: %s" C_RESET "\n", argv[i], strerror(errno));
        }
    }
    else if (!strcmp(c, "touch")) { for (int i = 1; i < argc; i++) { int fd = open(argv[i], O_WRONLY | O_CREAT, 0644); if (fd < 0) p(C_RED "touch: %s" C_RESET "\n", strerror(errno)); else close(fd); } }
    else if (!strcmp(c, "ps")) cmd_ps();
    else if (!strcmp(c, "kill")) {
        int sig = SIGTERM;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') { sig = atoi(argv[i] + 1); continue; }
            if (kill(atoi(argv[i]), sig)) p(C_RED "kill: %s: %s" C_RESET "\n", argv[i], strerror(errno));
        }
    }
    else if (!strcmp(c, "uname")) {
        struct utsname u; uname(&u);
        if (argc > 1 && strchr(argv[1], 'a')) p("%s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
        else p("%s\n", u.sysname);
    }
    else if (!strcmp(c, "free")) cmd_free();
    else if (!strcmp(c, "df")) cmd_df();
    else if (!strcmp(c, "ifconfig") || !strcmp(c, "ipconfig")) cmd_ifconfig();
    else if (!strcmp(c, "ping")) cmd_ping(argc, argv);
    else if (!strcmp(c, "curl")) cmd_curl(argc, argv);
    else if (!strcmp(c, "clear") || !strcmp(c, "cls")) p("\x1b[2J\x1b[H");
    else if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "proof")) cmd_proof(argc, argv);
    else if (!strcmp(c, "fetch") || !strcmp(c, "neofetch")) cmd_fetch();
    else if (!strcmp(c, "date")) { time_t t = time(0); char b[64]; strftime(b, sizeof b, "%A %d %B %Y %H:%M:%S", localtime(&t)); p("%s\n", b); }
    else if (!strcmp(c, "whoami")) p("user\n");
    else if (!strcmp(c, "hostname")) { char b[64]; gethostname(b, sizeof b); p("%s\n", b); }
    else if (!strcmp(c, "exit")) { fflush(stdout); exit(0); }
    else if (!strcmp(c, "open") || !strcmp(c, "start")) { if (argc < 2 || open_app(argv[1], argc > 2 ? argv[2] : 0)) p("usage: open APP [ARG]\n"); }
    else if (app_by_exe(c) >= 0) open_app(c, argc > 1 ? argv[1] : 0);
    else p(C_RED "w12sh: %s: command not found" C_RESET " (try " C_CYAN "help" C_RESET ")\n", c);
    fflush(stdout);
    if (saved >= 0) { dup2(saved, 1); close(saved); }
}

static void on_int(int s) { (void)s; interrupted = 1; }

// ---- line editor: raw tty, our own echo, history on Up/Down, Ctrl+C / Ctrl+L / Ctrl+U ------------
static char *hist[200]; static int nhist;
static int read_line(char *out, int cap, const char *prompt) {
    int n = 0, h = nhist;
    out[0] = 0;
    for (;;) {
        unsigned char c;
        int r = read(0, &c, 1);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) { if (interrupted) { interrupted = 0; p("^C\r\n"); out[0] = 0; return 0; } continue; }
            return -1;
        }
        if (c == '\r' || c == '\n') { p("\r\n"); out[n] = 0; return n; }
        if (c == 3) { p("^C\r\n"); out[0] = 0; return 0; }
        if (c == 4 && !n) return -1;
        if (c == 12) { p("\x1b[2J\x1b[H%s%s", prompt, out); fflush(stdout); continue; }
        if (c == 21) { while (n) { p("\b \b"); n--; } out[0] = 0; fflush(stdout); continue; }
        if (c == 127 || c == 8) {
            if (n) { do n--; while (n && ((unsigned char)out[n] & 0xc0) == 0x80); out[n] = 0; p("\b \b"); fflush(stdout); }
            continue;
        }
        if (c == 27) {   // ESC [ A/B (history), others ignored
            unsigned char a, b;
            if (read(0, &a, 1) != 1 || a != '[' || read(0, &b, 1) != 1) continue;
            if ((b == 'A' && h > 0) || (b == 'B' && h < nhist)) {
                h += b == 'A' ? -1 : 1;
                while (n) { p("\b \b"); n--; }
                snprintf(out, cap, "%s", h < nhist ? hist[h] : "");
                n = strlen(out);
                p("%s", out); fflush(stdout);
            }
            continue;
        }
        if (c == '\t') continue;
        if (c >= 32 && n < cap - 1) { out[n++] = c; out[n] = 0; write(1, &c, 1); }
    }
}

int main(void) {
    // our network library logs go to the serial console, not into the terminal
    int con = open("/dev/console", O_WRONLY | O_CLOEXEC);
    if (con >= 0) log_fd = con;
    struct sigaction sa = { .sa_handler = on_int };
    sigaction(SIGINT, &sa, 0);
    setvbuf(stdout, 0, _IOLBF, 0);
    chdir(home());
    struct termios cooked, raw;
    int tty = tcgetattr(0, &cooked) == 0;
    raw = cooked;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    p(C_CYAN C_BOLD "Windows 12" C_RESET " Terminal · w12sh " W12_VERSION "\n");
    p(C_DIM "Type 'help' for commands, 'proof' to see what this OS is made of. Up/Down recall history." C_RESET "\n\n");
    char line[2048];
    for (;;) {
        char cwd[512], prompt[700]; pretty_cwd(cwd, sizeof cwd);
        snprintf(prompt, sizeof prompt, C_GREEN "user@windows12" C_RESET " " C_BLUE "%s" C_RESET " > ", cwd);
        p("%s", prompt);
        fflush(stdout);
        if (tty) tcsetattr(0, TCSANOW, &raw);
        int r = tty ? read_line(line, sizeof line, prompt) : (fgets(line, sizeof line, stdin) ? (int)strlen(line) : -1);
        if (tty) tcsetattr(0, TCSANOW, &cooked);   // commands run with a normal tty (Ctrl+C -> SIGINT)
        if (r < 0) break;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] && (!nhist || strcmp(hist[nhist - 1], line))) {
            if (nhist == 200) { free(hist[0]); memmove(hist, hist + 1, 199 * sizeof *hist); nhist--; }
            hist[nhist++] = strdup(line);
        }
        MARK("SHELL %s", line);
        run(line);
    }
    return 0;
}
