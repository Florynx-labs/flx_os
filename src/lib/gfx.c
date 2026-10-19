// Software rasteriser: AA rounded rects (signed distance), lines, arcs, polygons,
// gradients, soft shadows, frosted glass, buffer/image blits. ARGB8888.
// Core shapes ported from Lumen OS 2 (MIT); shadow/glass/poly/gradient are new for Windows 12.
#include "w12.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "stb_image.h"

Canvas *canvas_new(int w, int h) {
    Canvas *c = calloc(1, sizeof *c);
    c->w = w; c->h = h; c->stride = w;
    c->px = calloc((size_t)w * h, 4);
    g_noclip(c);
    return c;
}
void canvas_wrap(Canvas *c, uint32_t *px, int w, int h, int stride) {
    c->px = px; c->w = w; c->h = h; c->stride = stride;
    g_noclip(c);
}
void g_noclip(Canvas *c) { c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h; }
void g_clip(Canvas *c, float x0, float y0, float x1, float y1) {
    c->cx0 = maxi(c->cx0, (int)floorf(x0)); c->cy0 = maxi(c->cy0, (int)floorf(y0));
    c->cx1 = mini(c->cx1, (int)ceilf(x1)); c->cy1 = mini(c->cy1, (int)ceilf(y1));
    if (c->cx1 < c->cx0) c->cx1 = c->cx0;
    if (c->cy1 < c->cy0) c->cy1 = c->cy0;
}
void g_getclip(Canvas *c, int s[4]) { s[0] = c->cx0; s[1] = c->cy0; s[2] = c->cx1; s[3] = c->cy1; }
void g_setclip(Canvas *c, const int s[4]) { c->cx0 = s[0]; c->cy0 = s[1]; c->cx1 = s[2]; c->cy1 = s[3]; }

uint32_t c_alpha(uint32_t col, float a) {
    int al = (int)((col >> 24) * clampf(a, 0, 1) + 0.5f);
    return (col & 0xffffff) | ((uint32_t)al << 24);
}
uint32_t c_mix(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0, 1);
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return r;
}

static inline uint32_t blend(uint32_t d, uint32_t s, unsigned a) {   // a: 0..256
    uint32_t rb = (((s & 0xff00ff) * a + (d & 0xff00ff) * (256 - a)) >> 8) & 0xff00ff;
    uint32_t g = (((s & 0x00ff00) * a + (d & 0x00ff00) * (256 - a)) >> 8) & 0x00ff00;
    return 0xff000000 | rb | g;
}
static inline unsigned a256(unsigned a255) { return a255 + (a255 >> 7); }
#define ROW(c, y) ((c)->px + (size_t)(y) * (c)->stride)

static void span(Canvas *c, int y, int x0, int x1, uint32_t col, unsigned a) {
    if (y < c->cy0 || y >= c->cy1) return;
    if (x0 < c->cx0) x0 = c->cx0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (x0 >= x1 || a == 0) return;
    uint32_t *p = ROW(c, y) + x0, *e = p + (x1 - x0);
    if (a >= 256) { uint32_t v = col | 0xff000000; while (p < e) *p++ = v; }
    else while (p < e) { *p = blend(*p, col, a); p++; }
}
static inline void plot(Canvas *c, int x, int y, uint32_t col, unsigned a) {
    if (x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1 || a == 0) return;
    uint32_t *p = ROW(c, y) + x;
    *p = a >= 256 ? (col | 0xff000000) : blend(*p, col, a);
}

void g_clear(Canvas *c, uint32_t col) {
    for (int y = c->cy0; y < c->cy1; y++) span(c, y, c->cx0, c->cx1, col, 256);
}

