// Browser: the Lumen OS 2 HTML/CSS engine (MIT) in a Windows 12 window. One worker thread
// fetches (our HTTP/TLS), parses, styles and lays out pages; image loaders decode off the
// UI thread; the UI paints the published display list. No JavaScript.
#include "../web/web.h"

typedef struct Tab {
    char url[2048], title[256];
    Doc *doc; DList *dl; Sheet *sheet;
    Scroll sc;
    volatile float progress;
    volatile int loading, relayout, nimg_pending, ver;
    char err[200];
    char *hist[64]; float hscroll[64]; int nhist, hpos;
    pthread_mutex_t mu;
    pthread_mutex_t jmu; pthread_cond_t jcv;
    char *job_url, *job_body; int job, job_hist;
    int gen, lw;
    Node *focus;
    float restore_scroll;
} Tab;
#define MAXTAB 1
// mobile-class UA: sites then send their simple layouts, which suit this engine best (Lumen OS 2 was a phone)
#define BROWSER_UA "Mozilla/5.0 (Linux; Windows 12; Mobile) W12Browser/1.0"
static Tab *tabs[1];
static int ntab, cur;
static volatile int page_w = 1100;
static double flash_t; static int flash_link = -1;
static pthread_mutex_t img_mu = PTHREAD_MUTEX_INITIALIZER;
static int img_threads;
static void add_history(const char *title, const char *url) { (void)title; (void)url; }

static float maxf_(float a, float b) { return a > b ? a : b; }
// ---- loading pipeline (worker thread) --------------------------------------------------------------
typedef struct { Tab *t; Node *n; char *url; int gen; } ImgJob;
static void *img_thread(void *a) {
    ImgJob *j = a;
    http_set_ua(BROWSER_UA);
    HttpResp *r = http_request("GET", j->url, 0, 0, 6 << 20);
    Image *im = r->status == 200 && r->body ? image_decode(r->body, r->len) : 0;
    http_free(r);
    Tab *t = j->t;
    pthread_mutex_lock(&t->mu);
    if (t->gen == j->gen) {   // page is still the same document
        if (im && (im->w > 4096 || im->h > 4096)) { image_free(im); im = 0; }
        j->n->img = im;
        j->n->img_state = im ? 2 : 3;
        t->relayout = 1; t->ver++;
    } else image_free(im);
    t->nimg_pending--;
    pthread_mutex_unlock(&t->mu);
    free(j->url); free(j);
    pthread_mutex_lock(&img_mu); img_threads--; pthread_mutex_unlock(&img_mu);
    return 0;
}
static void collect_imgs(Node *n, Node **out, int *k, int max) {
    for (Node *c = n->first; c && *k < max; c = c->next) {
        if (c->type != N_ELEM) continue;
        if (c->st && (c->st->display == D_NONE || c->st->hidden >= 1)) continue;
        if (!strcmp(c->tag, "img") && attr(c, "src") && strncmp(attr(c, "src"), "data:", 5)) out[(*k)++] = c;
        collect_imgs(c, out, k, max);
    }
}
static void start_images(Tab *t, Doc *d, int gen) {
    Node *imgs[80];
    int k = 0;
    collect_imgs(d->root, imgs, &k, 80);
    for (int i = 0; i < k; i++) {
        imgs[i]->img_state = 1;
        ImgJob *j = calloc(1, sizeof *j);
        j->t = t; j->n = imgs[i]; j->gen = gen;
        j->url = url_resolve(d->base, attr(imgs[i], "src"));
        // throttle: at most 4 decoders in flight
        for (;;) {
            pthread_mutex_lock(&img_mu);
            if (img_threads < 4) { img_threads++; pthread_mutex_unlock(&img_mu); break; }
            pthread_mutex_unlock(&img_mu);
            usleep(20000);
            if (t->gen != gen) { free(j->url); free(j); return; }
        }
        pthread_mutex_lock(&t->mu); t->nimg_pending++; pthread_mutex_unlock(&t->mu);
        pthread_t th;
        pthread_create(&th, 0, img_thread, j);
        pthread_detach(th);
    }
}

static void progress_cb(void *ctx, size_t got, size_t total) {
    Tab *t = ctx;
    float f = total ? (float)got / total : 1 - 1 / (1 + got / 60000.0f);
    t->progress = 0.12f + 0.5f * clampf(f, 0, 1);
}

