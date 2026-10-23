/*
 * dash.c - cluster info box for cl_render (VW Touareg MH2p, v17).
 *
 * A translucent box on the left of the CarPlay map on the instrument cluster, one value per row.
 * Which values and in which order is chosen in dash.conf (items=...), up to DASH_MAX_ROWS rows; see the
 * catalogue g_items[] below (key, Chinese label, unit). Seven rows or more switch to a compact font so the
 * box still fits between the cluster's own navigation bar and its gear box.
 *
 * Data comes from the head-unit Java bundle (VWClusterNav, class vwcn.Dash), which writes one line of
 * key=value pairs to /tmp/dash.state about 5 times a second while the CarPlay map is on the cluster.
 * The panel is drawn on the CPU into an RGBA pixmap and screen_blit'ed (source-over) onto every frame that
 * goes to the cluster window. Portable C, no QNX dependencies in this file (host-testable).
 * Glyphs: dash_font.h, pre-rendered from Noto Sans CJK SC (SIL Open Font License 1.1).
 * Units assume the car is set to metric (km/h, degC, L/100km).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dash_font.h"

/* own helpers: avoid libm lrint/lrintf and the isnan() -> _LDclass import on QNX */
#define DASH_NAN(x) ((x) != (x))
static int dash_round(double d) { return d < 0 ? -(int)(-d + 0.5) : (int)(d + 0.5); }

#define DASH_MAX_ROWS 10
#define DASH_MAX_W    600
#define DASH_MAX_H    640
#define DASH_PAD_X    18
#define DASH_PAD_Y    12
#define DASH_GAP      14
#define DASH_RADIUS   16
#define DASH_STATE    "/tmp/dash.state"

typedef struct {
    int enabled;   /* info=0 / show=0 hides the box */
    int x, y;      /* top-left in cluster window pixels */
    int alpha;     /* background opacity 0..255 */
    int tank_l;    /* tank capacity in litres for the fuel row; 0 = show percent */
    int stale_ms;  /* hide the box when the Java side stopped writing for this long */
    int lat_flip;  /* 1 = invert the lateral g sign */
    int max_h;     /* space available below y before the cluster's gear box (switch to compact above it) */
    int compact;   /* -1 auto, 0 normal, 1 compact */
} dash_conf_t;

static dash_conf_t g_dash_conf = { 1, 40, 172, 200, 90, 5000, 0, 348, -1 };

/* ---- the item catalogue: config key, label (also accepted in items=), unit, sample for column width ---- */
enum { IT_POWER, IT_TORQUE, IT_G, IT_GLONG, IT_GLAT, IT_RPM, IT_SPEED, IT_PEDAL, IT_BRAKE, IT_STEER, IT_GEAR,
       IT_GEARBOX, IT_OIL, IT_COOLANT, IT_OUTSIDE, IT_CONS, IT_TRIP, IT_REFUEL, IT_FUEL, IT_RANGE, IT_TRIPDIST,
       IT_TRIPSPEED, IT_TRIPTIME, IT_N };
typedef struct { const char *key; const char *label; const char *unit; const char *sample; } dash_item;
static const dash_item g_items[IT_N] = {
    { "power",     "功率",       "kW",      "-888" },
    { "torque",    "扭矩",       "Nm",      "-888" },
    { "g",         "G 值",       "g",       "-8.88" },
    { "glong",     "纵向 G",     "g",       "-8.88" },
    { "glat",      "横向 G",     "g",       "-8.88" },
    { "rpm",       "转速",       "rpm",     "8888" },
    { "speed",     "车速",       "km/h",    "888" },
    { "pedal",     "油门",       "%",       "888" },
    { "brake",     "刹车",       "bar",     "88.8" },
    { "steer",     "方向盘",     "°",       "-888" },
    { "gear",      "挡位",       NULL,      "M8" },
    { "gearbox",   "变速箱",     "°C",      "188" },
    { "oil",       "机油",       "°C",      "188" },
    { "coolant",   "冷却液",     "°C",      "188" },
    { "outside",   "外温",       "°C",      "-88" },
    { "cons",      "瞬时油耗",   "L/100km", "88.8" },
    { "trip",      "本程油耗",   "L/100km", "88.8" },
    { "refuel",    "加油后油耗", "L/100km", "88.8" },
    { "fuel",      "油量",       "L",       "188" },
    { "range",     "续航",       "km",      "1888" },
    { "tripdist",  "本程里程",   "km",      "8888" },
    { "tripspeed", "平均车速",   "km/h",    "188" },
    { "triptime",  "行驶时间",   "h",       "88:88" },
};
static int g_rows[DASH_MAX_ROWS] = { IT_POWER, IT_TORQUE, IT_G, IT_RPM, IT_OIL, IT_GEARBOX };
static int g_nrows = 6;

