/*
 * v23_sandbox.h — 途锐 MH2p v23 车机端 QuickJS 60fps 皮肤安全沙盒与数据总线核心（纯可移植 C99）
 *
 * 设计红线：
 *   - 零 QNX / EGL / Screen 平台依赖，既可由 cl_render (v23_gles.c) 直接包含运行在车机 Tegra K1 上，
 *     也可在主机/CI 直接编译并用 ASan + UBSan 进行全量单元测试。
 *   - 安全三防线：128 KB 单文件上限 + 4 MB 精确字节内存封顶（JS_NewRuntime2 16B 对齐追踪分配器，
 *     零 malloc_usable_size 依赖）+ 256 KB 栈上限 + 单帧 35 ms 硬中断掐断 + 物理移除 eval / Function。
 *   - 兼容单皮肤 (THEME + render) 与单文件多皮肤包 (const THEMES = [ ... ]，最多 8 套)。
 *   - 所有冷车未就绪信号（NaN / -999）强制兜底为有限数字 0 并提供 bus.ready.*，杜绝 .toFixed() 崩溃。
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under CC BY-NC-SA 4.0 (see LICENSES/CC-BY-NC-SA-4.0.md and LICENSES/THIRD_PARTY_NOTICES.md)
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#ifndef V23_SANDBOX_H
#define V23_SANDBOX_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "quickjs/quickjs.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define V23_JS_MEM_LIMIT_BYTES   (4u * 1024u * 1024u)  /* 4 MB 严格内存封顶 */
#define V23_JS_STACK_LIMIT_BYTES (256u * 1024u)        /* 256 KB 递归栈上限 */
#define V23_FRAME_TIMEOUT_US     35000ULL              /* 35 ms 单帧硬中断（防死循环，同时容忍 ARMv7 偶发 GC/光栅化抖动） */
#define V23_LOAD_TIMEOUT_US      120000ULL             /* 120 ms 编译加载超时 */
#define V23_MAX_THEME_BYTES      (128u * 1024u)        /* 128 KB 单文件上限 */
#define V23_MAX_SHADER_BYTES     8192u                 /* 8 KB 自定义 GLSL 上限 */
#define V23_MAX_THEMES_PER_FILE  8
#define V23_MAX_POLY_PTS         64
#define V23_CANVAS_W             1920
#define V23_CANVAS_H             639

typedef struct {
    float r, g, b, a; /* 0..1 */
    int   valid;
} v23_rgba_t;

typedef struct {
    float radius, alpha, vignette, border_width;
    int   is_circle;
    v23_rgba_t border_col;
} v23_map_opts_t;

typedef struct {
    float alpha;
    int   blend_add;
} v23_shader_opts_t;

typedef struct {
    v23_rgba_t fill, fill2, stroke, glow_col;
    int   grad_h;
    float line_width, radius, glow;
} v23_rect_opts_t;

typedef struct {
    v23_rgba_t fill, stroke, glow_col;
    float line_width, glow;
} v23_circle_opts_t;

typedef struct {
    v23_rgba_t color, glow_col;
    float line_width, glow;
    int   cap_round;
} v23_line_opts_t;

typedef struct {
    v23_rgba_t color, glow_col;
    float line_width, glow;
    int   cap_round;
} v23_arc_opts_t;

typedef struct {
    v23_rgba_t color, red_col;
    int   major_every;
    float major_len, line_width, major_width, red_from_frac;
    int   has_red;
} v23_ticks_opts_t;

typedef struct {
    v23_rgba_t color, tip_col, hub_col;
    float width, glow, hub_radius;
} v23_needle_opts_t;

typedef struct {
    int   segs, red_segs, pulse;
    v23_rgba_t color, warn_col, red_col;
} v23_segbar_opts_t;

typedef struct {
    v23_rgba_t pos_col, neg_col;
} v23_bibar_opts_t;

typedef struct {
    v23_rgba_t color;
    float tick_lo, tick_hi;
} v23_rangebar_opts_t;

typedef struct {
    float size, glow, max_width;
    int   bold, italic;
    int   align;    /* 0=left, 1=center, 2=right */
    int   baseline; /* 0=alphabetic, 1=middle, 2=top */
    v23_rgba_t color, glow_col;
} v23_text_opts_t;

/* 显卡/测试后端统一绘制回调表（17 个 ctx 方法 1:1 映射） */
typedef struct {
    void *user;
    void (*clear)(void *u, float r, float g, float b, float a);
    void (*draw_map)(void *u, float x, float y, float w, float h, const v23_map_opts_t *o);
    void (*draw_shader)(void *u, const v23_shader_opts_t *o);
    void (*rect)(void *u, float x, float y, float w, float h, const v23_rect_opts_t *o);
    void (*circle)(void *u, float cx, float cy, float r, const v23_circle_opts_t *o);
    void (*line)(void *u, float x1, float y1, float x2, float y2, const v23_line_opts_t *o);
    void (*poly)(void *u, const float *xy, int npts, const v23_circle_opts_t *o);
    void (*arc)(void *u, float cx, float cy, float r, float start_deg, float end_deg, const v23_arc_opts_t *o);
    void (*ticks)(void *u, float cx, float cy, float r_in, float r_out, float start_deg, float end_deg, int count, const v23_ticks_opts_t *o);
    void (*needle)(void *u, float cx, float cy, float len, float tail, float angle_deg, const v23_needle_opts_t *o);
    void (*seg_bar)(void *u, float x, float y, float w, float h, float frac, const v23_segbar_opts_t *o);
    void (*bi_bar)(void *u, float x, float y, float w, float h, float signed_frac, const v23_bibar_opts_t *o);
    void (*range_bar)(void *u, float x, float y, float w, float h, float frac, const v23_rangebar_opts_t *o);
    void (*text)(void *u, const char *str, float x, float y, const v23_text_opts_t *o);
    void (*draw_dash_box)(void *u, float x, float y, float w, int side);
    void (*draw_nav_card)(void *u, float x, float y, float w, float h, int alpha);
    void (*draw_turn_arrow)(void *u, float x, float y, float size, const char *type, v23_rgba_t col);
} v23_draw_ops_t;

/* 统一 60fps 车辆/导航/系统数据总线 */
typedef struct {
    double rpm, rpm_raw, rpm_peak;
    double speed, speed_raw;
    double power, torque, pedal, brake, steer;
    double g, glong, glat;
    double oil, gearbox, coolant, outside, cons, fuel, range;
    double pitch, roll, alt;
    double tpfl, tpfr, tprl, tprr;
    double t100, pmax, tqmax, gmax;
    double trip, tripdist, tripspeed, refuel, refueldist, refuelspeed, total, totaldist, totalspeed, tripkm;
    int    gear_num, t100_running;
    char   gear[12], tires[24], triptime[16], refueltime[16], totaltime[16];

    /* 冷车传感器就绪标志 */
    int ready_oil, ready_gearbox, ready_tires, ready_alt, ready_t100;

    /* 导航状态 */
    int    nav_live, nav_dist_m, nav_rem_min;
    double nav_rem_dist_km;
    char   nav_eta[16], nav_road[128], nav_next_road[128], nav_action[64], nav_turn_type[24];

    /* 系统状态 */
    int    sys_map_live, sys_night, sys_acc, sys_fps, sys_map_fps;
    double sys_time, sys_dt;

    /* 兼容 dash.conf */
    int    conf_show, conf_r_show, conf_bars, conf_alpha, conf_nav_box, conf_tank_l;
    char   conf_nav_side[12];
} v23_bus_t;

typedef struct {
    char id[64];
    char name[96];
    char author[64];
    char shader[V23_MAX_SHADER_BYTES + 1];
    int  has_shader;
} v23_theme_meta_t;

typedef struct {
    uint64_t deadline_us;
    int      timed_out;
    uint32_t draw_calls;
    const v23_draw_ops_t *ops;
} v23_guard_t;

typedef struct {
    JSRuntime *rt;
    JSContext *ctx;
    v23_guard_t guard;
    JSValue    draw_ctx;
    JSValue    theme_state;
    JSValue    entries;
    int        entry_count;
    int        active_idx;
    v23_theme_meta_t metas[V23_MAX_THEMES_PER_FILE];
    char       last_err[512];
    double     last_compile_ms;
} v23_sandbox_t;