static char *css_cache_url[24], *css_cache_txt[24];
static int css_cache_n;
static pthread_mutex_t css_mu = PTHREAD_MUTEX_INITIALIZER;
static char *fetch_css(const char *url) {
    pthread_mutex_lock(&css_mu);
    for (int i = 0; i < css_cache_n; i++) if (!strcmp(css_cache_url[i], url)) { char *r = xstrdup(css_cache_txt[i]); pthread_mutex_unlock(&css_mu); return r; }
    pthread_mutex_unlock(&css_mu);
    HttpResp *r = http_request("GET", url, 0, 0, 3 << 20);
    char *txt = r->status == 200 && r->body ? xstrdup(r->body) : 0;
    http_free(r);
    if (txt) {
        pthread_mutex_lock(&css_mu);
        int i = css_cache_n < 24 ? css_cache_n++ : (int)(now() * 10) % 24;
        if (i < css_cache_n && css_cache_url[i] && i != css_cache_n - 1) { free(css_cache_url[i]); free(css_cache_txt[i]); }
        css_cache_url[i] = xstrdup(url); css_cache_txt[i] = xstrdup(txt);
        pthread_mutex_unlock(&css_mu);
    }
    return txt;
}
static void gather_css(Node *n, Doc *d, Sheet *sh, int *nlink) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEM) continue;
        if (!strcmp(c->tag, "style")) {
            const char *m = attr(c, "media");
            if (!(m && strstr(m, "print") && !strstr(m, "screen")) && c->first && c->first->type == N_TEXT) css_parse(c->first->text, sh);
        } else if (!strcmp(c->tag, "link") && attr(c, "href") && attr(c, "rel") && strcasestr(attr(c, "rel"), "stylesheet") && !strcasestr(attr(c, "rel"), "alternate")) {
            const char *m = attr(c, "media");
            if (m && strstr(m, "print") && !strstr(m, "screen")) continue;
            if (*nlink >= 8) continue;
            (*nlink)++;
            char *u = url_resolve(d->base, attr(c, "href"));
            char *txt = fetch_css(u);
            LOG("web: stylesheet %s (%zu bytes)", u, txt ? strlen(txt) : 0);
            if (txt) { css_parse(txt, sh); free(txt); }
            free(u);
        }
        gather_css(c, d, sh, nlink);
    }
}

static char *error_page(const char *url, const char *err) {
    return strfmt("<html><body style='margin:48px;background:#ffffff;color:#1a1a1a'><h1 style='font-size:32px'>Hmm... can't reach this page</h1>"
                  "<p style='font-size:18px'>%s</p><p style='color:#666'>%s</p></body></html>", err, url);
}

