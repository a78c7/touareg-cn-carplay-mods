/*
 * dash.c - cluster info box for cl_render (VW Touareg MH2p, v21 inline sport-gauge & large-font edition).
 *
 * Translucent dark cards on the left/right of the CarPlay map on the instrument cluster, aligned flush with
 * the cluster's lower gear/speed trapezoid corners (left x=5..300, right x=1620..1915, both 295px wide).
 * Features:
 *   - Bold pre-rendered Noto Sans CJK SC glyphs (up to 46px value digits) across all 43 telemetry items;
 *   - Inline sport gauges beneath dynamic rows (bars=1 default, bars=0 to disable):
 *       1) Segmented slanted sport blocks (with redline tail blocks) for RPM, power, torque, pedal, brake;
 *       2) Center-zero bipolar slider bar (with center white tick) for G-force, steering angle, pitch & roll;
 *       3) Thermal / range bar (with cold/optimal/hot color transitions & target ticks) for oil, gearbox,
 *          coolant, tire pressures & fuel level;
 *       4) Crisp subtle row dividers across all rows.
 *   - Independent font sizing per box (left and right boxes never force each other into a smaller font).
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under CC BY-NC-SA 4.0 (see LICENSES/CC-BY-NC-SA-4.0.md and LICENSES/THIRD_PARTY_NOTICES.md)
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dash_font.h"

#define DASH_NAN(x) ((x) != (x))
static int dash_round(double d) { return d < 0 ? -(int)(-d + 0.5) : (int)(d + 0.5); }

#define DASH_MAX_ROWS 10
#define DASH_MAX_W    600
#define DASH_MAX_H    640
#define DASH_PAD_X    13
#define DASH_GAP      10
#define DASH_RADIUS   12
#define DASH_STATE    "/tmp/dash.state"

typedef struct {
    int enabled;   /* info=0 / show=0 hides the box */
    int x, y;      /* top-left in cluster window pixels */
    int alpha;     /* background opacity 0..255 */
    int tank_l;    /* tank capacity in litres for the fuel row; 0 = show percent */
    int stale_ms;  /* hide the box when the Java side stopped writing for this long */
    int lat_flip;  /* 1 = invert the lateral g sign */
    int max_h;     /* space available below y before the cluster's gear box */
    int compact;   /* font size: -1 auto (by row count), 0 normal, 1 compact, 2 small */
    int right;     /* x of the box's right edge (0 = width from the content) */
    int bars;      /* 1 = inline sport gauge bars beneath dynamic rows (default), 0 = text only */
    int smooth;    /* v22.0: 1 = exponential damping interpolation on gauge bars (default), 0 = raw step */
    double trip_base; /* odometer reading (km) where the cluster's Trip was last reset; Trip = odometer - this */
} dash_conf_t;

static dash_conf_t g_dash_conf = { 1, 5, 164, 200, 90, 5000, 0, 352, -1, 300, 1, 1, 0 };

/* ---- the item catalogue: config key, label (also accepted in items=), unit, sample for column width ---- */
enum { IT_POWER, IT_TORQUE, IT_G, IT_GLONG, IT_GLAT, IT_RPM, IT_SPEED, IT_PEDAL, IT_BRAKE, IT_STEER, IT_GEAR,
       IT_GEARBOX, IT_OIL, IT_COOLANT, IT_OUTSIDE, IT_CONS, IT_FUEL, IT_RANGE,
       IT_S_AVG, IT_S_DIST, IT_S_SPD, IT_S_TIME,      /* board computer memory 自启动起 (since start) */
       IT_R_AVG, IT_R_DIST, IT_R_SPD, IT_R_TIME,      /* 自上次加油起 (since refuelling) */
       IT_L_AVG, IT_L_DIST, IT_L_SPD, IT_L_TIME,      /* 累计 (long term) */
       IT_T100, IT_PMAX, IT_TQMAX, IT_GMAX,           /* v18: 0-100 km/h, maxima since the head unit started */
       IT_TRIPKM,                                     /* v19.2: Trip = odometer - trip_base (dash.conf) */
       IT_TIRES, IT_TPFL, IT_TPFR, IT_TPRL, IT_TPRR,  /* v21: 4-wheel & individual tire pressures (bar) */
       IT_PITCH, IT_ROLL, IT_ALT,                     /* v21: offroad pitch/roll angles, GNSS altitude */
       IT_N };
typedef struct { const char *key; const char *label; const char *unit; const char *sample; } dash_item;

static const dash_item g_items[IT_N] = {
    { "power",        "功率",           "kW",      "888" },
    { "torque",       "扭矩",           "Nm",      "888" },
    { "g",            "G 值",           "g",       "+8.88" },
    { "glong",        "纵向 G",         "g",       "+8.88" },
    { "glat",         "横向 G",         "g",       "+8.88" },
    { "rpm",          "转速",           "rpm",     "8888" },
    { "speed",        "车速",           "km/h",    "888" },
    { "pedal",        "油门",           "%",       "888" },
    { "brake",        "制动压力",       "bar",     "88.8" },
    { "steer",        "转向角",         "°",       "-888" },
    { "gear",         "挡位",           NULL,      "M8" },
    { "gearbox",      "变速箱油温",     "°C",      "188" },
    { "oil",          "机油温度",       "°C",      "188" },
    { "coolant",      "冷却液温度",     "°C",      "188" },
    { "outside",      "车外温度",       "°C",      "-88" },
    { "cons",         "当前消耗",       "L/100km", "88.8" },
    { "fuel",         "燃油存量",       "L",       "188" },
    { "range",        "续驶里程",       "km",      "1888" },
    { "trip",         "本次油耗",       "L/100km", "88.8" },
    { "tripdist",     "本次里程",       "km",      "8888" },
    { "tripspeed",    "本次均速",       "km/h",    "188" },
    { "triptime",     "本次时长",       "h",       "88:88" },
    { "refuel",       "加油后油耗",     "L/100km", "88.8" },
    { "refueldist",   "加油后里程",     "km",      "8888" },
    { "refuelspeed",  "加油后均速",     "km/h",    "188" },
    { "refueltime",   "加油后时长",     "h",       "88:88" },
    { "total",        "累计油耗",       "L/100km", "88.8" },
    { "totaldist",    "累计里程",       "km",      "8888" },
    { "totalspeed",   "累计均速",       "km/h",    "188" },
    { "totaltime",    "累计时长",       "h",       "88:88" },
    { "t100",         "0–100",          "s",       "88.8" },
    { "pmax",         "最大功率",       "kW",      "888" },
    { "tqmax",        "最大扭矩",       "Nm",      "8888" },
    { "gmax",         "最大 G 值",      "g",       "8.88" },
    { "tripkm",       "Trip",           "km",      "8888.8" },
    { "tires",        "四轮胎压",       "bar",     "8.8·8.8" },
    { "tpfl",         "左前胎压",       "bar",     "8.8" },
    { "tpfr",         "右前胎压",       "bar",     "8.8" },
    { "tprl",         "左后胎压",       "bar",     "8.8" },
    { "tprr",         "右后胎压",       "bar",     "8.8" },
    { "pitch",        "俯仰角",         "°",       "-88.8" },
    { "roll",         "侧倾角",         "°",       "-88.8" },
    { "alt",          "海拔",           "m",       "8888" },
};
static int g_rows[DASH_MAX_ROWS] = { IT_POWER, IT_TORQUE, IT_G, IT_RPM, IT_OIL, IT_GEARBOX };
static int g_nrows = 6;

