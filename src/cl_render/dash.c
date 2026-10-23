/*
 * dash.c - cluster info box for cl_render (VW Touareg MH2p, v17).
 *
 * A translucent box on the left of the CarPlay map on the instrument cluster, one value per row.
 * Which values and in which order is chosen in dash.conf (items=...), up to DASH_MAX_ROWS rows; see the
 * catalogue g_items[] below (key, Chinese label, unit). The font size follows the number of rows so the box
 * always fits between the cluster's own navigation bar and its gear box: 1-6 rows normal, 7-8 compact,
 * 9-10 small. Board-computer values use the VW names (memory "自启动起 / 自上次加油起 / 累计" on a small top
 * line, value name below) and VW display names (当前消耗, 续驶里程, 车外温度, ...).
 *
 * Data comes from the head-unit Java bundle (VWClusterNav, class vwcn.Dash), which writes one line of
 * key=value pairs to /tmp/dash.state about 5 times a second while the CarPlay map is on the cluster.
 * The panel is drawn on the CPU into an RGBA pixmap and screen_blit'ed (source-over) onto every frame that
 * goes to the cluster window. Portable C, no QNX dependencies in this file (host-testable).
 * v18: 0–100 km/h (elapsed while running, then the result) and maxima since the head unit started
 * (power, torque, total G); computed by the Java side (vwcn.Accel) at the DSI callback rate.
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
    int compact;   /* font size: -1 auto (by row count), 0 normal, 1 compact, 2 small */
    int right;     /* v18: x of the box's right edge (0 = width from the content) */
} dash_conf_t;

static dash_conf_t g_dash_conf = { 1, 40, 172, 200, 90, 5000, 0, 348, -1, 0 };

/* ---- the item catalogue: config key, label (also accepted in items=), unit, sample for column width ---- */
enum { IT_POWER, IT_TORQUE, IT_G, IT_GLONG, IT_GLAT, IT_RPM, IT_SPEED, IT_PEDAL, IT_BRAKE, IT_STEER, IT_GEAR,
       IT_GEARBOX, IT_OIL, IT_COOLANT, IT_OUTSIDE, IT_CONS, IT_FUEL, IT_RANGE,
       IT_S_AVG, IT_S_DIST, IT_S_SPD, IT_S_TIME,      /* board computer memory 自启动起 (since start) */
       IT_R_AVG, IT_R_DIST, IT_R_SPD, IT_R_TIME,      /* 自上次加油起 (since refuelling) */
       IT_L_AVG, IT_L_DIST, IT_L_SPD, IT_L_TIME,      /* 累计 (long term) */
       IT_T100, IT_PMAX, IT_TQMAX, IT_GMAX,           /* v18: 0-100 km/h, maxima since the head unit started */
       IT_N };
typedef struct { const char *key; const char *label; const char *unit; const char *sample; } dash_item;
/* label "A|B" = two lines (A small on top). In items= the label is matched without '|' and spaces. */
static const dash_item g_items[IT_N] = {
    { "power",        "功率",                   "kW",      "-888" },
    { "torque",       "扭矩",                   "Nm",      "-888" },
    { "g",            "G 值",                   "g",       "-8.88" },
    { "glong",        "纵向 G",                 "g",       "-8.88" },
    { "glat",         "横向 G",                 "g",       "-8.88" },
    { "rpm",          "转速",                   "rpm",     "8888" },
    { "speed",        "车速",                   "km/h",    "888" },
    { "pedal",        "油门",                   "%",       "888" },
    { "brake",        "制动压力",               "bar",     "88.8" },
    { "steer",        "转向角",                 "°",       "-888" },
    { "gear",         "挡位",                   NULL,      "M8" },
    { "gearbox",      "变速箱油温",             "°C",      "188" },
    { "oil",          "机油温度",               "°C",      "188" },
    { "coolant",      "冷却液温度",             "°C",      "188" },
    { "outside",      "车外温度",               "°C",      "-88" },
    { "cons",         "当前消耗",               "L/100km", "88.8" },
    { "fuel",         "燃油存量",               "L",       "188" },
    { "range",        "续驶里程",               "km",      "1888" },
    { "trip",         "自启动起|平均消耗",      "L/100km", "88.8" },
    { "tripdist",     "自启动起|行驶里程",      "km",      "8888" },
    { "tripspeed",    "自启动起|平均车速",      "km/h",    "188" },
    { "triptime",     "自启动起|行驶时间",      "h",       "88:88" },
    { "refuel",       "自上次加油起|平均消耗",  "L/100km", "88.8" },
    { "refueldist",   "自上次加油起|行驶里程",  "km",      "8888" },
    { "refuelspeed",  "自上次加油起|平均车速",  "km/h",    "188" },
    { "refueltime",   "自上次加油起|行驶时间",  "h",       "88:88" },
    { "total",        "累计|平均消耗",          "L/100km", "88.8" },
    { "totaldist",    "累计|行驶里程",          "km",      "8888" },
    { "totalspeed",   "累计|平均车速",          "km/h",    "188" },
    { "totaltime",    "累计|行驶时间",          "h",       "88:88" },
    { "t100",         "0–100",                  "s",       "88.8" },
    { "pmax",         "最大功率",               "kW",      "888" },
    { "tqmax",        "最大扭矩",               "Nm",      "8888" },
    { "gmax",         "最大 G 值",              "g",       "8.88" },
};
static int g_rows[DASH_MAX_ROWS] = { IT_POWER, IT_TORQUE, IT_G, IT_RPM, IT_OIL, IT_GEARBOX };
static int g_nrows = 6;

