// Small utilities shared by every FLX OS program (from Lumen OS 2, MIT).
#include "w12.h"
#include <sys/stat.h>
#include <unistd.h>

double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int log_fd = 2;
static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;
static void vlog(const char *pre, const char *fmt, va_list ap) {
    char buf[1200];
    int m = snprintf(buf, 64, "%s", pre);
    int n = vsnprintf(buf + m, sizeof buf - m - 2, fmt, ap);
    if (n < 0) return;
    n += m;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    pthread_mutex_lock(&log_mu);
    write(log_fd, buf, n);
    pthread_mutex_unlock(&log_mu);
}
void LOG(const char *fmt, ...) {
    char pre[64];
    snprintf(pre, sizeof pre, "[%8.3f] ", now());
    va_list ap; va_start(ap, fmt); vlog(pre, fmt, ap); va_end(ap);
}
void MARK(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("W12:", fmt, ap); va_end(ap);
}

char *xstrdup(const char *s) { return s ? strdup(s) : strdup(""); }
char *strfmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *r = 0;
    if (vasprintf(&r, fmt, ap) < 0) r = 0;
    va_end(ap);
    return r;
}
char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t cap = 4096, n = 0;
    char *b = malloc(cap);
    size_t r;
    while ((r = fread(b + n, 1, cap - n - 1, f)) > 0) {
        n += r;
        if (cap - n < 2) b = realloc(b, cap *= 2);
    }
    fclose(f);
    b[n] = 0;
    if (len) *len = n;
    return b;
}
int write_file(const char *path, const void *d, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(d, 1, n, f);
    int r = fclose(f);
    return w == n && r == 0 ? 0 : -1;
}
void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}
void human_size(double b, char *out, int n) {
    const char *u[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    while (b >= 1024 && i < 4) { b /= 1024; i++; }
    if (i == 0) snprintf(out, n, "%.0f %s", b, u[i]);
    else snprintf(out, n, b < 10 ? "%.1f %s" : "%.0f %s", b, u[i]);
}