/* parsed values; NAN / -999 = not available. Board computer memories: [0] 自启动起, [1] 自上次加油起, [2] 累计 */
typedef struct {
    long  n;
    float p, gx, gy, cv, spd, brk;
    float avg[3], dist[3], bspd[3];
    float t1, t1r, pmx, gmx;
    double odo;
    int   tqx;
    int   tq, gm, gc, gbt, oil, cool, cu, tank, rpm, ped, str, out, rng, acc;
    int   avgu[3], btime[3];
    int   tpst, alt;
    float tpfl, tpfr, tprl, tprr, pitch, roll;
} dash_vals_t;

/* ---- layout (computed once in dash_layout) ---- */
static const dash_glyph *L_val, *L_lab, *L_unit;
static int L_nval, L_nlab, L_nunit, L_row_h, L_base_off, L_pad_y, L_fidx;
static int g_dash_w = 295, g_dash_h = 100, L_lab_x, L_val_r, L_unit_x;
static int L_sz;

static char      g_dash_line[2048];
static int       g_dash_have;
static uint64_t  g_dash_last_new_ms;
static long      g_dash_last_n = -1;
static uint32_t  g_dash_pxbuf[2][DASH_MAX_W * DASH_MAX_H];
static uint32_t *g_dash_px = g_dash_pxbuf[0];

typedef struct {
    dash_conf_t conf; int rows[DASH_MAX_ROWS], nrows;
    const dash_glyph *val, *lab, *unit; int nval, nlab, nunit, row_h, base_off, pad_y, fidx, w, h, lab_x, val_r, unit_x;
} dash_box_t;
static dash_box_t g_box[2];
static int g_box_cur, g_box_init;

static void dash_save(dash_box_t *b) {
    b->conf = g_dash_conf; memcpy(b->rows, g_rows, sizeof g_rows); b->nrows = g_nrows;
    b->val = L_val; b->lab = L_lab; b->unit = L_unit; b->nval = L_nval; b->nlab = L_nlab; b->nunit = L_nunit;
    b->row_h = L_row_h; b->base_off = L_base_off; b->pad_y = L_pad_y; b->fidx = L_fidx; b->w = g_dash_w; b->h = g_dash_h;
    b->lab_x = L_lab_x; b->val_r = L_val_r; b->unit_x = L_unit_x;
}
static void dash_load(const dash_box_t *b) {
    g_dash_conf = b->conf; memcpy(g_rows, b->rows, sizeof g_rows); g_nrows = b->nrows;
    L_val = b->val; L_lab = b->lab; L_unit = b->unit; L_nval = b->nval; L_nlab = b->nlab; L_nunit = b->nunit;
    L_row_h = b->row_h; L_base_off = b->base_off; L_pad_y = b->pad_y; L_fidx = b->fidx; g_dash_w = b->w; g_dash_h = b->h;
    L_lab_x = b->lab_x; L_val_r = b->val_r; L_unit_x = b->unit_x;
}
static void dash_use(int i) {
    if (!g_box_init) {
        g_box_init = 1;
        dash_save(&g_box[0]); g_box[1] = g_box[0];
        g_box[1].conf.enabled = 0; g_box[1].conf.x = 1620; g_box[1].conf.right = 1915; g_box[1].nrows = 0;
    }
    if (i == g_box_cur) return;
    dash_save(&g_box[g_box_cur]);
    dash_load(&g_box[i]);
    g_dash_px = g_dash_pxbuf[i];
    g_box_cur = i;
}

static void dash_parse_items(const char *v) {
    int n = 0;
    char buf[512];
    strncpy(buf, v, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    char *s = buf;
    while (*s && n < DASH_MAX_ROWS) {
        char *e = s;
        while (*e && *e != ',' && *e != '\n' && *e != '\r' && *e != '#') e++;
        char term = *e; *e = 0;
        char tok[64]; int tl = 0;
        for (char *p = s; *p && tl < 63; p++) if (*p != ' ' && *p != '\t') tok[tl++] = *p;
        tok[tl] = 0;
        if (tl) {
            if (!strcmp(tok, "0-100")) { g_rows[n++] = IT_T100; tl = 0; }
            for (int i = 0; tl && i < IT_N; i++) {
                char lab[64]; int ll = 0;
                for (const char *p = g_items[i].label; *p && ll < 63; p++) if (*p != ' ' && *p != '|') lab[ll++] = *p;
                lab[ll] = 0;
                if (!strcmp(tok, g_items[i].key) || !strcmp(tok, lab)) { g_rows[n++] = i; break; }
                if (i >= IT_S_AVG && i <= IT_L_TIME) {
                    static const char *mem[] = { "自启动起", "自上次加油起", "累计" }, *val[] = { "平均消耗", "行驶里程", "平均车速", "行驶时间" };
                    char old[64]; snprintf(old, sizeof old, "%s%s", mem[(i - IT_S_AVG) / 4], val[(i - IT_S_AVG) % 4]);
                    if (!strcmp(tok, old)) { g_rows[n++] = i; break; }
                }
            }
        }
        if (!term || term == '\n' || term == '\r' || term == '#') break;
        s = e + 1;
    }
    if (n > 0) g_nrows = n;
}

static void dash_read_conf_line1(const char *l) {
    int v;
    while (*l == ' ' || *l == '\t') l++;
    if (*l == '#') return;
    if (!strncmp(l, "items", 5)) { const char *eq = strchr(l, '='); if (eq) dash_parse_items(eq + 1); return; }
    if (sscanf(l, "info = %d", &v) == 1 || sscanf(l, "show = %d", &v) == 1) g_dash_conf.enabled = v;
    else if (sscanf(l, "info_x = %d", &v) == 1 || sscanf(l, "x = %d", &v) == 1) g_dash_conf.x = v;
    else if (sscanf(l, "info_y = %d", &v) == 1 || sscanf(l, "y = %d", &v) == 1) g_dash_conf.y = v;
    else if (sscanf(l, "info_alpha = %d", &v) == 1 || sscanf(l, "alpha = %d", &v) == 1) g_dash_conf.alpha = v < 0 ? 0 : v > 255 ? 255 : v;
    else if (sscanf(l, "tank_l = %d", &v) == 1) g_dash_conf.tank_l = v;
    else if (sscanf(l, "trip_base = %lf", &g_dash_conf.trip_base) == 1) ;
    else if (sscanf(l, "lat_flip = %d", &v) == 1) g_dash_conf.lat_flip = v;
    else if (sscanf(l, "max_h = %d", &v) == 1) g_dash_conf.max_h = v;
    else if (sscanf(l, "compact = %d", &v) == 1) g_dash_conf.compact = v;
    else if (sscanf(l, "right = %d", &v) == 1) g_dash_conf.right = v;
    else if (sscanf(l, "bars = %d", &v) == 1) g_dash_conf.bars = v ? 1 : 0;
    else if (sscanf(l, "smooth = %d", &v) == 1) g_dash_conf.smooth = v ? 1 : 0;
}

static void dash_read_conf_line(const char *l) {
    while (*l == ' ' || *l == '\t') l++;
    if (!strncmp(l, "r_", 2)) {
        int prev = g_box_cur; dash_use(1); dash_read_conf_line1(l + 2); dash_use(prev);
        return;
    }
    int v, cur = g_box_cur;
    dash_read_conf_line1(l);
    double dv;
    if (sscanf(l, "tank_l = %d", &v) == 1 || sscanf(l, "lat_flip = %d", &v) == 1 || sscanf(l, "trip_base = %lf", &dv) == 1 ||
        sscanf(l, "alpha = %d", &v) == 1 || sscanf(l, "info_alpha = %d", &v) == 1 ||
        sscanf(l, "bars = %d", &v) == 1 || sscanf(l, "smooth = %d", &v) == 1) {
        dash_use(1 - cur); dash_read_conf_line1(l); dash_use(cur);
    }
}

static const dash_glyph *dash_find(const dash_glyph *set, int n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(set[i].key, key)) return &set[i];
    return NULL;
}