// ---- rounded rect geometry ---------------------------------------------------
typedef struct { float x0, y0, x1, y1, rt, rb, cx, cy, hw, hh; } RR;
static RR rr_make(float x, float y, float w, float h, float rt, float rb) {
    float m = fminf(w, h) * 0.5f;
    RR g = { x, y, x + w, y + h, fminf(fmaxf(rt, 0), m), fminf(fmaxf(rb, 0), m), x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f };
    return g;
}
static inline float rr_sdf(const RR *g, float px, float py) {
    float r = py < g->cy ? g->rt : g->rb;
    float qx = fabsf(px - g->cx) - g->hw + r, qy = fabsf(py - g->cy) - g->hh + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    if (in > 0) in = 0;
    return sqrtf(ox * ox + oy * oy) + in - r;
}
static inline float rr_cov(const RR *g, float px, float py) { return clampf(0.5f - rr_sdf(g, px, py), 0, 1); }
static void rr_full(const RR *g, float fy, int *a, int *b) {
    *a = 1; *b = 0;
    if (fy < g->y0 + 0.5f || fy > g->y1 - 0.5f) return;
    int ia = (int)ceilf(g->x0), ib = (int)floorf(g->x1) - 1;
    float r = fy < g->cy ? g->rt : g->rb;
    float cyc = clampf(fy, g->y0 + r, g->y1 - r), dy = fy - cyc;
    if (dy != 0) {
        float in = r - 0.5f;
        if (in <= 0 || fabsf(dy) >= in) return;
        float h = sqrtf(in * in - dy * dy);
        ia = maxi(ia, (int)ceilf(g->x0 + r - h - 0.5f));
        ib = mini(ib, (int)floorf(g->x1 - r + h - 0.5f));
    }
    *a = ia; *b = ib;
}

void g_rrect(Canvas *c, float x, float y, float w, float h, float rtop, float rbot, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    RR g = rr_make(x, y, w, h, rtop, rbot);
    unsigned ca = a256(col >> 24);
    int ya = maxi(c->cy0, (int)floorf(g.y0)), yb = mini(c->cy1, (int)ceilf(g.y1));
    int xa = maxi(c->cx0, (int)floorf(g.x0)), xb = mini(c->cx1, (int)ceilf(g.x1));
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        int a, b;
        rr_full(&g, fy, &a, &b);
        if (a <= b) {
            span(c, py, a, b + 1, col, ca);
            for (int px = xa; px < a && px < xb; px++) plot(c, px, py, col, (unsigned)(ca * rr_cov(&g, px + 0.5f, fy)));
            for (int px = maxi(b + 1, xa); px < xb; px++) plot(c, px, py, col, (unsigned)(ca * rr_cov(&g, px + 0.5f, fy)));
        } else {
            for (int px = xa; px < xb; px++) plot(c, px, py, col, (unsigned)(ca * rr_cov(&g, px + 0.5f, fy)));
        }
    }
}
void g_rect(Canvas *c, float x, float y, float w, float h, uint32_t col) {
    if (x == floorf(x) && y == floorf(y) && w == floorf(w) && h == floorf(h)) {
        int y0 = maxi((int)y, c->cy0), y1 = mini((int)(y + h), c->cy1);
        unsigned a = a256(col >> 24);
        for (int py = y0; py < y1; py++) span(c, py, (int)x, (int)(x + w), col, a);
        return;
    }
    g_rrect(c, x, y, w, h, 0, 0, col);
}