/* parsed values; NAN / -999 = not available */
typedef struct {
    long  n;
    float p, gx, gy, cv, ca, cr, spd, brk, tdist, tspd;
    int   tq, gm, gc, gbt, oil, cool, cu, au, cru, tank, rpm, ped, str, out, rng, ttime;
} dash_vals_t;

/* ---- layout (computed once in dash_layout) ---- */
static const dash_glyph *L_val, *L_lab, *L_unit;
static int L_nval, L_nlab, L_nunit, L_row_h, L_base_off;
static int g_dash_w = 300, g_dash_h = 100, L_lab_x, L_val_r, L_unit_x;

static char      g_dash_line[512];
static int       g_dash_have;
static uint64_t  g_dash_last_new_ms;
static long      g_dash_last_n = -1;
static uint32_t  g_dash_px[DASH_MAX_W * DASH_MAX_H];

/* items=power,torque,... (keys or Chinese labels, spaces ignored) */
static void dash_parse_items(const char *v) {
    int n = 0;
    char buf[512];
    strncpy(buf, v, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    char *s = buf;
    while (*s && n < DASH_MAX_ROWS) {
        char *e = s;
        while (*e && *e != ',' && *e != '\n' && *e != '\r' && *e != '#') e++;
        char term = *e; *e = 0;
        char tok[64]; int tl = 0;                         /* token without spaces */
        for (char *p = s; *p && tl < 63; p++) if (*p != ' ' && *p != '\t') tok[tl++] = *p;
        tok[tl] = 0;
        if (tl) {
            for (int i = 0; i < IT_N; i++) {
                char lab[64]; int ll = 0;
                for (const char *p = g_items[i].label; *p && ll < 63; p++) if (*p != ' ') lab[ll++] = *p;
                lab[ll] = 0;
                if (!strcmp(tok, g_items[i].key) || !strcmp(tok, lab)) { g_rows[n++] = i; break; }
            }
        }
        if (!term || term == '\n' || term == '\r' || term == '#') break;
        s = e + 1;
    }
    if (n > 0) g_nrows = n;
}

static void dash_read_conf_line(const char *l) {
    int v;
    while (*l == ' ' || *l == '\t') l++;
    if (*l == '#') return;
    if (!strncmp(l, "items", 5)) { const char *eq = strchr(l, '='); if (eq) dash_parse_items(eq + 1); return; }
    if (sscanf(l, "info = %d", &v) == 1 || sscanf(l, "show = %d", &v) == 1) g_dash_conf.enabled = v;
    else if (sscanf(l, "info_x = %d", &v) == 1 || sscanf(l, "x = %d", &v) == 1) g_dash_conf.x = v;
    else if (sscanf(l, "info_y = %d", &v) == 1 || sscanf(l, "y = %d", &v) == 1) g_dash_conf.y = v;
    else if (sscanf(l, "info_alpha = %d", &v) == 1 || sscanf(l, "alpha = %d", &v) == 1) g_dash_conf.alpha = v < 0 ? 0 : v > 255 ? 255 : v;
    else if (sscanf(l, "tank_l = %d", &v) == 1) g_dash_conf.tank_l = v;
    else if (sscanf(l, "lat_flip = %d", &v) == 1) g_dash_conf.lat_flip = v;
    else if (sscanf(l, "max_h = %d", &v) == 1) g_dash_conf.max_h = v;
    else if (sscanf(l, "compact = %d", &v) == 1) g_dash_conf.compact = v;
}

static const dash_glyph *dash_find(const dash_glyph *set, int n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(set[i].key, key)) return &set[i];
    return NULL;
}

