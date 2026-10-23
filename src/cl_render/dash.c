/*
 * dash.c - cluster info box for cl_render (VW Touareg MH2p, v16).
 *
 * A small panel on the left of the CarPlay map on the instrument cluster, one value per row:
 * power (kW), torque (Nm), G (total, + accelerating / - braking), engine speed (rpm), engine oil and
 * gearbox oil temperature (C). Gear, fuel and range are already shown by the cluster itself.
 * Data comes from the head-unit Java bundle (VWClusterNav, class vwcn.Dash), which writes one line of
 * key=value pairs to /tmp/dash.state about 5 times a second while the CarPlay map is on the cluster
 * (it also writes keys this panel does not show: gear, coolant, consumption, tank).
 * Settings in cl.conf: info=0 hides the box, info_x / info_y position, info_alpha background opacity.
 *
 * The panel is drawn on the CPU into an RGBA pixmap and screen_blit'ed (source-over) onto every frame
 * that goes to the cluster window. Portable C, no QNX dependencies in this file (host-testable).
 * Glyphs: dash_font.h, pre-rendered from Noto Sans CJK SC (SIL Open Font License).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* own helpers: avoid libm lrint/lrintf and the DASH_NAN() -> _LDclass import on QNX */
#define DASH_NAN(x) ((x) != (x))
static int dash_round(double d) { return d < 0 ? -(int)(-d + 0.5) : (int)(d + 0.5); }
#include "dash_font.h"

#define DASH_W        290
#define DASH_ROWS     6
#define DASH_ROW_H    52
#define DASH_PAD_Y    12
#define DASH_H        (DASH_PAD_Y * 2 + DASH_ROWS * DASH_ROW_H)
#define DASH_LAB_X    18
#define DASH_VAL_R    212   /* right edge of the value column */
#define DASH_UNIT_X   220
#define DASH_RADIUS   16
#define DASH_STATE    "/tmp/dash.state"

typedef struct {
    int enabled;   /* info=0 hides the box */
    int x, y;      /* top-left in cluster window pixels; y < 0 = vertically centred */
    int alpha;     /* background opacity 0..255 */
    int tank_l;    /* tank capacity in litres for the fuel row; 0 = show percent */
    int stale_ms;  /* hide the box when the Java side stopped writing for this long */
    int lat_flip;  /* 1 = invert the lateral g sign */
} dash_conf_t;

static dash_conf_t g_dash_conf = { 1, 40, 172, 200, 90, 5000, 0 };

/* parsed values; NAN / -1 = not available */
typedef struct {
    long  n;                 /* writer counter (changes every write) */
    float p, gx, gy, cv, ca; /* kW, longitudinal/lateral g (signed), current consumption, trip average */
    int   tq, gm, gc, gbt, oil, cool, cu, au, tank, rpm;
} dash_vals_t;

static char      g_dash_line[256];
static int       g_dash_have;          /* a fresh line is available */
static uint64_t  g_dash_last_new_ms;   /* when the counter last changed */
static long      g_dash_last_n = -1;
static int       g_dash_shown;         /* box currently drawn on posted frames */
static uint32_t  g_dash_px[DASH_W * DASH_H];

static void dash_read_conf_line(const char *l) {
    int v;
    if (sscanf(l, " info = %d", &v) == 1) g_dash_conf.enabled = v;
    else if (sscanf(l, " info_x = %d", &v) == 1) g_dash_conf.x = v;
    else if (sscanf(l, " info_y = %d", &v) == 1) g_dash_conf.y = v;
    else if (sscanf(l, " info_alpha = %d", &v) == 1) g_dash_conf.alpha = v < 0 ? 0 : v > 255 ? 255 : v;
    else if (sscanf(l, " tank_l = %d", &v) == 1) g_dash_conf.tank_l = v;
    else if (sscanf(l, " lat_flip = %d", &v) == 1) g_dash_conf.lat_flip = v;
}

