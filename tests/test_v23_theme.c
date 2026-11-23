/*
 * test_v23_theme.c — Host C unit tests for v23 QuickJS 60fps theme sandbox (ASan + UBSan).
 *
 * Tests:
 *   1. All 17 ctx.* draw op bindings + CSS color parsing (#rgb/#rrggbb/#rrggbbaa/rgba())
 *   2. Built-in V23_DEFAULT_CLASSIC_JS (CarPlay live + idle mode)
 *   3. Multi-skin THEMES[] single-file parsing & switching + 72KB payload + 140KB oversize reject
 *   4. Cold-start NaN/-999 sanitization + bus.ready.* flags (.toFixed() never throws)
 *   5. 6 safety fuses: syntax error, ReferenceError, 35ms infinite loop kill,
 *      4MB memory bomb OOM kill, 256KB stack overflow kill, blocked eval()/Function()
 *   6. Same-frame zero-black-screen fallback recovery
 *   7. CarPlay RGBX luminance auto day/night hysteresis
 *   8. VW OEM gear decoding (incl. Eco E / Hybrid B / unready "-"), iAP2 maneuver mapping & arrow ops,
 *      fuel litres conversion, 13 board-computer fields, ETA/remMin rounding, per-wheel tire summary
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "v23_sandbox.h"

typedef struct {
    int clear_n, map_n, shader_n, rect_n, circle_n, line_n, poly_n, arc_n;
    int ticks_n, needle_n, seg_n, bi_n, range_n, text_n, dash_n, nav_n, arrow_n;
    float last_r, last_g, last_b, last_a;
    char last_text[128];
    char last_arrow[32];
} mock_rec_t;

static void m_clear(void *u, float r, float g, float b, float a) { mock_rec_t *m = (mock_rec_t *)u; m->clear_n++; m->last_r = r; m->last_g = g; m->last_b = b; m->last_a = a; }
static void m_map(void *u, float x, float y, float w, float h, const v23_map_opts_t *o) { (void)x;(void)y;(void)w;(void)h;(void)o; ((mock_rec_t *)u)->map_n++; }
static void m_shader(void *u, const v23_shader_opts_t *o) { (void)o; ((mock_rec_t *)u)->shader_n++; }
static void m_rect(void *u, float x, float y, float w, float h, const v23_rect_opts_t *o) { (void)x;(void)y;(void)w;(void)h;(void)o; ((mock_rec_t *)u)->rect_n++; }
static void m_circle(void *u, float cx, float cy, float r, const v23_circle_opts_t *o) { (void)cx;(void)cy;(void)r;(void)o; ((mock_rec_t *)u)->circle_n++; }
static void m_line(void *u, float x1, float y1, float x2, float y2, const v23_line_opts_t *o) { (void)x1;(void)y1;(void)x2;(void)y2;(void)o; ((mock_rec_t *)u)->line_n++; }
static void m_poly(void *u, const float *xy, int n, const v23_circle_opts_t *o) { (void)xy;(void)n;(void)o; ((mock_rec_t *)u)->poly_n++; }
static void m_arc(void *u, float cx, float cy, float r, float s, float e, const v23_arc_opts_t *o) { (void)cx;(void)cy;(void)r;(void)s;(void)e;(void)o; ((mock_rec_t *)u)->arc_n++; }
static void m_ticks(void *u, float cx, float cy, float ri, float ro, float s, float e, int n, const v23_ticks_opts_t *o) { (void)cx;(void)cy;(void)ri;(void)ro;(void)s;(void)e;(void)n;(void)o; ((mock_rec_t *)u)->ticks_n++; }
static void m_needle(void *u, float cx, float cy, float l, float t, float d, const v23_needle_opts_t *o) { (void)cx;(void)cy;(void)l;(void)t;(void)d;(void)o; ((mock_rec_t *)u)->needle_n++; }
static void m_seg(void *u, float x, float y, float w, float h, float f, const v23_segbar_opts_t *o) { (void)x;(void)y;(void)w;(void)h;(void)f;(void)o; ((mock_rec_t *)u)->seg_n++; }
static void m_bi(void *u, float x, float y, float w, float h, float f, const v23_bibar_opts_t *o) { (void)x;(void)y;(void)w;(void)h;(void)f;(void)o; ((mock_rec_t *)u)->bi_n++; }
static void m_rng(void *u, float x, float y, float w, float h, float f, const v23_rangebar_opts_t *o) { (void)x;(void)y;(void)w;(void)h;(void)f;(void)o; ((mock_rec_t *)u)->range_n++; }
static void m_text(void *u, const char *s, float x, float y, const v23_text_opts_t *o) { (void)x;(void)y;(void)o; mock_rec_t *m = (mock_rec_t *)u; m->text_n++; snprintf(m->last_text, sizeof(m->last_text), "%s", s); }
static void m_dash(void *u, float x, float y, float w, int side) { (void)x;(void)y;(void)w;(void)side; ((mock_rec_t *)u)->dash_n++; }
static void m_nav(void *u, float x, float y, float w, float h, int a) { (void)x;(void)y;(void)w;(void)h;(void)a; ((mock_rec_t *)u)->nav_n++; }
static void m_arrow(void *u, float x, float y, float sz, const char *tp, v23_rgba_t c) { (void)x;(void)y;(void)sz;(void)c; mock_rec_t *m = (mock_rec_t *)u; m->arrow_n++; snprintf(m->last_arrow, sizeof(m->last_arrow), "%s", tp ? tp : ""); }

static v23_draw_ops_t make_ops(mock_rec_t *m) {
    v23_draw_ops_t ops = {
        m, m_clear, m_map, m_shader, m_rect, m_circle, m_line, m_poly,
        m_arc, m_ticks, m_needle, m_seg, m_bi, m_rng, m_text, m_dash, m_nav, m_arrow
    };
    return ops;
}

static v23_bus_t make_bus(int map_live, int cold_start) {
    v23_bus_t b;
    memset(&b, 0, sizeof(b));
    b.rpm = 3200; b.rpm_raw = 3200; b.rpm_peak = 3600;
    b.speed = 88; b.speed_raw = 88;
    b.power = 120; b.torque = 380; b.pedal = 45;
    snprintf(b.gear, sizeof(b.gear), "D5"); b.gear_num = 5;
    b.oil = v23_sanitize_num(cold_start ? NAN : 95.0, &b.ready_oil);
    b.gearbox = v23_sanitize_num(cold_start ? -999.0 : 80.0, &b.ready_gearbox);
    b.tpfl = v23_sanitize_num(cold_start ? NAN : 2.4, &b.ready_tires);
    b.alt = v23_sanitize_num(cold_start ? -999.0 : 120.0, &b.ready_alt);
    b.t100 = v23_sanitize_num(cold_start ? NAN : 6.2, &b.ready_t100);
    b.nav_live = 1; b.nav_dist_m = 200; b.nav_rem_dist_km = 5.4; b.nav_rem_min = 9;
    snprintf(b.nav_eta, sizeof(b.nav_eta), "12:15");
    snprintf(b.nav_road, sizeof(b.nav_road), "滨江大道");
    snprintf(b.nav_action, sizeof(b.nav_action), "前方右转");
    snprintf(b.nav_turn_type, sizeof(b.nav_turn_type), "right");
    b.sys_map_live = map_live; b.sys_night = 1; b.sys_acc = 1; b.sys_fps = 60; b.sys_map_fps = 20; b.sys_dt = 1.0 / 60.0;
    b.conf_show = 1; b.conf_r_show = 1; b.conf_bars = 1; b.conf_alpha = 205; b.conf_nav_box = 1; b.conf_tank_l = 90;
    snprintf(b.conf_nav_side, sizeof(b.conf_nav_side), "right");
    return b;
}

static int assert_fault(const char *name, const char *code, int expect_load_fail) {
    mock_rec_t rec; memset(&rec, 0, sizeof(rec));
    v23_draw_ops_t ops = make_ops(&rec);
    v23_sandbox_t sb;
    if (v23_sandbox_init(&sb, &ops) != 0) return 1;
    int lrc = v23_sandbox_load_code(&sb, "bad.js", code, strlen(code));
    if (expect_load_fail) {
        if (lrc == 0) { fprintf(stderr, "FAIL %s: expected compile reject\n", name); v23_sandbox_destroy(&sb); return 1; }
        v23_sandbox_destroy(&sb);
        return 0;
    }
    if (lrc != 0) { fprintf(stderr, "FAIL %s: unexpected load fail: %s\n", name, sb.last_err); v23_sandbox_destroy(&sb); return 1; }
    v23_bus_t bus = make_bus(1, 0);
    int rrc = v23_sandbox_render_frame(&sb, &bus, NULL, NULL);
    if (rrc == 0) { fprintf(stderr, "FAIL %s: expected runtime fuse\n", name); v23_sandbox_destroy(&sb); return 1; }
    v23_sandbox_destroy(&sb);
    return 0;
}

int main(void) {
    int fails = 0;

    /* 1. Color parser tests */
    {
        v23_rgba_t none = { 0, 0, 0, 0, 0 };
        v23_rgba_t c1 = v23_parse_color("#22d3ee", none);
        v23_rgba_t c2 = v23_parse_color("#f08", none);
        v23_rgba_t c3 = v23_parse_color("rgba(10, 20, 30, 0.5)", none);
        if (!c1.valid || fabsf(c1.r - 34.0f/255.0f) > 0.01f || !c2.valid || fabsf(c3.a - 0.5f) > 0.01f) {
            fprintf(stderr, "FAIL color parser\n"); fails++;
        }
    }

    /* 2. All 17 ctx.* APIs + built-in default classic */
    {
        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        const char *all17 =
            "const THEME={id:'all17',name:'全接口验证',shader:'precision mediump float; void main(){gl_FragColor=vec4(1.0);}'};\n"
            "function render(ctx, bus) {\n"
            "  ctx.clear(10,20,30,1);\n"
            "  ctx.drawMap(0,0,1920,639,{radius:12,vignette:0.3});\n"
            "  ctx.drawShader({alpha:0.8,blend:'add'});\n"
            "  ctx.rect(10,160,200,100,{fill:'#112233',fill2:'#223344',radius:10,stroke:'#fff'});\n"
            "  ctx.circle(960,320,120,{fill:'rgba(0,0,0,0.5)',stroke:'#22d3ee'});\n"
            "  ctx.line(100,200,300,200,{color:'#fff',lineWidth:3});\n"
            "  ctx.poly([[100,100],[150,120],[120,180]],{fill:'#f00',stroke:'#fff'});\n"
            "  ctx.arc(960,320,100,135,360,{color:'#22d3ee',lineWidth:12,cap:'round'});\n"
            "  ctx.ticks(960,320,80,95,135,405,30,{redFromFrac:0.8});\n"
            "  ctx.needle(960,320,90,20,220,{hubRadius:14});\n"
            "  ctx.segBar(400,520,300,14,0.7,{segs:16,redSegs:4,pulse:true});\n"
            "  ctx.biBar(400,540,300,10,-0.4,{});\n"
            "  ctx.rangeBar(400,560,300,10,0.6,{tickLo:0.3,tickHi:0.8});\n"
            "  ctx.text('途锐 ' + bus.speed.toFixed(0), 960, 340, {size:48,bold:true,align:'center'});\n"
            "  ctx.drawDashBox(5,164,295,'left');\n"
            "  ctx.drawNavCard(1620,164,295,344,{alpha:200});\n"
            "  ctx.drawTurnArrow(900,200,48,bus.nav.turnType,{color:'#fff'});\n"
            "}\n";
        v23_bus_t bus = make_bus(1, 0);
        uint32_t calls = 0;
        if (v23_sandbox_load_code(&sb, "all17.js", all17, strlen(all17)) != 0 ||
            v23_sandbox_render_frame(&sb, &bus, NULL, &calls) != 0 ||
            calls != 17 || !sb.metas[0].has_shader ||
            rec.clear_n != 1 || rec.map_n != 1 || rec.shader_n != 1 || rec.rect_n != 1 ||
            rec.circle_n != 1 || rec.line_n != 1 || rec.poly_n != 1 || rec.arc_n != 1 ||
            rec.ticks_n != 1 || rec.needle_n != 1 || rec.seg_n != 1 || rec.bi_n != 1 ||
            rec.range_n != 1 || rec.text_n != 1 || rec.dash_n != 1 || rec.nav_n != 1 || rec.arrow_n != 1) {
            fprintf(stderr, "FAIL all17 ops check (calls=%u err=%s)\n", calls, sb.last_err);
            fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    /* 3. Built-in default classic (CarPlay live + idle) */
    {
        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        v23_bus_t b_live = make_bus(1, 0);
        v23_bus_t b_idle = make_bus(0, 0);
        if (v23_sandbox_load_code(&sb, "default_classic.js", V23_DEFAULT_CLASSIC_JS, strlen(V23_DEFAULT_CLASSIC_JS)) != 0 ||
            v23_sandbox_render_frame(&sb, &b_live, NULL, NULL) != 0 ||
            v23_sandbox_render_frame(&sb, &b_idle, NULL, NULL) != 0 ||
            rec.map_n != 1 || rec.arc_n != 2 || rec.dash_n != 2 || rec.nav_n != 2) {
            fprintf(stderr, "FAIL default_classic check: %s\n", sb.last_err);
            fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    /* 4. Cold-start NaN/-999 safety + bus.ready */
    {
        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        const char *cold =
            "const THEME={id:'cold'};\n"
            "function render(ctx,bus){\n"
            "  var s = bus.oil.toFixed(1) + ',' + bus.gearbox.toFixed(0) + ',' + bus.tpfl.toFixed(1);\n"
            "  if (bus.ready.oil || bus.ready.gearbox || bus.ready.tires || !bus.sys.acc) throw new Error('cold/acc flag mismatch');\n"
            "  ctx.text(s, 960, 320);\n"
            "}\n";
        v23_bus_t bus = make_bus(1, 1);
        if (v23_sandbox_load_code(&sb, "cold.js", cold, strlen(cold)) != 0 ||
            v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0 ||
            strcmp(rec.last_text, "0.0,0,0.0") != 0) {
            fprintf(stderr, "FAIL cold start safety: %s (got=%s)\n", sb.last_err, rec.last_text);
            fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    /* 5. Multi-skin 72KB payload + 140KB oversize reject */
    {
        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        size_t cap = 96 * 1024;
        char *big = (char *)malloc(cap);
        size_t p = (size_t)snprintf(big, cap, "const THEMES=[\n");
        for (int i = 0; i < 6; i++) {
            p += (size_t)snprintf(big + p, cap - p,
                "  {id:'m%d',name:'S%d',render(ctx,bus){ctx.clear(0,0,0,1);ctx.text('S%d',10,20);}}%s\n",
                i, i, i, i < 5 ? "," : "");
        }
        p += (size_t)snprintf(big + p, cap - p, "];\n");
        while (p < 72 * 1024) p += (size_t)snprintf(big + p, cap - p, "// padding line 0123456789abcdef\n");
        v23_bus_t bus = make_bus(1, 0);
        if (v23_sandbox_load_code(&sb, "big.js", big, p) != 0 || sb.entry_count != 6) {
            fprintf(stderr, "FAIL 72KB multi-skin: %s\n", sb.last_err); fails++;
        } else {
            for (int i = 0; i < 6; i++) {
                sb.active_idx = i;
                if (v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0) { fails++; break; }
            }
        }
        free(big);

        char *over = (char *)malloc(140 * 1024);
        memset(over, ' ', 140 * 1024);
        if (v23_sandbox_load_code(&sb, "over.js", over, 140 * 1024) == 0) {
            fprintf(stderr, "FAIL 140KB oversize not rejected\n"); fails++;
        }
        free(over);
        v23_sandbox_destroy(&sb);
    }

    /* 6. 6 Safety fuses */
    fails += assert_fault("syntax", "const THEME={}; function render(){ const a = ((1+; }", 1);
    fails += assert_fault("ref_err", "const THEME={}; function render(){ return nope.x; }", 0);
    fails += assert_fault("loop_35ms", "const THEME={}; function render(){ while(true){} }", 0);
    fails += assert_fault("oom_4mb", "const THEME={}; function render(){ var a=[]; while(true) a.push('X'.repeat(8192)); }", 0);
    fails += assert_fault("stack_256k", "const THEME={}; function f(n){return f(n+1)+1;} function render(){f(0);}", 0);
    fails += assert_fault("no_eval", "const THEME={}; function render(){ eval('1+1'); }", 0);
    fails += assert_fault("no_Function", "const THEME={}; function render(){ return new Function('return 1')(); }", 0);

    /* 7. Auto day/night RGBX luminance hysteresis */
    {
        int W = 320, H = 120;
        uint8_t *img = (uint8_t *)malloc((size_t)W * (size_t)H * 4u);
        memset(img, 190, (size_t)W * (size_t)H * 4u);
        int n1 = v23_detect_night_rgbx(img, W, H, W * 4, 1); /* bright -> day(0) */
        memset(img, 115, (size_t)W * (size_t)H * 4u);
        int n2 = v23_detect_night_rgbx(img, W, H, W * 4, n1); /* mid -> stay day(0) */
        memset(img, 35, (size_t)W * (size_t)H * 4u);
        int n3 = v23_detect_night_rgbx(img, W, H, W * 4, n2); /* dark -> night(1) */
        memset(img, 120, (size_t)W * (size_t)H * 4u);
        int n4 = v23_detect_night_rgbx(img, W, H, W * 4, n3); /* mid -> stay night(1) */
        free(img);
        if (!(n1 == 0 && n2 == 0 && n3 == 1 && n4 == 1)) {
            fprintf(stderr, "FAIL night hysteresis (%d,%d,%d,%d)\n", n1, n2, n3, n4); fails++;
        }
    }

    /* 8. OEM gear decoding (P/R/N/D/S/M/E/B and unready "-") + JS empty gear fallback "-" */
    {
        char gbuf[12];
        v23_format_gear(1, 0, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "P") != 0)  { fprintf(stderr, "FAIL gear P: %s\n", gbuf); fails++; }
        v23_format_gear(2, 0, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "R") != 0)  { fprintf(stderr, "FAIL gear R: %s\n", gbuf); fails++; }
        v23_format_gear(3, 0, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "N") != 0)  { fprintf(stderr, "FAIL gear N: %s\n", gbuf); fails++; }
        v23_format_gear(4, 5, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "D5") != 0) { fprintf(stderr, "FAIL gear D5: %s\n", gbuf); fails++; }
        v23_format_gear(13, 3, gbuf, sizeof(gbuf));    if (strcmp(gbuf, "D3") != 0) { fprintf(stderr, "FAIL gear D3: %s\n", gbuf); fails++; }
        v23_format_gear(5, 4, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "S4") != 0) { fprintf(stderr, "FAIL gear S4: %s\n", gbuf); fails++; }
        v23_format_gear(11, 2, gbuf, sizeof(gbuf));    if (strcmp(gbuf, "S2") != 0) { fprintf(stderr, "FAIL gear S2: %s\n", gbuf); fails++; }
        v23_format_gear(6, 6, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "M6") != 0) { fprintf(stderr, "FAIL gear M6: %s\n", gbuf); fails++; }
        v23_format_gear(10, 8, gbuf, sizeof(gbuf));    if (strcmp(gbuf, "M8") != 0) { fprintf(stderr, "FAIL gear M8: %s\n", gbuf); fails++; }
        v23_format_gear(8, 4, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "E4") != 0) { fprintf(stderr, "FAIL gear E4: %s\n", gbuf); fails++; }
        v23_format_gear(8, 0, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "E") != 0)  { fprintf(stderr, "FAIL gear E: %s\n", gbuf); fails++; }
        v23_format_gear(9, 3, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "B3") != 0) { fprintf(stderr, "FAIL gear B3: %s\n", gbuf); fails++; }
        v23_format_gear(9, 0, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "B") != 0)  { fprintf(stderr, "FAIL gear B: %s\n", gbuf); fails++; }
        v23_format_gear(-999, 0, gbuf, sizeof(gbuf));  if (strcmp(gbuf, "-") != 0)  { fprintf(stderr, "FAIL gear unready: %s\n", gbuf); fails++; }
        v23_format_gear(0, 5, gbuf, sizeof(gbuf));     if (strcmp(gbuf, "-") != 0)  { fprintf(stderr, "FAIL gear unknown: %s\n", gbuf); fails++; }

        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        const char *gjs = "const THEME={id:'g'}; function render(ctx,bus){ ctx.text(bus.gear, 0, 0); }\n";
        v23_bus_t bus = make_bus(1, 0);
        bus.gear[0] = '\0';
        if (v23_sandbox_load_code(&sb, "g.js", gjs, strlen(gjs)) != 0 ||
            v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0 ||
            strcmp(rec.last_text, "-") != 0) {
            fprintf(stderr, "FAIL empty gear fallback: got '%s' err=%s\n", rec.last_text, sb.last_err); fails++;
        }
        v23_format_gear(8, 5, bus.gear, sizeof(bus.gear));
        if (v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0 || strcmp(rec.last_text, "E5") != 0) {
            fprintf(stderr, "FAIL JS E5 gear: got '%s'\n", rec.last_text); fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    /* 9. iAP2 maneuverType mapping + uturn/roundabout turn arrow dispatch */
    {
        if (strcmp(v23_map_turn_type(4, 0, 0), "uturn") != 0 ||
            strcmp(v23_map_turn_type(18, 0, 0), "uturn") != 0 ||
            strcmp(v23_map_turn_type(26, 0, 0), "uturn") != 0 ||
            strcmp(v23_map_turn_type(6, 0, 0), "roundabout") != 0 ||
            strcmp(v23_map_turn_type(7, 0, 0), "roundabout") != 0 ||
            strcmp(v23_map_turn_type(19, 1, 0), "roundabout") != 0 ||
            strcmp(v23_map_turn_type(30, 1, 0), "roundabout") != 0 ||
            strcmp(v23_map_turn_type(0, 1, 0), "roundabout") != 0 ||
            strcmp(v23_map_turn_type(8, 0, 0), "slight_right") != 0 ||
            strcmp(v23_map_turn_type(8, 0, 1), "slight_left") != 0 ||
            strcmp(v23_map_turn_type(1, 0, 0), "left") != 0 ||
            strcmp(v23_map_turn_type(2, 0, 0), "right") != 0 ||
            strcmp(v23_map_turn_type(13, 0, 0), "slight_left") != 0 ||
            strcmp(v23_map_turn_type(14, 0, 0), "slight_right") != 0 ||
            strcmp(v23_map_turn_type(3, 0, 0), "straight") != 0) {
            fprintf(stderr, "FAIL iAP2 maneuverType mapping\n"); fails++;
        }

        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        const char *ajs =
            "const THEME={id:'arr'};\n"
            "function render(ctx,bus){\n"
            "  ctx.drawTurnArrow(100,100,64,'uturn',{color:'#fff'});\n"
            "  ctx.drawTurnArrow(200,100,64,'roundabout',{color:'#22d3ee'});\n"
            "}\n";
        v23_bus_t bus = make_bus(1, 0);
        if (v23_sandbox_load_code(&sb, "arr.js", ajs, strlen(ajs)) != 0 ||
            v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0 ||
            rec.arrow_n != 2 || strcmp(rec.last_arrow, "roundabout") != 0) {
            fprintf(stderr, "FAIL uturn/roundabout arrow dispatch (n=%d last=%s)\n", rec.arrow_n, rec.last_arrow); fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    /* 10. Fuel litres conversion, 13 board-computer fields, ETA/remMin, per-wheel tires */
    {
        int r = -1;
        double f90 = v23_calc_fuel(50, 90, &r);
        if (r != 1 || fabs(f90 - 45.0) > 1e-6) { fprintf(stderr, "FAIL fuel 90L: %.2f r=%d\n", f90, r); fails++; }
        double f76 = v23_calc_fuel(50, 76, &r);
        if (r != 1 || fabs(f76 - 38.0) > 1e-6) { fprintf(stderr, "FAIL fuel 76L: %.2f r=%d\n", f76, r); fails++; }
        double fpct = v23_calc_fuel(64, 0, &r);
        if (r != 1 || fabs(fpct - 64.0) > 1e-6) { fprintf(stderr, "FAIL fuel pct: %.2f r=%d\n", fpct, r); fails++; }
        double fbad = v23_calc_fuel(-999, 90, &r);
        if (r != 0 || fbad != 0.0) { fprintf(stderr, "FAIL fuel invalid: %.2f r=%d\n", fbad, r); fails++; }

        if (v23_calc_rem_min(61) != 2 || v23_calc_rem_min(60) != 1 || v23_calc_rem_min(0) != 0 || v23_calc_rem_min(-5) != 0) {
            fprintf(stderr, "FAIL rem_min ceiling\n"); fails++;
        }

        char eta[16];
        /* 1700000000 UTC = 2023-11-14 22:13:20 UTC -> +8h = 06:13:20; +29s=06:13, +30s=06:14 */
        v23_format_eta(1700000000U, 0, 8, 1700000000L, eta, sizeof(eta));
        if (strcmp(eta, "06:13") != 0) { fprintf(stderr, "FAIL eta 06:13: %s\n", eta); fails++; }
        v23_format_eta(1700000010U, 0, 8, 1700000000L, eta, sizeof(eta)); /* 22:13:30 UTC -> rounds to 06:14 */
        if (strcmp(eta, "06:14") != 0) { fprintf(stderr, "FAIL eta +30s round 06:14: %s\n", eta); fails++; }
        /* Fallback when eta_ts < 1577836800 (1970 clock / missing ETA) */
        v23_format_eta(0U, 600, 8, 1700000000L, eta, sizeof(eta)); /* +10 min -> 06:23 */
        if (strcmp(eta, "06:23") != 0) { fprintf(stderr, "FAIL eta fallback 06:23: %s\n", eta); fails++; }
        v23_format_eta(0U, 600, 8, 100L, eta, sizeof(eta));
        if (strcmp(eta, "--:--") != 0) { fprintf(stderr, "FAIL eta invalid --:--: %s\n", eta); fails++; }

        char tbuf[24]; int tr = -1;
        /* Missing FL wheel must not gate ready_tires or pull front average down to 1.2 */
        v23_calc_tires(NAN, 2.4, 2.6, 2.8, &tr, tbuf, sizeof(tbuf));
        if (tr != 1 || strcmp(tbuf, "2.4·2.7") != 0) { fprintf(stderr, "FAIL tires partial: tr=%d %s\n", tr, tbuf); fails++; }
        v23_calc_tires(NAN, NAN, NAN, NAN, &tr, tbuf, sizeof(tbuf));
        if (tr != 0 || strcmp(tbuf, "--·--") != 0) { fprintf(stderr, "FAIL tires all-nan: tr=%d %s\n", tr, tbuf); fails++; }
        v23_calc_tires(2.5, 2.5, NAN, NAN, &tr, tbuf, sizeof(tbuf));
        if (tr != 1 || strcmp(tbuf, "2.5") != 0) { fprintf(stderr, "FAIL tires front-only: tr=%d %s\n", tr, tbuf); fails++; }

        /* 13 board-computer fields end-to-end through QuickJS marshal */
        v23_bus_t bus = make_bus(1, 0);
        bus.fuel = v23_calc_fuel(60, 90, &r);
        float avg[3]  = { 9.8f, 10.4f, 11.2f };
        float dist[3] = { 42.5f, 310.0f, 4560.0f };
        float bspd[3] = { 52.0f, 48.0f, 39.0f };
        int btime[3]  = { 49, 390, 7015 };
        v23_fill_board_computer(&bus, avg, dist, bspd, btime, 12345.6, 12000.0);

        mock_rec_t rec; memset(&rec, 0, sizeof(rec));
        v23_draw_ops_t ops = make_ops(&rec);
        v23_sandbox_t sb;
        v23_sandbox_init(&sb, &ops);
        const char *bcjs =
            "const THEME={id:'bc'};\n"
            "function render(ctx,b){\n"
            "  var s = [b.fuel.toFixed(0), b.trip.toFixed(1), b.tripdist.toFixed(1), b.tripspeed.toFixed(0), b.triptime,\n"
            "           b.refuel.toFixed(1), b.refueldist.toFixed(0), b.refuelspeed.toFixed(0), b.refueltime,\n"
            "           b.total.toFixed(1), b.totaldist.toFixed(0), b.totalspeed.toFixed(0), b.totaltime, b.tripkm.toFixed(1)].join('|');\n"
            "  ctx.text(s, 0, 0);\n"
            "}\n";
        const char *want = "54|9.8|42.5|52|0:49|10.4|310|48|6:30|11.2|4560|39|116:55|345.6";
        if (v23_sandbox_load_code(&sb, "bc.js", bcjs, strlen(bcjs)) != 0 ||
            v23_sandbox_render_frame(&sb, &bus, NULL, NULL) != 0 ||
            strcmp(rec.last_text, want) != 0) {
            fprintf(stderr, "FAIL 13 BC fields: got '%s' want '%s' err=%s\n", rec.last_text, want, sb.last_err); fails++;
        }
        /* Verify invalid BC inputs degrade safely to 0 / "--:--" */
        float bad_f[3] = { NAN, -1.0f, 1500.0f };
        int bad_t[3] = { -999, -1, -999 };
        v23_fill_board_computer(&bus, bad_f, bad_f, bad_f, bad_t, NAN, 100.0);
        if (bus.trip != 0.0 || bus.refuel != 0.0 || bus.total != 0.0 || bus.tripkm != 0.0 ||
            strcmp(bus.triptime, "--:--") != 0 || strcmp(bus.refueltime, "--:--") != 0 || strcmp(bus.totaltime, "--:--") != 0) {
            fprintf(stderr, "FAIL invalid BC sanitize\n"); fails++;
        }
        v23_sandbox_destroy(&sb);
    }

    if (fails == 0) printf("test_v23_theme: ALL PASSED (17 ctx ops + 7 fuses + cold-start + 72KB/140KB + night + gear/iAP2/fuel/13-BC/ETA/tires)\n");
    return fails;
}