static int dash_str_w(const char *s) {   /* width of a value string in the value font */
    int w = 0;
    for (const char *p = s; *p; p++) {
        char k[2] = { *p, 0 };
        const dash_glyph *gl = dash_find(L_val, L_nval, k);
        if (gl) w += gl->adv;
    }
    return w;
}

static void dash_layout(void) {
    int compact = g_dash_conf.compact;
    if (compact < 0) compact = DASH_PAD_Y * 2 + g_nrows * 52 > g_dash_conf.max_h;
    if (compact) {
        L_val = dash_c_val; L_nval = DASH_C_VAL_N; L_lab = dash_c_lab; L_nlab = DASH_C_LAB_N;
        L_unit = dash_c_unit; L_nunit = DASH_C_UNIT_N; L_row_h = 40; L_base_off = 11;
    } else {
        L_val = dash_n_val; L_nval = DASH_N_VAL_N; L_lab = dash_n_lab; L_nlab = DASH_N_LAB_N;
        L_unit = dash_n_unit; L_nunit = DASH_N_UNIT_N; L_row_h = 52; L_base_off = 13;
    }
    int lab_w = 0, val_w = 0, unit_w = 0;
    for (int r = 0; r < g_nrows; r++) {
        const dash_item *it = &g_items[g_rows[r]];
        const dash_glyph *gl = dash_find(L_lab, L_nlab, it->label);
        if (gl && gl->adv > lab_w) lab_w = gl->adv;
        int vw = dash_str_w(it->sample);
        if (vw > val_w) val_w = vw;
        const char *u = it->unit;
        if (g_rows[r] == IT_CONS) u = "L/100km";
        if (u) { gl = dash_find(L_unit, L_nunit, u); if (gl && gl->adv > unit_w) unit_w = gl->adv; }
    }
    L_lab_x = DASH_PAD_X;
    L_val_r = L_lab_x + lab_w + DASH_GAP + val_w;
    L_unit_x = L_val_r + 8;
    g_dash_w = L_unit_x + unit_w + DASH_PAD_X;
    g_dash_h = DASH_PAD_Y * 2 + g_nrows * L_row_h;
    if (g_dash_w > DASH_MAX_W) g_dash_w = DASH_MAX_W;
    if (g_dash_h > DASH_MAX_H) g_dash_h = DASH_MAX_H;
}

static void dash_parse(const char *line, dash_vals_t *o) {
    float nanv = 0.0f / 0.0f;
    o->n = -1;
    o->p = o->gx = o->gy = o->cv = o->ca = o->cr = o->spd = o->brk = o->tdist = o->tspd = nanv;
    o->tq = o->gm = o->gc = o->gbt = o->oil = o->cool = o->cu = o->au = o->cru = o->tank = o->rpm =
        o->ped = o->str = o->out = o->rng = o->ttime = -999;
    const char *s = line;
    while (*s) {
        while (*s == ' ' || *s == '\n' || *s == '\r') s++;
        if (!*s) break;
        const char *eq = strchr(s, '=');
        if (!eq) break;
        char key[8]; int kl = (int)(eq - s); if (kl > 7) kl = 7;
        memcpy(key, s, kl); key[kl] = 0;
        double d = strtod(eq + 1, NULL);
        int di = dash_round(d);
        if      (!strcmp(key, "n"))    o->n = (long)d;
        else if (!strcmp(key, "p"))    o->p = (float)d;
        else if (!strcmp(key, "gx"))   o->gx = (float)d;
        else if (!strcmp(key, "gy"))   o->gy = (float)d;
        else if (!strcmp(key, "cv"))   o->cv = (float)d;
        else if (!strcmp(key, "ca"))   o->ca = (float)d;
        else if (!strcmp(key, "cr"))   o->cr = (float)d;
        else if (!strcmp(key, "spd"))  o->spd = (float)d;
        else if (!strcmp(key, "brk"))  o->brk = (float)d;
        else if (!strcmp(key, "td"))   o->tdist = (float)d;
        else if (!strcmp(key, "ts"))   o->tspd = (float)d;
        else if (!strcmp(key, "tq"))   o->tq = di;
        else if (!strcmp(key, "gm"))   o->gm = di;
        else if (!strcmp(key, "gc"))   o->gc = di;
        else if (!strcmp(key, "gbt"))  o->gbt = di;
        else if (!strcmp(key, "oil"))  o->oil = di;
        else if (!strcmp(key, "cool")) o->cool = di;
        else if (!strcmp(key, "cu"))   o->cu = di;
        else if (!strcmp(key, "au"))   o->au = di;
        else if (!strcmp(key, "cru"))  o->cru = di;
        else if (!strcmp(key, "tank")) o->tank = di;
        else if (!strcmp(key, "rpm"))  o->rpm = di;
        else if (!strcmp(key, "ped"))  o->ped = di;
        else if (!strcmp(key, "str"))  o->str = di;
        else if (!strcmp(key, "out"))  o->out = di;
        else if (!strcmp(key, "rng"))  o->rng = di;
        else if (!strcmp(key, "tt"))   o->ttime = di;
        while (*s && *s != ' ') s++;
    }
}