/* 内置唯一默认皮肤（100% 沿用老用户 dash.conf 43 项勾选与大字导航卡） */
static const char V23_DEFAULT_CLASSIC_JS[] =
    "const THEME = {\n"
    "  id: 'default_classic',\n"
    "  name: '内置默认 60fps 经典版',\n"
    "  author: 'touareg-cn-carplay-mods',\n"
    "  version: '23.0',\n"
    "  shader: ''\n"
    "};\n"
    "function render(ctx, bus) {\n"
    "  ctx.clear(6, 10, 16, 1);\n"
    "  if (bus.sys.mapLive) {\n"
    "    ctx.drawMap(0, 0, 1920, 639, { alpha: 1.0 });\n"
    "  } else {\n"
    "    ctx.rect(0, 0, 1920, 639, { fill: '#070c16', fill2: '#101b2e', gradDir: 'v' });\n"
    "    var cx = 960, cy = 345, r = 160;\n"
    "    ctx.arc(cx, cy, r, 135, 405, { color: 'rgba(148,163,184,0.18)', lineWidth: 16, cap: 'round' });\n"
    "    var rf = Math.min(1, bus.rpm / 6800);\n"
    "    ctx.arc(cx, cy, r, 135, 135 + 270 * rf, { color: rf > 0.82 ? '#ef4444' : '#22d3ee', lineWidth: 16, cap: 'round', glow: 14 });\n"
    "    ctx.text(String(Math.round(bus.speed)), cx, cy + 16, { size: 96, bold: true, color: '#ffffff', align: 'center' });\n"
    "    ctx.text('km/h  ·  ' + bus.gear, cx, cy + 62, { size: 24, bold: true, color: '#38bdf8', align: 'center' });\n"
    "  }\n"
    "  if (bus.conf.show) ctx.drawDashBox(5, 200, 295, 'left');\n"
    "  if (bus.nav.live && bus.conf.nav_box > 0) {\n"
    "    ctx.drawNavCard(bus.conf.nav_side === 'left' ? 5 : 1620, 200, 295, 308, { alpha: bus.conf.alpha });\n"
    "  } else if (bus.conf.r_show) {\n"
    "    ctx.drawDashBox(1620, 200, 295, 'right');\n"
    "  }\n"
    "}\n";

/* ---- 时间、数学与冷车数据安全清洗 ---- */
static inline uint64_t v23_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static inline double v23_sanitize_num(double v, int *ready_out) {
    if (!isfinite(v) || v <= -900.0) {
        if (ready_out) *ready_out = 0;
        return 0.0;
    }
    if (ready_out) *ready_out = 1;
    return v;
}

/* ---- 纯可移植遥测/导航格式化函数（v23_gles.c 与主机单测共用单一实现） ---- */

/* 对齐 dash.c:744-760：完整支持大众原厂换挡杆模式码（含经济 E 挡与混动 B 挡），未就绪输出 "-" */
static inline void v23_format_gear(int gm, int gc, char *out, size_t sz) {
    char m = 0;
    switch (gm) {
    case 1: snprintf(out, sz, "P"); return;
    case 2: snprintf(out, sz, "R"); return;
    case 3: snprintf(out, sz, "N"); return;
    case 4: case 13: m = 'D'; break;
    case 5: case 11: m = 'S'; break;
    case 6: case 7: case 10: case 12: m = 'M'; break;
    case 8: m = 'E'; break;
    case 9: m = 'B'; break;
    default: snprintf(out, sz, "-"); return;
    }
    if (gc >= 1 && gc <= 10) snprintf(out, sz, "%c%d", m, gc);
    else snprintf(out, sz, "%c", m);
}

/* 对齐 Mapper.java / navbox.c / clusterd.c 的 iAP2 ManeuverType 映射 */
static inline const char *v23_map_turn_type(int mt, int junction, int driving_side) {
    if (junction == 1 || mt == 6 || mt == 7 || mt == 19 || (mt >= 28 && mt <= 46))
        return "roundabout";
    if (mt == 4 || mt == 18 || mt == 26)
        return "uturn";
    if (mt == 8)
        return (driving_side == 1) ? "slight_left" : "slight_right";
    if (mt == 1 || mt == 20 || mt == 47)
        return "left";
    if (mt == 2 || mt == 21 || mt == 48)
        return "right";
    if (mt == 13 || mt == 22 || mt == 49 || mt == 52)
        return "slight_left";
    if (mt == 14 || mt == 23 || mt == 50 || mt == 53)
        return "slight_right";
    return "straight";
}

/* 对齐 navbox.c:1499 & Bridge.java:356：剩余分钟向上取整 */
static inline int v23_calc_rem_min(int time_remaining) {
    return time_remaining >= 0 ? (time_remaining + 59) / 60 : 0;
}

/* 对齐 navbox.c:1150-1169：+30s 四舍五入 + 1970 车机时钟兜底 */
static inline void v23_format_eta(uint32_t eta_ts, int time_remaining, int tz_offset,
                                  time_t now_utc, char *out, size_t sz) {
    if (eta_ts < 1577836800U) {
        if (now_utc > 1577836800L && time_remaining >= 0) {
            eta_ts = (uint32_t)((unsigned long)now_utc + (unsigned long)time_remaining);
        } else {
            snprintf(out, sz, "--:--");
            return;
        }
    }
    int64_t local_s = (int64_t)eta_ts + (int64_t)tz_offset * 3600 + 30;
    uint32_t sod = (uint32_t)(((local_s % 86400) + 86400) % 86400);
    snprintf(out, sz, "%02u:%02u", (unsigned)(sod / 3600U), (unsigned)((sod % 3600U) / 60U));
}

/* 对齐 dash.c:796-800 & THEME_API_SPEC.md：按 tank_l 将油量百分比折算为升数（tank_l=0 保留百分比） */
static inline double v23_calc_fuel(int tank_pct, int tank_l, int *ready_out) {
    if (tank_pct == -999 || tank_pct < 0 || tank_pct > 100) {
        if (ready_out) *ready_out = 0;
        return 0.0;
    }
    if (ready_out) *ready_out = 1;
    if (tank_l > 0) return ((double)tank_pct * (double)tank_l) / 100.0;
    return (double)tank_pct;
}

/* 对齐 dash.c:814-821：逐轮判有效，缺轮不拉低均值，任一有效轮即置 ready_tires=1 */
static inline void v23_calc_tires(double tpfl, double tpfr, double tprl, double tprr,
                                  int *ready_out, char *out, size_t sz) {
    int ok_fl = isfinite(tpfl) && tpfl > -900.0;
    int ok_fr = isfinite(tpfr) && tpfr > -900.0;
    int ok_rl = isfinite(tprl) && tprl > -900.0;
    int ok_rr = isfinite(tprr) && tprr > -900.0;
    int ok_f = ok_fl || ok_fr;
    int ok_r = ok_rl || ok_rr;
    double f = (ok_fl && ok_fr) ? 0.5 * (tpfl + tpfr) : (ok_fl ? tpfl : tpfr);
    double r = (ok_rl && ok_rr) ? 0.5 * (tprl + tprr) : (ok_rl ? tprl : tprr);
    if (!ok_f && !ok_r) {
        if (ready_out) *ready_out = 0;
        snprintf(out, sz, "--·--");
    } else if (ok_f && ok_r) {
        if (ready_out) *ready_out = 1;
        snprintf(out, sz, "%.1f·%.1f", f, r);
    } else {
        if (ready_out) *ready_out = 1;
        snprintf(out, sz, "%.1f", ok_f ? f : r);
    }
}

/* 对齐 dash.c:809-813,831-837：填充 13 项行车电脑字段（自启动起 / 自上次加油后 / 累计 / Trip） */
static inline void v23_format_bc_time(int mins, char *out, size_t sz) {
    if (mins == -999 || mins < 0) snprintf(out, sz, "--:--");
    else snprintf(out, sz, "%d:%02d", mins / 60, mins % 60);
}