void g_rrect_line(Canvas *c, float x, float y, float w, float h, float r, float lw, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    RR o = rr_make(x, y, w, h, r, r);
    RR i = rr_make(x + lw, y + lw, w - 2 * lw, h - 2 * lw, fmaxf(r - lw, 0), fmaxf(r - lw, 0));
    unsigned ca = a256(col >> 24);
    int ya = maxi(c->cy0, (int)floorf(o.y0)), yb = mini(c->cy1, (int)ceilf(o.y1));
    int xa = maxi(c->cx0, (int)floorf(o.x0)), xb = mini(c->cx1, (int)ceilf(o.x1));
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        int oa, ob, ia, ib;
        rr_full(&o, fy, &oa, &ob);
        rr_full(&i, fy, &ia, &ib);
        for (int px = xa; px < xb; px++) {
            if (ia <= ib && px >= ia && px <= ib) { px = ib; continue; }
            float co = (px >= oa && px <= ob) ? 1 : rr_cov(&o, px + 0.5f, fy);
            float cv = co - rr_cov(&i, px + 0.5f, fy);
            if (cv > 0) plot(c, px, py, col, (unsigned)(ca * clampf(cv, 0, 1)));
        }
    }
}
void g_circle(Canvas *c, float cx, float cy, float r, uint32_t col) { g_rrect(c, cx - r, cy - r, 2 * r, 2 * r, r, r, col); }
void g_ring(Canvas *c, float cx, float cy, float r, float lw, uint32_t col) { g_rrect_line(c, cx - r, cy - r, 2 * r, 2 * r, r, lw, col); }

void g_line(Canvas *c, float x0, float y0, float x1, float y1, float w, uint32_t col) {
    float hw = w * 0.5f;
    int xa = maxi(c->cx0, (int)floorf(fminf(x0, x1) - hw - 1)), xb = mini(c->cx1, (int)ceilf(fmaxf(x0, x1) + hw + 1));
    int ya = maxi(c->cy0, (int)floorf(fminf(y0, y1) - hw - 1)), yb = mini(c->cy1, (int)ceilf(fmaxf(y0, y1) + hw + 1));
    float dx = x1 - x0, dy = y1 - y0, ll = dx * dx + dy * dy;
    unsigned ca = a256(col >> 24);
    for (int py = ya; py < yb; py++)
        for (int px = xa; px < xb; px++) {
            float fx = px + 0.5f - x0, fy = py + 0.5f - y0;
            float t = ll > 0 ? clampf((fx * dx + fy * dy) / ll, 0, 1) : 0;
            float ex = fx - t * dx, ey = fy - t * dy;
            float cov = clampf(hw + 0.5f - sqrtf(ex * ex + ey * ey), 0, 1);
            if (cov > 0) plot(c, px, py, col, (unsigned)(ca * cov));
        }
}

void g_arc(Canvas *c, float cx, float cy, float r, float a0, float a1, float w, uint32_t col) {
    float hw = w * 0.5f, R = r + hw + 1;
    int xa = maxi(c->cx0, (int)floorf(cx - R)), xb = mini(c->cx1, (int)ceilf(cx + R));
    int ya = maxi(c->cy0, (int)floorf(cy - R)), yb = mini(c->cy1, (int)ceilf(cy + R));
    float e0x = cx + r * cosf(a0), e0y = cy + r * sinf(a0), e1x = cx + r * cosf(a1), e1y = cy + r * sinf(a1);
    unsigned ca = a256(col >> 24);
    float span_ = a1 - a0;
    for (int py = ya; py < yb; py++)
        for (int px = xa; px < xb; px++) {
            float fx = px + 0.5f - cx, fy = py + 0.5f - cy;
            float d = sqrtf(fx * fx + fy * fy);
            if (fabsf(d - r) > hw + 1) continue;
            float ang = atan2f(fy, fx) - a0;
            while (ang < 0) ang += 2 * (float)M_PI;
            float dd;
            if (ang <= span_) dd = fabsf(d - r);
            else {
                float ax = px + 0.5f - e0x, ay = py + 0.5f - e0y, bx = px + 0.5f - e1x, by = py + 0.5f - e1y;
                dd = sqrtf(fminf(ax * ax + ay * ay, bx * bx + by * by));
            }
            float cov = clampf(hw + 0.5f - dd, 0, 1);
            if (cov > 0) plot(c, px, py, col, (unsigned)(ca * cov));
        }
}

