// FLX OS side panels: the Widgets board (left) and Copilot/AI assistant (right).
// Copilot talks plain HTTP to the bridge on the Mac (10.0.2.2:8766) with our own HTTP
// client, streams NDJSON tokens, parses them with our own JSON parser.
#include "comp.h"
#include <unistd.h>

#define PY 48
#define PH (SCR_H - 12 - 56 - 12 - PY)
#define WX0 12
#define WW 500
#define CW 440
#define BRIDGE "http://10.0.2.2:8766/ask"
#define OFFLINE "Copilot is offline. The bridge on the host isn't running."

enum { HW_PANEL = 300, HW_NOTES, HW_REFRESH, HC_PANEL = 400, HC_INPUT, HC_SEND, HC_CLOSE, HC_CHIP = 410, HC_CLEAR = 420 };

// ---- widgets ------------------------------------------------------------------------------------
static char notes[2048];
static int notes_focus;
static float cpu_hist[60]; static int nh;
static Canvas *art;

static void make_art(void) {   // procedural "photo" for the photo widget
    art = canvas_new(476, 190);
    time_t tt = time(0);
    unsigned seed = (unsigned)(tt / 86400) * 2654435761u;
    float hue = (seed % 360) / 360.0f;
    for (int y = 0; y < art->h; y++)
        for (int x = 0; x < art->w; x++) {
            float u = x / 476.0f, v = y / 190.0f;
            float r = 0.5f + 0.5f * sinf(6.28f * (u * 1.3f + hue) + 2 * sinf(v * 3 + u * 2));
            float g = 0.5f + 0.5f * sinf(6.28f * (v * 0.9f + hue + 0.33f) + 2 * sinf(u * 4 - v));
            float b = 0.6f + 0.4f * sinf(6.28f * (u * 0.7f + v * 0.6f + hue + 0.66f));
            float m = 0.55f + 0.45f * v;
            art->px[y * art->w + x] = 0xFF000000 | (uint32_t)(r * 255 * m) << 16 | (uint32_t)(g * 200 * m + 30) << 8 | (uint32_t)(b * 255);
        }
    // a soft horizon of hills
    for (int x = 0; x < art->w; x++) {
        float hy = 130 + 18 * sinf(x * 0.02f + hue * 6) + 8 * sinf(x * 0.07f);
        for (int y = (int)hy; y < art->h; y++) art->px[y * art->w + x] = c_mix(art->px[y * art->w + x], 0xFF101830, 0.75f);
    }
}

void panels_init(void) {
    char *n = read_file("/home/.config/notes.txt", 0);
    snprintf(notes, sizeof notes, "%s", n ? n : "Buy milk\nRecord the FLX OS video\n");
    free(n);
    make_art();
}

static void card(Canvas *c, float x, float y, float w, float h, float a) {
    g_rrect(c, x, y, w, h, 10, 10, c_alpha(TH.dark ? 0xFF2A2A2E : 0xFFFFFFFF, (TH.dark ? 0.55f : 0.65f) * a));
    g_rrect_line(c, x, y, w, h, 10, 1, c_alpha(TH.dark ? 0x30FFFFFF : 0x60FFFFFF, a));
}