static void do_load(Tab *t, char *url, char *body, int hist_mode) {
    int gen = ++t->gen;
    t->loading = 1; t->progress = 0.06f; t->err[0] = 0;
    double t0 = now();
    // DuckDuckGo result links are redirect wrappers: follow the real target directly
    char *real = 0;
    const char *ud = strstr(url, "duckduckgo.com/l/?");
    if (ud && (ud = strstr(url, "uddg="))) {
        ud += 5;
        real = malloc(strlen(ud) + 1);
        char *o = real;
        for (const char *p = ud; *p && *p != '&'; p++) {
            if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) { char h[3] = { p[1], p[2], 0 }; *o++ = strtol(h, 0, 16); p += 2; }
            else *o++ = *p == '+' ? ' ' : *p;
        }
        *o = 0;
    }
    const char *fetch_url = real ? real : url;
    http_set_progress(progress_cb, t);
    HttpResp *r = http_request(body ? "POST" : "GET", fetch_url, body, body ? "application/x-www-form-urlencoded" : 0, 12 << 20);
    http_set_progress(0, 0);
    if (gen != t->gen) { http_free(r); free(real); return; }
    t->progress = 0.65f;
    char *html;
    size_t hlen;
    const char *final = r->url ? r->url : fetch_url;
    if (r->status == 0 || !r->body) { html = error_page(fetch_url, r->err[0] ? r->err : "No response"); hlen = strlen(html); }
    else if (r->content_type && !strncmp(r->content_type, "image/", 6)) { html = strfmt("<html><body style='margin:0;background:#141414'><img src='%s' style='width:100%%'></body></html>", final); hlen = strlen(html); }
    else if (r->content_type && strstr(r->content_type, "text/plain")) {
        html = malloc(r->len + 64);
        strcpy(html, "<pre style='white-space:pre-wrap'>");
        size_t o = strlen(html);
        for (size_t i = 0; i < r->len; i++) {
            if (r->body[i] == '<') { memcpy(html + o, "&lt;", 4); o += 4; html = realloc(html, r->len * 4 + 64); }
            else html[o++] = r->body[i];
        }
        html[o] = 0;
        hlen = o;
    } else { html = r->body; r->body = 0; hlen = r->len; }
    Doc *d = html_parse(html, hlen, final);
    free(html);
    t->progress = 0.72f;
    Sheet *sh = css_parse(UA_CSS, 0);
    int nlink = 0;
    gather_css(d->root, d, sh, &nlink);
    t->progress = 0.85f;
    css_style_doc(d, sh, 16);
    DList *dl = layout_doc(d, page_w, 1);
    double ms = (now() - t0) * 1000;
    LOG("web: loaded %s -> %d \"%s\" (%zu bytes html, %d display items, height %.0f, %.0f ms)", final, r->status, d->title ? d->title : "", hlen, dl->n, dl->height, ms);
    MARK("BROWSER loaded %d %s", r->status, final);
    if (gen != t->gen) { doc_free(d); dlist_free(dl); css_free(sh); http_free(r); free(real); return; }
    pthread_mutex_lock(&t->mu);
    Doc *od = t->doc; DList *odl = t->dl; Sheet *osh = t->sheet;
    t->doc = d; t->dl = dl; t->sheet = sh;
    t->focus = 0;
    snprintf(t->url, sizeof t->url, "%s", final);
    snprintf(t->title, sizeof t->title, "%s", d->title && d->title[0] ? d->title : final);
    t->sc = (Scroll){ 0 }; t->lw = page_w;
    if (hist_mode == 0) {   // new navigation: truncate forward history
        for (int i = t->hpos + 1; i < t->nhist; i++) free(t->hist[i]);
        t->nhist = t->hpos + 1;
        if (t->nhist >= 64) { free(t->hist[0]); memmove(t->hist, t->hist + 1, 63 * sizeof(char *)); memmove(t->hscroll, t->hscroll + 1, 63 * sizeof(float)); t->nhist--; }
        t->hist[t->nhist] = xstrdup(final);
        t->hscroll[t->nhist] = 0;
        t->hpos = t->nhist++;
    } else {
        free(t->hist[t->hpos]);
        t->hist[t->hpos] = xstrdup(final);
        t->sc.pos = t->restore_scroll;
    }
    t->progress = 1; t->loading = 0; t->ver++;
    pthread_mutex_unlock(&t->mu);
    doc_free(od); dlist_free(odl); css_free(osh);
    if (r->status) add_history(t->title, final);
    char *refresh = d->refresh ? url_resolve(d->base, d->refresh) : 0;
    http_free(r);
    free(real);
    if (refresh) {   // meta refresh: queue a follow-up load
        pthread_mutex_lock(&t->jmu);
        if (!t->job) { t->job = 1; t->job_url = refresh; t->job_body = 0; t->job_hist = 0; pthread_cond_signal(&t->jcv); }
        else free(refresh);
        pthread_mutex_unlock(&t->jmu);
    }
    start_images(t, d, gen);
}

static void *tab_thread(void *a) {
    Tab *t = a;
    http_set_ua(BROWSER_UA);
    for (;;) {
        pthread_mutex_lock(&t->jmu);
        while (!t->job) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 250000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&t->jcv, &t->jmu, &ts);
            if (!t->job && t->doc && (t->relayout || t->lw != page_w)) break;
        }
        int job = t->job, hm = t->job_hist;
        char *url = t->job_url, *body = t->job_body;
        t->job = 0; t->job_url = 0; t->job_body = 0;
        pthread_mutex_unlock(&t->jmu);
        if (job == 1) { do_load(t, url, body, hm); free(url); free(body); continue; }
        // relayout after images arrived or the text size changed
        t->relayout = 0;
        int gen = t->gen;
        t->lw = page_w;
        DList *dl = layout_doc(t->doc, page_w, 1);
        pthread_mutex_lock(&t->mu);
        if (gen == t->gen) { DList *o = t->dl; t->dl = dl; dl = o; t->ver++; }
        pthread_mutex_unlock(&t->mu);
        dlist_free(dl);
    }
    return 0;
}