void g_vgrad(Canvas *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot) {
    if (w <= 0 || h <= 0) return;
    RR g = rr_make(x, y, w, h, r, r);
    int ya = maxi(c->cy0, (int)floorf(y)), yb = mini(c->cy1, (int)ceilf(y + h));
    int xa = maxi(c->cx0, (int)floorf(x)), xb = mini(c->cx1, (int)ceilf(x + w));
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        uint32_t col = c_mix(top, bot, (fy - y) / h);
        unsigned ca = a256(col >> 24);
        int a, b;
        rr_full(&g, fy, &a, &b);
        if (r <= 0) { a = xa; b = xb - 1; }
        if (a <= b) span(c, py, maxi(a, xa), mini(b + 1, xb), col, ca);
        for (int px = xa; px < xb; px++) {
            if (a <= b && px >= a && px <= b) { px = b; continue; }
            plot(c, px, py, col, (unsigned)(ca * rr_cov(&g, px + 0.5f, fy)));
        }
    }
}

void g_poly(Canvas *c, const float *xy, int n, uint32_t col0, uint32_t col1, float gx0, float gy0, float gx1, float gy1) {
    float minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9, area = 0;
    for (int i = 0; i < n; i++) {
        minx = fminf(minx, xy[2 * i]); maxx = fmaxf(maxx, xy[2 * i]);
        miny = fminf(miny, xy[2 * i + 1]); maxy = fmaxf(maxy, xy[2 * i + 1]);
        int j = (i + 1) % n;
        area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
    }
    float sgn = area > 0 ? 1 : -1;
    float nx[16], ny[16], nd[16];
    n = mini(n, 16);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ex = xy[2 * j] - xy[2 * i], ey = xy[2 * j + 1] - xy[2 * i + 1], l = sqrtf(ex * ex + ey * ey);
        if (l < 1e-6f) l = 1;
        nx[i] = -ey / l * sgn; ny[i] = ex / l * sgn;           // inward normal
        nd[i] = nx[i] * xy[2 * i] + ny[i] * xy[2 * i + 1];
    }
    float gdx = gx1 - gx0, gdy = gy1 - gy0, gl = gdx * gdx + gdy * gdy;
    if (gl < 1e-6f) gl = 1;
    int xa = maxi(c->cx0, (int)floorf(minx)), xb = mini(c->cx1, (int)ceilf(maxx));
    int ya = maxi(c->cy0, (int)floorf(miny)), yb = mini(c->cy1, (int)ceilf(maxy));
    for (int py = ya; py < yb; py++)
        for (int px = xa; px < xb; px++) {
            float fx = px + 0.5f, fy = py + 0.5f, d = 1e9;
            for (int i = 0; i < n; i++) d = fminf(d, nx[i] * fx + ny[i] * fy - nd[i]);
            float cov = clampf(d + 0.5f, 0, 1);
            if (cov <= 0) continue;
            uint32_t col = col0 == col1 ? col0 : c_mix(col0, col1, ((fx - gx0) * gdx + (fy - gy0) * gdy) / gl);
            plot(c, px, py, col, (unsigned)(a256(col >> 24) * cov));
        }
}

// Soft drop shadow around a rounded rect; the interior is skipped (a window covers it).
void g_shadow(Canvas *c, float x, float y, float w, float h, float r, float blur, int alpha, float dy) {
    y += dy;
    RR g = rr_make(x, y, w, h, r, r);
    int ya = maxi(c->cy0, (int)floorf(y - blur)), yb = mini(c->cy1, (int)ceilf(y + h + blur));
    int xa = maxi(c->cx0, (int)floorf(x - blur)), xb = mini(c->cx1, (int)ceilf(x + w + blur));
    int ix0 = (int)ceilf(x + r), ix1 = (int)floorf(x + w - r);
    float inv = 1.0f / blur;
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        bool mid = fy > y + r && fy < y + h - r;
        uint32_t *row = ROW(c, py);
        for (int px = xa; px < xb; px++) {
            if (mid && px >= ix0 && px < ix1) { px = ix1 - 1; continue; }
            float d = rr_sdf(&g, px + 0.5f, fy);
            if (d < 0 || d >= blur) continue;
            float t = 1 - d * inv;
            unsigned a = (unsigned)(alpha * t * t);
            if (a) row[px] = blend(row[px], 0, a256(a));
        }
    }
}