static const char *dash_next_char(const char *p, char out[5]) {
    unsigned char c = (unsigned char)*p;
    int len = 1;
    if ((c & 0xE0) == 0xC0 && p[1]) len = 2;
    else if ((c & 0xF0) == 0xE0 && p[1] && p[2]) len = 3;
    else if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) len = 4;
    for (int i = 0; i < len; i++) out[i] = p[i];
    out[len] = 0;
    return p + len;
}

static int dash_str_w(const char *s) {
    int w = 0;
    char k[5];
    for (const char *p = s; *p; ) {
        p = dash_next_char(p, k);
        const dash_glyph *gl = dash_find(L_val, L_nval, k);
        if (gl) w += gl->adv;
    }
    return w;
}

static void dash_fonts(int sz) {   /* 0 xlarge 46px, 1 large 41px, 2 medium 35px, 3 compact 29px */
    L_fidx = sz < 0 ? 0 : (sz > 3 ? 3 : sz);
    if (L_fidx == 1) {
        L_val = dash_m_val; L_nval = DASH_M_VAL_N; L_lab = dash_m_lab; L_nlab = DASH_M_LAB_N;
        L_unit = dash_m_unit; L_nunit = DASH_M_UNIT_N;
    } else if (L_fidx == 0) {
        L_val = dash_n_val; L_nval = DASH_N_VAL_N; L_lab = dash_n_lab; L_nlab = DASH_N_LAB_N;
        L_unit = dash_n_unit; L_nunit = DASH_N_UNIT_N;
    } else if (L_fidx == 2) {
        L_val = dash_c_val; L_nval = DASH_C_VAL_N; L_lab = dash_c_lab; L_nlab = DASH_C_LAB_N;
        L_unit = dash_c_unit; L_nunit = DASH_C_UNIT_N;
    } else {
        L_val = dash_s_val; L_nval = DASH_S_VAL_N; L_lab = dash_s_lab; L_nlab = DASH_S_LAB_N;
        L_unit = dash_s_unit; L_nunit = DASH_S_UNIT_N;
    }
}

static int dash_max_row_need(int gap, int ugap) {
    int max_need = 0;
    for (int r = 0; r < g_nrows; r++) {
        const dash_item *it = &g_items[g_rows[r]];
        const dash_glyph *gl = dash_find(L_lab, L_nlab, it->label);
        int lw = gl ? gl->adv : 0;
        int vw = dash_str_w(it->sample);
        int uw = 0;
        if (it->unit) {
            gl = dash_find(L_unit, L_nunit, it->unit);
            uw = gl ? gl->adv : 0;
        }
        int rneed = lw + gap + vw + (uw > 0 ? ugap + uw : 0);
        if (rneed > max_need) max_need = rneed;
    }
    return max_need;
}

static int dash_item_has_bar(int it) {
    if (!g_dash_conf.bars) return 0;
    switch (it) {
    case IT_RPM: case IT_POWER: case IT_TORQUE: case IT_PEDAL: case IT_BRAKE:
    case IT_G: case IT_GLONG: case IT_GLAT: case IT_STEER: case IT_PITCH: case IT_ROLL:
    case IT_OIL: case IT_GEARBOX: case IT_COOLANT: case IT_FUEL:
    case IT_TIRES: case IT_TPFL: case IT_TPFR: case IT_TPRL: case IT_TPRR:
        return 1;
    default:
        return 0;
    }
}