/* parsed values; NAN / -999 = not available. Board computer memories: [0] 自启动起, [1] 自上次加油起, [2] 累计 */
typedef struct {
    long  n;
    float p, gx, gy, cv, spd, brk;
    float avg[3], dist[3], bspd[3];
    float t1, t1r, pmx, gmx;   /* v18: last 0-100 s, running 0-100 s, max kW, max g */
    int   tqx;
    int   tq, gm, gc, gbt, oil, cool, cu, tank, rpm, ped, str, out, rng;
    int   avgu[3], btime[3];
} dash_vals_t;

/* ---- layout (computed once in dash_layout) ---- */
static const dash_glyph *L_val, *L_lab, *L_unit;
static int L_nval, L_nlab, L_nunit, L_row_h, L_base_off, L_pad_y;
static int g_dash_w = 300, g_dash_h = 100, L_lab_x, L_val_r, L_unit_x;

static char      g_dash_line[1024];
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
            if (!strcmp(tok, "0-100")) { g_rows[n++] = IT_T100; tl = 0; }   /* ASCII hyphen for the 0–100 label */
            for (int i = 0; tl && i < IT_N; i++) {
                char lab[64]; int ll = 0;
                for (const char *p = g_items[i].label; *p && ll < 63; p++) if (*p != ' ' && *p != '|') lab[ll++] = *p;
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
    else if (sscanf(l, "right = %d", &v) == 1) g_dash_conf.right = v;
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

static void dash_fonts(int sz) {   /* 0 normal 40px, 1 medium 36px (v18), 2 compact 32px, 3 small 26px */
    if (sz == 1) {
        L_val = dash_m_val; L_nval = DASH_M_VAL_N; L_lab = dash_m_lab; L_nlab = DASH_M_LAB_N;
        L_unit = dash_m_unit; L_nunit = DASH_M_UNIT_N;
    } else if (sz == 0) {
        L_val = dash_n_val; L_nval = DASH_N_VAL_N; L_lab = dash_n_lab; L_nlab = DASH_N_LAB_N;
        L_unit = dash_n_unit; L_nunit = DASH_N_UNIT_N;
    } else if (sz == 2) {
        L_val = dash_c_val; L_nval = DASH_C_VAL_N; L_lab = dash_c_lab; L_nlab = DASH_C_LAB_N;
        L_unit = dash_c_unit; L_nunit = DASH_C_UNIT_N;
    } else {
        L_val = dash_s_val; L_nval = DASH_S_VAL_N; L_lab = dash_s_lab; L_nlab = DASH_S_LAB_N;
        L_unit = dash_s_unit; L_nunit = DASH_S_UNIT_N;
    }
}

/* column widths of the chosen rows in the current font */
static void dash_cols(int *lab_w, int *val_w, int *unit_w) {
    *lab_w = *val_w = *unit_w = 0;
    for (int r = 0; r < g_nrows; r++) {
        const dash_item *it = &g_items[g_rows[r]];
        const dash_glyph *gl = dash_find(L_lab, L_nlab, it->label);
        if (gl && gl->adv > *lab_w) *lab_w = gl->adv;
        int vw = dash_str_w(it->sample);
        if (vw > *val_w) *val_w = vw;
        const char *u = it->unit;
        if (u) { gl = dash_find(L_unit, L_nunit, u); if (gl && gl->adv > *unit_w) *unit_w = gl->adv; }
    }
}

static void dash_layout(void) {
    /* sizes: row height, top/bottom padding, baseline offset of the value digits from the row middle */
    /* size 1 (medium, v18) keeps the normal row height: it exists for narrow boxes (right=), not short ones */
    static const int ROW_H[4] = { 52, 52, 40, 32 }, PAD_Y[4] = { 12, 12, 12, 10 }, BASE[4] = { 13, 12, 11, 9 };
    /* dash.conf compact=: -1 auto, 0 normal, 1 compact, 2 small (as in v17); internal sizes 0..3 */
    int sz = g_dash_conf.compact, autosz = sz < 0 || sz > 2;
    if (!autosz) sz = sz == 0 ? 0 : sz + 1;
    else {   /* auto: the largest size whose box fits into max_h */
        for (sz = 0; sz < 3; sz++) if (PAD_Y[sz] * 2 + g_nrows * ROW_H[sz] <= g_dash_conf.max_h) break;
    }
    /* v18: right=<x> puts the box's right edge at x (e.g. on the corner of the cluster's gear panel): the box is
     * exactly right - x wide. Wider than needed -> the label/value gap grows; narrower -> tighter spacing, then
     * (font size auto only) a smaller font. */
    int target = g_dash_conf.right > g_dash_conf.x ? g_dash_conf.right - g_dash_conf.x : 0;
    int lab_w, val_w, unit_w, pad = DASH_PAD_X, gap = DASH_GAP, ugap = 8;
    for (;;) {
        dash_fonts(sz);
        dash_cols(&lab_w, &val_w, &unit_w);
        pad = DASH_PAD_X; gap = DASH_GAP; ugap = 8;
        int need = pad + lab_w + gap + val_w + ugap + unit_w + pad;
        if (!target || need <= target) break;
        pad = 12; gap = 8; ugap = 5;                                     /* tighter spacing */
        need = pad + lab_w + gap + val_w + ugap + unit_w + pad;
        if (need <= target || !autosz || sz == 3) break;
        sz++;                                                            /* smaller font */
    }
    L_row_h = ROW_H[sz]; L_pad_y = PAD_Y[sz]; L_base_off = BASE[sz];
    L_lab_x = pad;
    if (target) {   /* units flush right, values right-aligned before them */
        g_dash_w = target;
        L_unit_x = target - pad - unit_w;
        L_val_r = L_unit_x - ugap;
    } else {
        L_val_r = L_lab_x + lab_w + gap + val_w;
        L_unit_x = L_val_r + ugap;
        g_dash_w = L_unit_x + unit_w + pad;
    }
    g_dash_h = L_pad_y * 2 + g_nrows * L_row_h;
    if (g_dash_w > DASH_MAX_W) g_dash_w = DASH_MAX_W;
    if (g_dash_h > DASH_MAX_H) g_dash_h = DASH_MAX_H;
}

static void dash_parse(const char *line, dash_vals_t *o) {
    float nanv = 0.0f / 0.0f;
    o->n = -1;
    o->p = o->gx = o->gy = o->cv = o->spd = o->brk = nanv;
    o->tq = o->gm = o->gc = o->gbt = o->oil = o->cool = o->cu = o->tank = o->rpm = o->ped = o->str = o->out = o->rng = -999;
    for (int m = 0; m < 3; m++) { o->avg[m] = o->dist[m] = o->bspd[m] = nanv; o->avgu[m] = o->btime[m] = -999; }
    o->t1 = o->t1r = o->pmx = o->gmx = nanv; o->tqx = -999;
    /* board computer keys per memory: average + unit, distance, speed, time (minutes) */
    static const char *BK[3][5] = { { "ca", "au", "td", "ts", "tt" }, { "cr", "cru", "rd", "rs", "rt" },
                                    { "cl", "clu", "ld", "ls", "lt" } };
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
        else if (!strcmp(key, "spd"))  o->spd = (float)d;
        else if (!strcmp(key, "brk"))  o->brk = (float)d;
        else if (!strcmp(key, "tq"))   o->tq = di;
        else if (!strcmp(key, "gm"))   o->gm = di;
        else if (!strcmp(key, "gc"))   o->gc = di;
        else if (!strcmp(key, "gbt"))  o->gbt = di;
        else if (!strcmp(key, "oil"))  o->oil = di;
        else if (!strcmp(key, "cool")) o->cool = di;
        else if (!strcmp(key, "cu"))   o->cu = di;
        else if (!strcmp(key, "tank")) o->tank = di;
        else if (!strcmp(key, "rpm"))  o->rpm = di;
        else if (!strcmp(key, "ped"))  o->ped = di;
        else if (!strcmp(key, "str"))  o->str = di;
        else if (!strcmp(key, "out"))  o->out = di;
        else if (!strcmp(key, "rng"))  o->rng = di;
        else if (!strcmp(key, "t1"))   o->t1 = (float)d;
        else if (!strcmp(key, "t1r"))  o->t1r = (float)d;
        else if (!strcmp(key, "pmx"))  o->pmx = (float)d;
        else if (!strcmp(key, "tqx"))  o->tqx = di;
        else if (!strcmp(key, "gmx"))  o->gmx = (float)d;
        else {
            for (int m = 0; m < 3; m++) {
                if      (!strcmp(key, BK[m][0])) o->avg[m] = (float)d;
                else if (!strcmp(key, BK[m][1])) o->avgu[m] = di;
                else if (!strcmp(key, BK[m][2])) o->dist[m] = (float)d;
                else if (!strcmp(key, BK[m][3])) o->bspd[m] = (float)d;
                else if (!strcmp(key, BK[m][4])) o->btime[m] = di;
            }
        }
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
    case IT_FUEL:
        if (v->tank == -999) snprintf(o, 16, "-");
        else if (g_dash_conf.tank_l > 0) snprintf(o, 16, "%d", (v->tank * g_dash_conf.tank_l + 50) / 100);
        else { snprintf(o, 16, "%d", v->tank); *unit = "%"; }
        break;
    case IT_RANGE:   I(v->rng == -999, "%d", v->rng); break;
    case IT_T100:    /* while a run is in progress: elapsed time, otherwise the last result */
        if (!DASH_NAN(v->t1r)) snprintf(o, 16, "%.1f", v->t1r);
        else I(DASH_NAN(v->t1), "%.1f", v->t1);
        break;
    case IT_PMAX:    I(DASH_NAN(v->pmx), "%d", dash_round(v->pmx)); break;
    case IT_TQMAX:   I(v->tqx == -999, "%d", v->tqx); break;
    case IT_GMAX:    I(DASH_NAN(v->gmx), "%.2f", v->gmx); break;
    default:
        if (it >= IT_S_AVG && it <= IT_L_TIME) {   /* board computer: memory m, value k */
            int m = (it - IT_S_AVG) / 4, k = (it - IT_S_AVG) % 4;
            if (k == 0) { I(DASH_NAN(v->avg[m]) || v->avg[m] < 0, "%.1f", v->avg[m]); *unit = dash_cons_unit(v->avgu[m]); }
            else if (k == 1) I(DASH_NAN(v->dist[m]) || v->dist[m] < 0, "%d", dash_round(v->dist[m]));
            else if (k == 2) I(DASH_NAN(v->bspd[m]) || v->bspd[m] < 0, "%d", dash_round(v->bspd[m]));
            else I(v->btime[m] == -999 || v->btime[m] < 0, "%d:%02d", v->btime[m] / 60, v->btime[m] % 60);
        } else snprintf(o, 16, "-");
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
        int top = L_pad_y + r * L_row_h, base = top + L_row_h / 2 + L_base_off;
        const dash_glyph *lg = dash_find(L_lab, L_nlab, g_items[it].label);
        if (lg && lg->base < 0) dash_blit_mask(lg, L_lab_x, top + (L_row_h - lg->h) / 2, C_LAB);   /* two-line label */
        else if (lg) dash_blit_mask(lg, L_lab_x, base - 1 - lg->base, C_LAB);
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
            char l[1024];
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
