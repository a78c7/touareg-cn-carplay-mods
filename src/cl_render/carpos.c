/* v19.4: find the AMap car icon on the cluster map (read by clusterd, amap_turn=3).
 * AMap draws the car at the centre of the safe area or ~213 px left/right of it, and the side follows the destination
 * direction, which the head unit never gets. So instead of guessing, look at the picture: once a second the renderer
 * copies the band under the map centre (cluster rows 380..639, columns 300..1619) at half size into a CPU-readable
 * pixmap and runs normalised cross-correlation with a 25x42 grey template of the icon. The result goes to /tmp/cl_car;
 * clusterd turns it into a safe-area choice. Plain C, no screen calls, so it can be tested on a PC with screenshots. */
#include <stdint.h>
#include "carpos_tmpl.h"

/* own square root: keeps libm out of cl_render's dependencies (it never needed it before) */
static double carpos_sqrt(double v) {
    if (v <= 0) return 0;
    double r = v > 1 ? v / 2 : 1;
    for (int i = 0; i < 40; i++) { double n = 0.5 * (r + v / r); if (n == r) break; r = n; }
    return r;
}

#define CARPOS_X0 300          /* band in cluster (output) pixels */
#define CARPOS_Y0 380
#define CARPOS_BW 1320
#define CARPOS_BH 260
#define CARPOS_W (CARPOS_BW / 2) /* band at half size, as the blit delivers it */
#define CARPOS_H (CARPOS_BH / 2)

static unsigned char carpos_g[CARPOS_W * CARPOS_H];
/* integral images in uint32: the totals of squares wrap, but window differences are exact modulo 2^32 and a window
 * (25x42 pixels, <= 68M) never reaches 2^32. Kept 32-bit also because QNX 6.6 libc lacks __aeabi_l2d. */
static uint32_t carpos_s1[(CARPOS_W + 1) * (CARPOS_H + 1)], carpos_s2[(CARPOS_W + 1) * (CARPOS_H + 1)];
static int16_t carpos_tz[CARPOS_NT][CARPOS_TW * CARPOS_TH];
static double carpos_tn[CARPOS_NT];

static void carpos_init(void) {
    int n = CARPOS_TW * CARPOS_TH;
    for (int k = 0; k < CARPOS_NT; k++) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += carpos_tmpl[k][i];
        double ss = 0;
        for (int i = 0; i < n; i++) {
            /* x16 keeps the rounding of the template mean */
            carpos_tz[k][i] = (int16_t)(carpos_tmpl[k][i] * 16 - (sum * 16 + n / 2) / n);
            ss += (double)carpos_tz[k][i] * carpos_tz[k][i];
        }
        carpos_tn[k] = carpos_sqrt(ss);
    }
}

static double carpos_ncc(int k, int x, int y) {
    const int n = CARPOS_TW * CARPOS_TH, W1 = CARPOS_W + 1;
    int32_t acc = 0;                          /* |acc| < 26M x 42 rows < 2^31 */
    for (int j = 0; j < CARPOS_TH; j++) {
        const unsigned char *g = carpos_g + (y + j) * CARPOS_W + x;
        const int16_t *t = carpos_tz[k] + j * CARPOS_TW;
        int32_t r = 0;
        for (int i = 0; i < CARPOS_TW; i++) r += g[i] * t[i];
        acc += r;
    }
    int a = y * W1 + x, b = a + CARPOS_TW, c = (y + CARPOS_TH) * W1 + x, d = c + CARPOS_TW;
    double s1 = (double)(uint32_t)(carpos_s1[d] - carpos_s1[b] - carpos_s1[c] + carpos_s1[a]);
    double s2 = (double)(uint32_t)(carpos_s2[d] - carpos_s2[b] - carpos_s2[c] + carpos_s2[a]);
    double var = s2 - s1 * s1 / n;
    if (var < n * 4.0) return 0;               /* flat area (std < 2): no icon */
    return acc / (carpos_sqrt(var) * carpos_tn[k]);
}

/* px: RGBA (or RGBX/BGRA, grey is the same either way) band of CARPOS_W x CARPOS_H, stride in bytes.
 * Out: icon centre in cluster pixels and the score (-1..1). */