static void dash_layout(void) {
    /* Row heights designed so 5 rows = 344px (66px/row, fits 46px digits + inline sport bar cleanly inside 348px!),
     * 6 rows = 348px (55px/row, fits 41px digits + inline bar), 7-8 rows = 42px, 9-10 rows = 33px. */
    static const int ROW_H[4] = { 66, 55, 42, 33 }, PAD_Y[4] = { 7, 9, 8, 8 }, BASE[4] = { 16, 14, 12, 10 };
    int sz = g_dash_conf.compact, autosz = sz < 0 || sz > 2;
    if (!autosz) sz = sz == 0 ? 0 : sz + 1;
    else {
        for (sz = 0; sz < 3; sz++) if (PAD_Y[sz] * 2 + g_nrows * ROW_H[sz] <= g_dash_conf.max_h) break;
    }
    int szh = sz;
    int target = g_dash_conf.right > g_dash_conf.x ? g_dash_conf.right - g_dash_conf.x : 0;
    int rneed = 0, pad = DASH_PAD_X, gap = DASH_GAP, ugap = 5;
    for (;;) {
        dash_fonts(sz);
        pad = DASH_PAD_X; gap = DASH_GAP; ugap = 5;
        rneed = dash_max_row_need(gap, ugap);
        int need = pad + rneed + pad;
        if (!target || need <= target) break;
        pad = 10; gap = 6; ugap = 4;
        rneed = dash_max_row_need(gap, ugap);
        need = pad + rneed + pad;
        if (need <= target || !autosz || sz == 3) break;
        sz++;
    }
    L_row_h = ROW_H[szh]; L_pad_y = PAD_Y[szh]; L_base_off = BASE[sz]; L_sz = szh;
    /* When <= 4 rows, stretch row height up to 76px so bars and huge digits breathe naturally */
    if (szh == 0 && g_nrows > 0 && g_nrows <= 4) {
        int rh = (g_dash_conf.max_h - 16) / g_nrows;
        if (rh > 76) rh = 76;
        if (rh > L_row_h) L_row_h = rh;
    }
    L_lab_x = pad;
    L_unit_x = ugap; /* stores ugap between value and unit */
    g_dash_w = target ? target : (pad + rneed + pad);
    L_val_r = g_dash_w - pad;
    g_dash_h = L_pad_y * 2 + g_nrows * L_row_h;
    if (g_dash_w > DASH_MAX_W) g_dash_w = DASH_MAX_W;
    if (g_dash_h > DASH_MAX_H) g_dash_h = DASH_MAX_H;
}

static void dash_parse(const char *line, dash_vals_t *o) {
    float nanv = 0.0f / 0.0f;
    o->n = -1;
    o->p = o->gx = o->gy = o->cv = o->spd = o->brk = nanv;
    o->tq = o->gm = o->gc = o->gbt = o->oil = o->cool = o->cu = o->tank = o->rpm = o->ped = o->str = o->out = o->rng = o->acc = -999;
    for (int m = 0; m < 3; m++) { o->avg[m] = o->dist[m] = o->bspd[m] = nanv; o->avgu[m] = o->btime[m] = -999; }
    o->t1 = o->t1r = o->pmx = o->gmx = nanv; o->tqx = -999; o->odo = nanv;
    o->alt = -999; o->tpst = 0;
    o->tpfl = o->tpfr = o->tprl = o->tprr = o->pitch = o->roll = nanv;
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
        if      (!strcmp(key, "n"))     o->n = (long)d;
        else if (!strcmp(key, "p"))     o->p = (float)d;
        else if (!strcmp(key, "gx"))    o->gx = (float)d;
        else if (!strcmp(key, "gy"))    o->gy = (float)d;
        else if (!strcmp(key, "cv"))    o->cv = (float)d;
        else if (!strcmp(key, "spd"))   o->spd = (float)d;
        else if (!strcmp(key, "brk"))   o->brk = (float)d;
        else if (!strcmp(key, "tq"))    o->tq = di;
        else if (!strcmp(key, "gm"))    o->gm = di;
        else if (!strcmp(key, "gc"))    o->gc = di;
        else if (!strcmp(key, "gbt"))   o->gbt = di;
        else if (!strcmp(key, "oil"))   o->oil = di;
        else if (!strcmp(key, "cool"))  o->cool = di;
        else if (!strcmp(key, "cu"))    o->cu = di;
        else if (!strcmp(key, "tank"))  o->tank = di;
        else if (!strcmp(key, "rpm"))   o->rpm = di;
        else if (!strcmp(key, "ped"))   o->ped = di;
        else if (!strcmp(key, "str"))   o->str = di;
        else if (!strcmp(key, "out"))   o->out = di;
        else if (!strcmp(key, "rng"))   o->rng = di;
        else if (!strcmp(key, "acc"))   o->acc = di;
        else if (!strcmp(key, "t1"))    o->t1 = (float)d;
        else if (!strcmp(key, "t1r"))   o->t1r = (float)d;
        else if (!strcmp(key, "pmx"))   o->pmx = (float)d;
        else if (!strcmp(key, "tqx"))   o->tqx = di;
        else if (!strcmp(key, "gmx"))   o->gmx = (float)d;
        else if (!strcmp(key, "odo"))   o->odo = d;
        else if (!strcmp(key, "tpfl"))  o->tpfl = (float)d;
        else if (!strcmp(key, "tpfr"))  o->tpfr = (float)d;
        else if (!strcmp(key, "tprl"))  o->tprl = (float)d;
        else if (!strcmp(key, "tprr"))  o->tprr = (float)d;
        else if (!strcmp(key, "tpst"))  o->tpst = di;
        else if (!strcmp(key, "pitch")) o->pitch = (float)d;
        else if (!strcmp(key, "roll"))  o->roll = (float)d;
        else if (!strcmp(key, "alt"))   o->alt = di;
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

/* ---- drawing primitives ---- */
static inline uint32_t dash_px(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void dash_blend_rgb(int px, int py, int r, int g, int b, int cov) {
    if (cov <= 0 || px < 0 || px >= g_dash_w || py < 0 || py >= g_dash_h) return;
    if (cov > 255) cov = 255;
    uint32_t d = g_dash_px[py * g_dash_w + px];
    int da = (int)(d >> 24), dr = (int)((d >> 16) & 255), dg = (int)((d >> 8) & 255), db = (int)(d & 255);
    int inv = 255 - cov;
    g_dash_px[py * g_dash_w + px] = dash_px((dr * inv + r * cov + 127) / 255,
                                            (dg * inv + g * cov + 127) / 255,
                                            (db * inv + b * cov + 127) / 255,
                                            da + ((255 - da) * cov + 127) / 255);
}

static void dash_fill_rect(int x0, int y0, int w, int h, int r, int g, int b, int a) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) dash_blend_rgb(x0 + dx, y0 + dy, r, g, b, a);
    }
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
            g_dash_px[y * W + x] = dash_px(14, 18, 24, (int)(g_dash_conf.alpha * cov + 0.5f));
        }
    }
    /* Crisp subtle top & inner edge border like VW Digital Cockpit */
    for (int x = R; x < W - R; x++) dash_blend_rgb(x, 0, 180, 210, 240, 38);
}

/* Smooth 4-stop dynamic heat color for segmented sport bars:
 *   <= 22% : base cool cyan/teal (idle / light cruise)
 *      34% : bright yellow  (~2200 rpm / ~153 Nm / ~85 kW)
 *      56% : vivid orange   (~3640 rpm / ~252 Nm / ~140 kW)
 *   >= 80% : crimson red    (~5200 rpm / ~360 Nm / ~200 kW) */