// ---- blur ----------------------------------------------------------------------------
// Downsample by 4 (box), blur the small buffer with 3 box passes, upsample bilinear.
static void box_pass(uint32_t *b, uint32_t *tmp, int w, int h, int rad, int horiz) {
    int n = horiz ? w : h, m = horiz ? h : w;
    for (int j = 0; j < m; j++) {
        int sr = 0, sg = 0, sb = 0;
        int div = 2 * rad + 1;
#define AT(i) b[horiz ? (size_t)j * w + (i) : (size_t)(i) * w + j]
        for (int i = -rad; i <= rad; i++) { uint32_t p = AT(i < 0 ? 0 : i >= n ? n - 1 : i); sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255; }
        for (int i = 0; i < n; i++) {
            tmp[i] = 0xff000000 | (sr / div) << 16 | (sg / div) << 8 | (sb / div);
            int o = i - rad, a = i + rad + 1;
            uint32_t po = AT(o < 0 ? 0 : o), pa = AT(a >= n ? n - 1 : a);
            sr += ((pa >> 16) & 255) - ((po >> 16) & 255);
            sg += ((pa >> 8) & 255) - ((po >> 8) & 255);
            sb += (pa & 255) - (po & 255);
        }
        for (int i = 0; i < n; i++) AT(i) = tmp[i];
#undef AT
    }
}
static uint32_t *small_blur(const Canvas *src, int x0, int y0, int w, int h, int *sw_, int *sh_, int rad) {
    int sw = (w + 3) / 4 + 2, sh = (h + 3) / 4 + 2;
    uint32_t *b = malloc((size_t)sw * sh * 4), *tmp = malloc((size_t)(sw > sh ? sw : sh) * 4);
    for (int sy = 0; sy < sh; sy++)
        for (int sx = 0; sx < sw; sx++) {
            int bx = x0 + (sx - 1) * 4, by = y0 + (sy - 1) * 4;
            unsigned r = 0, g = 0, bl = 0;
            for (int k = 0; k < 4; k++) {
                int yy = by + k; yy = yy < 0 ? 0 : yy >= src->h ? src->h - 1 : yy;
                const uint32_t *row = src->px + (size_t)yy * src->stride;
                for (int l = 0; l < 4; l += 2) {   // sample 2 of 4 columns: plenty for a blur
                    int xx = bx + l + 1; xx = xx < 0 ? 0 : xx >= src->w ? src->w - 1 : xx;
                    uint32_t p = row[xx];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; bl += p & 255;
                }
            }
            b[(size_t)sy * sw + sx] = 0xff000000 | (r / 8) << 16 | (g / 8) << 8 | (bl / 8);
        }
    for (int p = 0; p < 3; p++) { box_pass(b, tmp, sw, sh, rad, 1); box_pass(b, tmp, sw, sh, rad, 0); }
    free(tmp);
    *sw_ = sw; *sh_ = sh;
    return b;
}
static inline uint32_t bilerp(const uint32_t *px, int stride, int w, int h, int fx, int fy /*16.16*/) {
    int x = fx >> 16, y = fy >> 16;
    unsigned ax = (fx >> 8) & 255, ay = (fy >> 8) & 255;
    if (fx < 0) { x = 0; ax = 0; }
    if (fy < 0) { y = 0; ay = 0; }
    if (x >= w - 1) { x = w - 1; ax = 0; }
    if (y >= h - 1) { y = h - 1; ay = 0; }
    const uint32_t *p = px + (size_t)y * stride + x;
    uint32_t a = p[0], b = ax ? p[1] : a, c = ay ? p[stride] : a, d = (ax && ay) ? p[stride + 1] : c;
    if (!ax) d = c;
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        unsigned top = ((a >> s) & 255) * (256 - ax) + ((b >> s) & 255) * ax;
        unsigned bot = ((c >> s) & 255) * (256 - ax) + ((d >> s) & 255) * ax;
        r |= ((top * (256 - ay) + bot * ay) >> 16) << s;
    }
    return r;
}