/* ---- drawing ---- */
static uint32_t dash_px(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void dash_fill_bg(void) {
    const int R = DASH_RADIUS, W = g_dash_w, H = g_dash_h;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float cov = 1.0f;
            int cx = x < R ? R : (x >= W - R ? W - R - 1 : x);
            int cy = y < R ? R : (y >= H - R ? H - R - 1 : y);
            if (cx != x || cy != y) {
                float dx = (float)(x - cx), dy = (float)(y - cy);
                cov = (float)R + 0.5f - sqrtf(dx * dx + dy * dy);
                if (cov < 0) cov = 0; if (cov > 1) cov = 1;
            }
            g_dash_px[y * W + x] = dash_px(16, 18, 22, (int)(g_dash_conf.alpha * cov + 0.5f));
        }
    }
}

static void dash_blit_mask(const dash_glyph *gl, int x, int top, int c) {
    const int W = g_dash_w, H = g_dash_h;
    for (int y = 0; y < gl->h; y++) {
        int py = top + y; if (py < 0 || py >= H) continue;
        for (int i = 0; i < gl->w; i++) {
            int px = x + i; if (px < 0 || px >= W) continue;
            int cov = gl->bm[y * gl->w + i];
            if (!cov) continue;
            uint32_t d = g_dash_px[py * W + px];
            int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
            g_dash_px[py * W + px] = dash_px((dr * (255 - cov) + c * cov) / 255, (dg * (255 - cov) + c * cov) / 255,
                                             (db * (255 - cov) + c * cov) / 255, da + (255 - da) * cov / 255);
        }
    }
}