static void dash_seg_heat_color(float frac, int ar, int ag, int ab, int *or_, int *og, int *ob) {
    if (frac <= 0.22f) {
        *or_ = ar; *og = ag; *ob = ab;
        return;
    }
    int r0, g0, b0, r1, g1, b1;
    float t;
    if (frac < 0.34f) {
        t = (frac - 0.22f) / 0.12f;
        r0 = ar;  g0 = ag;  b0 = ab;
        r1 = 250; g1 = 218; b1 = 42;
    } else if (frac < 0.56f) {
        t = (frac - 0.34f) / 0.22f;
        r0 = 250; g0 = 218; b0 = 42;
        r1 = 255; g1 = 134; b1 = 28;
    } else if (frac < 0.80f) {
        t = (frac - 0.56f) / 0.24f;
        r0 = 255; g0 = 134; b0 = 28;
        r1 = 255; g1 = 48;  b1 = 66;
    } else {
        *or_ = 255; *og = 48; *ob = 66;
        return;
    }
    *or_ = (int)((float)r0 + (float)(r1 - r0) * t + 0.5f);
    *og  = (int)((float)g0 + (float)(g1 - g0) * t + 0.5f);
    *ob  = (int)((float)b0 + (float)(b1 - b0) * t + 0.5f);
}

/* 1) Anti-aliased segmented slanted sport blocks (whole-bar smooth 4-stop heat color: cyan -> yellow -> orange -> red) */
static void dash_draw_seg_bar(int x0, int y0, int w, int h, float frac, int nseg, int red_segs, int ar, int ag, int ab) {
    if (w < 40 || h < 4) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float slant = (float)h * 0.42f;
    float gap = 3.0f;
    float seg_w = ((float)w - slant - gap * (float)(nseg - 1)) / (float)nseg;
    if (seg_w < 3.0f) seg_w = 3.0f;
    float lit_pos = frac * (float)nseg;

    /* All lit segments share a single smooth dynamic heat color based on current load */
    int lr, lg, lb;
    dash_seg_heat_color(frac, ar, ag, ab, &lr, &lg, &lb);

    for (int s = 0; s < nseg; s++) {
        int is_red = (s >= nseg - red_segs);
        float fill = lit_pos - (float)s;
        if (fill < 0.0f) fill = 0.0f;
        if (fill > 1.0f) fill = 1.0f;

        /* Unlit background block color (redline tail stays visible in dim crimson even at idle) */
        int br = is_red ? 138 : 42;
        int bg = is_red ? 32  : 56;
        int bb = is_red ? 48  : 70;
        int ba = is_red ? 205 : 155;

        int cr = (int)(br + (lr - br) * fill + 0.5f);
        int cg = (int)(bg + (lg - bg) * fill + 0.5f);
        int cb = (int)(bb + (lb - bb) * fill + 0.5f);
        int ca = (int)(ba + (250 - ba) * fill + 0.5f);

        float sx_base = (float)x0 + (float)s * (seg_w + gap);
        for (int dy = 0; dy < h; dy++) {
            float shift = ((float)(h - 1 - dy) / (float)(h > 1 ? h - 1 : 1)) * slant;
            float L = sx_base + shift;
            float R = L + seg_w;
            int ix0 = (int)L - 1, ix1 = (int)R + 2;
            for (int px = ix0; px <= ix1; px++) {
                float cL = (float)px + 0.5f - L;
                float cR = R - ((float)px - 0.5f);
                float cov = cL < cR ? cL : cR;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
                dash_blend_rgb(px, y0 + dy, cr, cg, cb, (int)(ca * cov + 0.5f));
            }
        }
    }
}

/* 2) Center-zero bipolar slider bar (for G-force, steering angle, pitch & roll) */
static void dash_draw_bipolar_bar(int x0, int y0, int w, int h, float norm_signed, int has_val) {
    if (w < 40 || h < 4) return;
    int track_h = h >= 8 ? 6 : 5;
    int ty = y0 + (h - track_h) / 2;
    dash_fill_rect(x0, ty, w, track_h, 42, 56, 70, 165);

    int mid_x = x0 + w / 2;
    if (has_val) {
        if (norm_signed < -1.0f) norm_signed = -1.0f;
        if (norm_signed > 1.0f) norm_signed = 1.0f;
        int cur_x = mid_x + (int)(norm_signed * (float)(w / 2 - 4) + (norm_signed >= 0 ? 0.5f : -0.5f));
        int lx = cur_x < mid_x ? cur_x : mid_x;
        int rx = cur_x > mid_x ? cur_x : mid_x;
        if (rx > lx) {
            dash_fill_rect(lx, ty, rx - lx, track_h, 64, 196, 255, 225);
        }
        /* Bright white/cyan thumb marker */
        dash_fill_rect(cur_x - 2, y0, 5, h, 235, 248, 255, 250);
    }
    /* Center zero reference tick */
    dash_fill_rect(mid_x - 1, y0 - 1, 2, h + 2, 220, 232, 245, 220);
}

/* 3) Continuous thermal / pressure range bar with optimal-zone tick marks */
static void dash_draw_range_bar(int x0, int y0, int w, int h, float frac, int has_val,
                                float tick_lo, float tick_hi, int r, int g, int b) {
    if (w < 40 || h < 4) return;
    int track_h = h >= 8 ? 6 : 5;
    int ty = y0 + (h - track_h) / 2;
    dash_fill_rect(x0, ty, w, track_h, 42, 56, 70, 165);

    if (has_val) {
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        int fw = (int)(frac * (float)w + 0.5f);
        if (fw < 4 && frac > 0.01f) fw = 4;
        if (fw > 0) dash_fill_rect(x0, ty, fw, track_h, r, g, b, 240);
    }
    if (tick_lo > 0.0f && tick_lo < 1.0f) {
        int tx = x0 + (int)(tick_lo * (float)w + 0.5f);
        dash_fill_rect(tx, y0, 2, h, 195, 210, 225, 180);
    }
    if (tick_hi > 0.0f && tick_hi < 1.0f) {
        int tx = x0 + (int)(tick_hi * (float)w + 0.5f);
        dash_fill_rect(tx, y0, 2, h, 255, 110, 110, 185);
    }
}