static void carpos_prepare(const unsigned char *px, int stride) {
    if (carpos_tn[0] == 0) carpos_init();
    const int W1 = CARPOS_W + 1;
    for (int y = 0; y < CARPOS_H; y++) {
        const unsigned char *p = px + y * stride;
        unsigned char *g = carpos_g + y * CARPOS_W;
        for (int x = 0; x < CARPOS_W; x++, p += 4) g[x] = (unsigned char)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
    }
    for (int x = 0; x <= CARPOS_W; x++) carpos_s1[x] = carpos_s2[x] = 0;
    for (int y = 0; y < CARPOS_H; y++) {
        uint32_t r1 = 0, r2 = 0;
        carpos_s1[(y + 1) * W1] = carpos_s2[(y + 1) * W1] = 0;
        for (int x = 0; x < CARPOS_W; x++) {
            unsigned v = carpos_g[y * CARPOS_W + x];
            r1 += v; r2 += v * v;
            carpos_s1[(y + 1) * W1 + x + 1] = carpos_s1[y * W1 + x + 1] + r1;
            carpos_s2[(y + 1) * W1 + x + 1] = carpos_s2[y * W1 + x + 1] + r2;
        }
    }
}

static void carpos_find(const unsigned char *px, int stride, int *ox, int *oy, double *oscore) {
    carpos_prepare(px, stride);
    /* per template: every 2nd position, then the 5x5 around its best; keep the best of all */
    int bx = 0, by = 0; double best = -2;
    for (int k = 0; k < CARPOS_NT; k++) {
        int kx = 0, ky = 0; double kb = -2;
        for (int y = 0; y + CARPOS_TH <= CARPOS_H; y += 2)
            for (int x = 0; x + CARPOS_TW <= CARPOS_W; x += 2) {
                double s = carpos_ncc(k, x, y);
                if (s > kb) { kb = s; kx = x; ky = y; }
            }
        int cx = kx, cy = ky;
        for (int y = cy - 2; y <= cy + 2; y++)
            for (int x = cx - 2; x <= cx + 2; x++) {
                if (x < 0 || y < 0 || x + CARPOS_TW > CARPOS_W || y + CARPOS_TH > CARPOS_H) continue;
                double s = carpos_ncc(k, x, y);
                if (s > kb) { kb = s; kx = x; ky = y; }
            }
        if (kb > best) { best = kb; bx = kx; by = ky; }
    }
    *ox = CARPOS_X0 + bx * 2 + CARPOS_TW;      /* centre: (bx + TW/2) * 2 */
    *oy = CARPOS_Y0 + by * 2 + CARPOS_TH;
    *oscore = best;
}

/* v19.5: AMap puts the car at the centre of the band or 213 px (106.5 at half size) left or right of it, nowhere else.
 * Instead of one global best match against a fixed threshold (failed on views the templates did not cover, such as
 * the lane-level view), score each of the three spots (best template, x within +-20 half-size px, any y) and take
 * the spot that stands out. side -1/0/1, best and second-best scores, centre of the winner in band pixels
 * (full scale, 0..CARPOS_BW / 0..CARPOS_BH). */
static void carpos_find3(const unsigned char *px, int stride, int *side, double *best, double *second, int *ox, int *oy) {
    carpos_prepare(px, stride);
    double sc[3]; int bx[3], by[3];
    for (int c = 0; c < 3; c++) {
        int cx = CARPOS_W / 2 + (c - 1) * 107 - CARPOS_TW / 2;     /* template left edge for an icon centred there */
        sc[c] = -2; bx[c] = cx; by[c] = 0;
        for (int k = 0; k < CARPOS_NT; k++)
            for (int y = 0; y + CARPOS_TH <= CARPOS_H; y++)
                for (int x = cx - 20; x <= cx + 20; x++) {
                    if (x < 0 || x + CARPOS_TW > CARPOS_W) continue;
                    double s = carpos_ncc(k, x, y);
                    if (s > sc[c]) { sc[c] = s; bx[c] = x; by[c] = y; }
                }
    }
    int w = 0; for (int c = 1; c < 3; c++) if (sc[c] > sc[w]) w = c;
    double s2 = -2; for (int c = 0; c < 3; c++) if (c != w && sc[c] > s2) s2 = sc[c];
    *side = w - 1; *best = sc[w]; *second = s2;
    *ox = bx[w] * 2 + CARPOS_TW; *oy = by[w] * 2 + CARPOS_TH;
}