static void dash_parse(const char *line, dash_vals_t *o) {
    o->n = -1; o->p = o->gx = o->gy = o->cv = o->ca = 0.0f / 0.0f;
    o->tq = o->gm = o->gc = o->gbt = o->oil = o->cool = o->cu = o->au = o->tank = o->rpm = -999;
    const char *s = line;
    while (*s) {
        while (*s == ' ' || *s == '\n' || *s == '\r') s++;
        if (!*s) break;
        const char *eq = strchr(s, '=');
        if (!eq) break;
        char key[8]; int kl = (int)(eq - s); if (kl > 7) kl = 7;
        memcpy(key, s, kl); key[kl] = 0;
        const char *v = eq + 1;
        double d = strtod(v, NULL);
        if      (!strcmp(key, "n"))    o->n = (long)d;
        else if (!strcmp(key, "p"))    o->p = (float)d;
        else if (!strcmp(key, "gx"))   o->gx = (float)d;
        else if (!strcmp(key, "gy"))   o->gy = (float)d;
        else if (!strcmp(key, "cv"))   o->cv = (float)d;
        else if (!strcmp(key, "ca"))   o->ca = (float)d;
        else if (!strcmp(key, "au"))   o->au = (int)d;
        else if (!strcmp(key, "tq"))   o->tq = (int)d;
        else if (!strcmp(key, "gm"))   o->gm = (int)d;
        else if (!strcmp(key, "gc"))   o->gc = (int)d;
        else if (!strcmp(key, "gbt"))  o->gbt = dash_round(d);
        else if (!strcmp(key, "oil"))  o->oil = dash_round(d);
        else if (!strcmp(key, "cool")) o->cool = dash_round(d);
        else if (!strcmp(key, "cu"))   o->cu = (int)d;
        else if (!strcmp(key, "tank")) o->tank = (int)d;
        else if (!strcmp(key, "rpm"))  o->rpm = (int)d;
        while (*s && *s != ' ') s++;
    }
}

/* ---- drawing ---- */
static uint32_t dash_px(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void dash_fill_bg(void) {
    const int R = DASH_RADIUS;
    for (int y = 0; y < DASH_H; y++) {
        for (int x = 0; x < DASH_W; x++) {
            float cov = 1.0f;
            int cx = x < R ? R : (x >= DASH_W - R ? DASH_W - R - 1 : x);
            int cy = y < R ? R : (y >= DASH_H - R ? DASH_H - R - 1 : y);
            if (cx != x || cy != y) {
                float dx = (float)(x - cx), dy = (float)(y - cy);
                float d = sqrtf(dx * dx + dy * dy);
                cov = (float)R + 0.5f - d;
                if (cov < 0) cov = 0; if (cov > 1) cov = 1;
            }
            int a = (int)(g_dash_conf.alpha * cov + 0.5f);
            g_dash_px[y * DASH_W + x] = dash_px(16, 18, 22, a);
        }
    }
}

/* blend a coverage bitmap (text colour c) into the panel at (x, top) */
static void dash_blit_mask(const dash_glyph *gl, int x, int top, int c) {
    for (int y = 0; y < gl->h; y++) {
        int py = top + y; if (py < 0 || py >= DASH_H) continue;
        for (int i = 0; i < gl->w; i++) {
            int px = x + i; if (px < 0 || px >= DASH_W) continue;
            int cov = gl->bm[y * gl->w + i];
            if (!cov) continue;
            uint32_t d = g_dash_px[py * DASH_W + px];
            int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
            int r = (dr * (255 - cov) + c * cov) / 255;
            int g = (dg * (255 - cov) + c * cov) / 255;
            int b = (db * (255 - cov) + c * cov) / 255;
            int a = da + (255 - da) * cov / 255;
            g_dash_px[py * DASH_W + px] = dash_px(r, g, b, a);
        }
    }
}

static const dash_glyph *dash_find(const dash_glyph *set, int n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(set[i].key, key)) return &set[i];
    return NULL;
}

/* value string right-aligned so it ends at x_right; baseline at base_y */
static void dash_draw_value(const char *s, int x_right, int base_y, int c) {
    int w = 0;
    for (const char *p = s; *p; p++) {
        char k[2] = { *p, 0 };
        const dash_glyph *gl = dash_find(dash_val, DASH_VAL_N, k);
        if (gl) w += gl->adv;
    }
    int x = x_right - w;
    for (const char *p = s; *p; p++) {
        char k[2] = { *p, 0 };
        const dash_glyph *gl = dash_find(dash_val, DASH_VAL_N, k);
        if (!gl) continue;
        dash_blit_mask(gl, x, base_y - gl->base, c);
        x += gl->adv;
    }
}

static void dash_draw_str(const dash_glyph *set, int n, const char *key, int x, int base_y, int c) {
    const dash_glyph *gl = dash_find(set, n, key);
    if (gl) dash_blit_mask(gl, x, base_y - gl->base, c);
}