static void dash_draw_row_bar(int it, const dash_vals_t *v, int x0, int y0, int w, int h) {
    switch (it) {
    case IT_RPM: {
        float f = (v->rpm > 0) ? ((float)v->rpm / 6500.0f) : 0.0f;
        dash_draw_seg_bar(x0, y0, w, h, f, 18, 4, 42, 232, 196);
        break;
    }
    case IT_POWER: {
        float f = (!DASH_NAN(v->p) && v->p > 0.0f) ? (v->p / 250.0f) : 0.0f;
        dash_draw_seg_bar(x0, y0, w, h, f, 18, 3, 56, 212, 255);
        break;
    }
    case IT_TORQUE: {
        float f = (v->tq > 0) ? ((float)v->tq / 450.0f) : 0.0f;
        dash_draw_seg_bar(x0, y0, w, h, f, 18, 3, 46, 228, 186);
        break;
    }
    case IT_PEDAL: {
        float f = (v->ped > 0) ? ((float)v->ped / 100.0f) : 0.0f;
        dash_draw_seg_bar(x0, y0, w, h, f, 18, 2, 64, 204, 255);
        break;
    }
    case IT_BRAKE: {
        float f = (!DASH_NAN(v->brk) && v->brk > 0.0f) ? (v->brk / 60.0f) : 0.0f;
        dash_draw_seg_bar(x0, y0, w, h, f, 18, 4, 64, 204, 255);
        break;
    }
    case IT_G: {
        int ok = !DASH_NAN(v->gx) && !DASH_NAN(v->gy);
        float gt = ok ? sqrtf(v->gx * v->gx + v->gy * v->gy) : 0.0f;
        float sg = (ok && v->gx < -0.02f) ? -gt : gt;
        dash_draw_bipolar_bar(x0, y0, w, h, sg / 0.85f, ok);
        break;
    }
    case IT_GLONG:
        dash_draw_bipolar_bar(x0, y0, w, h, DASH_NAN(v->gx) ? 0.0f : (v->gx / 0.85f), !DASH_NAN(v->gx));
        break;
    case IT_GLAT: {
        float gy = DASH_NAN(v->gy) ? 0.0f : (g_dash_conf.lat_flip ? -v->gy : v->gy);
        dash_draw_bipolar_bar(x0, y0, w, h, gy / 0.85f, !DASH_NAN(v->gy));
        break;
    }
    case IT_STEER:
        dash_draw_bipolar_bar(x0, y0, w, h, (v->str == -999) ? 0.0f : ((float)v->str / 450.0f), v->str != -999);
        break;
    case IT_PITCH:
        dash_draw_bipolar_bar(x0, y0, w, h, DASH_NAN(v->pitch) ? 0.0f : (v->pitch / 25.0f), !DASH_NAN(v->pitch));
        break;
    case IT_ROLL:
        dash_draw_bipolar_bar(x0, y0, w, h, DASH_NAN(v->roll) ? 0.0f : (v->roll / 25.0f), !DASH_NAN(v->roll));
        break;
    case IT_OIL: {
        int ok = (v->oil != -999);
        int r = 68, g = 228, b = 152;
        if (ok && v->oil < 76)      { r = 72;  g = 192; b = 255; }
        else if (ok && v->oil >= 115) { r = 255; g = 68;  b = 72;  }
        dash_draw_range_bar(x0, y0, w, h, ok ? ((float)(v->oil - 50) / 80.0f) : 0.0f, ok,
                            (80.0f - 50.0f) / 80.0f, (115.0f - 50.0f) / 80.0f, r, g, b);
        break;
    }
    case IT_GEARBOX: {
        int ok = (v->gbt != -999 && v->gbt >= -40);
        int r = 68, g = 228, b = 152;
        if (ok && v->gbt < 60)       { r = 72;  g = 192; b = 255; }
        else if (ok && v->gbt >= 106) { r = 255; g = 68;  b = 72;  }
        dash_draw_range_bar(x0, y0, w, h, ok ? ((float)(v->gbt - 40) / 80.0f) : 0.0f, ok,
                            (65.0f - 40.0f) / 80.0f, (106.0f - 40.0f) / 80.0f, r, g, b);
        break;
    }
    case IT_COOLANT: {
        int ok = (v->cool != -999);
        int r = 68, g = 228, b = 152;
        if (ok && v->cool < 80)       { r = 72;  g = 192; b = 255; }
        else if (ok && v->cool >= 106) { r = 255; g = 68;  b = 72;  }
        dash_draw_range_bar(x0, y0, w, h, ok ? ((float)(v->cool - 50) / 70.0f) : 0.0f, ok,
                            (85.0f - 50.0f) / 70.0f, (106.0f - 50.0f) / 70.0f, r, g, b);
        break;
    }
    case IT_FUEL: {
        int ok = (v->tank != -999);
        int r = (ok && v->tank <= 15) ? 255 : 64;
        int g = (ok && v->tank <= 15) ? 128 : 208;
        int b = (ok && v->tank <= 15) ? 56  : 255;
        dash_draw_range_bar(x0, y0, w, h, ok ? ((float)v->tank / 100.0f) : 0.0f, ok, 0.15f, 0.0f, r, g, b);
        break;
    }
    case IT_TIRES: case IT_TPFL: case IT_TPFR: case IT_TPRL: case IT_TPRR: {
        float p = 0.0f / 0.0f;
        int warn = 0;
        if (it == IT_TPFL)      { p = v->tpfl; warn = v->tpst & 1; }
        else if (it == IT_TPFR) { p = v->tpfr; warn = v->tpst & 2; }
        else if (it == IT_TPRL) { p = v->tprl; warn = v->tpst & 4; }
        else if (it == IT_TPRR) { p = v->tprr; warn = v->tpst & 8; }
        else {
            warn = v->tpst;
            if (!DASH_NAN(v->tpfl) && !DASH_NAN(v->tprl)) p = 0.5f * (v->tpfl + v->tprl);
            else if (!DASH_NAN(v->tpfl)) p = v->tpfl;
            else p = v->tprl;
        }
        int ok = !DASH_NAN(p);
        int low = warn || (ok && p < 1.95f);
        /* 1.5..3.4 bar 视区：低红警 <1.95 bar，高红刻度放宽至 3.1 bar 避免满载冷胎压 2.8~3.0 bar 及热胎误显红区 */
        dash_draw_range_bar(x0, y0, w, h, ok ? ((p - 1.5f) / 1.9f) : 0.0f, ok,
                            (2.1f - 1.5f) / 1.9f, (3.1f - 1.5f) / 1.9f,
                            low ? 255 : 68, low ? 72 : 228, low ? 72 : 152);
        break;
    }
    default:
        break;
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
    char k[5];
    for (const char *p = s; *p; ) {
        p = dash_next_char(p, k);
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
    if (signed_long) {
        if (gx > -0.03f && gx < 0.03f) snprintf(o, 16, "%.2f", g);
        else snprintf(o, 16, "%c%.2f", gx < 0 ? '-' : '+', g);
    } else snprintf(o, 16, "%+.2f", g);
}

static void dash_item_value(int it, const dash_vals_t *v, char *o, const char **unit) {
    char gb[24];
    *unit = g_items[it].unit;
    #define I(cond, ...) do { if (cond) snprintf(o, 32, "-"); else snprintf(o, 32, __VA_ARGS__); } while (0)
    switch (it) {
    case IT_POWER:   I(DASH_NAN(v->p), "%d", dash_round(v->p)); break;
    case IT_TORQUE:  I(v->tq == -999, "%d", v->tq); break;
    case IT_G:
        if (DASH_NAN(v->gx) || DASH_NAN(v->gy)) snprintf(o, 32, "-");
        else dash_fmt_g(o, sqrtf(v->gx * v->gx + v->gy * v->gy), 1, v->gx);
        break;
    case IT_GLONG:   if (DASH_NAN(v->gx)) snprintf(o, 32, "-"); else dash_fmt_g(o, v->gx, 0, 0); break;
    case IT_GLAT:    if (DASH_NAN(v->gy)) snprintf(o, 32, "-"); else dash_fmt_g(o, g_dash_conf.lat_flip ? -v->gy : v->gy, 0, 0); break;
    case IT_RPM:     I(v->rpm == -999, "%d", v->rpm); break;
    case IT_SPEED:   I(DASH_NAN(v->spd), "%d", dash_round(v->spd)); break;
    case IT_PEDAL:   I(v->ped == -999, "%d", v->ped); break;
    case IT_BRAKE:   I(DASH_NAN(v->brk), "%.1f", v->brk); break;
    case IT_STEER:   I(v->str == -999, "%d", v->str); break;
    case IT_GEAR:    snprintf(o, 32, "%s", v->gm == -999 ? "-" : dash_gear(v->gm, v->gc, gb)); break;
    case IT_GEARBOX: I(v->gbt == -999 || v->gbt < -40, "%d", v->gbt); break;
    case IT_OIL:     I(v->oil == -999, "%d", v->oil); break;
    case IT_COOLANT: I(v->cool == -999, "%d", v->cool); break;
    case IT_OUTSIDE: I(v->out == -999, "%d", v->out); break;
    case IT_CONS:    I(DASH_NAN(v->cv) || v->cv > 999, "%.1f", v->cv); *unit = dash_cons_unit(v->cu); break;
    case IT_FUEL:
        if (v->tank == -999) snprintf(o, 32, "-");
        else if (g_dash_conf.tank_l > 0) snprintf(o, 32, "%d", (v->tank * g_dash_conf.tank_l + 50) / 100);
        else { snprintf(o, 32, "%d", v->tank); *unit = "%"; }
        break;
    case IT_RANGE:   I(v->rng == -999, "%d", v->rng); break;
    case IT_T100:
        if (!DASH_NAN(v->t1r)) snprintf(o, 32, "%.1f", v->t1r);
        else I(DASH_NAN(v->t1), "%.1f", v->t1);
        break;
    case IT_PMAX:    I(DASH_NAN(v->pmx), "%d", dash_round(v->pmx)); break;
    case IT_TQMAX:   I(v->tqx == -999, "%d", v->tqx); break;
    case IT_GMAX:    I(DASH_NAN(v->gmx), "%.2f", v->gmx); break;
    case IT_TRIPKM: {
        double t = v->odo - g_dash_conf.trip_base;
        I(v->odo != v->odo || g_dash_conf.trip_base <= 0 || t < 0 || t >= 100000, t < 10000 ? "%.1f" : "%.0f", t);
        break;
    }
    case IT_TIRES:
        if (DASH_NAN(v->tpfl) && DASH_NAN(v->tprl)) snprintf(o, 32, "-");
        else {
            float f = (!DASH_NAN(v->tpfl) && !DASH_NAN(v->tpfr)) ? 0.5f * (v->tpfl + v->tpfr) : (!DASH_NAN(v->tpfl) ? v->tpfl : v->tpfr);
            float r = (!DASH_NAN(v->tprl) && !DASH_NAN(v->tprr)) ? 0.5f * (v->tprl + v->tprr) : (!DASH_NAN(v->tprl) ? v->tprl : v->tprr);
            if (DASH_NAN(f) || DASH_NAN(r)) snprintf(o, 32, "%.1f", DASH_NAN(f) ? r : f);
            else snprintf(o, 32, "%.1f·%.1f", f, r);
        }
        break;
    case IT_TPFL:    I(DASH_NAN(v->tpfl), "%.1f%s", v->tpfl, (v->tpst & 1) ? "!" : ""); break;
    case IT_TPFR:    I(DASH_NAN(v->tpfr), "%.1f%s", v->tpfr, (v->tpst & 2) ? "!" : ""); break;
    case IT_TPRL:    I(DASH_NAN(v->tprl), "%.1f%s", v->tprl, (v->tpst & 4) ? "!" : ""); break;
    case IT_TPRR:    I(DASH_NAN(v->tprr), "%.1f%s", v->tprr, (v->tpst & 8) ? "!" : ""); break;
    case IT_PITCH:   I(DASH_NAN(v->pitch), "%+.1f", v->pitch); break;
    case IT_ROLL:    I(DASH_NAN(v->roll), "%+.1f", v->roll); break;
    case IT_ALT:     I(v->alt == -999, "%d", v->alt); break;
    default:
        if (it >= IT_S_AVG && it <= IT_L_TIME) {
            int m = (it - IT_S_AVG) / 4, k = (it - IT_S_AVG) % 4;
            if (k == 0) { I(DASH_NAN(v->avg[m]) || v->avg[m] < 0 || v->avg[m] > 999, "%.1f", v->avg[m]); *unit = dash_cons_unit(v->avgu[m]); }
            else if (k == 1) I(DASH_NAN(v->dist[m]) || v->dist[m] < 0, "%d", dash_round(v->dist[m]));
            else if (k == 2) I(DASH_NAN(v->bspd[m]) || v->bspd[m] < 0, "%d", dash_round(v->bspd[m]));
            else I(v->btime[m] == -999 || v->btime[m] < 0, "%d:%02d", v->btime[m] / 60, v->btime[m] % 60);
        } else snprintf(o, 32, "-");
    }
    #undef I
}

static void dash_render(const dash_vals_t *v) {
    const int C_LAB = 224, C_VAL = 255, C_UNIT = 190;
    dash_fill_bg();
    for (int r = 0; r < g_nrows; r++) {
        int it = g_rows[r];
        char val[32]; const char *unit;
        dash_item_value(it, v, val, &unit);
        int top = L_pad_y + r * L_row_h;
        int has_bar = dash_item_has_bar(it) && (L_row_h >= 46);
        int bar_h = L_row_h >= 64 ? 9 : (L_row_h >= 52 ? 8 : 6);
        int txt_h = has_bar ? (L_row_h - bar_h - 6) : L_row_h;
        int base = top + txt_h / 2 + L_base_off;

        const dash_glyph *lg = dash_find(L_lab, L_nlab, g_items[it].label);
        if (lg && lg->base < 0) dash_blit_mask(lg, L_lab_x, top + (txt_h - lg->h) / 2, C_LAB);
        else if (lg) dash_blit_mask(lg, L_lab_x, base - 2 - lg->base, C_LAB);

        /* Right-align [value] [unit] flush to the card's right inner margin (matching OEM / sport reference card) */
        int right_edge = g_dash_w - L_lab_x;
        int val_r = right_edge;
        if (unit) {
            const dash_glyph *ug = dash_find(L_unit, L_nunit, unit);
            if (ug) {
                int ux = right_edge - ug->adv;
                dash_blit_mask(ug, ux, base - 1 - ug->base, C_UNIT);
                val_r = ux - L_unit_x;
            }
        }
        dash_draw_value(val, val_r, base, C_VAL);

        if (has_bar) {
            int bar_y = top + L_row_h - bar_h - 4;
            dash_draw_row_bar(it, v, L_lab_x, bar_y, g_dash_w - L_lab_x * 2, bar_h);
        }
        /* Subtle horizontal divider between rows (matching the reference photo) */
        if (r + 1 < g_nrows) {
            dash_fill_rect(L_lab_x, top + L_row_h - 1, g_dash_w - L_lab_x * 2, 1, 185, 205, 225, 34);
        }
    }
}

/* Lay out each box independently so a 6-row box on one side never shrinks a 5-row box on the other side */
static void dash_layout_all(void) {
    int cur = g_box_cur;
    for (int i = 0; i < 2; i++) {
        dash_use(i);
        if (g_dash_conf.enabled && g_nrows > 0) dash_layout();
    }
    int h = 0, both = 1;
    for (int i = 0; i < 2; i++) {
        dash_use(i);
        if (g_dash_conf.enabled && g_nrows > 0) { if (g_dash_h > h) h = g_dash_h; }
        else both = 0;
    }
    if (both && h > 0) {
        for (int i = 0; i < 2; i++) { dash_use(i); g_dash_h = h; }
    }
    dash_use(cur);
}

static void dash_render_all(const dash_vals_t *v) {
    int cur = g_box_cur;
    for (int i = 0; i < 2; i++) {
        dash_use(i);
        if (g_dash_conf.enabled && g_nrows > 0) dash_render(v);
    }
    dash_use(cur);
}

static dash_vals_t g_dash_tgt, g_dash_sm;
static int         g_dash_sm_inited;
static int         g_dash_animating;
static uint64_t    g_dash_sm_ms;

static int dash_step_f(float *cur, float tgt, float alpha, float eps) {
    if (DASH_NAN(tgt)) { *cur = tgt; return 0; }
    if (DASH_NAN(*cur)) { *cur = tgt; return 1; }
    float d = tgt - *cur;
    if (d > -eps && d < eps) { *cur = tgt; return 0; }
    *cur += d * alpha;
    return 1;
}

static int dash_step_i(int *cur, int tgt, float alpha, int eps) {
    if (tgt == -999) { *cur = tgt; return 0; }
    if (*cur == -999) { *cur = tgt; return 1; }
    int d = tgt - *cur;
    if (d >= -eps && d <= eps) { *cur = tgt; return 0; }
    int step = dash_round((double)d * (double)alpha);
    if (step == 0) step = d > 0 ? 1 : -1;
    *cur += step;
    return 1;
}

static int dash_poll(uint64_t now_ms) {
    int want = 0, changed = 0, got_new = 0;
    if (!g_box[0].conf.enabled && !g_box[1].conf.enabled && !g_dash_conf.enabled) {
        want = 0;
        g_dash_animating = 0;
    } else {
        FILE *f = fopen(DASH_STATE, "r");
        if (!f) f = fopen(DASH_STATE ".new", "r");
        if (f) {
            char l[2048];
            if (fgets(l, sizeof l, f) && strchr(l, '\n')) {
                dash_vals_t v; dash_parse(l, &v);
                if (v.n != g_dash_last_n) { g_dash_last_n = v.n; g_dash_last_new_ms = now_ms; }
                if (strcmp(l, g_dash_line)) {
                    strncpy(g_dash_line, l, sizeof g_dash_line - 1);
                    g_dash_line[sizeof g_dash_line - 1] = 0;
                    g_dash_tgt = v;
                    if (!g_dash_sm_inited || !g_dash_conf.smooth) {
                        g_dash_sm = v;
                        g_dash_sm_inited = 1;
                    }
                    got_new = 1;
                }
            }
            fclose(f);
        }
        want = g_dash_last_n >= 0 && now_ms - g_dash_last_new_ms < (uint64_t)g_dash_conf.stale_ms;
        if (want && g_dash_sm_inited) {
            if (!g_dash_conf.smooth) {
                g_dash_animating = 0;
                if (got_new) { dash_render_all(&g_dash_tgt); changed = 1; }
            } else {
                float dt = (!g_dash_animating || !g_dash_sm_ms) ? 0.033f : (float)(now_ms - g_dash_sm_ms) * 0.001f;
                if (dt < 0.01f) dt = 0.01f;
                if (dt > 0.10f) dt = 0.10f;
                g_dash_sm_ms = now_ms;
                float alpha = 1.0f - expf(-14.0f * dt);
                /* Keep discrete/text values exact, smoothly interpolate dynamic bars & fast gauges */
                dash_vals_t prev = g_dash_sm;
                g_dash_sm = g_dash_tgt;
                g_dash_sm.rpm = prev.rpm; g_dash_sm.tq = prev.tq; g_dash_sm.ped = prev.ped; g_dash_sm.str = prev.str;
                g_dash_sm.p = prev.p; g_dash_sm.brk = prev.brk; g_dash_sm.gx = prev.gx; g_dash_sm.gy = prev.gy;
                int mv = 0;
                mv |= dash_step_i(&g_dash_sm.rpm, g_dash_tgt.rpm, alpha, 25);
                mv |= dash_step_i(&g_dash_sm.tq,  g_dash_tgt.tq,  alpha, 4);
                mv |= dash_step_i(&g_dash_sm.ped, g_dash_tgt.ped, alpha, 1);
                mv |= dash_step_i(&g_dash_sm.str, g_dash_tgt.str, alpha, 2);
                mv |= dash_step_f(&g_dash_sm.p,   g_dash_tgt.p,   alpha, 2.0f);
                mv |= dash_step_f(&g_dash_sm.brk, g_dash_tgt.brk, alpha, 0.4f);
                mv |= dash_step_f(&g_dash_sm.gx,  g_dash_tgt.gx,  alpha, 0.02f);
                mv |= dash_step_f(&g_dash_sm.gy,  g_dash_tgt.gy,  alpha, 0.02f);
                g_dash_animating = mv;
                if (got_new || mv) { dash_render_all(&g_dash_sm); changed = 1; }
            }
        }
    }
    if (want != g_dash_have) { g_dash_have = want; changed = 1; }
    return changed;
}