static Tab *new_tab(void) {
    if (ntab >= MAXTAB) return tabs[cur];
    Tab *t = calloc(1, sizeof *t);
    pthread_mutex_init(&t->mu, 0);
    pthread_mutex_init(&t->jmu, 0);
    pthread_cond_init(&t->jcv, 0);
    t->hpos = -1;
    pthread_t th;
    pthread_create(&th, 0, tab_thread, t);
    pthread_detach(th);
    tabs[ntab] = t;
    cur = ntab++;
    return t;
}
static void navigate(Tab *t, const char *url, const char *body, int hist_mode) {
    if (hist_mode == 0 && t->hpos >= 0 && t->hpos < t->nhist) t->hscroll[t->hpos] = t->sc.pos;   // for Back
    pthread_mutex_lock(&t->jmu);
    free(t->job_url); free(t->job_body);
    t->job = 1; t->job_url = xstrdup(url); t->job_body = body ? xstrdup(body) : 0; t->job_hist = hist_mode;
    pthread_cond_signal(&t->jcv);
    pthread_mutex_unlock(&t->jmu);
    t->loading = 1; t->progress = 0.03f;
    snprintf(t->url, sizeof t->url, "%s", url);
    LOG("web: navigate %s%s", url, body ? " (POST)" : "");
    MARK("BROWSER navigate %s", url);
}
static void go(const char *input) {
    while (*input == ' ') input++;
    if (!*input) return;
    char *url;
    bool has_scheme = strstr(input, "://") != 0;
    bool looks_url = !strchr(input, ' ') && (strchr(input, '.') || !strncmp(input, "localhost", 9));
    if (has_scheme) url = xstrdup(input);
    else if (looks_url) url = strfmt("https://%s", input);
    else { char *q = url_encode(input); url = strfmt("https://html.duckduckgo.com/html/?q=%s", q); free(q); }
    navigate(tabs[cur], url, 0, 0);
    free(url);
}
static void history_go(int dir) {
    Tab *t = tabs[cur];
    int np = t->hpos + dir;
    if (np < 0 || np >= t->nhist) return;
    t->hscroll[t->hpos] = t->sc.pos;
    t->hpos = np;
    t->restore_scroll = t->hscroll[np];
    navigate(t, t->hist[np], 0, 1);
}

// ---- forms ------------------------------------------------------------------------------------------
static const char *input_value(Node *n) { return n->value ? n->value : attr(n, "value") ? attr(n, "value") : ""; }
static void form_walk(Node *n, Node *submitter, char *q, int cap) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEM) continue;
        const char *name = attr(c, "name");
        if (name && *name) {
            const char *type = attr(c, "type") ? attr(c, "type") : "text";
            const char *val = 0;
            if (!strcmp(c->tag, "input")) {
                if (!strcasecmp(type, "submit") || !strcasecmp(type, "button") || !strcasecmp(type, "image")) val = c == submitter ? input_value(c) : 0;
                else if (!strcasecmp(type, "checkbox") || !strcasecmp(type, "radio")) val = (c->checked || attr(c, "checked")) ? (attr(c, "value") ? attr(c, "value") : "on") : 0;
                else val = input_value(c);
            } else if (!strcmp(c->tag, "textarea")) val = c->value ? c->value : "";
            else if (!strcmp(c->tag, "select")) {
                for (Node *o = c->first; o; o = o->next)
                    if (o->type == N_ELEM && !strcmp(o->tag, "option")) { val = attr(o, "value") ? attr(o, "value") : ""; if (attr(o, "selected")) break; }
            } else if (!strcmp(c->tag, "button") && c == submitter) val = attr(c, "value") ? attr(c, "value") : "";
            if (val) {
                char *k = url_encode(name), *v = url_encode(val);
                int l = strlen(q);
                snprintf(q + l, cap - l, "%s%s=%s", l ? "&" : "", k, v);
                free(k); free(v);
            }
        }
        form_walk(c, submitter, q, cap);
    }
}
static void submit_form(Tab *t, Node *from) {
    Node *f = from;
    while (f && !(f->type == N_ELEM && !strcmp(f->tag, "form"))) f = f->parent;
    if (!f) return;
    char *q = calloc(1, 8192);
    form_walk(f, from, q, 8192);
    const char *action = attr(f, "action");
    char *base = url_resolve(t->doc->base, action && *action ? action : t->url);
    const char *method = attr(f, "method");
    LOG("web: submit form %s %s ? %s", method ? method : "get", base, q);
    if (method && !strcasecmp(method, "post")) navigate(t, base, q, 0);
    else {
        char *qs = strchr(base, '?');
        if (qs) *qs = 0;
        char *u = strfmt("%s?%s", base, q);
        navigate(t, u, 0, 0);
        free(u);
    }
    free(base); free(q);
    t->focus = 0;
}