void g_glass(Canvas *c, float x, float y, float w, float h, float r, uint32_t tint) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), iw = (int)ceilf(w) + 1, ih = (int)ceilf(h) + 1;
    int sw, sh;
    uint32_t *b = small_blur(c, x0, y0, iw, ih, &sw, &sh, 3);
    RR g = rr_make(x, y, w, h, r, r);
    unsigned ta = a256(tint >> 24);
    int ya = maxi(c->cy0, y0), yb = mini(c->cy1, (int)ceilf(y + h));
    int xa = maxi(c->cx0, x0), xb = mini(c->cx1, (int)ceilf(x + w));
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        int fa, fb;
        rr_full(&g, fy, &fa, &fb);
        int sfy = (int)(((py - y0) / 4.0f + 1 - 0.375f) * 65536);
        uint32_t *row = ROW(c, py);
        for (int px = xa; px < xb; px++) {
            unsigned cov = 256;
            if (px < fa || px > fb) { cov = (unsigned)(256 * rr_cov(&g, px + 0.5f, fy)); if (!cov) continue; }
            int sfx = (int)(((px - x0) / 4.0f + 1 - 0.375f) * 65536);
            uint32_t s = bilerp(b, sw, sw, sh, sfx, sfy);
            s = blend(s, tint, ta);
            row[px] = cov >= 256 ? s : blend(row[px], s, cov);
        }
    }
    free(b);
}

void g_blur_full(Canvas *dst, const Canvas *src, int passes) {
    int sw, sh;
    uint32_t *b = small_blur(src, 0, 0, src->w, src->h, &sw, &sh, 4 + passes);
    for (int py = 0; py < dst->h; py++) {
        int sfy = (int)((py / 4.0f + 1 - 0.375f) * 65536);
        uint32_t *row = ROW(dst, py);
        for (int px = 0; px < dst->w; px++) row[px] = bilerp(b, sw, sw, sh, (int)((px / 4.0f + 1 - 0.375f) * 65536), sfy);
    }
    free(b);
}

void g_copy_from(Canvas *dst, const Canvas *src, float x, float y, float w, float h, float rt, float rb) {
    g_buf(dst, src->px + (size_t)maxi(0, (int)y) * src->stride + maxi(0, (int)x), src->stride, w, h, maxi(0, (int)x), maxi(0, (int)y), w, h, rt, rb, 255, 1);
}