static void dash_draw_value(const char *s, int x_right, int base_y, int c) {
    int x = x_right - dash_str_w(s);
    for (const char *p = s; *p; p++) {
        char k[2] = { *p, 0 };
        const dash_glyph *gl = dash_find(L_val, L_nval, k);
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

static const char *dash_cons_unit(int u) { return u == 0 ? "L/100km" : u == 1 ? "km/L" : u == 2 ? "L/h" : NULL; }

static void dash_fmt_g(char *o, float g, int signed_long, float gx) {
    if (g < 0.005f && g > -0.005f) { snprintf(o, 16, "0.00"); return; }
    if (signed_long) {   /* total G: sign from the longitudinal part, none when < 0.03 g */
        if (gx > -0.03f && gx < 0.03f) snprintf(o, 16, "%.2f", g);
        else snprintf(o, 16, "%c%.2f", gx < 0 ? '-' : '+', g);
    } else snprintf(o, 16, "%+.2f", g);
}

/* value string + unit for one catalogue item */
static void dash_item_value(int it, const dash_vals_t *v, char *o, const char **unit) {
    char gb[8];
    *unit = g_items[it].unit;
    #define I(cond, ...) do { if (cond) snprintf(o, 16, "-"); else snprintf(o, 16, __VA_ARGS__); } while (0)
    switch (it) {
    case IT_POWER:   I(DASH_NAN(v->p), "%d", dash_round(v->p)); break;
    case IT_TORQUE:  I(v->tq == -999, "%d", v->tq); break;
    case IT_G:
        if (DASH_NAN(v->gx) || DASH_NAN(v->gy)) snprintf(o, 16, "-");
        else dash_fmt_g(o, sqrtf(v->gx * v->gx + v->gy * v->gy), 1, v->gx);
        break;
    case IT_GLONG:   if (DASH_NAN(v->gx)) snprintf(o, 16, "-"); else dash_fmt_g(o, v->gx, 0, 0); break;
    case IT_GLAT:    if (DASH_NAN(v->gy)) snprintf(o, 16, "-"); else dash_fmt_g(o, g_dash_conf.lat_flip ? -v->gy : v->gy, 0, 0); break;
    case IT_RPM:     I(v->rpm == -999, "%d", v->rpm); break;
    case IT_SPEED:   I(DASH_NAN(v->spd), "%d", dash_round(v->spd)); break;
    case IT_PEDAL:   I(v->ped == -999, "%d", v->ped); break;
    case IT_BRAKE:   I(DASH_NAN(v->brk), "%.1f", v->brk); break;
    case IT_STEER:   I(v->str == -999, "%d", v->str); break;
    case IT_GEAR:    snprintf(o, 16, "%s", v->gm == -999 ? "-" : dash_gear(v->gm, v->gc, gb)); break;
    case IT_GEARBOX: I(v->gbt == -999, "%d", v->gbt); break;
    case IT_OIL:     I(v->oil == -999, "%d", v->oil); break;
    case IT_COOLANT: I(v->cool == -999, "%d", v->cool); break;
    case IT_OUTSIDE: I(v->out == -999, "%d", v->out); break;
    case IT_CONS:    I(DASH_NAN(v->cv), "%.1f", v->cv); *unit = dash_cons_unit(v->cu); break;
    case IT_TRIP:    I(DASH_NAN(v->ca), "%.1f", v->ca); *unit = dash_cons_unit(v->au); break;
    case IT_REFUEL:  I(DASH_NAN(v->cr), "%.1f", v->cr); *unit = dash_cons_unit(v->cru); break;
    case IT_FUEL:
        if (v->tank == -999) snprintf(o, 16, "-");
        else if (g_dash_conf.tank_l > 0) snprintf(o, 16, "%d", (v->tank * g_dash_conf.tank_l + 50) / 100);
        else { snprintf(o, 16, "%d", v->tank); *unit = "%"; }
        break;
    case IT_RANGE:     I(v->rng == -999, "%d", v->rng); break;
    case IT_TRIPDIST:  I(DASH_NAN(v->tdist), "%d", dash_round(v->tdist)); break;
    case IT_TRIPSPEED: I(DASH_NAN(v->tspd), "%d", dash_round(v->tspd)); break;
    case IT_TRIPTIME:  I(v->ttime == -999 || v->ttime < 0, "%d:%02d", v->ttime / 60, v->ttime % 60); break;
    default: snprintf(o, 16, "-");
    }
    #undef I
}

static void dash_render(const dash_vals_t *v) {
    const int C_LAB = 165, C_VAL = 255, C_UNIT = 150;
    dash_fill_bg();
    for (int r = 0; r < g_nrows; r++) {
        int it = g_rows[r];
        char val[16]; const char *unit;
        dash_item_value(it, v, val, &unit);
        int base = DASH_PAD_Y + r * L_row_h + L_row_h / 2 + L_base_off;
        dash_draw_str(L_lab, L_nlab, g_items[it].label, L_lab_x, base - 1, C_LAB);
        dash_draw_value(val, L_val_r, base, C_VAL);
        if (unit) dash_draw_str(L_unit, L_nunit, unit, L_unit_x, base - 1, C_UNIT);
    }
}

/* Poll the state file. Returns 1 when the box content or visibility changed (re-render/re-post needed). */
static int dash_poll(uint64_t now_ms) {
    int want = 0, changed = 0;
    if (g_dash_conf.enabled) {
        /* /tmp on this unit has no rename/link: older Java builds left the data in .new -> read either.
         * Only complete lines (ending in '\n') are used, so a read during a rewrite is skipped. */
        FILE *f = fopen(DASH_STATE, "r");
        if (!f) f = fopen(DASH_STATE ".new", "r");
        if (f) {
            char l[512];
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