// ---- Windows 12 browser chrome ---------------------------------------------------------------------
#define TABBAR 40
#define TOOLBAR 48
#define TOP (TABBAR + TOOLBAR)
static int editing, addr_sel, last_ver = -1;
static char addr[2048];
static char hover_url[512];
static const char *QUICK[][2] = {
    { "Wikipedia", "https://en.m.wikipedia.org/wiki/Microsoft_Windows" }, { "Hacker News", "https://news.ycombinator.com/" },
    { "example.com", "https://example.com/" }, { "CNN Lite", "https://lite.cnn.com/" },
    { "NPR Text", "https://text.npr.org/" }, { "DuckDuckGo Lite", "https://lite.duckduckgo.com/lite/" } };

static void draw_page(Canvas *c, Tab *t) {
    float top = TOP, bot = U.h, W = U.w;
    int sc[4]; g_getclip(c, sc);
    g_clip(c, 0, top, W, bot);
    pthread_mutex_lock(&t->mu);
    DList *dl = t->dl;
    float view = bot - top;
    if (dl) {
        ui_scroll(&t->sc, 0, top, W, view, dl->height + 20);
        g_rect(c, 0, top, W, view, dl->page_bg | 0xff000000);
        float off = top - t->sc.pos, ox = maxf_(0, (W - page_w) / 2);
        for (int i = 0; i < dl->n; i++) {
            DItem *it = &dl->it[i];
            float y = it->y + off, x = it->x + ox;
            if (it->kind == DI_TEXT) { if (y < top - 4 || y - it->size > bot) continue; }
            else if (y > bot || y + it->h < top) continue;
            switch (it->kind) {
            case DI_RECT: case DI_LINE: g_rect(c, x, y, it->w, it->h, it->color); break;
            case DI_TEXT:
                if (it->link >= 0 && it->link == flash_link && now() - flash_t < 0.35) g_rect(c, x, y - it->size * 0.9f, it->w, it->size * 1.2f, TH.accent_soft);
                text_n(c, it->font, it->size, 0, x, y, it->color, it->text, it->len);
                break;
            case DI_IMAGE:
                if (it->node->img) g_image(c, it->node->img, x, y, it->w, it->h, 0, 255);
                else g_rect(c, x, y, it->w, it->h, 0xffe8e6e1);
                break;
            case DI_INPUT: {
                Node *n = it->node;
                const char *type = attr(n, "type");
                if (type && (!strcasecmp(type, "checkbox") || !strcasecmp(type, "radio"))) {
                    g_rrect_line(c, x, y, it->w, it->h, 3, 1.5f, 0xff555555);
                    if (n->checked || attr(n, "checked")) g_rect(c, x + it->w * 0.25f, y + it->h * 0.25f, it->w / 2, it->h / 2, 0xff333333);
                    break;
                }
                bool foc = t->focus == n;
                g_rrect(c, x, y, it->w, it->h, 4, 4, 0xffffffff);
                g_rrect_line(c, x, y, it->w, it->h, 4, foc ? 2 : 1, foc ? TH.accent : 0xff8a8a8a);
                const char *v = n->value ? n->value : attr(n, "value") ? attr(n, "value") : "";
                bool ph = !*v;
                if (ph) v = attr(n, "placeholder") ? attr(n, "placeholder") : "";
                int s2[4]; g_getclip(c, s2);
                g_clip(c, x + 3, y, x + it->w - 3, y + it->h);
                float tw = text_w(F_REG, it->size, 0, v), tx = x + 6;
                if (!ph && tw > it->w - 16) tx = x + it->w - 10 - tw;
                text(c, F_REG, it->size, 0, tx, y + it->h / 2 + it->size * 0.35f, ph ? 0xff9a9a9a : 0xff111111, v);
                if (foc) g_rect(c, tx + (ph ? 0 : tw) + 1, y + it->h * 0.2f, 1.5f, it->h * 0.6f, TH.accent);
                g_setclip(c, s2);
                break;
            }
            case DI_BUTTON:
                g_rrect(c, x, y, it->w, it->h, 4, 4, 0xffe9e9e9);
                g_rrect_line(c, x, y, it->w, it->h, 4, 1, 0xff8a8a8a);
                text_c(c, F_SEMI, it->size, 0, x + it->w / 2, y + it->h / 2 + it->size * 0.35f, 0xff111111, it->text);
                break;
            }
        }
    } else g_rect(c, 0, top, W, view, 0xFFFFFFFF);
    pthread_mutex_unlock(&t->mu);
    g_setclip(c, sc);
}

