// Build-time procedural wallpaper generator for FLX OS. Runs in the build container,
// writes 1920x1080 raw images ("W12W", width, height, then ARGB words) into the rootfs.
//   bloom.raw  - cinematic sunset city (golden hour skyline with river) — FLX OS default
//   aurora.raw - green/violet curtains over a night sky
//   dune.raw   - layered sunset dunes with mountain silhouettes
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1920
#define H 1080
static float img[H][W][3];

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static void hex(uint32_t c, float o[3]) { o[0] = ((c >> 16) & 255) / 255.f; o[1] = ((c >> 8) & 255) / 255.f; o[2] = (c & 255) / 255.f; }
static void mix3(float *d, const float *s, float t) { for (int i = 0; i < 3; i++) d[i] += (s[i] - d[i]) * t; }
static void add3(float *d, const float *s, float t) { for (int i = 0; i < 3; i++) d[i] = 1 - (1 - d[i]) * (1 - s[i] * t); }
static uint32_t rng = 12345;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

static void vgrad(uint32_t top, uint32_t bot) {
    float a[3], b[3];
    hex(top, a); hex(bot, b);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float t = (float)y / H; for (int i = 0; i < 3; i++) img[y][x][i] = a[i] + (b[i] - a[i]) * t; }
}
static void glow(float cx, float cy, float r, uint32_t col, float k) {
    float c[3]; hex(col, c);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float dx = (x - cx) / r, dy = (y - cy) / r, d = dx * dx + dy * dy;
        if (d < 6) add3(img[y][x], c, k * expf(-d * 1.4f));
    }
}

