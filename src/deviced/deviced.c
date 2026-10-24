// Small, capability-limited hardware broker for FLX Settings.
// It exposes inventory and two fixed controls (backlight and rfkill); callers
// can never pass a path, ioctl, command, or arbitrary sysfs value.
#include "../lib/w12.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/rfkill.h>
#include <sound/asound.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/run/flx-device.sock"
#define RESPONSE_MAX 4096
static void add(char *b, size_t cap, const char *fmt, ...) {
    size_t n = strlen(b);
    if (n >= cap - 1) return;
    va_list ap; va_start(ap, fmt); vsnprintf(b + n, cap - n, fmt, ap); va_end(ap);
}
static int read_int(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) return -1;
    int v = -1; if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); return v;
}
static void read_text(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r"); if (!f) { out[0] = 0; return; }
    if (!fgets(out, n, f)) out[0] = 0;
    fclose(f); out[strcspn(out, "\r\n")] = 0;
}
static void item(char *out, const char *kind, const char *name, const char *detail) {
    add(out, RESPONSE_MAX, "%s\t%s\t%s\n", kind, name, detail);
}
static int rfkill_state(void) {
    DIR *d = opendir("/sys/class/rfkill"); if (!d) return -1;
    struct dirent *e; int state = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "rfkill", 6)) continue;
        char p[128], type[32]; snprintf(p, sizeof p, "/sys/class/rfkill/%s/type", e->d_name);
        read_text(p, type, sizeof type);
        if (strcmp(type, "bluetooth")) continue;
        snprintf(p, sizeof p, "/sys/class/rfkill/%s/hard", e->d_name);
        if (read_int(p) == 1) { state = -2; break; }
        snprintf(p, sizeof p, "/sys/class/rfkill/%s/soft", e->d_name);
        state = read_int(p) == 0; break;
    }
    closedir(d); return state;
}
static int set_bluetooth(int on) {
    DIR *d = opendir("/sys/class/rfkill"); if (!d) return -1;
    struct dirent *e; int fd = open("/dev/rfkill", O_WRONLY | O_CLOEXEC), rc = -1;
    while (fd >= 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "rfkill", 6)) continue;
        char p[128], type[32]; snprintf(p, sizeof p, "/sys/class/rfkill/%s/type", e->d_name);
        read_text(p, type, sizeof type); if (strcmp(type, "bluetooth")) continue;
        int idx = atoi(e->d_name + 6);
        struct rfkill_event ev = { .idx = (uint32_t)idx, .type = RFKILL_TYPE_BLUETOOTH,
            .op = RFKILL_OP_CHANGE, .soft = (uint8_t)!on, .hard = 0 };
        rc = write(fd, &ev, sizeof ev) == sizeof ev ? 0 : -1; break;
    }
    if (fd >= 0) close(fd); closedir(d); return rc;
}
static int set_brightness(int percent) {
    if (percent < 5 || percent > 100) return -1;
    DIR *d = opendir("/sys/class/backlight"); if (!d) return -1;
    struct dirent *e; int rc = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[160]; snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
        int max = read_int(p); if (max <= 0) continue;
        snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
        FILE *f = fopen(p, "w"); if (!f) continue;
        int ok = fprintf(f, "%d\n", (max * percent + 50) / 100) > 0;
        if (fclose(f)) ok = 0;
        rc = ok ? 0 : -1;
        break;
    }
    closedir(d); return rc;
}
static int mixer_control(int *card_out, struct snd_ctl_elem_id *id, struct snd_ctl_elem_info *info) {
    static const char *wanted[] = { "Master Playback Volume", "PCM Playback Volume", "Speaker Playback Volume", "Headphone Playback Volume", NULL };
    for (int card = 0; card < 8; card++) {
        char path[48]; snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
        int fd = open(path, O_RDWR | O_CLOEXEC); if (fd < 0) continue;
        struct snd_ctl_elem_list list = {0};
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0 || !list.count || list.count > 1024) { close(fd); continue; }
        list.space = list.count; list.pids = calloc(list.space, sizeof *list.pids);
        if (!list.pids) { close(fd); continue; }
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0) {
            for (unsigned int i = 0; i < list.used; i++) {
                for (int w = 0; wanted[w]; w++) if (!strcmp((char *)list.pids[i].name, wanted[w])) {
                    memset(info, 0, sizeof *info); info->id = list.pids[i];
                    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, info) == 0 && info->type == SNDRV_CTL_ELEM_TYPE_INTEGER && (info->access & SNDRV_CTL_ELEM_ACCESS_WRITE)) {
                        *id = list.pids[i]; *card_out = card; free(list.pids); return fd;
                    }
                }
            }
        }
        free(list.pids); close(fd);
    }
    return -1;
}
static int mixer_volume(int set, int percent, int *current) {
    int card = 0; struct snd_ctl_elem_id id; struct snd_ctl_elem_info info;
    int fd = mixer_control(&card, &id, &info); (void)card;
    if (fd < 0 || info.value.integer.max <= info.value.integer.min) { if (fd >= 0) close(fd); return -1; }
    struct snd_ctl_elem_value val = {0}; val.id = id;
    int rc = -1;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &val) == 0) {
        long min = info.value.integer.min, max = info.value.integer.max;
        if (set) {
            long level = min + (max - min) * percent / 100;
            for (unsigned int i = 0; i < info.count && i < 128; i++) val.value.integer.value[i] = level;
            rc = ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) >= 0 ? 0 : -1;
            if (rc == 0 && ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &val) < 0) rc = -1;
        } else rc = 0;
        if (rc == 0 && current) {
            long cur = val.value.integer.value[0];
            *current = (int)((cur - min) * 100 / (max - min));
        }
    }
    close(fd); return rc;
}
static void inventory(char *out) {
    DIR *d; struct dirent *e; char p[160], v[160], dsc[192];
    d = opendir("/sys/class/backlight");
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name); int cur = read_int(p);
        snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name); int max = read_int(p);
        snprintf(dsc, sizeof dsc, "%d/%d", cur, max); item(out, "DISPLAY", e->d_name, dsc); }
        closedir(d); }
    d = opendir("/sys/class/drm");
    if (d) { while ((e = readdir(d))) { if (strchr(e->d_name, '-') == NULL) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/status", e->d_name); read_text(p, v, sizeof v);
        if (*v) item(out, "SCREEN", e->d_name, v); }
        closedir(d); }
    d = opendir("/sys/class/sound");
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/sound/%s", e->d_name); ssize_t n = readlink(p, v, sizeof v-1);
        if (n > 0) { v[n] = 0; char *base = strrchr(v, '/'); item(out, "SOUND", e->d_name, base ? base+1 : v); } }
        closedir(d); }
    int volume = -1;
    if (!mixer_volume(0, 0, &volume)) { snprintf(dsc, sizeof dsc, "%d%%", volume); item(out, "VOLUME", "Volume de sortie", dsc); }
    else item(out, "VOLUME", "Volume de sortie", "indisponible (aucun mixeur matériel)");
    int bt = rfkill_state(); item(out, "BLUETOOTH", "Bluetooth", bt == -1 ? "aucun adaptateur détecté" : bt == -2 ? "bloqué par le matériel" : bt ? "activé" : "désactivé");
    d = opendir("/sys/class/net");
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name); read_text(p, v, sizeof v); item(out, "NETWORK", e->d_name, v); }
        closedir(d); }
    d = opendir("/sys/class/power_supply");
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name); read_text(p, v, sizeof v);
        snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name); int c = read_int(p);
        snprintf(dsc, sizeof dsc, "%s%s", v[0] ? v : "alimentation", c >= 0 ? "" : " · secteur");
        item(out, "POWER", e->d_name, dsc); }
        closedir(d); }
    d = opendir("/sys/block");
    if (d) { while ((e = readdir(d))) { if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/block/%s/size", e->d_name); long long sectors = -1;
        FILE *f = fopen(p, "r"); if (f) { fscanf(f, "%lld", &sectors); fclose(f); }
        snprintf(dsc, sizeof dsc, "%s · %.1f Go", e->d_name, sectors > 0 ? sectors * 512.0 / 1e9 : 0); item(out, "STORAGE", e->d_name, dsc); }
        closedir(d); }
    d = opendir("/sys/class/input");
    if (d) { while ((e = readdir(d))) { if (strncmp(e->d_name, "event", 5)) continue;
        snprintf(p, sizeof p, "/sys/class/input/%s/device/name", e->d_name); read_text(p, v, sizeof v);
        if (!*v) snprintf(v, sizeof v, "%s", e->d_name); item(out, "INPUT", e->d_name, v); }
        closedir(d); }
    if (!*out) snprintf(out, RESPONSE_MAX, "INFO\tPériphériques\tAucun périphérique exposé par le noyau.\n");
}
static void handle(int fd) {
    char req[96] = {0}, out[RESPONSE_MAX] = {0};
    ssize_t n = recv(fd, req, sizeof req - 1, 0); if (n <= 0) return;
    if (!strcmp(req, "GET")) inventory(out);
    else if (!strncmp(req, "BRIGHTNESS ", 11)) {
        int v = atoi(req + 11); snprintf(out, sizeof out, "%s", set_brightness(v) ? "UNAVAILABLE" : "OK");
    } else if (!strcmp(req, "VOLUME GET")) {
        int v; if (!mixer_volume(0, 0, &v)) snprintf(out, sizeof out, "%d", v); else snprintf(out, sizeof out, "UNAVAILABLE");
    } else if (!strncmp(req, "VOLUME ", 7)) {
        char *end = NULL; long v = strtol(req + 7, &end, 10);
        snprintf(out, sizeof out, "%s", end && !*end && v >= 0 && v <= 100 && !mixer_volume(1, (int)v, NULL) ? "OK" : "UNAVAILABLE");
    } else if (!strncmp(req, "BLUETOOTH ", 10) && (!strcmp(req + 10, "on") || !strcmp(req + 10, "off"))) {
        snprintf(out, sizeof out, "%s", set_bluetooth(!strcmp(req + 10, "on")) ? "UNAVAILABLE" : "OK");
    } else snprintf(out, sizeof out, "INVALID");
    send(fd, out, strlen(out), MSG_NOSIGNAL);
}
int main(void) {
    unlink(SOCK_PATH);
    int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0); if (s < 0) return 1;
    struct sockaddr_un a = { .sun_family = AF_UNIX }; snprintf(a.sun_path, sizeof a.sun_path, "%s", SOCK_PATH);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || chmod(SOCK_PATH, 0666) || listen(s, 8)) return 2;
    for (;;) { int c = accept4(s, NULL, NULL, SOCK_CLOEXEC); if (c < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
        handle(c); close(c); }
}