static void draw_start(Canvas *c) {
    float W = U.w, H = U.h, top = TOP;
    g_vgrad(c, 0, top, W, H - top, 0, TH.dark ? 0xFF1C2230 : 0xFFEAF1FB, TH.dark ? 0xFF14161C : 0xFFF7F7F9);
    icon_app(c, APP_BROWSER, W / 2 - 36, top + 70, 72);
    text_c(c, F_SEMI, 30, 0, W / 2, top + 186, TH.text, "Where to today?");
    float sw = fminf(640, W - 80), sx = (W - sw) / 2, sy = top + 216;
    g_shadow(c, sx, sy, sw, 50, 25, 18, 50, 4);
    g_rrect(c, sx, sy, sw, 50, 25, 25, TH.dark ? 0xFF2A2D35 : 0xFFFFFFFF);
    icon_glyph(c, G_SEARCH, sx + 28, sy + 25, 18, TH.sub);
    text(c, F_REG, 16, 0, sx + 50, sy + 31, TH.dim, "Search DuckDuckGo or type a web address");
    if (ui_click(sx, sy, sw, 50)) { editing = 1; addr[0] = 0; addr_sel = 0; }
    float tw = 150, th = 110, gap = 18;
    int per = mini(6, maxi(1, (int)((W - 80) / (tw + gap))));
    float gx = (W - per * tw - (per - 1) * gap) / 2;
    for (int i = 0; i < 6; i++) {
        float x = gx + (i % per) * (tw + gap), y = sy + 90 + (i / per) * (th + gap);
        bool h = ui_hover(x, y, tw, th);
        g_rrect(c, x, y, tw, th, 10, 10, h ? (TH.dark ? 0xFF323640 : 0xFFFFFFFF) : (TH.dark ? 0xFF262A32 : 0xD8FFFFFF));
        g_rrect_line(c, x, y, tw, th, 10, 1, TH.line);
        uint32_t col = ACCENTS[i % NACCENT];
        g_circle(c, x + tw / 2, y + 40, 22, col);
        char ch[2] = { QUICK[i][0][0], 0 };
        text_c(c, F_BOLD, 20, 0, x + tw / 2, y + 47, 0xFFFFFFFF, ch);
        char *t = text_ellipsize(F_REG, 13, QUICK[i][0], tw - 16);
        text_c(c, F_REG, 13, 0, x + tw / 2, y + 90, TH.text, t); free(t);
        if (ui_click(x, y, tw, th)) navigate(tabs[0], QUICK[i][1], 0, 0);
    }
    text_c(c, F_REG, 13, 0, W / 2, H - 24, TH.dim, "Own HTML tokenizer, CSS cascade and layout · HTTPS via BearSSL · no JavaScript");
}

static void page_click(Tab *t) {
    float ox = maxf_(0, (U.w - page_w) / 2);
    float px = U.mx - ox, py = U.my - TOP + t->sc.pos;
    pthread_mutex_lock(&t->mu);
    DList *dl = t->dl;
    char *target = 0;
    Node *focus = 0, *button = 0;
    if (dl) for (int i = dl->n - 1; i >= 0; i--) {
        DItem *it = &dl->it[i];
        float x0 = it->x - 2, x1 = it->x + it->w + 2, y0, y1;
        if (it->kind == DI_TEXT) { y0 = it->y - it->size * 0.95f; y1 = it->y + it->size * 0.3f; }
        else { y0 = it->y; y1 = it->y + it->h; }
        if (px < x0 || px > x1 || py < y0 || py > y1) continue;
        if (it->kind == DI_INPUT) {
            const char *type = attr(it->node, "type");
            if (type && (!strcasecmp(type, "checkbox") || !strcasecmp(type, "radio"))) { it->node->checked = !(it->node->checked || attr(it->node, "checked")); break; }
            focus = it->node; break;
        }
        if (it->kind == DI_BUTTON) { button = it->node; break; }
        if (it->link >= 0) { target = xstrdup(dl->links[it->link]); flash_link = it->link; flash_t = now(); break; }
    }
    pthread_mutex_unlock(&t->mu);
    if (focus) { t->focus = focus; editing = 0; return; }
    t->focus = 0;
    if (button) { submit_form(t, button); return; }
    if (target) {
        if (!strncasecmp(target, "http://", 7) || !strncasecmp(target, "https://", 8)) {
            char *h = strchr(target, '#');
            bool same = h && !strncmp(target, t->url, h - target) && strlen(t->url) == (size_t)(h - target);
            if (!same) navigate(t, target, 0, 0);
        }
        free(target);
    }
}
static void link_hover(Tab *t) {
    hover_url[0] = 0;
    if (!t->dl || U.my < TOP) return;
    float ox = maxf_(0, (U.w - page_w) / 2), px = U.mx - ox, py = U.my - TOP + t->sc.pos;
    pthread_mutex_lock(&t->mu);
    DList *dl = t->dl;
    for (int i = dl ? dl->n - 1 : -1; i >= 0; i--) {
        DItem *it = &dl->it[i];
        if (it->kind != DI_TEXT || it->link < 0) continue;
        if (px >= it->x && px <= it->x + it->w && py >= it->y - it->size * 0.95f && py <= it->y + it->size * 0.3f) { snprintf(hover_url, sizeof hover_url, "%s", dl->links[it->link]); break; }
    }
    pthread_mutex_unlock(&t->mu);
}