// ---- buffers & images -----------------------------------------------------------------------
void g_buf(Canvas *c, const uint32_t *px, int stride, int sw, int sh, float x, float y, float w, float h,
           float rt, float rb, int alpha, int opaque) {
    if (w <= 0 || h <= 0 || !px || sw <= 0 || sh <= 0) return;
    RR g = rr_make(x, y, w, h, rt, rb);
    int ya = maxi(c->cy0, (int)floorf(y)), yb = mini(c->cy1, (int)ceilf(y + h));
    int xa = maxi(c->cx0, (int)floorf(x)), xb = mini(c->cx1, (int)ceilf(x + w));
    int stepx = (int)((float)sw / w * 65536), stepy = (int)((float)sh / h * 65536);
    bool exact = (stepx == 65536 && stepy == 65536 && x == floorf(x) && y == floorf(y));
    unsigned A = a256(alpha);
    (void)stepy;
    for (int py = ya; py < yb; py++) {
        float fy = py + 0.5f;
        int fa, fb;
        if (rt > 0 || rb > 0) rr_full(&g, fy, &fa, &fb); else { fa = xa; fb = xb - 1; }
        uint32_t *d = ROW(c, py);
        if (exact) {
            const uint32_t *srow = px + (size_t)(py - (int)y) * stride - (int)x;
            int ma = maxi(fa, xa), mb = mini(fb + 1, xb);
            for (int q = xa; q < xb; q++) {
                if (q >= ma && q < mb) {
                    if (opaque && A >= 256) { memcpy(d + q, srow + q, (size_t)(mb - q) * 4); q = mb - 1; continue; }
                    uint32_t s = srow[q];
                    unsigned a = opaque ? A : (a256(s >> 24) * A) >> 8;
                    d[q] = a >= 256 ? s | 0xff000000 : a ? blend(d[q], s, a) : d[q];
                } else {
                    uint32_t s = srow[q];
                    unsigned a = ((opaque ? 256 : a256(s >> 24)) * A) >> 8;
                    a = (unsigned)(a * rr_cov(&g, q + 0.5f, fy));
                    if (a) d[q] = blend(d[q], s, a);
                }
            }
            continue;
        }
        int sfy = (int)(((fy - y) * sh / h - 0.5f) * 65536);
        int sfx = (int)(((xa + 0.5f - x) * sw / w - 0.5f) * 65536);
        for (int q = xa; q < xb; q++, sfx += stepx) {
            uint32_t s = bilerp(px, stride, sw, sh, sfx, sfy);
            unsigned a = ((opaque ? 256 : a256(s >> 24)) * A) >> 8;
            if (q < fa || q > fb) a = (unsigned)(a * rr_cov(&g, q + 0.5f, fy));
            if (a >= 256) d[q] = s | 0xff000000;
            else if (a) d[q] = blend(d[q], s, a);
        }
    }
}
void g_image(Canvas *c, const Image *im, float x, float y, float w, float h, float radius, int alpha) {
    if (im) g_buf(c, im->px, im->w, im->w, im->h, x, y, w, h, radius, radius, alpha, 0);
}
void g_image_cover(Canvas *c, const Image *im, float x, float y, float w, float h, float radius, int alpha) {
    if (!im) return;
    float ar = w / h, iar = (float)im->w / im->h;
    if (iar > ar) { int sw = (int)(im->h * ar); g_buf(c, im->px + (im->w - sw) / 2, im->w, sw, im->h, x, y, w, h, radius, radius, alpha, 0); }
    else { int sh = (int)(im->w / ar); g_buf(c, im->px + (size_t)((im->h - sh) / 2) * im->w, im->w, im->w, sh, x, y, w, h, radius, radius, alpha, 0); }
}
void g_layer_rect(Canvas *dst, const Canvas *src, float x, float y, float w, float h, float radius, int alpha) {
    g_buf(dst, src->px, src->stride, src->w, src->h, x, y, w, h, radius, radius, alpha, 1);
}

Image *image_new(int w, int h) {
    Image *im = calloc(1, sizeof *im);
    im->w = w; im->h = h;
    im->px = calloc((size_t)w * h, 4);
    return im;
}
void image_free(Image *im) { if (im) { free(im->px); free(im); } }
Image *image_decode(const void *data, int len) {
    int w, h, n;
    unsigned char *rgba = stbi_load_from_memory(data, len, &w, &h, &n, 4);
    if (!rgba) return NULL;
    Image *im = malloc(sizeof *im);
    im->w = w; im->h = h;
    im->px = (uint32_t *)rgba;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned char *p = rgba + i * 4;
        im->px[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    return im;
}
Image *image_load(const char *path) {
    size_t n;
    char *d = read_file(path, &n);
    if (!d) return 0;
    Image *im = image_decode(d, (int)n);
    free(d);
    return im;
}