// ---- Wallpaper 0: FLX OS Sunset City ---------------------------------------------------------
// Cinematic golden-hour skyline with river/valley below, warm amber/gold sky,
// matching the reference image's mood.
static void bloom(void) {
    // sky gradient: deep navy top -> warm amber -> bright gold at horizon
    float sky_top[3]  = { 0.08f, 0.12f, 0.22f };   // deep blue-grey
    float sky_mid[3]  = { 0.40f, 0.25f, 0.10f };   // warm amber mid
    float sky_gold[3] = { 0.92f, 0.65f, 0.12f };   // bright gold at sun
    float sky_haze[3] = { 0.98f, 0.82f, 0.42f };   // pale gold near horizon
    float horiz = 0.52f;   // horizon line (fraction of H)

    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = (float)y / H;
        float sky_v = v / horiz;   // 0=top..1=horizon
        float r, g, b;
        if (v <= horiz) {
            if (sky_v < 0.5f) {
                float t = sky_v * 2;
                r = sky_top[0] + (sky_mid[0] - sky_top[0]) * t;
                g = sky_top[1] + (sky_mid[1] - sky_top[1]) * t;
                b = sky_top[2] + (sky_mid[2] - sky_top[2]) * t;
            } else {
                float t = (sky_v - 0.5f) * 2;
                r = sky_mid[0] + (sky_gold[0] - sky_mid[0]) * t;
                g = sky_mid[1] + (sky_gold[1] - sky_mid[1]) * t;
                b = sky_mid[2] + (sky_gold[2] - sky_mid[2]) * t;
            }
        } else {
            // below horizon: hazy warm foreground
            float t = (v - horiz) / (1 - horiz);
            r = sky_haze[0] + (0.35f - sky_haze[0]) * t;
            g = sky_haze[1] + (0.22f - sky_haze[1]) * t;
            b = sky_haze[2] + (0.08f - sky_haze[2]) * t;
        }
        img[y][x][0] = r; img[y][x][1] = g; img[y][x][2] = b;
    }

    // Sun: large bright disc just above horizon
    float sun_x = W * 0.64f, sun_y = H * horiz - 60;
    glow(sun_x, sun_y, 140, 0xFFFFE878, 1.1f);
    glow(sun_x, sun_y, 300, 0xFFFF9A20, 0.55f);
    glow(sun_x, sun_y, 600, 0xFFFF6B00, 0.25f);
    // sun disc itself (bright white-gold core)
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float dx = x - sun_x, dy = y - sun_y;
        float d = sqrtf(dx*dx + dy*dy);
        if (d < 48) {
            float t = clampf(1 - d/48, 0, 1);
            img[y][x][0] = img[y][x][0] + (1.0f - img[y][x][0]) * t * 0.9f;
            img[y][x][1] = img[y][x][1] + (0.97f - img[y][x][1]) * t * 0.85f;
            img[y][x][2] = img[y][x][2] + (0.60f - img[y][x][2]) * t * 0.7f;
        }
    }

    // Light streaks / crepuscular rays
    for (int k = 0; k < 12; k++) {
        float ang = -1.2f + k * 0.22f + (k % 3) * 0.06f;
        float bright = 0.12f + (k == 5 || k == 6) * 0.12f;
        float wr = 8 + k * 2;
        float ray[3] = { 1.0f, 0.85f, 0.30f };
        for (int y = 0; y < (int)(H * horiz); y++) for (int x = 0; x < W; x++) {
            float dx = x - sun_x, dy = y - sun_y;
            float proj = dx * cosf(ang) - dy * sinf(ang);
            float perp = dx * sinf(ang) + dy * cosf(ang);
            float dist_along = sqrtf(dx*dx + dy*dy);
            if (dist_along > 50 && fabsf(perp) < wr) {
                float fade = expf(-dist_along / 500.0f) * (1 - fabsf(perp)/wr);
                add3(img[y][x], ray, bright * fade);
            }
        }
    }

    // Clouds: soft wispy bands in upper sky
    for (int k = 0; k < 8; k++) {
        float cy2 = H * (0.06f + k * 0.055f);
        float cx2 = W * (0.1f + k * 0.14f + (k%2)*0.05f);
        float cw2 = 180 + k * 40, ch2 = 28 + k * 6;
        float lit[3] = { 0.98f, 0.82f, 0.55f };
        float drk[3] = { 0.22f, 0.18f, 0.14f };
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float dx = (x - cx2) / cw2, dy = (y - cy2) / ch2;
            float d = dx*dx + dy*dy;
            if (d < 1) {
                float t = 1 - d;
                float bright2 = (k < 4) ? 0.55f : 0.30f;
                float *col2 = (k < 5) ? drk : lit;
                mix3(img[y][x], col2, t * bright2);
            }
        }
    }

    // River / water reflection in valley (lower third)
    float river_base = H * 0.68f;
    float riv[3] = { 0.62f, 0.42f, 0.14f };  // gold reflection
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float ry = river_base + 30 * sinf(x * 0.004f) + 12 * sinf(x * 0.013f + 1.2f);
        float rw2 = 90 + 20 * sinf(x * 0.002f);
        if (y > ry && y < ry + rw2) {
            float t = smooth(ry, ry + rw2, y) * (1 - smooth(ry + rw2*0.5f, ry + rw2, y));
            t *= 0.55f + 0.45f * sinf(x * 0.03f + y * 0.1f);  // shimmer
            mix3(img[y][x], riv, t * 0.7f);
        }
    }

    // City silhouette: dark buildings on horizon
    // Low skyline left third, tall buildings centre
    float bld[3] = { 0.08f, 0.06f, 0.04f };
    float bld_lit[3] = { 0.22f, 0.15f, 0.05f };  // backlit edge
    for (int x = 0; x < W; x++) {
        // building height profile
        float base = H * (0.52f + 0.02f * sinf(x * 0.05f));
        int bx = x / 32;
        float seed = (float)(bx * 1664525 + 1013904223);
        float h1 = 40 + 120 * fabsf(sinf(seed * 0.0001f)) * smooth(0.25f, 0.75f, (float)x / W);
        // tall spire near centre
        if (x > 900 && x < 960) h1 = fmaxf(h1, 200 - fabsf(x - 930) * 0.8f);
        if (x > 1050 && x < 1100) h1 = fmaxf(h1, 150 - fabsf(x - 1075) * 0.5f);
        // right side mountains
        float mountain = 60 * smooth(0.6f, 0.9f, (float)x/W) * fabsf(sinf(x * 0.002f + 3));
        float hy = base - h1 - mountain;

        for (int y = 0; y < H; y++) {
            if (y >= (int)hy && y < (int)(hy + 3)) {
                // backlit rim
                mix3(img[y][x], bld_lit, 0.8f);
            } else if (y >= (int)(hy + 3)) {
                mix3(img[y][x], bld, 0.95f);
                // occasional lit windows
                if ((x / 6 + y / 10 * 7) % 13 < 2 && y > (int)hy + 20 && y < (int)(hy + h1 - 10)) {
                    float win[3] = { 0.9f, 0.7f, 0.3f };
                    mix3(img[y][x], win, 0.7f);
                }
                break;
            }
        }
    }

    // Tree silhouettes in the foreground
    float tree[3] = { 0.04f, 0.03f, 0.02f };
    for (int k = 0; k < 20; k++) {
        float tx = W * (0.02f + k * 0.051f + (k%3)*0.008f);
        float ty = H * (0.76f + 0.04f * sinf(k * 2.1f));
        float tr = 18 + 12 * sinf(k * 1.7f);
        float th2 = 60 + 30 * sinf(k * 0.9f);
        // trunk
        for (int y = (int)ty; y < (int)(ty + th2); y++) {
            int x = (int)tx;
            if (x >= 0 && x < W && y < H) mix3(img[y][x], tree, 0.95f);
            if (x+1 < W && y < H) mix3(img[y][x+1], tree, 0.95f);
        }
        // canopy
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float dx = x - tx, dy = y - (ty - tr * 0.6f);
            if (dx*dx/(tr*tr) + dy*dy/(tr*1.4f*tr*1.4f) < 1 && y < (int)(ty + th2))
                mix3(img[y][x], tree, 0.9f);
        }
    }

    // Final: darken bottom strip for dock readability
    for (int y = H - 140; y < H; y++) for (int x = 0; x < W; x++) {
        float t = smooth(H - 140, H, y) * 0.55f;
        for (int i = 0; i < 3; i++) img[y][x][i] *= (1 - t);
    }
    // Darken top strip for topbar
    for (int y = 0; y < 50; y++) for (int x = 0; x < W; x++) {
        float t = smooth(50, 0, y) * 0.4f;
        for (int i = 0; i < 3; i++) img[y][x][i] *= (1 - t);
    }
}