static int field_key(char *buf, int cap) {
    int n = strlen(buf);
    if (U.key == K_ENTER) return 1;
    if (U.key == K_BACKSPACE) { while (n > 0 && ((unsigned char)buf[n - 1] & 0xc0) == 0x80) n--; if (n) buf[n - 1] = 0; return 0; }
    if ((U.mods & MOD_CTRL) && U.key == 'v') { char *s = app_clip_get(); snprintf(buf + n, cap - n, "%s", s); free(s); return 0; }
    if (U.text[0] && (unsigned char)U.text[0] >= 32 && n + 4 < cap) strcat(buf, U.text);
    return 0;
}

static void draw(Canvas *c) {
    Tab *t = tabs[0];
    float W = U.w;
    int pw = mini((int)W - 24, 1240);
    if (pw != page_w) page_w = pw;
    last_ver = t->ver;
    // keyboard
    if (U.ev == EV_KEY) {
        if (editing) {
            if (addr_sel && U.key != K_ENTER && U.key != K_ESC) { addr[0] = 0; addr_sel = 0; if (U.key == K_BACKSPACE) goto keys_done; }
            addr_sel = 0;
            if (U.key == K_ESC) editing = 0;
            else if (field_key(addr, sizeof addr)) { editing = 0; go(addr); }
        } else if (t->focus) {
            Node *n = t->focus;
            if (!n->value) { n->value = calloc(1, 1024); snprintf(n->value, 1024, "%s", attr(n, "value") ? attr(n, "value") : ""); }
            if (field_key(n->value, 1024)) submit_form(t, n);
        } else if ((U.mods & MOD_CTRL) && U.key == 'l') { editing = 1; addr_sel = 1; snprintf(addr, sizeof addr, "%s", t->url); }
        else if (U.key == K_DOWN) t->sc.pos += 60;
        else if (U.key == K_UP) t->sc.pos -= 60;
        else if (U.key == ' ' || U.key == K_PGDN) t->sc.pos += U.h - TOP - 60;
        else if (U.key == K_PGUP) t->sc.pos -= U.h - TOP - 60;
        else if (U.key == K_F5) { if (t->url[0]) navigate(t, t->url, 0, 1); }
        else if ((U.mods & MOD_ALT) && U.key == K_LEFT) history_go(-1);
        else if (U.key > 32 && U.key < 127 && !(U.mods & (MOD_CTRL | MOD_ALT))) { editing = 1; addr_sel = 0; snprintf(addr, sizeof addr, "%s", U.text); }
    }
keys_done:
    // page first (chrome is drawn over it)
    bool page = t->url[0] && (t->dl || t->loading);
    if (page) {
        if (U.ev == EV_MOVE) link_hover(t);
        if (U.ev == EV_DOWN && U.btn == 1 && U.my > TOP) { editing = 0; page_click(t); U.consumed = 1; }
        draw_page(c, t);
    } else draw_start(c);
    // tab strip
    g_rect(c, 0, 0, W, TABBAR, TH.dark ? 0xFF1F1F1F : 0xFFDFE3E8);
    g_rrect(c, 8, 6, 260, TABBAR - 6, 8, 0, TH.dark ? 0xFF2B2B2B : 0xFFF7F7F7);
    icon_app(c, APP_BROWSER, 20, 13, 18);
    char *tt = text_ellipsize(F_REG, 13, t->title[0] ? t->title : "New tab", 200);
    text(c, F_REG, 13, 0, 46, 28, TH.text, tt); free(tt);
    if (t->loading) { float a = (float)now() * 7; g_arc(c, 250, 23, 6, a, a + 4.2f, 2, TH.accent); }
    // toolbar
    g_rect(c, 0, TABBAR, W, TOOLBAR, TH.dark ? 0xFF2B2B2B : 0xFFF7F7F7);
    g_rect(c, 0, TOP - 1, W, 1, TH.line);
    if (ui_icon_btn(8, TABBAR + 6, 36, 36, G_BACK, 0)) history_go(-1);
    if (ui_icon_btn(48, TABBAR + 6, 36, 36, G_FWD, 0)) history_go(1);
    if (ui_icon_btn(88, TABBAR + 6, 36, 36, t->loading ? G_CLOSE : G_REFRESH, 0)) {
        if (t->loading) { t->gen++; t->loading = 0; } else if (t->url[0]) navigate(t, t->url, 0, 1);
    }
    if (ui_icon_btn(128, TABBAR + 6, 36, 36, G_HOME, 0)) { t->url[0] = 0; pthread_mutex_lock(&t->mu); t->gen++; t->loading = 0; pthread_mutex_unlock(&t->mu); snprintf(t->title, sizeof t->title, "New tab"); }
    float ax = 174, ay = TABBAR + 7, aw = W - ax - 16, ah = 34;
    g_rrect(c, ax, ay, aw, ah, 17, 17, editing ? (TH.dark ? 0xFF1F1F1F : 0xFFFFFFFF) : (TH.dark ? 0xFF383838 : 0xFFECEEF1));
    if (editing) g_rrect_line(c, ax, ay, aw, ah, 17, 2, TH.accent);
    int s[4]; g_getclip(c, s);
    g_clip(c, ax + 12, ay, ax + aw - 12, ay + ah);
    if (editing) {
        float w = text_w(F_REG, 15, 0, addr);
        if (addr_sel && addr[0]) g_rect(c, ax + 40, ay + 7, w + 2, ah - 14, TH.sel);
        icon_glyph(c, G_SEARCH, ax + 22, ay + ah / 2, 14, TH.sub);
        if (!addr[0]) text(c, F_REG, 15, 0, ax + 41, ay + 23, TH.dim, "Search or enter web address");
        text(c, F_REG, 15, 0, ax + 41, ay + 23, TH.text, addr);
        g_rect(c, ax + 41 + w + 1, ay + 8, 1.5f, ah - 16, TH.text);
    } else {
        Url u;
        bool ok = !url_parse(t->url, &u);
        if (ok && !strcmp(u.scheme, "https")) icon_glyph(c, G_LOCK, ax + 22, ay + ah / 2, 14, TH.sub);
        else icon_glyph(c, G_SEARCH, ax + 22, ay + ah / 2, 14, TH.sub);
        if (ok) {
            float hx = text(c, F_REG, 15, 0, ax + 41, ay + 23, TH.sub, !strcmp(u.scheme, "https") ? "https://" : "http://");
            hx = text(c, F_SEMI, 15, 0, hx, ay + 23, TH.text, u.host);
            text(c, F_REG, 15, 0, hx, ay + 23, TH.sub, strcmp(u.path, "/") ? u.path : "");
        } else text(c, F_REG, 15, 0, ax + 41, ay + 23, TH.dim, "Search or enter web address");
    }
    g_setclip(c, s);
    if (ui_downin(ax, ay, aw, ah)) { editing = 1; addr_sel = t->url[0] != 0; snprintf(addr, sizeof addr, "%s", t->url); t->focus = 0; }
    if (t->loading) g_rect(c, 0, TOP - 2, W * fminf(1, t->progress), 2, TH.accent);
    if (hover_url[0] && page) {
        float w = fminf(text_w(F_REG, 12, 0, hover_url) + 20, W * 0.6f);
        g_rrect(c, 0, U.h - 24, w, 24, 0, 6, TH.dark ? 0xF0303030 : 0xF0F2F2F2);
        char *e = text_ellipsize(F_REG, 12, hover_url, w - 16);
        text(c, F_REG, 12, 0, 8, U.h - 8, TH.text, e); free(e);
    }
    char ti[300];
    snprintf(ti, sizeof ti, "%s - Browser", t->title[0] ? t->title : "New tab");
    static char last_title[300];
    if (strcmp(ti, last_title)) { snprintf(last_title, sizeof last_title, "%s", ti); app_set_title(ti); }
}

static void tick(void) {
    Tab *t = tabs[0];
    if (t->ver != last_ver || t->loading) app_redraw();
}
static void init(int argc, char **argv) {
    new_tab();
    snprintf(tabs[0]->title, sizeof tabs[0]->title, "New tab");
    if (argc > 1 && argv[1][0]) go(argv[1]);
}

int main(int argc, char **argv) {
    static AppDef a = { .exe = "browser", .title = "Browser", .w = 1240, .h = 760, .init = init, .draw = draw, .tick = tick, .tick_ms = 60 };
    return app_main(&a, argc, argv);
}