static inline void v23_fill_board_computer(v23_bus_t *bus,
                                           const float avg[3], const float dist[3],
                                           const float bspd[3], const int btime[3],
                                           double odo, double trip_base) {
    double a[3], d[3], sp[3];
    for (int m = 0; m < 3; m++) {
        a[m]  = (isfinite(avg[m])  && avg[m]  >= 0.0f && avg[m]  <= 999.0f) ? (double)avg[m]  : 0.0;
        d[m]  = (isfinite(dist[m]) && dist[m] >= 0.0f)                      ? (double)dist[m] : 0.0;
        sp[m] = (isfinite(bspd[m]) && bspd[m] >= 0.0f)                      ? (double)bspd[m] : 0.0;
    }
    bus->trip        = a[0];  bus->tripdist   = d[0];  bus->tripspeed   = sp[0];
    bus->refuel      = a[1];  bus->refueldist = d[1];  bus->refuelspeed = sp[1];
    bus->total       = a[2];  bus->totaldist  = d[2];  bus->totalspeed  = sp[2];
    v23_format_bc_time(btime[0], bus->triptime,   sizeof(bus->triptime));
    v23_format_bc_time(btime[1], bus->refueltime, sizeof(bus->refueltime));
    v23_format_bc_time(btime[2], bus->totaltime,  sizeof(bus->totaltime));
    double tk = odo - trip_base;
    bus->tripkm = (isfinite(odo) && trip_base > 0.0 && tk >= 0.0 && tk < 100000.0) ? tk : 0.0;
}

/* 纯多项式 exp(-x)（x >= 0，避免某些旧工具链引入额外 libm float 私有符号） */
static inline float v23_exp_negf(float x) {
    if (x <= 0.0f) return 1.0f;
    if (x >= 12.0f) return 0.0f;
    /* Pade [2/2] 拆 4 次半角缩放，精度 < 1e-4 */
    float h = x * 0.125f;
    float num = 1.0f - 0.5f * h + 0.08333333f * h * h;
    float den = 1.0f + 0.5f * h + 0.08333333f * h * h;
    float r = num / den;
    r *= r; r *= r; r *= r;
    return r;
}

/* 零 DSI 依赖自动昼夜识别：稀疏采样 RGBX/BGRA 或 Y 缓冲亮度 + 滞回防闪烁 */
static inline int v23_detect_night_rgbx(const uint8_t *buf, int w, int h, int stride, int prev_night) {
    if (!buf || w <= 0 || h <= 0 || stride < w * 4) return prev_night;
    uint64_t sum = 0;
    uint32_t cnt = 0;
    int y0 = h / 4, y1 = (h * 3) / 4;
    int x0 = w / 5, x1 = (w * 4) / 5;
    for (int y = y0; y < y1; y += 16) {
        const uint8_t *row = buf + (size_t)y * (size_t)stride;
        for (int x = x0; x < x1; x += 16) {
            const uint8_t *p = row + (size_t)x * 4u;
            /* Rec.601 近似亮度（对 RGBA 与 BGRA 权重近似对称，避免通道顺序偏差） */
            uint32_t luma = ((uint32_t)p[0] * 54u + (uint32_t)p[1] * 183u + (uint32_t)p[2] * 19u) >> 8;
            sum += luma;
            cnt++;
        }
    }
    if (cnt == 0) return prev_night;
    int avg = (int)(sum / cnt);
    if (prev_night) return (avg > 135) ? 0 : 1;
    return (avg < 95) ? 1 : 0;
}

/* ---- CSS 颜色字符串解析（支持 #rgb / #rrggbb / #rrggbbaa / rgb() / rgba()） ---- */
static int v23_hex1(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static v23_rgba_t v23_parse_color(const char *s, v23_rgba_t def) {
    if (!s || !s[0]) return def;
    while (*s == ' ' || *s == '\t') s++;
    v23_rgba_t out = { 1.0f, 1.0f, 1.0f, 1.0f, 1 };
    if (s[0] == '#') {
        size_t len = strlen(s + 1);
        if (len == 3 || len == 4) {
            int r = v23_hex1(s[1]), g = v23_hex1(s[2]), b = v23_hex1(s[3]);
            int a = (len == 4) ? v23_hex1(s[4]) : 15;
            if (r < 0 || g < 0 || b < 0 || a < 0) return def;
            out.r = (float)(r * 17) / 255.0f;
            out.g = (float)(g * 17) / 255.0f;
            out.b = (float)(b * 17) / 255.0f;
            out.a = (float)(a * 17) / 255.0f;
            return out;
        }
        if (len == 6 || len == 8) {
            int r1 = v23_hex1(s[1]), r2 = v23_hex1(s[2]);
            int g1 = v23_hex1(s[3]), g2 = v23_hex1(s[4]);
            int b1 = v23_hex1(s[5]), b2 = v23_hex1(s[6]);
            int a1 = (len == 8) ? v23_hex1(s[7]) : 15;
            int a2 = (len == 8) ? v23_hex1(s[8]) : 15;
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0 || a1 < 0 || a2 < 0) return def;
            out.r = (float)((r1 << 4) | r2) / 255.0f;
            out.g = (float)((g1 << 4) | g2) / 255.0f;
            out.b = (float)((b1 << 4) | b2) / 255.0f;
            out.a = (float)((a1 << 4) | a2) / 255.0f;
            return out;
        }
        return def;
    }
    if (strncmp(s, "rgba(", 5) == 0 || strncmp(s, "rgb(", 4) == 0) {
        const char *p = strchr(s, '(');
        if (!p) return def;
        float r = 0, g = 0, b = 0, a = 1.0f;
        int n = sscanf(p + 1, " %f , %f , %f , %f", &r, &g, &b, &a);
        if (n >= 3) {
            out.r = r < 0 ? 0.0f : (r > 255.0f ? 1.0f : r / 255.0f);
            out.g = g < 0 ? 0.0f : (g > 255.0f ? 1.0f : g / 255.0f);
            out.b = b < 0 ? 0.0f : (b > 255.0f ? 1.0f : b / 255.0f);
            out.a = (n >= 4) ? (a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)) : 1.0f;
            return out;
        }
    }
    return def;
}

/* ---- 16 字节对齐精确计数分配器（不依赖 malloc_usable_size，QNX 6.6 精确执行 4MB 封顶） ---- */
#define V23_MALLOC_HDR_SZ 16u

static size_t v23_js_usable(const void *ptr) {
    if (!ptr) return 0;
    size_t sz = 0;
    memcpy(&sz, (const uint8_t *)ptr - V23_MALLOC_HDR_SZ, sizeof(sz));
    return sz;
}

static void *v23_js_malloc(JSMallocState *s, size_t size) {
    if (size == 0) size = 1;
    if (size > s->malloc_limit || s->malloc_size > s->malloc_limit - size) return NULL;
    uint8_t *raw = (uint8_t *)malloc(size + V23_MALLOC_HDR_SZ);
    if (!raw) return NULL;
    memcpy(raw, &size, sizeof(size));
    s->malloc_count++;
    s->malloc_size += size;
    return raw + V23_MALLOC_HDR_SZ;
}

static void v23_js_free(JSMallocState *s, void *ptr) {
    if (!ptr) return;
    uint8_t *raw = (uint8_t *)ptr - V23_MALLOC_HDR_SZ;
    size_t sz = 0;
    memcpy(&sz, raw, sizeof(sz));
    s->malloc_count--;
    s->malloc_size -= sz;
    free(raw);
}

static void *v23_js_realloc(JSMallocState *s, void *ptr, size_t size) {
    if (!ptr) return v23_js_malloc(s, size);
    if (size == 0) { v23_js_free(s, ptr); return NULL; }
    uint8_t *old_raw = (uint8_t *)ptr - V23_MALLOC_HDR_SZ;
    size_t old_sz = 0;
    memcpy(&old_sz, old_raw, sizeof(old_sz));
    if (size > old_sz) {
        size_t delta = size - old_sz;
        if (delta > s->malloc_limit || s->malloc_size > s->malloc_limit - delta) return NULL;
    }
    uint8_t *new_raw = (uint8_t *)realloc(old_raw, size + V23_MALLOC_HDR_SZ);
    if (!new_raw) return NULL;
    memcpy(new_raw, &size, sizeof(size));
    s->malloc_size = s->malloc_size - old_sz + size;
    return new_raw + V23_MALLOC_HDR_SZ;
}