void widgets_draw(Canvas *c, float p) {
    p = ease_out(p);
    if (p <= 0.01f) return;
    float x = WX0 - (1 - p) * (WW + 24), y = PY;
    g_shadow(c, x, y, WW, PH, 12, 30, (int)(90 * p), 8);
    g_glass(c, x, y, WW, PH, 12, TH.dark ? 0xB81C1C20 : 0xB8F3F4F7);
    g_rrect_line(c, x, y, WW, PH, 12, 1, TH.dark ? 0x30FFFFFF : 0x50FFFFFF);
    hit_add(HW_PANEL, x, y, WW, PH);
    uint32_t fg = TH.text, sub = TH.sub;
    time_t tt = time(0);
    struct tm tm; localtime_r(&tt, &tm);
    char b[128];
    static const char *MON[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    static const char *DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    snprintf(b, sizeof b, "%d:%02d", tm.tm_hour, tm.tm_min);
    text(c, F_SEMI, 40, 0, x + 24, y + 58, fg, b);
    snprintf(b, sizeof b, "%s, %d %s", DAYS[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon]);
    text(c, F_REG, 15, 0, x + 26, y + 84, sub, b);
    float cy = y + 104, cx = x + 12, fw = WW - 24;
    // weather
    card(c, cx, cy, fw, 140, 1);
    text(c, F_SEMI, 13, 0, cx + 16, cy + 26, sub, "Weather");
    if (weather.ok) {
        icon_glyph(c, weather_glyph(weather.code), cx + 52, cy + 82, 56, weather.code == 0 ? 0xFFFFB020 : 0xFF8FB8E8);
        snprintf(b, sizeof b, "%.0f°", weather.temp);
        text(c, F_SEMILIGHT, 54, 0, cx + 100, cy + 104, fg, b);
        text(c, F_SEMI, 16, 0, cx + 210, cy + 70, fg, weather.cond);
        snprintf(b, sizeof b, "High %.0f° · Low %.0f° · Wind %.0f km/h", weather.tmax, weather.tmin, weather.wind);
        text(c, F_REG, 13, 0, cx + 210, cy + 94, sub, b);
        snprintf(b, sizeof b, "%s · %s", weather.place, weather.fetched > 0 ? "live from open-meteo.com" : "cached");
        text(c, F_REG, 12, 0, cx + 210, cy + 116, TH.dim, b);
    } else text(c, F_REG, 15, 0, cx + 16, cy + 80, sub, "Connecting to open-meteo.com…");
    cy += 152;
    // calendar
    float hw = (fw - 12) / 2;
    card(c, cx, cy, hw, 250, 1);
    snprintf(b, sizeof b, "%s %d", MON[tm.tm_mon], tm.tm_year + 1900);
    text(c, F_SEMI, 14, 0, cx + 16, cy + 28, fg, b);
    const char *wd = "MTWTFSS";
    for (int i = 0; i < 7; i++) { char s2[2] = { wd[i], 0 }; text_c(c, F_SEMI, 12, 0, cx + 26 + i * 30, cy + 56, sub, s2); }
    int first = (tm.tm_wday - (tm.tm_mday - 1) % 7 + 7 + 6) % 7;   // Monday = 0
    static const int ML[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int days = ML[tm.tm_mon] + (tm.tm_mon == 1 && (tm.tm_year % 4 == 0));
    for (int d = 1; d <= days; d++) {
        int k = first + d - 1;
        float dx = cx + 26 + (k % 7) * 30, dy = cy + 82 + (k / 7) * 28;
        if (d == tm.tm_mday) g_circle(c, dx, dy - 5, 13, TH.accent);
        snprintf(b, sizeof b, "%d", d);
        text_c(c, d == tm.tm_mday ? F_SEMI : F_REG, 13, 0, dx, dy, d == tm.tm_mday ? TH.accent_fg : fg, b);
    }
    // system
    float sx = cx + hw + 12;
    card(c, sx, cy, hw, 250, 1);
    text(c, F_SEMI, 14, 0, sx + 16, cy + 28, fg, "System");
    snprintf(b, sizeof b, "CPU  %.0f%%", cpu_pct);
    text(c, F_REG, 13, 0, sx + 16, cy + 58, sub, b);
    g_rrect(c, sx + 16, cy + 66, hw - 32, 6, 3, 3, TH.dark ? 0x30FFFFFF : 0x20000000);
    g_rrect(c, sx + 16, cy + 66, (hw - 32) * clampf(cpu_pct / 100, 0.02f, 1), 6, 3, 3, TH.accent);
    for (int i = 1; i < nh; i++) {
        float x0 = sx + 16 + (i - 1) * (hw - 32) / 59.0f, x1 = sx + 16 + i * (hw - 32) / 59.0f;
        g_line(c, x0, cy + 140 - cpu_hist[i - 1] * 0.5f, x1, cy + 140 - cpu_hist[i] * 0.5f, 1.6f, TH.accent);
    }
    g_rect(c, sx + 16, cy + 140, hw - 32, 1, TH.line);
    char used[32], tot[32];
    human_size(mem_used_kb * 1024.0, used, sizeof used); human_size(mem_total_kb * 1024.0, tot, sizeof tot);
    snprintf(b, sizeof b, "Memory  %s / %s", used, tot);
    text(c, F_REG, 13, 0, sx + 16, cy + 172, sub, b);
    g_rrect(c, sx + 16, cy + 180, hw - 32, 6, 3, 3, TH.dark ? 0x30FFFFFF : 0x20000000);
    g_rrect(c, sx + 16, cy + 180, (hw - 32) * clampf(mem_pct / 100, 0.02f, 1), 6, 3, 3, 0xFF8B5CF6);
    double up = 0; char *u = read_file("/proc/uptime", 0); if (u) { up = atof(u); free(u); }
    snprintf(b, sizeof b, "Up %dh %02dm · from /proc", (int)(up / 3600), (int)fmod(up / 60, 60));
    text(c, F_REG, 12, 0, sx + 16, cy + 222, TH.dim, b);
    cy += 262;
    // notes
    card(c, cx, cy, fw, 150, 1);
    hit_add(HW_NOTES, cx, cy, fw, 150);
    text(c, F_SEMI, 14, 0, cx + 16, cy + 28, fg, "Notes");
    text_r(c, F_REG, 12, 0, cx + fw - 16, cy + 28, TH.dim, notes_focus ? "Typing… saved automatically" : "Click to edit");
    if (notes_focus) g_rrect_line(c, cx + 1, cy + 1, fw - 2, 148, 9, 1.5f, TH.accent);
    int st[12], ln[12];
    int nl = text_wrap(F_REG, 14, notes, fw - 32, st, ln, 12);
    int show0 = maxi(0, nl - 5);
    for (int i = show0; i < nl; i++) text_n(c, F_REG, 14, 0, cx + 16, cy + 56 + (i - show0) * 20, fg, notes + st[i], ln[i]);
    if (notes_focus && nl) {
        float lx = cx + 16 + text_wn(F_REG, 14, 0, notes + st[nl - 1], ln[nl - 1]);
        g_rect(c, lx + 1, cy + 42 + (nl - 1 - show0) * 20, 1.5f, 18, TH.text);
    }
    cy += 162;
    // photo
    float ph = fminf(190, y + PH - 12 - cy);
    if (ph > 60) {
        g_buf(c, art->px, art->w, art->w, (int)ph, cx, cy, fw, ph, 10, 10, 255, 1);
        text(c, F_SEMI, 14, 0, cx + 16, cy + ph - 18, 0xFFFFFFFF, "Daily art · generated on this PC");
    }
}

int widgets_mouse(IEv *e, float p) {
    (void)p;
    int h = hit_at(e->x, e->y);
    if (e->type == IE_DOWN) {
        if (h == HW_NOTES) { notes_focus = 1; dirty = 1; return 1; }
        if (h == HW_PANEL) { notes_focus = 0; dirty = 1; return 1; }
    }
    return h >= HW_PANEL && h < HC_PANEL;
}
int widgets_key(IEv *e) {
    if (!notes_focus) return 0;
    int n = strlen(notes);
    if (e->key == K_BACKSPACE) { if (n) notes[n - 1] = 0; }
    else if (e->key == K_ENTER) { if (n < (int)sizeof notes - 2) { notes[n] = '\n'; notes[n + 1] = 0; } }
    else if (e->text[0] && n < (int)sizeof notes - 2) { notes[n] = e->text[0]; notes[n + 1] = 0; }
    else return 0;
    write_file("/home/.config/notes.txt", notes, strlen(notes));
    return 1;
}

// ---- copilot ----------------------------------------------------------------------------------------
typedef struct { int user; char *text; } Bubble;
static Bubble chat[80]; static int nchat;
static char cinput[1024];
int copilot_busy;
static pthread_mutex_t cmu = PTHREAD_MUTEX_INITIALIZER;
static float cscroll; static int stick_bottom = 1;
static const char *CHIPS[3] = { "What makes FLX OS different?", "Write a haiku about a glass taskbar", "How does this OS boot?" };

static void append(Bubble *b, const char *s) {
    size_t a = b->text ? strlen(b->text) : 0, n = strlen(s);
    b->text = realloc(b->text, a + n + 1);
    memcpy(b->text + a, s, n + 1);
}

static void *ask_th(void *arg) {
    (void)arg;
    // request JSON: {"prompt": "...", "history": [{"role": "user|assistant", "text": "..."}]}
    pthread_mutex_lock(&cmu);
    size_t cap = 4096; char *body = malloc(cap); size_t n = 0;
    char *pr = json_escape(chat[nchat - 2].text);
    n += snprintf(body + n, cap - n, "{\"prompt\": %s, \"history\": [", pr);
    free(pr);
    for (int i = 0; i < nchat - 2; i++) {
        char *t = json_escape(chat[i].text ? chat[i].text : "");
        size_t need = strlen(t) + 64;
        if (n + need >= cap) { cap = (n + need) * 2; body = realloc(body, cap); }
        n += snprintf(body + n, cap - n, "%s{\"role\": \"%s\", \"text\": %s}", i ? ", " : "", chat[i].user ? "user" : "assistant", t);
        free(t);
    }
    if (n + 4 >= cap) body = realloc(body, cap = n + 8);
    n += snprintf(body + n, cap - n, "]}");
    Bubble *out = &chat[nchat - 1];
    pthread_mutex_unlock(&cmu);
    int status = 0;
    char err[128] = "";
    double t0 = now();
    HttpStream *s = http_open_req("POST", BRIDGE, body, "application/json", &status, err, sizeof err);
    free(body);
    int chars = 0, ok = 0;
    if (!s || status != 200) {
        LOG("copilot: bridge unreachable (%d %s)", status, err);
        pthread_mutex_lock(&cmu); append(out, OFFLINE); pthread_mutex_unlock(&cmu);
        if (s) http_close(s);
        MARK("COPILOT_DONE offline");
    } else {
        char line[8192]; int ll = 0;
        char buf[2048];
        int k;
        while ((k = http_read(s, buf, sizeof buf)) > 0) {
            for (int i = 0; i < k; i++) {
                if (buf[i] != '\n') { if (ll < (int)sizeof line - 1) line[ll++] = buf[i]; continue; }
                line[ll] = 0; ll = 0;
                Json *j = json_parse(line);
                const char *t = json_str(json_get(j, "t"), 0), *e = json_str(json_get(j, "error"), 0);
                pthread_mutex_lock(&cmu);
                if (t && *t) { append(out, t); chars += strlen(t); }
                if (e) append(out, e);
                pthread_mutex_unlock(&cmu);
                if (json_get(j, "done")) ok = 1;
                json_free(j);
                dirty = 1;
            }
        }
        http_close(s);
        pthread_mutex_lock(&cmu);
        if (!chars && !ok) append(out, OFFLINE);
        pthread_mutex_unlock(&cmu);
        LOG("copilot: answer %d chars in %.1f s", chars, now() - t0);
        MARK("COPILOT_DONE %s %d", chars ? "ok" : "empty", chars);
    }
    copilot_busy = 0;
    dirty = 1;
    return 0;
}

static void send_prompt(const char *p) {
    if (copilot_busy || !*p || nchat >= 78) return;
    pthread_mutex_lock(&cmu);
    chat[nchat++] = (Bubble){ 1, xstrdup(p) };
    chat[nchat++] = (Bubble){ 0, 0 };
    pthread_mutex_unlock(&cmu);
    copilot_busy = 1; stick_bottom = 1;
    MARK("COPILOT_ASK %zu", strlen(p));
    pthread_t t;
    pthread_create(&t, 0, ask_th, 0);
    pthread_detach(t);
}

static float bubble_h(const char *t, float maxw, int *nl, int *st, int *ln) {
    *nl = text_wrap(F_REG, 15, t && *t ? t : " ", maxw, st, ln, 200);
    return *nl * 21 + 20;
}

void copilot_draw(Canvas *c, float p) {
    p = ease_out(p);
    if (p <= 0.01f) return;
    float x = SCR_W - 12 - CW + (1 - p) * (CW + 24), y = PY;
    g_shadow(c, x, y, CW, PH, 12, 30, (int)(90 * p), 8);
    g_glass(c, x, y, CW, PH, 12, TH.dark ? 0xB81C1C20 : 0xB8F3F4F7);
    g_rrect_line(c, x, y, CW, PH, 12, 1, TH.dark ? 0x30FFFFFF : 0x50FFFFFF);
    hit_add(HC_PANEL, x, y, CW, PH);
    uint32_t fg = TH.text, sub = TH.sub;
    icon_copilot(c, x + 36, y + 36, 30);
    text(c, F_SEMI, 18, 0, x + 62, y + 34, fg, "Copilot");
    text(c, F_REG, 12, 0, x + 62, y + 52, sub, "Answers come from Claude via the host bridge");
    hit_add(HC_CLEAR, x + CW - 84, y + 18, 36, 36);
    if (hover_id == HC_CLEAR) g_rrect(c, x + CW - 84, y + 18, 36, 36, 6, 6, TH.hover);
    icon_glyph(c, G_REFRESH, x + CW - 66, y + 36, 16, fg);
    hit_add(HC_CLOSE, x + CW - 46, y + 18, 36, 36);
    if (hover_id == HC_CLOSE) g_rrect(c, x + CW - 46, y + 18, 36, 36, 6, 6, TH.hover);
    icon_glyph(c, G_CLOSE, x + CW - 28, y + 36, 14, fg);
    float top = y + 72, bot = y + PH - 76;
    g_rect(c, x + 16, top - 1, CW - 32, 1, TH.dark ? 0x20FFFFFF : 0x18000000);
    pthread_mutex_lock(&cmu);
    if (!nchat) {
        text(c, F_SEMI, 22, 0, x + 28, top + 60, fg, "Hi, I'm Copilot.");
        text(c, F_REG, 15, 0, x + 28, top + 88, sub, "Ask me anything. Try one of these:");
        for (int i = 0; i < 3; i++) {
            float cy = top + 116 + i * 54;
            hit_add(HC_CHIP + i, x + 24, cy, CW - 48, 44);
            g_rrect(c, x + 24, cy, CW - 48, 44, 10, 10, hover_id == HC_CHIP + i ? TH.accent_soft : (TH.dark ? 0x30FFFFFF : 0x90FFFFFF));
            g_rrect_line(c, x + 24, cy, CW - 48, 44, 10, 1, TH.dark ? 0x30FFFFFF : 0x20000000);
            text(c, F_REG, 14, 0, x + 40, cy + 27, fg, CHIPS[i]);
        }
    } else {
        int sc[4]; g_getclip(c, sc);
        g_clip(c, x, top, x + CW, bot);
        float maxw = CW - 110, total = 12;
        static int st[200], ln[200];
        int nl;
        for (int i = 0; i < nchat; i++) total += bubble_h(chat[i].text, maxw, &nl, st, ln) + 12;
        float view = bot - top;
        if (stick_bottom) cscroll = fmaxf(0, total - view);
        cscroll = clampf(cscroll, 0, fmaxf(0, total - view));
        float by = top + 12 - cscroll;
        for (int i = 0; i < nchat; i++) {
            Bubble *b = &chat[i];
            float h = bubble_h(b->text, maxw, &nl, st, ln);
            float bw = 0;
            for (int k = 0; k < nl; k++) bw = fmaxf(bw, text_wn(F_REG, 15, 0, (b->text ? b->text : " ") + st[k], ln[k]));
            bw += 28;
            if (!b->user && !b->text) bw = 70;
            float bx = b->user ? x + CW - 20 - bw : x + 20;
            if (by + h > top && by < bot) {
                if (b->user) g_rrect(c, bx, by, bw, h, 14, 14, TH.accent);
                else { g_rrect(c, bx, by, bw, h, 14, 14, TH.dark ? 0xE0303034 : 0xF0FFFFFF); g_rrect_line(c, bx, by, bw, h, 14, 1, TH.dark ? 0x20FFFFFF : 0x14000000); }
                uint32_t tc = b->user ? TH.accent_fg : fg;
                if (b->text) for (int k = 0; k < nl; k++) text_n(c, F_REG, 15, 0, bx + 14, by + 25 + k * 21, tc, b->text + st[k], ln[k]);
                if (!b->user && i == nchat - 1 && copilot_busy) {   // typing dots / caret
                    if (!b->text) for (int d = 0; d < 3; d++) g_circle(c, bx + 22 + d * 13, by + h / 2, 3.5f, c_alpha(sub, 0.4f + 0.6f * (0.5f + 0.5f * sinf((float)now() * 6 - d))));
                    else if (fmod(now(), 0.8) < 0.5) {
                        float lx = bx + 14 + text_wn(F_REG, 15, 0, b->text + st[nl - 1], ln[nl - 1]);
                        g_rect(c, lx + 2, by + 11 + (nl - 1) * 21, 7, 17, TH.accent);
                    }
                }
            }
            by += h + 12;
        }
        g_setclip(c, sc);
    }
    pthread_mutex_unlock(&cmu);
    // input
    float ix = x + 16, iy = y + PH - 64, iw = CW - 32, ih = 48;
    hit_add(HC_INPUT, ix, iy, iw - 52, ih);
    g_rrect(c, ix, iy, iw, ih, 24, 24, TH.dark ? 0xFF2A2A2E : 0xFFFFFFFF);
    g_rrect_line(c, ix, iy, iw, ih, 24, 1.5f, TH.accent);
    int sc2[4]; g_getclip(c, sc2);
    g_clip(c, ix + 16, iy, ix + iw - 56, iy + ih);
    float tw = text_w(F_REG, 15, 0, cinput), tx = ix + 20;
    if (tw > iw - 90) tx = ix + iw - 66 - tw;
    if (cinput[0]) text(c, F_REG, 15, 0, tx, iy + 30, fg, cinput);
    else text(c, F_REG, 15, 0, ix + 20, iy + 30, TH.dim, copilot_busy ? "Copilot is answering…" : "Message Copilot");
    if (!copilot_busy) g_rect(c, (cinput[0] ? tx + tw : ix + 20) + 1, iy + 14, 1.5f, 20, TH.text);
    g_setclip(c, sc2);
    hit_add(HC_SEND, ix + iw - 46, iy + 6, 36, 36);
    g_circle(c, ix + iw - 28, iy + 24, 17, cinput[0] && !copilot_busy ? TH.accent : (TH.dark ? 0x30FFFFFF : 0x20000000));
    icon_glyph(c, G_SEND, ix + iw - 27, iy + 24, 16, cinput[0] && !copilot_busy ? TH.accent_fg : TH.dim);
}

int copilot_mouse(IEv *e, float p) {
    (void)p;
    int h = hit_at(e->x, e->y);
    if (e->type == IE_WHEEL && h >= HC_PANEL && h < HC_PANEL + 100) { cscroll -= e->wheel / 120.0f * 60; stick_bottom = 0; dirty = 1; return 1; }
    if (e->type != IE_DOWN) return h >= HC_PANEL && h < HC_PANEL + 100;
    if (h == HC_CLOSE) { copilot_open = 0; dirty = 1; return 1; }
    if (h == HC_SEND) { send_prompt(cinput); cinput[0] = 0; dirty = 1; return 1; }
    if (h == HC_CLEAR && !copilot_busy) {
        pthread_mutex_lock(&cmu);
        for (int i = 0; i < nchat; i++) free(chat[i].text);
        nchat = 0;
        pthread_mutex_unlock(&cmu);
        dirty = 1;
        return 1;
    }
    if (h >= HC_CHIP && h < HC_CHIP + 3) { send_prompt(CHIPS[h - HC_CHIP]); dirty = 1; return 1; }
    return h >= HC_PANEL && h < HC_PANEL + 100;
}

int copilot_key(IEv *e) {
    int n = strlen(cinput);
    if (e->key == K_ENTER) { send_prompt(cinput); if (!copilot_busy || cinput[0]) cinput[0] = 0; return 1; }
    if (e->key == K_BACKSPACE) { if (n) { while (n > 0 && ((unsigned char)cinput[n - 1] & 0xc0) == 0x80) n--; if (n) cinput[n - 1] = 0; } return 1; }
    if ((e->mods & MOD_CTRL) && e->key == 'v' && clipboard) { snprintf(cinput + n, sizeof cinput - n, "%s", clipboard); return 1; }
    if (e->text[0] && n < (int)sizeof cinput - 2) { cinput[n] = e->text[0]; cinput[n + 1] = 0; return 1; }
    return 0;
}

void panels_tick(void) {
    static double last;
    if (now() - last >= 1) {
        last = now();
        if (nh < 60) cpu_hist[nh++] = cpu_pct;
        else { memmove(cpu_hist, cpu_hist + 1, 59 * sizeof(float)); cpu_hist[59] = cpu_pct; }
        if (widgets_open) dirty = 1;
    }
}