static const char *dash_gear(int gm, int gc, char *buf) {
    char m = 0;
    switch (gm) {
    case 1: return "P";
    case 2: return "R";
    case 3: return "N";
    case 4: case 13: m = 'D'; break;
    case 5: case 11: m = 'S'; break;
    case 6: case 7: case 10: case 12: m = 'M'; break;
    case 8: m = 'E'; break;
    case 9: m = 'B'; break;
    default: return "-";
    }
    if (gc >= 1 && gc <= 10) snprintf(buf, 8, "%c%d", m, gc);
    else snprintf(buf, 8, "%c", m);
    return buf;
}

static void dash_render(const dash_vals_t *v) {
    /* v16g: 6 rows. Gear (cluster bottom-left), fuel/range (bottom-right) and both consumptions dropped at the user's request;
     * y = 172 keeps the box between the cluster's own navigation bar (top) and the gear box (bottom-left). */
    static const char *labels[DASH_ROWS] = { "功率", "扭矩", "G 值", "转速", "机油", "变速箱" };
    const int C_LAB = 165, C_VAL = 255, C_UNIT = 150;
    char val[DASH_ROWS][16]; const char *unit[DASH_ROWS];
    char gb[8];
    memset(unit, 0, sizeof unit);
    #define NA(cond, ...) do { if (cond) snprintf(val[i], 16, "-"); else snprintf(val[i], 16, __VA_ARGS__); } while (0)
    int i;
    i = 0; NA(DASH_NAN(v->p), "%d", dash_round(v->p));           unit[i] = "kW";
    i = 1; NA(v->tq == -999, "%d", v->tq);                     unit[i] = "Nm";
    /* G: total (longitudinal + lateral). Sign from the longitudinal part: + accelerating, - braking;
     * no sign when it is under 0.03 g (pure cornering / cruising) */
    i = 2;
    if (DASH_NAN(v->gx) || DASH_NAN(v->gy)) snprintf(val[i], 16, "-");
    else {
        float gt = sqrtf(v->gx * v->gx + v->gy * v->gy);
        if (gt < 0.005f) snprintf(val[i], 16, "0.00");
        else if (v->gx > -0.03f && v->gx < 0.03f) snprintf(val[i], 16, "%.2f", gt);
        else snprintf(val[i], 16, "%c%.2f", v->gx < 0 ? '-' : '+', gt);
    }
    unit[i] = "g";
    i = 3; NA(v->rpm == -999, "%d", v->rpm);                   unit[i] = "rpm";   /* v16i: replaces coolant */
    i = 4; NA(v->oil == -999, "%d", v->oil);                   unit[i] = "°C";
    i = 5; NA(v->gbt == -999, "%d", v->gbt);                   unit[i] = "°C";    /* v16j order: power torque G rpm oil gearbox */
    (void)gb; (void)dash_gear;
    #undef NA

    dash_fill_bg();
    for (i = 0; i < DASH_ROWS; i++) {
        int top = DASH_PAD_Y + i * DASH_ROW_H;
        int base = top + DASH_ROW_H / 2 + 13;   /* visually centred baseline for the 40px digits */
        dash_draw_str(dash_lab, DASH_LAB_N, labels[i], DASH_LAB_X, base - 1, C_LAB);
        dash_draw_value(val[i], DASH_VAL_R, base, C_VAL);
        if (unit[i]) dash_draw_str(dash_unit, DASH_UNIT_N, unit[i], DASH_UNIT_X, base - 1, C_UNIT);
    }
}

/* Poll the state file. Returns 1 when the box content or visibility changed (re-render/re-post needed). */
static int dash_poll(uint64_t now_ms) {
    int want = 0, changed = 0;
    if (g_dash_conf.enabled) {
        /* /tmp on this unit has no rename/link: the Java side's tmp+rename leaves the data in .new -> read either.
         * Only complete lines (ending in '\n') are used, so a read during a rewrite is skipped. */
        FILE *f = fopen(DASH_STATE, "r");
        if (!f) f = fopen(DASH_STATE ".new", "r");
        if (f) {
            char l[256];
            if (fgets(l, sizeof l, f) && strchr(l, '\n')) {
                dash_vals_t v; dash_parse(l, &v);
                if (v.n != g_dash_last_n) { g_dash_last_n = v.n; g_dash_last_new_ms = now_ms; }
                if (strcmp(l, g_dash_line)) {
                    strncpy(g_dash_line, l, sizeof g_dash_line - 1);
                    dash_render(&v);
                    changed = 1;
                }
            }
            fclose(f);
        }
        want = g_dash_last_n >= 0 && now_ms - g_dash_last_new_ms < (uint64_t)g_dash_conf.stale_ms;
    }
    if (want != g_dash_have) { g_dash_have = want; changed = 1; }
    return changed;
}