static const JSMallocFunctions g_v23_mf = {
    v23_js_malloc, v23_js_free, v23_js_realloc, v23_js_usable
};

static int v23_interrupt_cb(JSRuntime *rt, void *opaque) {
    (void)rt;
    v23_guard_t *g = (v23_guard_t *)opaque;
    if (g && g->deadline_us > 0 && v23_now_us() >= g->deadline_us) {
        g->timed_out = 1;
        return 1;
    }
    return 0;
}

/* ---- JS 参数读取辅助 ---- */
static float v23_arg_f(JSContext *ctx, int argc, JSValueConst *argv, int idx, float def) {
    if (idx >= argc || JS_IsUndefined(argv[idx]) || JS_IsNull(argv[idx])) return def;
    double d = 0.0;
    if (JS_ToFloat64(ctx, &d, argv[idx]) != 0 || !isfinite(d)) return def;
    return (float)d;
}

static float v23_opt_f(JSContext *ctx, JSValueConst obj, const char *key, float def) {
    if (!JS_IsObject(obj)) return def;
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    float out = def;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        double d = 0.0;
        if (JS_ToFloat64(ctx, &d, v) == 0 && isfinite(d)) out = (float)d;
    }
    JS_FreeValue(ctx, v);
    return out;
}

static int v23_opt_bool(JSContext *ctx, JSValueConst obj, const char *key, int def) {
    if (!JS_IsObject(obj)) return def;
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    int out = def;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) out = JS_ToBool(ctx, v) > 0 ? 1 : 0;
    JS_FreeValue(ctx, v);
    return out;
}