// ---- Wallpaper 1: Aurora (unchanged logic, just tweaked colors) --------------------------------
static void aurora(void) {
    vgrad(0xFF020812, 0xFF0B1F33);
    for (int i = 0; i < 900; i++) {   // stars
        int x = frand() * W, y = frand() * H * 0.7f;
        float b = 0.3f + 0.7f * frand();
        float s[3] = { b, b, b };
        add3(img[y][x], s, 0.8f);
    }
    const uint32_t cols[4] = { 0xFF38F2A4, 0xFF2BD8C6, 0xFF8A6CFF, 0xFF54FF8A };
    for (int k = 0; k < 4; k++) {
        float c[3]; hex(cols[k], c);
        float x0 = 300 + k * 420, amp = 180, f = 0.004f + k * 0.0007f, wid = 120 + 40 * k;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float cx2 = x0 + amp * sinf(y * f + k) + 0.35f * (y - 540);
            float d = (x - cx2) / wid;
            float v = expf(-d * d) * smooth(80, 420, y) * (1 - smooth(560, 900, y));
            float streak = 0.75f + 0.25f * sinf(x * 0.07f + k * 3);
            add3(img[y][x], c, 0.6f * v * streak);
        }
    }
    float hill[3]; hex(0xFF03070C, hill);
    for (int x = 0; x < W; x++) {
        float hy = 900 + 40 * sinf(x * 0.003f) + 25 * sinf(x * 0.011f + 1);
        for (int y = (int)hy; y < H; y++) mix3(img[y][x], hill, clampf(y - hy, 0, 1));
    }
}

// ---- Wallpaper 2: Dune (layered sand dunes, warm palette) ------------------------------------
static void dune(void) {
    vgrad(0xFF2A1450, 0xFFFF9A6A);
    glow(1240, 690, 260, 0xFFFFD27A, 0.9f);
    glow(1240, 690, 700, 0xFFFF6F91, 0.4f);
    const uint32_t layers[4] = { 0xFFB2507A, 0xFF7E3A73, 0xFF4E2560, 0xFF2A1640 };
    for (int k = 0; k < 4; k++) {
        float c[3]; hex(layers[k], c);
        float base = 700 + k * 95, amp = 70 - k * 8, f = 0.0018f + k * 0.0006f, ph = k * 2.1f;
        for (int x = 0; x < W; x++) {
            float hy = base + amp * sinf(x * f + ph) + 22 * sinf(x * f * 3.1f + ph);
            for (int y = (int)hy - 1; y < H; y++) {
                if (y < 0) continue;
                float t = clampf(y - hy + 1, 0, 1);
                float shade[3] = { c[0] * (1 - 0.25f * smooth(hy, H, y)), c[1] * (1 - 0.25f * smooth(hy, H, y)), c[2] * (1 - 0.2f * smooth(hy, H, y)) };
                mix3(img[y][x], shade, t);
            }
        }
    }
}

static void save(const char *dir, const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s.raw", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    uint32_t hdr[3] = { 0x57313257, W, H };
    fwrite(hdr, 4, 3, f);
    static uint32_t row[W];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t o = 0xFF000000;
            for (int i = 0; i < 3; i++) {
                float v = clampf(img[y][x][i] + (frand() - 0.5f) / 255.0f, 0, 1);
                o |= (uint32_t)(v * 255 + 0.5f) << (16 - 8 * i);
            }
            row[x] = o;
        }
        fwrite(row, 4, W, f);
    }
    fclose(f);
    printf("[wallpaper] %s\n", p);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    bloom();  save(dir, "bloom");    // Sunset City (FLX OS default)
    aurora(); save(dir, "aurora");
    dune();   save(dir, "dune");
    return 0;
}
