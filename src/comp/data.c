// Live data for the shell: weather (HTTPS to open-meteo via our own net stack, cached on
// the home disk) and system stats (CPU/RAM from /proc).
#include "comp.h"
#include <unistd.h>

Weather weather;
float cpu_pct, mem_pct;
long mem_total_kb, mem_used_kb;
static pthread_mutex_t wmu = PTHREAD_MUTEX_INITIALIZER;
#define CACHE "/home/.config/weather.json"
#define WURL "https://api.open-meteo.com/v1/forecast?latitude=12.65&longitude=-8.00&current=temperature_2m,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"

const char *weather_desc(int c) {
    if (c == 0) return "Ciel dégagé";
    if (c <= 2) return "Peu nuageux";
    if (c == 3) return "Nuageux";
    if (c == 45 || c == 48) return "Brouillard";
    if (c >= 51 && c <= 57) return "Bruine";
    if (c >= 61 && c <= 67) return "Pluie";
    if (c >= 71 && c <= 77) return "Neige";
    if (c >= 80 && c <= 82) return "Averses";
    if (c >= 95) return "Orage";
    return "Nuageux";
}
int weather_glyph(int c) { return c == 0 ? G_SUN : (c >= 51 && c <= 82) || c >= 95 ? G_RAIN : G_CLOUD; }

static int parse(const char *s, Weather *w) {
    Json *j = json_parse(s);
    Json *cur = json_get(j, "current"), *d = json_get(j, "daily");
    if (!cur) { json_free(j); return -1; }
    w->temp = json_num(json_get(cur, "temperature_2m"), 0);
    w->code = (int)json_num(json_get(cur, "weather_code"), 3);
    w->wind = json_num(json_get(cur, "wind_speed_10m"), 0);
    w->tmax = json_num(json_at(json_get(d, "temperature_2m_max"), 0), w->temp);
    w->tmin = json_num(json_at(json_get(d, "temperature_2m_min"), 0), w->temp);
    snprintf(w->place, sizeof w->place, "Bamako");
    snprintf(w->cond, sizeof w->cond, "%s", weather_desc(w->code));
    w->ok = 1;
    json_free(j);
    return 0;
}

static void *weather_th(void *a) {
    (void)a;
    for (;;) {
        for (int i = 0; i < 60 && !net_up(); i++) sleep(1);
        HttpResp *r = http_get(WURL);
        Weather w = {0};
        if (r->status == 200 && r->body && parse(r->body, &w) == 0) {
            w.fetched = now();
            pthread_mutex_lock(&wmu); weather = w; pthread_mutex_unlock(&wmu);
            write_file(CACHE, r->body, r->len);
            LOG("weather: %.1f C %s (live, %.0f ms)", w.temp, w.cond, r->ms);
            MARK("WEATHER live %.0f", w.temp);
            dirty = 1;
            http_free(r);
            sleep(900);
        } else {
            LOG("weather: fetch failed (%d %s), keeping cache", r->status, r->err);
            http_free(r);
            sleep(60);
        }
    }
    return 0;
}

static long cpu_prev_idle, cpu_prev_tot;
static void stats(void) {
    char *s = read_file("/proc/stat", 0);
    if (s) {
        long u, n, sy, id, io, irq, sirq;
        if (sscanf(s, "cpu %ld %ld %ld %ld %ld %ld %ld", &u, &n, &sy, &id, &io, &irq, &sirq) == 7) {
            long tot = u + n + sy + id + io + irq + sirq, idle = id + io;
            if (cpu_prev_tot && tot > cpu_prev_tot) cpu_pct = 100.0f * (1 - (float)(idle - cpu_prev_idle) / (tot - cpu_prev_tot));
            cpu_prev_idle = idle; cpu_prev_tot = tot;
        }
        free(s);
    }
    s = read_file("/proc/meminfo", 0);
    if (s) {
        long tot = 0, avail = 0;
        char *p = strstr(s, "MemTotal:"); if (p) tot = atol(p + 9);
        p = strstr(s, "MemAvailable:"); if (p) avail = atol(p + 13);
        mem_total_kb = tot; mem_used_kb = tot - avail;
        mem_pct = tot ? 100.0f * mem_used_kb / tot : 0;
        free(s);
    }
}

void data_init(void) {
    char *c = read_file(CACHE, 0);
    if (c) { if (parse(c, &weather) == 0) LOG("weather: cached %.1f C", weather.temp); free(c); }
    stats();
    pthread_t t;
    pthread_create(&t, 0, weather_th, 0);
    pthread_detach(t);
}
void data_tick(void) {
    static double last;
    if (now() - last >= 1) { last = now(); stats(); }
}