static v23_rgba_t v23_opt_col(JSContext *ctx, JSValueConst obj, const char *key, v23_rgba_t def) {
    if (!JS_IsObject(obj)) return def;
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    v23_rgba_t out = def;
    if (JS_IsString(v)) {
        const char *s = JS_ToCString(ctx, v);
        if (s) { out = v23_parse_color(s, def); JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, v);
    return out;
}

static void v23_opt_str(JSContext *ctx, JSValueConst obj, const char *key, char *out, size_t cap, const char *def) {
    snprintf(out, cap, "%s", def ? def : "");
    if (!JS_IsObject(obj)) return;
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        const char *s = JS_ToCString(ctx, v);
        if (s) { snprintf(out, cap, "%s", s); JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, v);
}

/* ---- 17 个 ctx.* JS 绑定实现 ---- */
static JSValue v23_fn_clear(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->clear) {
        float r = v23_arg_f(ctx, argc, argv, 0, 0.0f) / 255.0f;
        float gr = v23_arg_f(ctx, argc, argv, 1, 0.0f) / 255.0f;
        float b = v23_arg_f(ctx, argc, argv, 2, 0.0f) / 255.0f;
        float a = v23_arg_f(ctx, argc, argv, 3, 1.0f);
        g->ops->clear(g->ops->user, r, gr, b, a);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_draw_map(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->draw_map) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, (float)V23_CANVAS_W);
        float h = v23_arg_f(ctx, argc, argv, 3, (float)V23_CANVAS_H);
        JSValueConst opt = (argc > 4) ? argv[4] : JS_UNDEFINED;
        v23_map_opts_t o;
        memset(&o, 0, sizeof(o));
        o.radius = v23_opt_f(ctx, opt, "radius", 0.0f);
        o.alpha = v23_opt_f(ctx, opt, "alpha", 1.0f);
        o.vignette = v23_opt_f(ctx, opt, "vignette", 0.0f);
        o.border_width = v23_opt_f(ctx, opt, "borderWidth", 0.0f);
        v23_rgba_t none = { 0, 0, 0, 0, 0 };
        o.border_col = v23_opt_col(ctx, opt, "borderColor", none);
        char shape[16];
        v23_opt_str(ctx, opt, "shape", shape, sizeof(shape), "rect");
        o.is_circle = (strcmp(shape, "circle") == 0);
        g->ops->draw_map(g->ops->user, x, y, w, h, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_draw_shader(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->draw_shader) {
        JSValueConst opt = (argc > 0) ? argv[0] : JS_UNDEFINED;
        v23_shader_opts_t o;
        o.alpha = v23_opt_f(ctx, opt, "alpha", 1.0f);
        char blend[16];
        v23_opt_str(ctx, opt, "blend", blend, sizeof(blend), "normal");
        o.blend_add = (strcmp(blend, "add") == 0);
        g->ops->draw_shader(g->ops->user, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_rect(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->rect) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 0.0f);
        float h = v23_arg_f(ctx, argc, argv, 3, 0.0f);
        JSValueConst opt = (argc > 4) ? argv[4] : JS_UNDEFINED;
        v23_rgba_t none = { 0, 0, 0, 0, 0 };
        v23_rect_opts_t o;
        memset(&o, 0, sizeof(o));
        o.fill = v23_opt_col(ctx, opt, "fill", none);
        o.fill2 = v23_opt_col(ctx, opt, "fill2", none);
        o.stroke = v23_opt_col(ctx, opt, "stroke", none);
        o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.fill.valid ? o.fill : o.stroke);
        o.line_width = v23_opt_f(ctx, opt, "lineWidth", 1.5f);
        o.radius = v23_opt_f(ctx, opt, "radius", 0.0f);
        o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
        char gd[8];
        v23_opt_str(ctx, opt, "gradDir", gd, sizeof(gd), "v");
        o.grad_h = (gd[0] == 'h' || gd[0] == 'H');
        g->ops->rect(g->ops->user, x, y, w, h, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_circle(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->circle) {
        float cx = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float cy = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float r  = v23_arg_f(ctx, argc, argv, 2, 1.0f);
        JSValueConst opt = (argc > 3) ? argv[3] : JS_UNDEFINED;
        v23_rgba_t none = { 0, 0, 0, 0, 0 };
        v23_circle_opts_t o;
        o.fill = v23_opt_col(ctx, opt, "fill", none);
        o.stroke = v23_opt_col(ctx, opt, "stroke", none);
        o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.fill.valid ? o.fill : o.stroke);
        o.line_width = v23_opt_f(ctx, opt, "lineWidth", 2.0f);
        o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
        g->ops->circle(g->ops->user, cx, cy, r, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_line(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->line) {
        float x1 = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y1 = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float x2 = v23_arg_f(ctx, argc, argv, 2, 0.0f);
        float y2 = v23_arg_f(ctx, argc, argv, 3, 0.0f);
        JSValueConst opt = (argc > 4) ? argv[4] : JS_UNDEFINED;
        v23_rgba_t white = { 1, 1, 1, 1, 1 };
        v23_line_opts_t o;
        o.color = v23_opt_col(ctx, opt, "color", white);
        o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.color);
        o.line_width = v23_opt_f(ctx, opt, "lineWidth", 2.0f);
        o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
        char cap[16];
        v23_opt_str(ctx, opt, "cap", cap, sizeof(cap), "round");
        o.cap_round = (strcmp(cap, "round") == 0);
        g->ops->line(g->ops->user, x1, y1, x2, y2, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_poly(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->poly && argc > 0 && JS_IsObject(argv[0])) {
        JSValue len_v = JS_GetPropertyStr(ctx, argv[0], "length");
        int32_t n = 0;
        JS_ToInt32(ctx, &n, len_v);
        JS_FreeValue(ctx, len_v);
        if (n >= 2) {
            if (n > V23_MAX_POLY_PTS) n = V23_MAX_POLY_PTS;
            float pts[V23_MAX_POLY_PTS * 2];
            int valid_n = 0;
            for (int i = 0; i < n; i++) {
                JSValue pt = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
                if (JS_IsObject(pt)) {
                    JSValue vx = JS_GetPropertyUint32(ctx, pt, 0);
                    JSValue vy = JS_GetPropertyUint32(ctx, pt, 1);
                    double dx = 0, dy = 0;
                    if (JS_ToFloat64(ctx, &dx, vx) == 0 && JS_ToFloat64(ctx, &dy, vy) == 0 &&
                        isfinite(dx) && isfinite(dy)) {
                        pts[valid_n * 2] = (float)dx;
                        pts[valid_n * 2 + 1] = (float)dy;
                        valid_n++;
                    }
                    JS_FreeValue(ctx, vx);
                    JS_FreeValue(ctx, vy);
                }
                JS_FreeValue(ctx, pt);
            }
            if (valid_n >= 2) {
                JSValueConst opt = (argc > 1) ? argv[1] : JS_UNDEFINED;
                v23_rgba_t none = { 0, 0, 0, 0, 0 };
                v23_circle_opts_t o;
                o.fill = v23_opt_col(ctx, opt, "fill", none);
                o.stroke = v23_opt_col(ctx, opt, "stroke", none);
                o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.fill.valid ? o.fill : o.stroke);
                o.line_width = v23_opt_f(ctx, opt, "lineWidth", 2.0f);
                o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
                g->ops->poly(g->ops->user, pts, valid_n, &o);
            }
        }
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_arc(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->arc) {
        float cx = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float cy = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float r  = v23_arg_f(ctx, argc, argv, 2, 10.0f);
        float sdeg = v23_arg_f(ctx, argc, argv, 3, 0.0f);
        float edeg = v23_arg_f(ctx, argc, argv, 4, 0.0f);
        JSValueConst opt = (argc > 5) ? argv[5] : JS_UNDEFINED;
        v23_rgba_t cyan = { 0.133f, 0.827f, 0.933f, 1.0f, 1 };
        v23_arc_opts_t o;
        o.color = v23_opt_col(ctx, opt, "color", cyan);
        o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.color);
        o.line_width = v23_opt_f(ctx, opt, "lineWidth", 12.0f);
        o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
        char cap[16];
        v23_opt_str(ctx, opt, "cap", cap, sizeof(cap), "butt");
        o.cap_round = (strcmp(cap, "round") == 0);
        g->ops->arc(g->ops->user, cx, cy, r, sdeg, edeg, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_ticks(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->ticks) {
        float cx = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float cy = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float r_in = v23_arg_f(ctx, argc, argv, 2, 80.0f);
        float r_out = v23_arg_f(ctx, argc, argv, 3, 96.0f);
        float sdeg = v23_arg_f(ctx, argc, argv, 4, 135.0f);
        float edeg = v23_arg_f(ctx, argc, argv, 5, 405.0f);
        int cnt = (int)v23_arg_f(ctx, argc, argv, 6, 30.0f);
        if (cnt < 1) cnt = 1;
        if (cnt > 180) cnt = 180;
        JSValueConst opt = (argc > 7) ? argv[7] : JS_UNDEFINED;
        v23_rgba_t def_c = { 1.0f, 1.0f, 1.0f, 0.45f, 1 };
        v23_rgba_t def_r = { 0.937f, 0.267f, 0.267f, 1.0f, 1 };
        v23_ticks_opts_t o;
        o.color = v23_opt_col(ctx, opt, "color", def_c);
        o.red_col = v23_opt_col(ctx, opt, "redColor", def_r);
        o.major_every = (int)v23_opt_f(ctx, opt, "majorEvery", 5.0f);
        if (o.major_every < 1) o.major_every = 5;
        o.major_len = v23_opt_f(ctx, opt, "majorLen", 8.0f);
        o.line_width = v23_opt_f(ctx, opt, "lineWidth", 1.5f);
        o.major_width = v23_opt_f(ctx, opt, "majorWidth", 3.0f);
        o.red_from_frac = v23_opt_f(ctx, opt, "redFromFrac", 2.0f);
        o.has_red = (o.red_from_frac >= 0.0f && o.red_from_frac <= 1.0f);
        g->ops->ticks(g->ops->user, cx, cy, r_in, r_out, sdeg, edeg, cnt, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_needle(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->needle) {
        float cx = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float cy = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float len = v23_arg_f(ctx, argc, argv, 2, 100.0f);
        float tail = v23_arg_f(ctx, argc, argv, 3, 20.0f);
        float deg = v23_arg_f(ctx, argc, argv, 4, 0.0f);
        JSValueConst opt = (argc > 5) ? argv[5] : JS_UNDEFINED;
        v23_rgba_t white = { 1, 1, 1, 1, 1 };
        v23_rgba_t red = { 1.0f, 0.23f, 0.30f, 1.0f, 1 };
        v23_rgba_t hub = { 0.067f, 0.094f, 0.153f, 1.0f, 1 };
        v23_needle_opts_t o;
        o.color = v23_opt_col(ctx, opt, "color", white);
        o.tip_col = v23_opt_col(ctx, opt, "tipColor", red);
        o.hub_col = v23_opt_col(ctx, opt, "hubColor", hub);
        o.width = v23_opt_f(ctx, opt, "width", 6.0f);
        o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
        o.hub_radius = v23_opt_f(ctx, opt, "hubRadius", 0.0f);
        g->ops->needle(g->ops->user, cx, cy, len, tail, deg, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_segbar(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->seg_bar) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 100.0f);
        float h = v23_arg_f(ctx, argc, argv, 3, 12.0f);
        float f = v23_arg_f(ctx, argc, argv, 4, 0.0f);
        JSValueConst opt = (argc > 5) ? argv[5] : JS_UNDEFINED;
        v23_rgba_t c_main = { 0.165f, 0.910f, 0.769f, 1.0f, 1 };
        v23_rgba_t c_warn = { 1.0f, 0.737f, 0.204f, 1.0f, 1 };
        v23_rgba_t c_red  = { 1.0f, 0.220f, 0.282f, 1.0f, 1 };
        v23_segbar_opts_t o;
        o.segs = (int)v23_opt_f(ctx, opt, "segs", 18.0f);
        if (o.segs < 4) o.segs = 4;
        if (o.segs > 48) o.segs = 48;
        o.red_segs = (int)v23_opt_f(ctx, opt, "redSegs", 4.0f);
        o.pulse = v23_opt_bool(ctx, opt, "pulse", 0);
        o.color = v23_opt_col(ctx, opt, "color", c_main);
        o.warn_col = v23_opt_col(ctx, opt, "warnColor", c_warn);
        o.red_col = v23_opt_col(ctx, opt, "redColor", c_red);
        g->ops->seg_bar(g->ops->user, x, y, w, h, f, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_bibar(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->bi_bar) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 100.0f);
        float h = v23_arg_f(ctx, argc, argv, 3, 10.0f);
        float f = v23_arg_f(ctx, argc, argv, 4, 0.0f);
        JSValueConst opt = (argc > 5) ? argv[5] : JS_UNDEFINED;
        v23_rgba_t pos = { 0.251f, 0.769f, 1.0f, 1.0f, 1 };
        v23_rgba_t neg = { 0.984f, 0.573f, 0.235f, 1.0f, 1 };
        v23_bibar_opts_t o;
        o.pos_col = v23_opt_col(ctx, opt, "posColor", pos);
        o.neg_col = v23_opt_col(ctx, opt, "negColor", neg);
        g->ops->bi_bar(g->ops->user, x, y, w, h, f, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_rangebar(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->range_bar) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 100.0f);
        float h = v23_arg_f(ctx, argc, argv, 3, 10.0f);
        float f = v23_arg_f(ctx, argc, argv, 4, 0.0f);
        JSValueConst opt = (argc > 5) ? argv[5] : JS_UNDEFINED;
        v23_rgba_t grn = { 0.267f, 0.894f, 0.596f, 1.0f, 1 };
        v23_rangebar_opts_t o;
        o.color = v23_opt_col(ctx, opt, "color", grn);
        o.tick_lo = v23_opt_f(ctx, opt, "tickLo", -1.0f);
        o.tick_hi = v23_opt_f(ctx, opt, "tickHi", -1.0f);
        g->ops->range_bar(g->ops->user, x, y, w, h, f, &o);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_text(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->text && argc > 0) {
        const char *str = JS_ToCString(ctx, argv[0]);
        if (str) {
            float x = v23_arg_f(ctx, argc, argv, 1, 0.0f);
            float y = v23_arg_f(ctx, argc, argv, 2, 0.0f);
            JSValueConst opt = (argc > 3) ? argv[3] : JS_UNDEFINED;
            v23_rgba_t white = { 1, 1, 1, 1, 1 };
            v23_text_opts_t o;
            memset(&o, 0, sizeof(o));
            o.size = v23_opt_f(ctx, opt, "size", 24.0f);
            if (o.size < 8.0f) o.size = 8.0f;
            if (o.size > 200.0f) o.size = 200.0f;
            o.bold = v23_opt_bool(ctx, opt, "bold", 0);
            o.italic = v23_opt_bool(ctx, opt, "italic", 0);
            o.glow = v23_opt_f(ctx, opt, "glow", 0.0f);
            o.max_width = v23_opt_f(ctx, opt, "maxWidth", 0.0f);
            o.color = v23_opt_col(ctx, opt, "color", white);
            o.glow_col = v23_opt_col(ctx, opt, "glowColor", o.color);
            char al[16], bl[16];
            v23_opt_str(ctx, opt, "align", al, sizeof(al), "left");
            v23_opt_str(ctx, opt, "baseline", bl, sizeof(bl), "alphabetic");
            o.align = (strcmp(al, "center") == 0) ? 1 : (strcmp(al, "right") == 0 ? 2 : 0);
            o.baseline = (strcmp(bl, "middle") == 0) ? 1 : (strcmp(bl, "top") == 0 ? 2 : 0);
            g->ops->text(g->ops->user, str, x, y, &o);
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_dash_box(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->draw_dash_box) {
        float x = v23_arg_f(ctx, argc, argv, 0, 5.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 200.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 295.0f);
        int side = 0;
        if (argc > 3 && JS_IsString(argv[3])) {
            const char *s = JS_ToCString(ctx, argv[3]);
            if (s) { side = (strcmp(s, "right") == 0) ? 1 : 0; JS_FreeCString(ctx, s); }
        }
        g->ops->draw_dash_box(g->ops->user, x, y, w, side);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_nav_card(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->draw_nav_card) {
        float x = v23_arg_f(ctx, argc, argv, 0, 1620.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 200.0f);
        float w = v23_arg_f(ctx, argc, argv, 2, 295.0f);
        float h = v23_arg_f(ctx, argc, argv, 3, 308.0f);
        JSValueConst opt = (argc > 4) ? argv[4] : JS_UNDEFINED;
        int alpha = (int)v23_opt_f(ctx, opt, "alpha", 205.0f);
        g->ops->draw_nav_card(g->ops->user, x, y, w, h, alpha);
    }
    return JS_UNDEFINED;
}

static JSValue v23_fn_turn_arrow(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
    (void)th;
    v23_guard_t *g = (v23_guard_t *)JS_GetContextOpaque(ctx);
    if (!g) return JS_UNDEFINED;
    g->draw_calls++;
    if (g->ops && g->ops->draw_turn_arrow) {
        float x = v23_arg_f(ctx, argc, argv, 0, 0.0f);
        float y = v23_arg_f(ctx, argc, argv, 1, 0.0f);
        float sz = v23_arg_f(ctx, argc, argv, 2, 48.0f);
        char tp[24];
        snprintf(tp, sizeof(tp), "straight");
        if (argc > 3 && JS_IsString(argv[3])) {
            const char *s = JS_ToCString(ctx, argv[3]);
            if (s) { snprintf(tp, sizeof(tp), "%s", s); JS_FreeCString(ctx, s); }
        }
        JSValueConst opt = (argc > 4) ? argv[4] : JS_UNDEFINED;
        v23_rgba_t white = { 1, 1, 1, 1, 1 };
        v23_rgba_t col = v23_opt_col(ctx, opt, "color", white);
        g->ops->draw_turn_arrow(g->ops->user, x, y, sz, tp, col);
    }
    return JS_UNDEFINED;
}

static JSValue v23_build_draw_ctx(JSContext *ctx) {
    static const struct { const char *name; JSCFunction *fn; int argc; } tab[] = {
        { "clear",         v23_fn_clear,      4 },
        { "drawMap",       v23_fn_draw_map,   5 },
        { "drawShader",    v23_fn_draw_shader,1 },
        { "rect",          v23_fn_rect,       5 },
        { "circle",        v23_fn_circle,     4 },
        { "line",          v23_fn_line,       5 },
        { "poly",          v23_fn_poly,       2 },
        { "arc",           v23_fn_arc,        6 },
        { "ticks",         v23_fn_ticks,      8 },
        { "needle",        v23_fn_needle,     6 },
        { "segBar",        v23_fn_segbar,     6 },
        { "biBar",         v23_fn_bibar,      6 },
        { "rangeBar",      v23_fn_rangebar,   6 },
        { "text",          v23_fn_text,       4 },
        { "drawDashBox",   v23_fn_dash_box,   4 },
        { "drawNavCard",   v23_fn_nav_card,   5 },
        { "drawTurnArrow", v23_fn_turn_arrow, 5 },
    };
    JSValue obj = JS_NewObject(ctx);
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        JS_SetPropertyStr(ctx, obj, tab[i].name, JS_NewCFunction(ctx, tab[i].fn, tab[i].name, tab[i].argc));
    }
    return obj;
}

static JSValue v23_marshal_bus(JSContext *ctx, const v23_bus_t *in) {
    JSValue b = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, b, "rpm",         JS_NewFloat64(ctx, in->rpm));
    JS_SetPropertyStr(ctx, b, "rpmRaw",      JS_NewFloat64(ctx, in->rpm_raw));
    JS_SetPropertyStr(ctx, b, "rpmPeak",     JS_NewFloat64(ctx, in->rpm_peak));
    JS_SetPropertyStr(ctx, b, "speed",       JS_NewFloat64(ctx, in->speed));
    JS_SetPropertyStr(ctx, b, "speedRaw",    JS_NewFloat64(ctx, in->speed_raw));
    JS_SetPropertyStr(ctx, b, "power",       JS_NewFloat64(ctx, in->power));
    JS_SetPropertyStr(ctx, b, "torque",      JS_NewFloat64(ctx, in->torque));
    JS_SetPropertyStr(ctx, b, "pedal",       JS_NewFloat64(ctx, in->pedal));
    JS_SetPropertyStr(ctx, b, "brake",       JS_NewFloat64(ctx, in->brake));
    JS_SetPropertyStr(ctx, b, "steer",       JS_NewFloat64(ctx, in->steer));
    JS_SetPropertyStr(ctx, b, "gear",        JS_NewString(ctx, in->gear[0] ? in->gear : "-"));
    JS_SetPropertyStr(ctx, b, "gearNum",     JS_NewInt32(ctx, in->gear_num));
    JS_SetPropertyStr(ctx, b, "g",           JS_NewFloat64(ctx, in->g));
    JS_SetPropertyStr(ctx, b, "glong",       JS_NewFloat64(ctx, in->glong));
    JS_SetPropertyStr(ctx, b, "glat",        JS_NewFloat64(ctx, in->glat));
    JS_SetPropertyStr(ctx, b, "oil",         JS_NewFloat64(ctx, in->oil));
    JS_SetPropertyStr(ctx, b, "gearbox",     JS_NewFloat64(ctx, in->gearbox));
    JS_SetPropertyStr(ctx, b, "coolant",     JS_NewFloat64(ctx, in->coolant));
    JS_SetPropertyStr(ctx, b, "outside",     JS_NewFloat64(ctx, in->outside));
    JS_SetPropertyStr(ctx, b, "cons",        JS_NewFloat64(ctx, in->cons));
    JS_SetPropertyStr(ctx, b, "fuel",        JS_NewFloat64(ctx, in->fuel));
    JS_SetPropertyStr(ctx, b, "range",       JS_NewFloat64(ctx, in->range));
    JS_SetPropertyStr(ctx, b, "pitch",       JS_NewFloat64(ctx, in->pitch));
    JS_SetPropertyStr(ctx, b, "roll",        JS_NewFloat64(ctx, in->roll));
    JS_SetPropertyStr(ctx, b, "alt",         JS_NewFloat64(ctx, in->alt));
    JS_SetPropertyStr(ctx, b, "tpfl",        JS_NewFloat64(ctx, in->tpfl));
    JS_SetPropertyStr(ctx, b, "tpfr",        JS_NewFloat64(ctx, in->tpfr));
    JS_SetPropertyStr(ctx, b, "tprl",        JS_NewFloat64(ctx, in->tprl));
    JS_SetPropertyStr(ctx, b, "tprr",        JS_NewFloat64(ctx, in->tprr));
    JS_SetPropertyStr(ctx, b, "tires",       JS_NewString(ctx, in->tires[0] ? in->tires : "--"));
    JS_SetPropertyStr(ctx, b, "t100",        JS_NewFloat64(ctx, in->t100));
    JS_SetPropertyStr(ctx, b, "t100Running", JS_NewBool(ctx, in->t100_running));
    JS_SetPropertyStr(ctx, b, "pmax",        JS_NewFloat64(ctx, in->pmax));
    JS_SetPropertyStr(ctx, b, "tqmax",       JS_NewFloat64(ctx, in->tqmax));
    JS_SetPropertyStr(ctx, b, "gmax",        JS_NewFloat64(ctx, in->gmax));
    JS_SetPropertyStr(ctx, b, "trip",        JS_NewFloat64(ctx, in->trip));
    JS_SetPropertyStr(ctx, b, "tripdist",    JS_NewFloat64(ctx, in->tripdist));
    JS_SetPropertyStr(ctx, b, "tripspeed",   JS_NewFloat64(ctx, in->tripspeed));
    JS_SetPropertyStr(ctx, b, "triptime",    JS_NewString(ctx, in->triptime));
    JS_SetPropertyStr(ctx, b, "refuel",      JS_NewFloat64(ctx, in->refuel));
    JS_SetPropertyStr(ctx, b, "refueldist",  JS_NewFloat64(ctx, in->refueldist));
    JS_SetPropertyStr(ctx, b, "refuelspeed", JS_NewFloat64(ctx, in->refuelspeed));
    JS_SetPropertyStr(ctx, b, "refueltime",  JS_NewString(ctx, in->refueltime));
    JS_SetPropertyStr(ctx, b, "total",       JS_NewFloat64(ctx, in->total));
    JS_SetPropertyStr(ctx, b, "totaldist",   JS_NewFloat64(ctx, in->totaldist));
    JS_SetPropertyStr(ctx, b, "totalspeed",  JS_NewFloat64(ctx, in->totalspeed));
    JS_SetPropertyStr(ctx, b, "totaltime",   JS_NewString(ctx, in->totaltime));
    JS_SetPropertyStr(ctx, b, "tripkm",      JS_NewFloat64(ctx, in->tripkm));

    JSValue ready = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ready, "oil",     JS_NewBool(ctx, in->ready_oil));
    JS_SetPropertyStr(ctx, ready, "gearbox", JS_NewBool(ctx, in->ready_gearbox));
    JS_SetPropertyStr(ctx, ready, "tires",   JS_NewBool(ctx, in->ready_tires));
    JS_SetPropertyStr(ctx, ready, "alt",     JS_NewBool(ctx, in->ready_alt));
    JS_SetPropertyStr(ctx, ready, "t100",    JS_NewBool(ctx, in->ready_t100));
    JS_SetPropertyStr(ctx, b, "ready", ready);

    JSValue nav = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, nav, "live",      JS_NewBool(ctx, in->nav_live));
    JS_SetPropertyStr(ctx, nav, "distM",     JS_NewInt32(ctx, in->nav_dist_m));
    JS_SetPropertyStr(ctx, nav, "remDistKm", JS_NewFloat64(ctx, in->nav_rem_dist_km));
    JS_SetPropertyStr(ctx, nav, "remMin",    JS_NewInt32(ctx, in->nav_rem_min));
    JS_SetPropertyStr(ctx, nav, "eta",       JS_NewString(ctx, in->nav_eta));
    JS_SetPropertyStr(ctx, nav, "road",      JS_NewString(ctx, in->nav_road));
    JS_SetPropertyStr(ctx, nav, "nextRoad",  JS_NewString(ctx, in->nav_next_road));
    JS_SetPropertyStr(ctx, nav, "action",    JS_NewString(ctx, in->nav_action));
    JS_SetPropertyStr(ctx, nav, "turnType",  JS_NewString(ctx, in->nav_turn_type[0] ? in->nav_turn_type : "straight"));
    JS_SetPropertyStr(ctx, nav, "lanes",     JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, b, "nav", nav);

    JSValue sys = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, sys, "mapLive", JS_NewBool(ctx, in->sys_map_live));
    JS_SetPropertyStr(ctx, sys, "night",   JS_NewBool(ctx, in->sys_night));
    JS_SetPropertyStr(ctx, sys, "acc",     JS_NewBool(ctx, in->sys_acc));
    JS_SetPropertyStr(ctx, sys, "time",    JS_NewFloat64(ctx, in->sys_time));
    JS_SetPropertyStr(ctx, sys, "dt",      JS_NewFloat64(ctx, in->sys_dt));
    JS_SetPropertyStr(ctx, sys, "fps",     JS_NewInt32(ctx, in->sys_fps));
    JS_SetPropertyStr(ctx, sys, "mapFps",  JS_NewInt32(ctx, in->sys_map_fps));
    JS_SetPropertyStr(ctx, sys, "width",   JS_NewInt32(ctx, V23_CANVAS_W));
    JS_SetPropertyStr(ctx, sys, "height",  JS_NewInt32(ctx, V23_CANVAS_H));
    JS_SetPropertyStr(ctx, b, "sys", sys);

    JSValue conf = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, conf, "show",     JS_NewBool(ctx, in->conf_show));
    JS_SetPropertyStr(ctx, conf, "r_show",   JS_NewBool(ctx, in->conf_r_show));
    JS_SetPropertyStr(ctx, conf, "bars",     JS_NewBool(ctx, in->conf_bars));
    JS_SetPropertyStr(ctx, conf, "alpha",    JS_NewInt32(ctx, in->conf_alpha));
    JS_SetPropertyStr(ctx, conf, "nav_box",  JS_NewInt32(ctx, in->conf_nav_box));
    JS_SetPropertyStr(ctx, conf, "tank_l",   JS_NewInt32(ctx, in->conf_tank_l));
    JS_SetPropertyStr(ctx, conf, "nav_side", JS_NewString(ctx, in->conf_nav_side[0] ? in->conf_nav_side : "right"));
    JS_SetPropertyStr(ctx, b, "conf", conf);

    return b;
}

static void v23_extract_js_error(JSContext *ctx, v23_guard_t *g, char *out, size_t cap) {
    if (g && g->timed_out) {
        snprintf(out, cap, "单帧执行超时 (>35ms / 死循环)，已被 QuickJS 安全中断");
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
        return;
    }
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    const char *st = JS_IsUndefined(stack) ? NULL : JS_ToCString(ctx, stack);
    if (st && st[0]) {
        char line1[200];
        size_t i = 0;
        while (st[i] && st[i] != '\n' && i + 1 < sizeof(line1)) { line1[i] = st[i]; i++; }
        line1[i] = '\0';
        snprintf(out, cap, "%s [%s]", msg ? msg : "JS error", line1);
    } else {
        snprintf(out, cap, "%s", msg ? msg : "JS error");
    }
    if (msg) JS_FreeCString(ctx, msg);
    if (st)  JS_FreeCString(ctx, st);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
}

static int v23_sandbox_init(v23_sandbox_t *sb, const v23_draw_ops_t *ops) {
    memset(sb, 0, sizeof(*sb));
    sb->guard.ops = ops;
    sb->rt = JS_NewRuntime2(&g_v23_mf, NULL);
    if (!sb->rt) return -1;
    JS_SetMemoryLimit(sb->rt, V23_JS_MEM_LIMIT_BYTES);
    JS_SetMaxStackSize(sb->rt, V23_JS_STACK_LIMIT_BYTES);
    JS_SetInterruptHandler(sb->rt, v23_interrupt_cb, &sb->guard);

    sb->ctx = JS_NewContext(sb->rt);
    if (!sb->ctx) {
        JS_FreeRuntime(sb->rt);
        sb->rt = NULL;
        return -1;
    }
    JS_SetContextOpaque(sb->ctx, &sb->guard);

    /* 删除 eval / Function，物理阻断动态代码执行 */
    JSValue glob = JS_GetGlobalObject(sb->ctx);
    JSAtom a_eval = JS_NewAtom(sb->ctx, "eval");
    JS_DeleteProperty(sb->ctx, glob, a_eval, 0);
    JS_FreeAtom(sb->ctx, a_eval);
    JSAtom a_fn = JS_NewAtom(sb->ctx, "Function");
    JS_DeleteProperty(sb->ctx, glob, a_fn, 0);
    JS_FreeAtom(sb->ctx, a_fn);
    JS_FreeValue(sb->ctx, glob);

    sb->draw_ctx = v23_build_draw_ctx(sb->ctx);
    sb->theme_state = JS_NewObject(sb->ctx);
    sb->entries = JS_UNDEFINED;
    return 0;
}

static void v23_sandbox_destroy(v23_sandbox_t *sb) {
    if (!sb->ctx) return;
    JS_FreeValue(sb->ctx, sb->entries);
    JS_FreeValue(sb->ctx, sb->theme_state);
    JS_FreeValue(sb->ctx, sb->draw_ctx);
    JS_FreeContext(sb->ctx);
    JS_FreeRuntime(sb->rt);
    memset(sb, 0, sizeof(*sb));
}

static int v23_sandbox_load_code(v23_sandbox_t *sb, const char *filename, const char *code, size_t clen) {
    if (!code || clen == 0 || clen > V23_MAX_THEME_BYTES) {
        snprintf(sb->last_err, sizeof(sb->last_err),
                 "皮肤文件大小不合法 (%lu B，限制 1 B ~ %u B)",
                 (unsigned long)clen, V23_MAX_THEME_BYTES);
        return -1;
    }
    static const char tail[] =
        "\n;(function(){\n"
        "  var out = [];\n"
        "  if (typeof THEMES !== 'undefined' && Array.isArray(THEMES) && THEMES.length > 0) {\n"
        "    for (var i = 0; i < THEMES.length && i < 8; i++) {\n"
        "      var it = THEMES[i];\n"
        "      if (!it || typeof it.render !== 'function') throw new Error('THEMES[' + i + '] 缺少 render 函数');\n"
        "      out.push({ id: String(it.id || ('skin_' + i)), name: String(it.name || ('自定义皮肤 ' + (i+1))), author: String(it.author || ''), shader: String(it.shader || ''), render: it.render });\n"
        "    }\n"
        "  } else if (typeof render === 'function') {\n"
        "    var m = (typeof THEME === 'object' && THEME) ? THEME : {};\n"
        "    out.push({ id: String(m.id || 'custom'), name: String(m.name || '自定义皮肤'), author: String(m.author || ''), shader: String(m.shader || ''), render: render });\n"
        "  } else {\n"
        "    throw new Error('未找到 THEMES 数组或 render(ctx, bus, state) 函数');\n"
        "  }\n"
        "  return out;\n"
        "})();\n";
    size_t total = clen + sizeof(tail) - 1;
    char *wrapped = (char *)malloc(total + 1);
    if (!wrapped) {
        snprintf(sb->last_err, sizeof(sb->last_err), "内存不足");
        return -1;
    }
    memcpy(wrapped, code, clen);
    memcpy(wrapped + clen, tail, sizeof(tail));

    uint64_t t0 = v23_now_us();
    sb->guard.timed_out = 0;
    sb->guard.deadline_us = t0 + V23_LOAD_TIMEOUT_US;
    JSValue val = JS_Eval(sb->ctx, wrapped, total, filename ? filename : "theme.js", JS_EVAL_TYPE_GLOBAL);
    sb->guard.deadline_us = 0;
    sb->last_compile_ms = (double)(v23_now_us() - t0) / 1000.0;
    free(wrapped);

    if (JS_IsException(val)) {
        v23_extract_js_error(sb->ctx, &sb->guard, sb->last_err, sizeof(sb->last_err));
        return -1;
    }
    JS_FreeValue(sb->ctx, sb->entries);
    sb->entries = val;
    JSValue len_v = JS_GetPropertyStr(sb->ctx, val, "length");
    int32_t cnt = 0;
    JS_ToInt32(sb->ctx, &cnt, len_v);
    JS_FreeValue(sb->ctx, len_v);
    if (cnt < 1) {
        snprintf(sb->last_err, sizeof(sb->last_err), "未解析到有效皮肤");
        return -1;
    }
    if (cnt > V23_MAX_THEMES_PER_FILE) cnt = V23_MAX_THEMES_PER_FILE;
    sb->entry_count = cnt;
    sb->active_idx = 0;
    for (int i = 0; i < cnt; i++) {
        JSValue ent = JS_GetPropertyUint32(sb->ctx, val, (uint32_t)i);
        v23_opt_str(sb->ctx, ent, "id", sb->metas[i].id, sizeof(sb->metas[i].id), "custom");
        v23_opt_str(sb->ctx, ent, "name", sb->metas[i].name, sizeof(sb->metas[i].name), "自定义皮肤");
        v23_opt_str(sb->ctx, ent, "author", sb->metas[i].author, sizeof(sb->metas[i].author), "");
        v23_opt_str(sb->ctx, ent, "shader", sb->metas[i].shader, sizeof(sb->metas[i].shader), "");
        sb->metas[i].has_shader = (sb->metas[i].shader[0] != '\0');
        JS_FreeValue(sb->ctx, ent);
    }
    sb->last_err[0] = '\0';
    return 0;
}

static int v23_sandbox_render_frame(v23_sandbox_t *sb, const v23_bus_t *bus_data, uint64_t *out_us, uint32_t *out_calls) {
    if (!sb->ctx || sb->entry_count <= 0) return -1;
    int idx = sb->active_idx;
    if (idx < 0 || idx >= sb->entry_count) idx = 0;
    JSValue ent = JS_GetPropertyUint32(sb->ctx, sb->entries, (uint32_t)idx);
    JSValue fn  = JS_GetPropertyStr(sb->ctx, ent, "render");
    JSValue bus = v23_marshal_bus(sb->ctx, bus_data);
    JSValue argv[3] = { sb->draw_ctx, bus, sb->theme_state };

    sb->guard.draw_calls = 0;
    sb->guard.timed_out = 0;
    uint64_t t0 = v23_now_us();
    sb->guard.deadline_us = t0 + V23_FRAME_TIMEOUT_US;

    JSValue ret = JS_Call(sb->ctx, fn, ent, 3, argv);
    uint64_t elapsed = v23_now_us() - t0;
    sb->guard.deadline_us = 0;

    JS_FreeValue(sb->ctx, bus);
    JS_FreeValue(sb->ctx, fn);
    JS_FreeValue(sb->ctx, ent);

    if (out_us) *out_us = elapsed;
    if (out_calls) *out_calls = sb->guard.draw_calls;

    if (JS_IsException(ret)) {
        v23_extract_js_error(sb->ctx, &sb->guard, sb->last_err, sizeof(sb->last_err));
        return -1;
    }
    JS_FreeValue(sb->ctx, ret);
    return 0;
}

/* JSON 字符串安全转义（写 /tmp/theme_status.json 用） */
static inline void v23_json_escape(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    if (!src) { if (cap) dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && o + 6 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[o++] = '\\'; dst[o++] = (char)c; }
        else if (c == '\n') { dst[o++] = '\\'; dst[o++] = 'n'; }
        else if (c == '\r') { /* skip */ }
        else if (c < 0x20) { dst[o++] = ' '; }
        else dst[o++] = (char)c;
    }
    if (cap) dst[o] = '\0';
}

#endif /* V23_SANDBOX_H */
