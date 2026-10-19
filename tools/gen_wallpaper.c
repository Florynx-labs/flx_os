// Build-time procedural wallpaper generator for Windows 12. Runs in the build container,
// writes 1920x1080 raw images ("W12W", width, height, then ARGB words) into the rootfs.
//   bloom.raw  - soft glass ribbons over deep blue
//   aurora.raw - green/violet curtains over a night sky
//   dune.raw   - layered sunset dunes
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
static void add3(float *d, const float *s, float t) { for (int i = 0; i < 3; i++) d[i] = 1 - (1 - d[i]) * (1 - s[i] * t); }   // screen blend
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
        if (d < 4) add3(img[y][x], c, k * expf(-d * 1.6f));
    }
}

static void bloom(void) {
    vgrad(0xFF07102E, 0xFF14307A);
    glow(1380, 620, 700, 0xFF2D6BFF, 0.55f);
    glow(520, 200, 520, 0xFF6A3CFF, 0.35f);
    const uint32_t cols[5] = { 0xFF7FE3FF, 0xFF3D8BFF, 0xFFA77BFF, 0xFF4FD1FF, 0xFFFF7AC6 };
    for (int k = 0; k < 5; k++) {
        float c[3]; hex(cols[k], c);
        float base = 380 + k * 95, amp = 140 + k * 30, f = 0.0021f + k * 0.0004f, ph = k * 1.3f, wid = 26 + k * 9;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float cy = base + amp * sinf(x * f + ph) + 60 * sinf(x * f * 2.7f + ph * 2);
            float d = (y - cy) / wid;
            float e = expf(-d * d) * (0.35f + 0.65f * smooth(0, 900, x)) * (1 - 0.6f * smooth(1500, 1920, x));
            float edge = expf(-powf((y - cy - wid * 0.9f) / 3.0f, 2)) * 0.5f;   // bright glassy rim
            add3(img[y][x], c, 0.55f * e + edge * 0.35f * e / fmaxf(e, 0.05f) * smooth(0, 700, x));
        }
    }
}

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
            float cx = x0 + amp * sinf(y * f + k) + 0.35f * (y - 540);
            float d = (x - cx) / wid;
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
                float v = clampf(img[y][x][i] + (frand() - 0.5f) / 255.0f, 0, 1);   // dither against banding
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
    bloom(); save(dir, "bloom");
    aurora(); save(dir, "aurora");
    dune(); save(dir, "dune");
    return 0;
}
