/*
 * navbox.c - 仪表 CarPlay 地图右侧导航信息框（适配高德、百度、苹果地图）。
 *
 * 工作方式：
 * - 轮询 /tmp/cluster_cp.state（无线 dio_cluster.so 与有线 cl_probe.so 均写入同一格式），
 *   解析剩余路程、剩余时间、到达时间 (ETA)、下一路口转向/距离/完整路名/出口牌、
 *   下下路口预告 (slot_1) 以及车道指引 (iAP2 0x5204)。
 * - 导航激活时自动接管右信息框位置（与左信息框等高对齐）；退出导航自动隐藏并恢复原右信息框。
 * - 使用单头文件 stb_truetype.h 直接加载车机自带的原厂字体（ZYHei_GB18030_c.ttf +
 *   VWTextCarUI-Bold.ttf），配合 256 槽 LRU 字形缓存，零新增动态库依赖，支持任意中文路名。
 * - 转向箭头与车道指引采用抗锯齿 SDF 矢量绘制，按图层合并覆盖度，无重叠接缝。
 * - 纯可移植 C，无 QNX 系统依赖，可在主机直接编译生成预览图测试。
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- 纯 C 数学与字符串辅助（避免引入 QNX 6.5/6.6 libm 私有符号如 _FSin / __assert） ---- */
static inline int nav_ifloor(double x) { int i = (int)x; return (x < (double)i) ? (i - 1) : i; }
static inline int nav_iceil(double x)  { int i = (int)x; return (x > (double)i) ? (i + 1) : i; }
static inline double nav_fabs(double x) { return x < 0.0 ? -x : x; }
static inline float nav_fabsf(float x)  { return x < 0.0f ? -x : x; }

static double nav_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    /* 初始估计 + 6 次牛顿迭代，双精度全位精确 */
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u = (v.u >> 1) + 0x1ff8000000000000ULL;
    double g = v.d;
    for (int i = 0; i < 6; i++) g = 0.5 * (g + x / g);
    return g;
}

static float nav_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; uint32_t u; } v;
    v.f = x;
    v.u = (v.u >> 1) + 0x1fc00000U;
    float g = v.f;
    for (int i = 0; i < 4; i++) g = 0.5f * (g + x / g);
    return g;
}

static double nav_fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    int qi = (int)q;
    return x - (double)qi * y;
}

static float nav_sinf(float x) {
    const float PI = 3.14159265f, TWO_PI = 6.28318531f;
    while (x > PI)  x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    if (x > PI * 0.5f)  x = PI - x;
    if (x < -PI * 0.5f) x = -PI - x;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}

static float nav_cosf(float x) {
    return nav_sinf(x + 1.57079633f);
}

static float nav_atan2f(float y, float x) {
    const float PI = 3.14159265f, HALF_PI = 1.57079633f;
    if (x == 0.0f) return y > 0.0f ? HALF_PI : (y < 0.0f ? -HALF_PI : 0.0f);
    float ax = nav_fabsf(x), ay = nav_fabsf(y);
    float z, r;
    if (ax >= ay) {
        z = ay / ax;
        r = z / (1.0f + 0.28125f * z * z);
    } else {
        z = ax / ay;
        r = HALF_PI - z / (1.0f + 0.28125f * z * z);
    }
    if (x < 0.0f) r = PI - r;
    return y < 0.0f ? -r : r;
}

static const char *nav_memchr(const char *s, int c, size_t n) {
    for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] == (unsigned char)c) return s + i;
    return NULL;
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_assert(x) ((void)0)
#define STBTT_ifloor(x) nav_ifloor(x)
#define STBTT_iceil(x)  nav_iceil(x)
#define STBTT_sqrt(x)   nav_sqrt(x)
#define STBTT_pow(x,y)  ((double)(x))
#define STBTT_fmod(x,y) nav_fmod(x,y)
#define STBTT_cos(x)    ((double)nav_cosf((float)(x)))
#define STBTT_acos(x)   (0.0)
#define STBTT_fabs(x)   nav_fabs(x)
#include "stb_truetype.h"

#define NAV_STATE_PATH  "/tmp/cluster_cp.state"
#define NAV_OFF_FLAG    "/tmp/navbox_off"
#define NAV_MAX_W       560
#define NAV_MAX_H       640
#define NAV_MAX_LANES   8
#define NAV_MAX_ANGLES  12
#define NAV_GLYPH_CACHE 256

/* ---- 配置（从 dash.conf 读取，未指定项自动继承对应侧信息框配置） ---- */
typedef struct {
    int enabled;       /* nav_box=1 默认开启，0 关闭 */
    int side;          /* nav_side=right(1，默认) 放右框，left(0) 放左框 */
    int x, y, right;   /* -1 表示自动继承对应侧信息框 x / y / right */
    int alpha;         /* -1 表示自动继承 alpha */
    int tz_offset;     /* 预计到达时间时区偏移（小时），中国默认 +8 */
} nav_conf_t;

static nav_conf_t g_nav_conf = { 1, 1, -1, -1, -1, -1, 8 };

/* ---- 解析后的导航快照 ---- */
typedef struct {
    int      pos;
    int      dir;                      /* 高亮主方向（角度，1000=无） */
    int      status;                   /* 0=不可走, 1=可走, 2=推荐 */
    int      angles[NAV_MAX_ANGLES];   /* 该车道全部合法方向 */
    int      nangles;
} nav_lane_t;

typedef struct {
    int        live;                   /* 1 = 正在导航 */
    int        session_active;
    int        route_state;
    int        src_supports_rg;
    int        slot_count;
    int        cur_slot;
    int        dist_to_maneuver;       /* 到下一路口距离（米），-1 无效 */
    int        dist_remaining;         /* 剩余总路程（米），-1 无效 */
    int        time_remaining;         /* 剩余总时间（秒），-1 无效 */
    uint32_t   eta;                    /* 预计到达 UTC 时间戳（秒，2106 年前 32 位无溢出） */
    /* slot_0：当前下一路口 */
    int        s0_type;
    int        s0_driving_side;        /* 0 右行，1 左行 */
    int        s0_junction;            /* 0 普通路口，1 环岛 */
    int        s0_exit_angle;
    char       s0_road[256];
    char       s0_desc[256];
    char       s0_exit_info[128];
    /* slot_1：下下路口预告 */
    int        s1_type;
    int        s1_driving_side;
    int        s1_junction;
    int        s1_exit_angle;
    int        s1_dist;                /* 距上一机动点距离（米） */
    char       s1_road[256];
    /* 其他元数据 */
    char       cur_road[256];
    char       destination[256];
    char       source_name[64];
    /* 车道指引（0x5204） */
    int        lane_active;
    int        lane_count;
    nav_lane_t lanes[NAV_MAX_LANES];
} nav_state_t;

/* ---- 画布与状态缓存 ---- */
static uint32_t g_nav_px[NAV_MAX_W * NAV_MAX_H];
static int      g_nav_w = 295, g_nav_h = 336;
static int      g_nav_x = 1620, g_nav_y = 164;
static int      g_nav_alpha = 200;
static int      g_nav_active = 0;         /* 当前是否应显示导航框（含消抖） */
static int      g_nav_not_live = 0;       /* 连续非导航计数（消抖防闪烁） */
static char     g_nav_raw_prev[8192];
static int      g_nav_raw_len = -1;
static int      g_nav_dirty = 0;          /* 像素缓冲已重绘，需同步到 pixmap */

/* ---- 字体与 LRU 字形缓存 ---- */
typedef struct {
    unsigned char *buf;
    stbtt_fontinfo info;
    int            loaded;
} nav_font_slot_t;

typedef struct {
    uint32_t       cp;
    uint16_t       px;
    uint8_t        fi;
    uint8_t        bold;
    int            w, h, xoff, yoff, adv;
    uint32_t       seq;
    unsigned char *bm;
} nav_glyph_ent_t;

static nav_font_slot_t g_nav_font[2];     /* 0 = CJK (ZYHei), 1 = Latin/Digit Bold (VWTextCarUI) */
static int             g_nav_fonts_init = 0;
static nav_glyph_ent_t g_nav_gcache[NAV_GLYPH_CACHE];
static uint32_t        g_nav_gseq = 0;

/* ---- 基础像素混合（与 dash.c 一致：0xAARRGGBB 直通 Alpha） ---- */
static inline uint32_t nav_px(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void nav_blend_px(int x, int y, int r, int g, int b, int cov) {
    if (cov <= 0 || x < 0 || x >= g_nav_w || y < 0 || y >= g_nav_h) return;
    if (cov > 255) cov = 255;
    uint32_t d = g_nav_px[y * g_nav_w + x];
    int da = (int)(d >> 24);
    int dr = (int)((d >> 16) & 255);
    int dg = (int)((d >> 8) & 255);
    int db = (int)(d & 255);
    int inv = 255 - cov;
    int nr = (dr * inv + r * cov + 127) / 255;
    int ng = (dg * inv + g * cov + 127) / 255;
    int nb = (db * inv + b * cov + 127) / 255;
    int na = da + ((255 - da) * cov + 127) / 255;
    g_nav_px[y * g_nav_w + x] = nav_px(nr, ng, nb, na);
}

static void nav_fill_rrect_bg(int W, int H, int R, int r, int g, int b, int alpha) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float cov = 1.0f;
            int cx = x < R ? R : (x >= W - R ? W - R - 1 : x);
            int cy = y < R ? R : (y >= H - R ? H - R - 1 : y);
            if (cx != x || cy != y) {
                float dx = (float)(x - cx), dy = (float)(y - cy);
                cov = (float)R + 0.5f - nav_sqrtf(dx * dx + dy * dy);
                if (cov < 0.0f) cov = 0.0f;
                if (cov > 1.0f) cov = 1.0f;
            }
            g_nav_px[y * W + x] = nav_px(r, g, b, (int)(alpha * cov + 0.5f));
        }
    }
}

static void nav_overlay_rrect(int rx, int ry, int rw, int rh, int R, int r, int g, int b, int a) {
    if (rw <= 0 || rh <= 0 || a <= 0) return;
    if (R * 2 > rw) R = rw / 2;
    if (R * 2 > rh) R = rh / 2;
    for (int y = 0; y < rh; y++) {
        int py = ry + y;
        if (py < 0 || py >= g_nav_h) continue;
        for (int x = 0; x < rw; x++) {
            int px = rx + x;
            if (px < 0 || px >= g_nav_w) continue;
            float cov = 1.0f;
            int cx = x < R ? R : (x >= rw - R ? rw - R - 1 : x);
            int cy = y < R ? R : (y >= rh - R ? rh - R - 1 : y);
            if (cx != x || cy != y) {
                float dx = (float)(x - cx), dy = (float)(y - cy);
                cov = (float)R + 0.5f - nav_sqrtf(dx * dx + dy * dy);
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
            }
            nav_blend_px(px, py, r, g, b, (int)(a * cov + 0.5f));
        }
    }
}

static void nav_draw_hline(int x0, int x1, int y, int r, int g, int b, int a) {
    if (y < 0 || y >= g_nav_h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > g_nav_w) x1 = g_nav_w;
    for (int x = x0; x < x1; x++) nav_blend_px(x, y, r, g, b, a);
}

/* ---- TTF 字体加载与 UTF-8 渲染 ---- */
static int nav_load_font_file(nav_font_slot_t *slot, const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz <= 64 || sz > 32 * 1024 * 1024L) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    int off = stbtt_GetFontOffsetForIndex(buf, 0);
    if (off < 0 || !stbtt_InitFont(&slot->info, buf, off)) {
        free(buf);
        return 0;
    }
    slot->buf = buf;
    slot->loaded = 1;
    return 1;
}

static void nav_fonts_init_once(void) {
    if (g_nav_fonts_init) return;
    g_nav_fonts_init = 1;
    const char *env_cjk = getenv("NAV_CJK_FONT");
    const char *env_lat = getenv("NAV_LATIN_FONT");
    static const char *cjk_paths[] = {
        "/mnt/app/eso/hmi/lsd/Resources/skin3/Fonts/ZYHei_GB18030_c.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin1/Fonts/ZYHei_GB18030_c.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin2/Fonts/ZYHei_GB18030_c.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin5/Fonts/ZYHei_GB18030_c.ttf",
        "/eso/browser/fonts/ZYHei_GB18030_c.ttf",
        NULL
    };
    static const char *lat_paths[] = {
        "/mnt/app/eso/hmi/lsd/Resources/skin3/Fonts/VWTextCarUI-Bold.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin1/Fonts/VWTextCarUI-Bold.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin3/Fonts/VWTextCarUI-Regular.ttf",
        "/mnt/app/eso/hmi/lsd/Resources/skin0/Fonts/Default.ttf",
        NULL
    };
    if (env_cjk && nav_load_font_file(&g_nav_font[0], env_cjk)) {
        /* 使用主机测试指定的 CJK 字体 */
    } else {
        for (int i = 0; cjk_paths[i]; i++) {
            if (nav_load_font_file(&g_nav_font[0], cjk_paths[i])) break;
        }
    }
    if (env_lat && nav_load_font_file(&g_nav_font[1], env_lat)) {
        /* 使用主机测试指定的数字/英文字体 */
    } else {
        for (int i = 0; lat_paths[i]; i++) {
            if (nav_load_font_file(&g_nav_font[1], lat_paths[i])) break;
        }
    }
}

static uint32_t nav_utf8_next(const char **pp) {
    const unsigned char *s = (const unsigned char *)(*pp);
    unsigned char c = *s;
    if (!c) return 0;
    if (c < 0x80) { *pp += 1; return c; }
    if ((c & 0xE0) == 0xC0 && s[1]) {
        *pp += 2;
        return ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
        *pp += 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *pp += 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *pp += 1;
    return '?';
}

/* 选择字体槽位：ASCII/标点优先用大众原厂粗体，其余中文用 ZYHei */
static int nav_pick_font(uint32_t cp, int prefer_latin, int *glyph_out) {
    int order[2];
    if (prefer_latin || cp < 0x0250) { order[0] = 1; order[1] = 0; }
    else { order[0] = 0; order[1] = 1; }
    for (int k = 0; k < 2; k++) {
        int fi = order[k];
        if (!g_nav_font[fi].loaded) continue;
        int g = stbtt_FindGlyphIndex(&g_nav_font[fi].info, (int)cp);
        if (g != 0) { *glyph_out = g; return fi; }
    }
    *glyph_out = 0;
    return -1;
}

static const nav_glyph_ent_t *nav_get_glyph(uint32_t cp, int px, int bold, int prefer_latin) {
    nav_fonts_init_once();
    int gid = 0;
    int fi = nav_pick_font(cp, prefer_latin, &gid);
    if (fi < 0) return NULL;

    /* 查缓存 */
    int empty = -1, oldest = 0;
    for (int i = 0; i < NAV_GLYPH_CACHE; i++) {
        nav_glyph_ent_t *e = &g_nav_gcache[i];
        if (e->cp == cp && e->px == (uint16_t)px && e->fi == (uint8_t)fi && e->bold == (uint8_t)bold && e->seq > 0) {
            e->seq = ++g_nav_gseq;
            return e;
        }
        if (e->seq == 0 && empty < 0) empty = i;
        if (e->seq < g_nav_gcache[oldest].seq) oldest = i;
    }

    int slot = empty >= 0 ? empty : oldest;
    nav_glyph_ent_t *e = &g_nav_gcache[slot];
    if (e->bm) { free(e->bm); e->bm = NULL; }

    stbtt_fontinfo *info = &g_nav_font[fi].info;
    float scale = stbtt_ScaleForPixelHeight(info, (float)px);
    int adv_raw = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, gid, &adv_raw, &lsb);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(info, gid, scale, scale, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;

    /* 车机原厂 ZYHei_GB18030_c.ttf 复合汉字的 glyf 头 bbox 全为 (0,0,0,0)，
     * 导致 stbtt_GetGlyphBitmapBox 返回空框。兜底直接解析矢量轮廓顶点重算真实外框并光栅化。 */
    stbtt_vertex *verts = NULL;
    int nverts = 0;
    int use_shape = 0;
    if (bw <= 0 || bh <= 0) {
        nverts = stbtt_GetGlyphShape(info, gid, &verts);
        if (nverts > 0 && verts) {
            int minx = 32767, miny = 32767, maxx = -32768, maxy = -32768;
            for (int i = 0; i < nverts; i++) {
                if (verts[i].x < minx) minx = verts[i].x;
                if (verts[i].x > maxx) maxx = verts[i].x;
                if (verts[i].y < miny) miny = verts[i].y;
                if (verts[i].y > maxy) maxy = verts[i].y;
                if (verts[i].type == STBTT_vcurve) {
                    if (verts[i].cx < minx) minx = verts[i].cx;
                    if (verts[i].cx > maxx) maxx = verts[i].cx;
                    if (verts[i].cy < miny) miny = verts[i].cy;
                    if (verts[i].cy > maxy) maxy = verts[i].cy;
                }
            }
            if (maxx > minx && maxy > miny) {
                x0 = nav_ifloor((float)minx * scale);
                y0 = nav_ifloor((float)(-maxy) * scale);
                x1 = nav_iceil((float)maxx * scale);
                y1 = nav_iceil((float)(-miny) * scale);
                bw = x1 - x0;
                bh = y1 - y0;
                use_shape = 1;
            }
        }
    }

    unsigned char *bm = NULL;
    if (bw > 0 && bh > 0 && bw <= 128 && bh <= 128) {
        int out_w = bold ? (bw + 1) : bw;
        bm = (unsigned char *)calloc((size_t)(out_w * bh), 1);
        if (bm) {
            if (!bold) {
                if (use_shape) {
                    stbtt__bitmap gbm;
                    gbm.w = bw; gbm.h = bh; gbm.stride = bw; gbm.pixels = bm;
                    stbtt_Rasterize(&gbm, 0.35f, verts, nverts, scale, scale, 0.0f, 0.0f, x0, y0, 1, info->userdata);
                } else {
                    stbtt_MakeGlyphBitmap(info, bm, bw, bh, bw, scale, scale, gid);
                }
            } else {
                /* 轻量级 1px 水平加粗（让 Regular 中文字体在仪表深色背景上更饱满） */
                unsigned char *tmp = (unsigned char *)calloc((size_t)(bw * bh), 1);
                if (tmp) {
                    if (use_shape) {
                        stbtt__bitmap gbm;
                        gbm.w = bw; gbm.h = bh; gbm.stride = bw; gbm.pixels = tmp;
                        stbtt_Rasterize(&gbm, 0.35f, verts, nverts, scale, scale, 0.0f, 0.0f, x0, y0, 1, info->userdata);
                    } else {
                        stbtt_MakeGlyphBitmap(info, tmp, bw, bh, bw, scale, scale, gid);
                    }
                    for (int y = 0; y < bh; y++) {
                        for (int x = 0; x < out_w; x++) {
                            int v0 = (x < bw) ? tmp[y * bw + x] : 0;
                            int v1 = (x > 0) ? tmp[y * bw + (x - 1)] : 0;
                            /* 合成 0.45 权重右移副本，既清晰又不发糊 */
                            int v = v0 + (v1 * 115) / 255;
                            if (v > 255) v = 255;
                            bm[y * out_w + x] = (unsigned char)v;
                        }
                    }
                    free(tmp);
                }
            }
            bw = out_w;
        }
    } else {
        bw = bh = 0;
    }
    if (verts) stbtt_FreeShape(info, verts);

    e->cp   = cp;
    e->px   = (uint16_t)px;
    e->fi   = (uint8_t)fi;
    e->bold = (uint8_t)bold;
    e->w    = bw;
    e->h    = bh;
    e->xoff = x0;
    e->yoff = y0;
    e->adv  = (int)(adv_raw * scale + 0.5f) + (bold && cp >= 0x0250 ? 1 : 0);
    e->seq  = ++g_nav_gseq;
    e->bm   = bm;
    return e;
}

static int nav_text_width(const char *s, int px, int bold, int prefer_latin) {
    if (!s) return 0;
    int w = 0;
    const char *p = s;
    uint32_t cp;
    while ((cp = nav_utf8_next(&p)) != 0) {
        if (cp == ' ') { w += (px * 30) / 100; continue; }
        const nav_glyph_ent_t *gl = nav_get_glyph(cp, px, bold, prefer_latin);
        if (gl) w += gl->adv;
    }
    return w;
}

static int nav_font_ascent(int px, int prefer_latin) {
    nav_fonts_init_once();
    int fi = (prefer_latin && g_nav_font[1].loaded) ? 1 : (g_nav_font[0].loaded ? 0 : (g_nav_font[1].loaded ? 1 : -1));
    if (fi < 0) return (px * 80) / 100;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&g_nav_font[fi].info, &asc, &desc, &gap);
    float scale = stbtt_ScaleForPixelHeight(&g_nav_font[fi].info, (float)px);
    return (int)(asc * scale + 0.5f);
}

static int nav_draw_text(const char *s, int x, int base_y, int px, int bold, int prefer_latin,
                         int r, int g, int b, int alpha) {
    if (!s || alpha <= 0) return x;
    const char *p = s;
    uint32_t cp;
    while ((cp = nav_utf8_next(&p)) != 0) {
        if (cp == ' ') { x += (px * 30) / 100; continue; }
        const nav_glyph_ent_t *gl = nav_get_glyph(cp, px, bold, prefer_latin);
        if (!gl) continue;
        if (gl->bm && gl->w > 0 && gl->h > 0) {
            int gx = x + gl->xoff;
            int gy = base_y + gl->yoff;
            for (int row = 0; row < gl->h; row++) {
                int py = gy + row;
                if (py < 0 || py >= g_nav_h) continue;
                const unsigned char *src = gl->bm + row * gl->w;
                for (int col = 0; col < gl->w; col++) {
                    int cov = src[col];
                    if (!cov) continue;
                    /* 轻微伽马增强让小字更锐利 */
                    cov = cov + (cov * (255 - cov)) / 512;
                    if (alpha < 255) cov = (cov * alpha + 127) / 255;
                    nav_blend_px(gx + col, py, r, g, b, cov);
                }
            }
        }
        x += gl->adv;
    }
    return x;
}

/* 在 max_w 内截断单行文字并加省略号 */
static void nav_draw_text_clip(const char *s, int x, int base_y, int max_w, int px, int bold,
                               int prefer_latin, int r, int g, int b, int alpha) {
    if (!s || !s[0] || max_w <= 0) return;
    if (nav_text_width(s, px, bold, prefer_latin) <= max_w) {
        nav_draw_text(s, x, base_y, px, bold, prefer_latin, r, g, b, alpha);
        return;
    }
    int ell_w = nav_text_width("...", px, bold, prefer_latin);
    int avail = max_w - ell_w;
    if (avail < 0) avail = 0;
    char buf[256];
    size_t bi = 0;
    int w = 0;
    const char *p = s;
    while (*p) {
        const char *prev = p;
        uint32_t cp = nav_utf8_next(&p);
        if (!cp) break;
        int cw = (cp == ' ') ? (px * 30) / 100 : 0;
        if (cp != ' ') {
            const nav_glyph_ent_t *gl = nav_get_glyph(cp, px, bold, prefer_latin);
            cw = gl ? gl->adv : 0;
        }
        if (w + cw > avail) break;
        size_t clen = (size_t)(p - prev);
        if (bi + clen + 4 >= sizeof(buf)) break;
        memcpy(buf + bi, prev, clen);
        bi += clen;
        w += cw;
    }
    memcpy(buf + bi, "...", 4);
    nav_draw_text(buf, x, base_y, px, bold, prefer_latin, r, g, b, alpha);
}

/* 将长路名自动按像素宽度折成 1~2 行（优先在空格处断开） */
static int nav_wrap_2lines(const char *s, int max_w, int px, int bold, char *l1, size_t sz1,
                           char *l2, size_t sz2) {
    l1[0] = l2[0] = '\0';
    if (!s || !s[0]) return 0;
    if (nav_text_width(s, px, bold, 0) <= max_w) {
        snprintf(l1, sz1, "%s", s);
        return 1;
    }
    const char *p = s;
    const char *cut = s;
    const char *last_sp = NULL;
    int w = 0;
    while (*p) {
        const char *prev = p;
        uint32_t cp = nav_utf8_next(&p);
        if (!cp) break;
        int cw = (cp == ' ') ? (px * 30) / 100 : 0;
        if (cp != ' ') {
            const nav_glyph_ent_t *gl = nav_get_glyph(cp, px, bold, 0);
            cw = gl ? gl->adv : 0;
        }
        if (w + cw > max_w) {
            cut = prev;
            break;
        }
        if (cp == ' ' && w > max_w / 3) last_sp = p;
        w += cw;
        cut = p;
    }
    if (last_sp && last_sp < cut && *last_sp) cut = last_sp;
    size_t n1 = (size_t)(cut - s);
    while (n1 > 0 && s[n1 - 1] == ' ') n1--;
    if (n1 >= sz1) n1 = sz1 - 1;
    memcpy(l1, s, n1);
    l1[n1] = '\0';

    const char *rest = cut;
    while (*rest == ' ') rest++;
    if (!*rest) return 1;
    snprintf(l2, sz2, "%s", rest);
    return 2;
}

/* ---- 抗锯齿 SDF 矢量图标渲染引擎（零重叠接缝） ---- */
static float nav_sdf_seg(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float d2 = bax * bax + bay * bay;
    float h = d2 > 1e-6f ? (pax * bax + pay * bay) / d2 : 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    float dx = pax - bax * h, dy = pay - bay * h;
    return nav_sqrtf(dx * dx + dy * dy);
}

/* 三角形有符号距离（内部为负，外部为正） */
static float nav_sdf_tri(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2) {
    float e0x = x1 - x0, e0y = y1 - y0;
    float e1x = x2 - x1, e1y = y2 - y1;
    float e2x = x0 - x2, e2y = y0 - y2;
    float v0x = px - x0, v0y = py - y0;
    float v1x = px - x1, v1y = py - y1;
    float v2x = px - x2, v2y = py - y2;
    float d0 = nav_sdf_seg(px, py, x0, y0, x1, y1);
    float d1 = nav_sdf_seg(px, py, x1, y1, x2, y2);
    float d2 = nav_sdf_seg(px, py, x2, y2, x0, y0);
    float d = d0 < d1 ? d0 : d1;
    if (d2 < d) d = d2;
    float c0 = e0x * v0y - e0y * v0x;
    float c1 = e1x * v1y - e1y * v1x;
    float c2 = e2x * v2y - e2y * v2x;
    int inside = (c0 >= 0 && c1 >= 0 && c2 >= 0) || (c0 <= 0 && c1 <= 0 && c2 <= 0);
    return inside ? -d : d;
}

/* 圆弧到点的距离（角度为标准弧度，screen 坐标系 +x 右、+y 下；a0 -> a1 按 ccw_screen=+1/-1 走） */
static float nav_sdf_arc(float px, float py, float cx, float cy, float R, float a0, float a1) {
    /* 假设 a0 <= a1（按递增弧度扫描，总跨度 <= 2*PI） */
    float dx = px - cx, dy = py - cy;
    float ang = nav_atan2f(dy, dx);
    float two_pi = 6.2831853f;
    while (ang < a0) ang += two_pi;
    while (ang > a0 + two_pi) ang -= two_pi;
    if (ang <= a1) {
        return nav_fabsf(nav_sqrtf(dx * dx + dy * dy) - R);
    }
    float ax0 = cx + R * nav_cosf(a0), ay0 = cy + R * nav_sinf(a0);
    float ax1 = cx + R * nav_cosf(a1), ay1 = cy + R * nav_sinf(a1);
    float dx0 = px - ax0, dy0 = py - ay0;
    float dx1 = px - ax1, dy1 = py - ay1;
    float d0 = nav_sqrtf(dx0 * dx0 + dy0 * dy0);
    float d1 = nav_sqrtf(dx1 * dx1 + dy1 * dy1);
    return d0 < d1 ? d0 : d1;
}

typedef struct {
    int   type;   /* 0=capsule(x0,y0,x1,y1,r), 1=tri(x0,y0,x1,y1,x2,y2), 2=arc(cx,cy,R,a0,a1,r), 3=circle(cx,cy,r) */
    int   layer;  /* 0=暗淡底图, 1=前景高亮 */
    float p[6];
} nav_prim_t;

#define NAV_MAX_PRIMS 32
typedef struct {
    nav_prim_t list[NAV_MAX_PRIMS];
    int        n;
} nav_shape_t;

static void nav_sh_cap(nav_shape_t *sh, int layer, float x0, float y0, float x1, float y1, float r) {
    if (sh->n >= NAV_MAX_PRIMS) return;
    nav_prim_t *q = &sh->list[sh->n++];
    q->type = 0; q->layer = layer;
    q->p[0] = x0; q->p[1] = y0; q->p[2] = x1; q->p[3] = y1; q->p[4] = r;
}

static void nav_sh_tri(nav_shape_t *sh, int layer, float x0, float y0, float x1, float y1, float x2, float y2) {
    if (sh->n >= NAV_MAX_PRIMS) return;
    nav_prim_t *q = &sh->list[sh->n++];
    q->type = 1; q->layer = layer;
    q->p[0] = x0; q->p[1] = y0; q->p[2] = x1; q->p[3] = y1; q->p[4] = x2; q->p[5] = y2;
}

static void nav_sh_arc(nav_shape_t *sh, int layer, float cx, float cy, float R, float a0, float a1, float r) {
    if (sh->n >= NAV_MAX_PRIMS) return;
    nav_prim_t *q = &sh->list[sh->n++];
    q->type = 2; q->layer = layer;
    q->p[0] = cx; q->p[1] = cy; q->p[2] = R; q->p[3] = a0; q->p[4] = a1; q->p[5] = r;
}

static void nav_sh_circ(nav_shape_t *sh, int layer, float cx, float cy, float r) {
    if (sh->n >= NAV_MAX_PRIMS) return;
    nav_prim_t *q = &sh->list[sh->n++];
    q->type = 3; q->layer = layer;
    q->p[0] = cx; q->p[1] = cy; q->p[2] = r;
}

/* 在 (tx,ty) 处沿单位方向 (dx,dy) 添加三角形箭头 */
static void nav_sh_head(nav_shape_t *sh, int layer, float tx, float ty, float dx, float dy, float len, float hw) {
    float l = nav_sqrtf(dx * dx + dy * dy);
    if (l < 1e-5f) return;
    dx /= l; dy /= l;
    float bx = tx - dx * len, by = ty - dy * len;
    float nx = -dy, ny = dx;
    nav_sh_tri(sh, layer, tx, ty, bx + nx * hw, by + ny * hw, bx - nx * hw, by - ny * hw);
}

/* 将形状组在局部坐标 [0..size]×[0..size] 栅格化到 (ox, oy) 处 */
static void nav_sh_render(const nav_shape_t *sh, int ox, int oy, int w, int h,
                          int dr, int dg, int db, int da,
                          int fr, int fg, int fb, int fa) {
    for (int y = 0; y < h; y++) {
        int py = oy + y;
        if (py < 0 || py >= g_nav_h) continue;
        float fy = (float)y + 0.5f;
        for (int x = 0; x < w; x++) {
            int px = ox + x;
            if (px < 0 || px >= g_nav_w) continue;
            float fx = (float)x + 0.5f;
            float dmin[2] = { 1e9f, 1e9f };
            for (int i = 0; i < sh->n; i++) {
                const nav_prim_t *q = &sh->list[i];
                float d = 1e9f;
                if (q->type == 0) {
                    d = nav_sdf_seg(fx, fy, q->p[0], q->p[1], q->p[2], q->p[3]) - q->p[4];
                } else if (q->type == 1) {
                    d = nav_sdf_tri(fx, fy, q->p[0], q->p[1], q->p[2], q->p[3], q->p[4], q->p[5]);
                } else if (q->type == 2) {
                    d = nav_sdf_arc(fx, fy, q->p[0], q->p[1], q->p[2], q->p[3], q->p[4]) - q->p[5];
                } else if (q->type == 3) {
                    float dcx = fx - q->p[0], dcy = fy - q->p[1];
                    d = nav_sqrtf(dcx * dcx + dcy * dcy) - q->p[2];
                }
                if (d < dmin[q->layer]) dmin[q->layer] = d;
            }
            if (dmin[0] < 0.7f && da > 0) {
                float cov = 0.55f - dmin[0];
                if (cov > 1.0f) cov = 1.0f;
                if (cov > 0.0f) nav_blend_px(px, py, dr, dg, db, (int)(da * cov + 0.5f));
            }
            if (dmin[1] < 0.7f && fa > 0) {
                float cov = 0.55f - dmin[1];
                if (cov > 1.0f) cov = 1.0f;
                if (cov > 0.0f) nav_blend_px(px, py, fr, fg, fb, (int)(fa * cov + 0.5f));
            }
        }
    }
}

/* ---- 转向图标生成（覆盖全部 iAP2 ManeuverType 0..53） ---- */
static void nav_build_turn_icon(nav_shape_t *sh, int S, int mtype, int junction, int exit_angle, int driving_side, int *rb_exit_num) {
    sh->n = 0;
    *rb_exit_num = 0;
    float s = (float)S / 64.0f;
    float st = 4.6f * s;     /* 主干半径（线宽 ~9.2px @ 64px） */
    float dst = 3.6f * s;    /* 暗淡辅线半径 */
    float ah_l = 16.0f * s;  /* 箭头长度 */
    float ah_w = 11.5f * s;  /* 箭头半宽 */
    const float PI = 3.14159265f;

    /* 环岛判断 */
    int is_rb = (junction == 1) || mtype == 6 || mtype == 7 || mtype == 19 || (mtype >= 28 && mtype <= 46);
    if (is_rb) {
        if (mtype >= 28 && mtype <= 46) *rb_exit_num = mtype - 27;
        float cx = 32.0f * s, cy = 28.0f * s, R = 15.0f * s;
        /* 暗淡完整环岛圆环 + 底部入口 */
        nav_sh_arc(sh, 0, cx, cy, R, 0.0f, 6.283185f, 3.2f * s);
        nav_sh_cap(sh, 0, cx, 57.0f * s, cx, cy + R, 3.6f * s);
        /* 出口角度：iAP2 0=直行(正上 -PI/2), +90=右(0), -90=左(-PI), ±180=掉头(+PI/2) */
        float ex_deg = (float)exit_angle;
        if (mtype == 19) ex_deg = 180.0f;
        else if (exit_angle == 0 && mtype >= 28 && mtype <= 46) {
            int k = mtype - 27;
            if (k == 1) ex_deg = driving_side ? -90.0f : 90.0f;
            else if (k == 2) ex_deg = 0.0f;
            else if (k == 3) ex_deg = driving_side ? 90.0f : -90.0f;
            else ex_deg = 180.0f;
        } else if (mtype == 6 || mtype == 7) {
            ex_deg = driving_side ? -60.0f : 60.0f;
        }
        float ex_ang = (-90.0f + ex_deg) * (PI / 180.0f);
        /* 入口位于正下方 +PI/2；右行逆时针绕行（屏幕坐标角度递减），左行顺时针（角度递增） */
        float ent = PI * 0.5f;
        if (!driving_side) {
            float a1 = ent, a0 = ex_ang;
            while (a0 >= a1) a0 -= 2.0f * PI;
            if (a1 - a0 < 0.35f) a0 = a1 - 0.8f;
            nav_sh_arc(sh, 1, cx, cy, R, a0, a1, 3.8f * s);
        } else {
            float a0 = ent, a1 = ex_ang;
            while (a1 <= a0) a1 += 2.0f * PI;
            if (a1 - a0 < 0.35f) a1 = a0 + 0.8f;
            nav_sh_arc(sh, 1, cx, cy, R, a0, a1, 3.8f * s);
        }
        nav_sh_cap(sh, 1, cx, 57.0f * s, cx, cy + R, 3.8f * s);
        float dx = nav_cosf(ex_ang), dy = nav_sinf(ex_ang);
        float sx0 = cx + dx * R, sy0 = cy + dy * R;
        float tx = cx + dx * (R + 13.0f * s), ty = cy + dy * (R + 13.0f * s);
        nav_sh_cap(sh, 1, sx0, sy0, cx + dx * (R + 6.0f * s), cy + dy * (R + 6.0f * s), 3.6f * s);
        nav_sh_head(sh, 1, tx, ty, dx, dy, 12.0f * s, 8.5f * s);
        return;
    }

    /* 到达目的地 */
    if (mtype == 10 || mtype == 12 || mtype == 24 || mtype == 25 || mtype == 27) {
        float cx = 32.0f * s, cy = 24.0f * s;
        /* 水滴/定位大头针 */
        nav_sh_circ(sh, 1, cx, cy, 14.0f * s);
        nav_sh_tri(sh, 1, cx - 12.0f * s, cy + 7.0f * s, cx + 12.0f * s, cy + 7.0f * s, cx, 53.0f * s);
        nav_sh_circ(sh, 0, cx, cy, 5.5f * s);
        nav_sh_cap(sh, 0, 18.0f * s, 57.0f * s, 46.0f * s, 57.0f * s, 2.2f * s);
        return;
    }

    switch (mtype) {
    case 1:  /* 左转 */
    case 20: /* 尽头左转 */
        if (mtype == 1) nav_sh_cap(sh, 0, 42.0f * s, 26.0f * s, 42.0f * s, 10.0f * s, dst);
        nav_sh_cap(sh, 1, 42.0f * s, 56.0f * s, 42.0f * s, 32.0f * s, st);
        nav_sh_arc(sh, 1, 32.0f * s, 32.0f * s, 10.0f * s, -PI * 0.5f, 0.0f, st);
        nav_sh_cap(sh, 1, 32.0f * s, 22.0f * s, 20.0f * s, 22.0f * s, st);
        nav_sh_head(sh, 1, 7.0f * s, 22.0f * s, -1.0f, 0.0f, ah_l, ah_w);
        break;

    case 2:  /* 右转 */
    case 21: /* 尽头右转 */
        if (mtype == 2) nav_sh_cap(sh, 0, 22.0f * s, 26.0f * s, 22.0f * s, 10.0f * s, dst);
        nav_sh_cap(sh, 1, 22.0f * s, 56.0f * s, 22.0f * s, 32.0f * s, st);
        nav_sh_arc(sh, 1, 32.0f * s, 32.0f * s, 10.0f * s, -PI, -PI * 0.5f, st);
        nav_sh_cap(sh, 1, 32.0f * s, 22.0f * s, 44.0f * s, 22.0f * s, st);
        nav_sh_head(sh, 1, 57.0f * s, 22.0f * s, 1.0f, 0.0f, ah_l, ah_w);
        break;

    case 13: /* 靠左 */
    case 49: /* 向左前方 */
    case 52: /* 左侧并入/切换高速 */
    case 22: /* 高速左侧出口 */
        nav_sh_cap(sh, 0, 38.0f * s, 56.0f * s, 38.0f * s, 14.0f * s, dst);
        nav_sh_head(sh, 0, 38.0f * s, 8.0f * s, 0.0f, -1.0f, 11.0f * s, 7.5f * s);
        nav_sh_cap(sh, 1, 38.0f * s, 56.0f * s, 38.0f * s, 36.0f * s, st);
        nav_sh_cap(sh, 1, 38.0f * s, 36.0f * s, 21.0f * s, 19.0f * s, st);
        nav_sh_head(sh, 1, 12.0f * s, 10.0f * s, -0.707f, -0.707f, ah_l, ah_w);
        break;

    case 14: /* 靠右 */
    case 50: /* 向右前方 */
    case 53: /* 右侧并入/切换高速 */
    case 8:  /* 高速出口（默认右侧，左行国家左侧） */
    case 23: /* 高速右侧出口 */
        if (mtype == 8 && driving_side == 1) {
            nav_sh_cap(sh, 0, 38.0f * s, 56.0f * s, 38.0f * s, 14.0f * s, dst);
            nav_sh_cap(sh, 1, 38.0f * s, 56.0f * s, 38.0f * s, 36.0f * s, st);
            nav_sh_cap(sh, 1, 38.0f * s, 36.0f * s, 21.0f * s, 19.0f * s, st);
            nav_sh_head(sh, 1, 12.0f * s, 10.0f * s, -0.707f, -0.707f, ah_l, ah_w);
        } else {
            nav_sh_cap(sh, 0, 26.0f * s, 56.0f * s, 26.0f * s, 14.0f * s, dst);
            nav_sh_head(sh, 0, 26.0f * s, 8.0f * s, 0.0f, -1.0f, 11.0f * s, 7.5f * s);
            nav_sh_cap(sh, 1, 26.0f * s, 56.0f * s, 26.0f * s, 36.0f * s, st);
            nav_sh_cap(sh, 1, 26.0f * s, 36.0f * s, 43.0f * s, 19.0f * s, st);
            nav_sh_head(sh, 1, 52.0f * s, 10.0f * s, 0.707f, -0.707f, ah_l, ah_w);
        }
        break;

    case 9:  /* 上匝道并入主路 */
        nav_sh_cap(sh, 0, 22.0f * s, 56.0f * s, 22.0f * s, 24.0f * s, dst);
        nav_sh_cap(sh, 1, 44.0f * s, 56.0f * s, 28.0f * s, 32.0f * s, st);
        nav_sh_cap(sh, 1, 28.0f * s, 32.0f * s, 28.0f * s, 20.0f * s, st);
        nav_sh_head(sh, 1, 28.0f * s, 7.0f * s, 0.0f, -1.0f, ah_l, ah_w);
        break;

    case 47: /* 向左后方急转 */
        nav_sh_cap(sh, 0, 44.0f * s, 24.0f * s, 44.0f * s, 10.0f * s, dst);
        nav_sh_cap(sh, 1, 44.0f * s, 56.0f * s, 44.0f * s, 24.0f * s, st);
        nav_sh_cap(sh, 1, 44.0f * s, 24.0f * s, 21.0f * s, 42.0f * s, st);
        nav_sh_head(sh, 1, 11.0f * s, 50.0f * s, -0.78f, 0.62f, ah_l, ah_w);
        break;

    case 48: /* 向右后方急转 */
        nav_sh_cap(sh, 0, 20.0f * s, 24.0f * s, 20.0f * s, 10.0f * s, dst);
        nav_sh_cap(sh, 1, 20.0f * s, 56.0f * s, 20.0f * s, 24.0f * s, st);
        nav_sh_cap(sh, 1, 20.0f * s, 24.0f * s, 43.0f * s, 42.0f * s, st);
        nav_sh_head(sh, 1, 53.0f * s, 50.0f * s, 0.78f, 0.62f, ah_l, ah_w);
        break;

    case 4:  /* 掉头 */
    case 18:
    case 26:
        if (!driving_side) {
            nav_sh_cap(sh, 1, 44.0f * s, 56.0f * s, 44.0f * s, 24.0f * s, st);
            nav_sh_arc(sh, 1, 32.0f * s, 24.0f * s, 12.0f * s, -PI, 0.0f, st);
            nav_sh_cap(sh, 1, 20.0f * s, 24.0f * s, 20.0f * s, 39.0f * s, st);
            nav_sh_head(sh, 1, 20.0f * s, 53.0f * s, 0.0f, 1.0f, ah_l, ah_w);
        } else {
            nav_sh_cap(sh, 1, 20.0f * s, 56.0f * s, 20.0f * s, 24.0f * s, st);
            nav_sh_arc(sh, 1, 32.0f * s, 24.0f * s, 12.0f * s, -PI, 0.0f, st);
            nav_sh_cap(sh, 1, 44.0f * s, 24.0f * s, 44.0f * s, 39.0f * s, st);
            nav_sh_head(sh, 1, 44.0f * s, 53.0f * s, 0.0f, 1.0f, ah_l, ah_w);
        }
        break;

    default: /* 直行 / 沿道路继续行驶 */
        nav_sh_cap(sh, 1, 32.0f * s, 56.0f * s, 32.0f * s, 20.0f * s, st);
        nav_sh_head(sh, 1, 32.0f * s, 7.0f * s, 0.0f, -1.0f, ah_l, ah_w);
        break;
    }
}

/* 将 iAP2 ManeuverType 转为简洁中文动作词 */
static void nav_action_label(int mtype, int junction, int driving_side, char *out, size_t sz) {
    if (mtype >= 28 && mtype <= 46) {
        snprintf(out, sz, "环岛第%d出口", mtype - 27);
        return;
    }
    if (junction == 1 && (mtype == 6 || mtype == 7)) {
        snprintf(out, sz, mtype == 6 ? "驶入环岛" : "驶出环岛");
        return;
    }
    switch (mtype) {
    case 1:  snprintf(out, sz, "前方左转"); break;
    case 2:  snprintf(out, sz, "前方右转"); break;
    case 3:  snprintf(out, sz, "前方直行"); break;
    case 4:
    case 18:
    case 26: snprintf(out, sz, "前方掉头"); break;
    case 0:
    case 5:  snprintf(out, sz, "沿路直行"); break;
    case 6:  snprintf(out, sz, "驶入环岛"); break;
    case 7:  snprintf(out, sz, "驶出环岛"); break;
    case 8:  snprintf(out, sz, driving_side ? "左侧出口" : "右侧出口"); break;
    case 9:  snprintf(out, sz, "驶入匝道"); break;
    case 10:
    case 12:
    case 27: snprintf(out, sz, "到达终点"); break;
    case 11: snprintf(out, sz, "开始导航"); break;
    case 13: snprintf(out, sz, "靠左行驶"); break;
    case 14: snprintf(out, sz, "靠右行驶"); break;
    case 15:
    case 16:
    case 17: snprintf(out, sz, "乘轮渡"); break;
    case 19: snprintf(out, sz, "环岛掉头"); break;
    case 20: snprintf(out, sz, "尽头左转"); break;
    case 21: snprintf(out, sz, "尽头右转"); break;
    case 22: snprintf(out, sz, "左侧出口"); break;
    case 23: snprintf(out, sz, "右侧出口"); break;
    case 24: snprintf(out, sz, "终点在左侧"); break;
    case 25: snprintf(out, sz, "终点在右侧"); break;
    case 47: snprintf(out, sz, "左后方急转"); break;
    case 48: snprintf(out, sz, "右后方急转"); break;
    case 49: snprintf(out, sz, "向左前方"); break;
    case 50: snprintf(out, sz, "向右前方"); break;
    case 51: snprintf(out, sz, "切换高速"); break;
    case 52: snprintf(out, sz, "靠左并入"); break;
    case 53: snprintf(out, sz, "靠右并入"); break;
    default: snprintf(out, sz, "继续行驶"); break;
    }
}

/* 简短动作词（用于 slot_1 “随后 120m 右转”） */
static const char *nav_short_action(int mtype, int driving_side) {
    if (mtype >= 28 && mtype <= 46) return "环岛出口";
    switch (mtype) {
    case 1: case 20: return "左转";
    case 2: case 21: return "右转";
    case 3: case 5: case 0: return "直行";
    case 4: case 18: case 19: case 26: return "掉头";
    case 6: return "进环岛";
    case 7: return "出环岛";
    case 8: return driving_side ? "左出口" : "右出口";
    case 9: return "上匝道";
    case 10: case 12: case 24: case 25: case 27: return "到达终点";
    case 13: case 49: case 52: return "靠左";
    case 14: case 50: case 53: return "靠右";
    case 22: return "左出口";
    case 23: return "右出口";
    case 47: return "左急转";
    case 48: return "右急转";
    default: return "直行";
    }
}

/* ---- 车道指引（0x5204）单条车道矢量箭头 ---- */
static int nav_snap_lane_bucket(int deg) {
    /* 返回 0:直行, 1:左前(-45), 2:左(-90), 3:左急(-135), 4:左掉头(-180),
     *      5:右前(+45), 6:右(+90), 7:右急(+135), 8:右掉头(+180) */
    while (deg > 180) deg -= 360;
    while (deg < -180) deg += 360;
    if (deg >= -22 && deg <= 22)   return 0;
    if (deg >= -67 && deg < -22)   return 1;
    if (deg >= -112 && deg < -67)  return 2;
    if (deg >= -157 && deg < -112) return 3;
    if (deg < -157)                return 4;
    if (deg > 22 && deg <= 67)     return 5;
    if (deg > 67 && deg <= 112)    return 6;
    if (deg > 112 && deg <= 157)   return 7;
    return 4; /* 中国右行默认左掉头 */
}

static void nav_add_lane_arrow(nav_shape_t *sh, int layer, int bucket, float sw, float sh_h) {
    float cx = sw * 0.5f;
    float yb = sh_h - 6.0f;
    float ym = sh_h * 0.56f;
    float yt = 7.0f;
    float st = sw < 32.0f ? 1.9f : 2.4f;
    float hl = sw < 32.0f ? 7.8f : 10.0f;
    float hw = sw < 32.0f ? 5.6f : 7.2f;
    float dx_max = sw * 0.40f;
    const float PI = 3.14159265f;

    switch (bucket) {
    case 0: /* 直行 */
        nav_sh_cap(sh, layer, cx, yb, cx, yt + hl * 0.7f, st);
        nav_sh_head(sh, layer, cx, yt, 0.0f, -1.0f, hl, hw);
        break;
    case 1: /* 左前 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym, st);
        nav_sh_cap(sh, layer, cx, ym, cx - dx_max * 0.65f, yt + 7.5f, st);
        nav_sh_head(sh, layer, cx - dx_max * 0.92f, yt + 2.0f, -0.64f, -0.77f, hl, hw);
        break;
    case 2: /* 左转 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym - 4.0f, st);
        nav_sh_cap(sh, layer, cx, ym - 4.0f, cx - dx_max * 0.58f, ym - 4.0f, st);
        nav_sh_head(sh, layer, cx - dx_max, ym - 4.0f, -1.0f, 0.0f, hl, hw);
        break;
    case 3: /* 左急转 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym - 6.0f, st);
        nav_sh_cap(sh, layer, cx, ym - 6.0f, cx - dx_max * 0.65f, ym + 3.0f, st);
        nav_sh_head(sh, layer, cx - dx_max, ym + 8.0f, -0.75f, 0.66f, hl, hw);
        break;
    case 4: { /* 掉头 */
        float r = dx_max * 0.42f;
        if (r < 4.2f) r = 4.2f;
        float top_y = yt + r + 5.0f;
        float rx = cx + r * 0.45f;
        float lx = rx - 2.0f * r;
        nav_sh_cap(sh, layer, rx, yb, rx, top_y, st);
        nav_sh_arc(sh, layer, rx - r, top_y, r, -PI, 0.0f, st);
        nav_sh_cap(sh, layer, lx, top_y, lx, top_y + 8.0f, st);
        nav_sh_head(sh, layer, lx, top_y + 15.0f, 0.0f, 1.0f, hl * 0.88f, hw * 0.88f);
        break;
    }
    case 5: /* 右前 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym, st);
        nav_sh_cap(sh, layer, cx, ym, cx + dx_max * 0.65f, yt + 7.5f, st);
        nav_sh_head(sh, layer, cx + dx_max * 0.92f, yt + 2.0f, 0.64f, -0.77f, hl, hw);
        break;
    case 6: /* 右转 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym - 4.0f, st);
        nav_sh_cap(sh, layer, cx, ym - 4.0f, cx + dx_max * 0.58f, ym - 4.0f, st);
        nav_sh_head(sh, layer, cx + dx_max, ym - 4.0f, 1.0f, 0.0f, hl, hw);
        break;
    case 7: /* 右急转 */
        nav_sh_cap(sh, layer, cx, yb, cx, ym - 6.0f, st);
        nav_sh_cap(sh, layer, cx, ym - 6.0f, cx + dx_max * 0.65f, ym + 3.0f, st);
        nav_sh_head(sh, layer, cx + dx_max, ym + 8.0f, 0.75f, 0.66f, hl, hw);
        break;
    }
}

/* ---- 路名清洗（保留完整路名与多方向，只去掉多余前导动作词） ---- */
static void nav_clean_road(const nav_state_t *st, char *out, size_t sz) {
    const char *raw = st->s0_road[0] ? st->s0_road : (st->s0_desc[0] ? st->s0_desc : st->cur_road);
    char tmp[256];
    size_t ti = 0;
    /* 去首尾空白，合并连续空格（包含全角空格 U+3000） */
    const char *p = raw ? raw : "";
    int prev_sp = 1;
    while (*p && ti + 4 < sizeof(tmp)) {
        if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80) {
            if (!prev_sp) { tmp[ti++] = ' '; prev_sp = 1; }
            p += 3;
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (!prev_sp) { tmp[ti++] = ' '; prev_sp = 1; }
            p++;
            continue;
        }
        tmp[ti++] = *p++;
        prev_sp = 0;
    }
    while (ti > 0 && tmp[ti - 1] == ' ') ti--;
    tmp[ti] = '\0';

    /* 剥离常见动作前缀（只剥开头，绝不截断路名长度） */
    static const char *prefixes[] = {
        "向右前方行驶进入", "向左前方行驶进入", "向右前方行驶", "向左前方行驶",
        "左转进入", "右转进入", "掉头进入", "调头进入", "直行进入",
        "靠左进入", "靠右进入", "请行驶到", "行驶到",
        "进入", "驶入", "去往", "前往", "沿着", "经由",
        NULL
    };
    const char *s = tmp;
    for (int round = 0; round < 2; round++) {
        int stripped = 0;
        for (int i = 0; prefixes[i]; i++) {
            size_t pl = strlen(prefixes[i]);
            if (strncmp(s, prefixes[i], pl) == 0) {
                const char *q = s + pl;
                while (*q == ' ') q++;
                if (*q) { s = q; stripped = 1; break; }
            }
        }
        if (!stripped) break;
    }

    if (!s[0] || !strcmp(s, "无名道路") || !strcmp(s, "无名路") || !strcmp(s, "未命名道路")) {
        s = "无名道路";
    }

    /* 若有出口号/指示牌且未包含在路名中，拼到前面 */
    if (st->s0_exit_info[0] && strstr(s, st->s0_exit_info) == NULL) {
        snprintf(out, sz, "%s · %s", st->s0_exit_info, s);
    } else {
        snprintf(out, sz, "%s", s);
    }
}

/* ---- 格式化距离 / 时间 / ETA ---- */
static void nav_fmt_dist(int meters, char *num, size_t nsz, const char **unit) {
    if (meters < 0) { snprintf(num, nsz, "-"); *unit = ""; return; }
    if (meters < 1000) {
        snprintf(num, nsz, "%d", meters);
        *unit = "m";
    } else if (meters < 100000) {
        /* 1.0 .. 99.9 km（四舍五入到 0.1 km） */
        int tenths = (meters + 50) / 100;
        snprintf(num, nsz, "%d.%d", tenths / 10, tenths % 10);
        *unit = "km";
    } else {
        snprintf(num, nsz, "%d", (meters + 500) / 1000);
        *unit = "km";
    }
}

static void nav_fmt_eta(const nav_state_t *st, char *out, size_t sz) {
    uint32_t ts = st->eta;
    /* 1577836800 = 2020-01-01 00:00:00 UTC；有效时间戳直接转本地时区，无视车机 1970 时钟 */
    if (ts < 1577836800U) {
        time_t now = time(NULL);
        if (now > 1577836800L && st->time_remaining >= 0) {
            ts = (uint32_t)((unsigned long)now + (unsigned long)st->time_remaining);
        } else {
            snprintf(out, sz, "--:--");
            return;
        }
    }
    int tz_sec = g_nav_conf.tz_offset * 3600;
    /* 四舍五入到最近分钟 (+30s) */
    uint32_t local = (uint32_t)((int64_t)ts + (int64_t)tz_sec + 30);
    uint32_t sod = local % 86400U;
    unsigned hh = sod / 3600U;
    unsigned mm = (sod % 3600U) / 60U;
    snprintf(out, sz, "%02u:%02u", hh, mm);
}

/* ---- 解析 /tmp/cluster_cp.state ---- */
static int nav_to_int(const char *s, int def) {
    if (!s) return def;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (*s < '0' || *s > '9') return def;
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10L + (*s - '0'); s++; }
    return (int)(neg ? -v : v);
}

static uint32_t nav_to_u32(const char *s) {
    if (!s || !*s) return 0;
    uint32_t v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static void nav_parse_angles_csv(const char *csv, int *out, int *cnt) {
    int n = 0;
    const char *p = csv;
    while (p && *p && n < NAV_MAX_ANGLES) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        else if (*p == '+') p++;
        if (*p >= '0' && *p <= '9') {
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (neg) v = -v;
            if (v != 1000) out[n++] = v;
        }
        while (*p && *p != ',') p++;
    }
    *cnt = n;
}

static void nav_parse_state(const char *buf, nav_state_t *st) {
    memset(st, 0, sizeof(*st));
    st->route_state      = -1;
    st->src_supports_rg  = -1;
    st->cur_slot         = -1;
    st->dist_to_maneuver = -1;
    st->dist_remaining   = -1;
    st->time_remaining   = -1;
    st->s0_type          = -1;
    st->s1_type          = -1;
    for (int i = 0; i < NAV_MAX_LANES; i++) {
        st->lanes[i].pos = i;
        st->lanes[i].dir = 1000;
    }

    const char *p = buf;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r') ll--;
        if (ll > 0 && p[0] != '@') {
            const char *c1 = nav_memchr(p, ':', ll);
            if (c1) {
                size_t rem = ll - (size_t)(c1 - p) - 1;
                const char *c2 = nav_memchr(c1 + 1, ':', rem);
                if (c2) {
                    char key[48], val[256];
                    size_t kl = (size_t)(c1 - p);
                    size_t vl = ll - (size_t)(c2 - p) - 1;
                    if (kl >= sizeof(key)) kl = sizeof(key) - 1;
                    if (vl >= sizeof(val)) vl = sizeof(val) - 1;
                    memcpy(key, p, kl); key[kl] = '\0';
                    memcpy(val, c2 + 1, vl); val[vl] = '\0';

                    if (!strcmp(key, "session_active"))      st->session_active = (!strncmp(val, "true", 4) || val[0] == '1');
                    else if (!strcmp(key, "route_state"))    st->route_state = nav_to_int(val, -1);
                    else if (!strcmp(key, "source_supports_rg")) st->src_supports_rg = nav_to_int(val, -1);
                    else if (!strcmp(key, "slot_count"))     st->slot_count = nav_to_int(val, 0);
                    else if (!strcmp(key, "current_slot"))   st->cur_slot = nav_to_int(val, -1);
                    else if (!strcmp(key, "dist_to_maneuver")) st->dist_to_maneuver = nav_to_int(val, -1);
                    else if (!strcmp(key, "distance_remaining")) st->dist_remaining = nav_to_int(val, -1);
                    else if (!strcmp(key, "time_remaining")) st->time_remaining = nav_to_int(val, -1);
                    else if (!strcmp(key, "eta"))            st->eta = nav_to_u32(val);
                    else if (!strcmp(key, "current_road"))   snprintf(st->cur_road, sizeof(st->cur_road), "%s", val);
                    else if (!strcmp(key, "destination"))    snprintf(st->destination, sizeof(st->destination), "%s", val);
                    else if (!strcmp(key, "source_name"))    snprintf(st->source_name, sizeof(st->source_name), "%s", val);
                    else if (!strcmp(key, "slot_0_type"))    st->s0_type = nav_to_int(val, -1);
                    else if (!strcmp(key, "slot_0_driving_side")) st->s0_driving_side = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_0_junction_type")) st->s0_junction = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_0_exit_angle")) st->s0_exit_angle = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_0_road"))    snprintf(st->s0_road, sizeof(st->s0_road), "%s", val);
                    else if (!strcmp(key, "slot_0_description")) snprintf(st->s0_desc, sizeof(st->s0_desc), "%s", val);
                    else if (!strcmp(key, "slot_0_exit_info")) snprintf(st->s0_exit_info, sizeof(st->s0_exit_info), "%s", val);
                    else if (!strcmp(key, "slot_1_type"))    st->s1_type = nav_to_int(val, -1);
                    else if (!strcmp(key, "slot_1_driving_side")) st->s1_driving_side = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_1_junction_type")) st->s1_junction = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_1_exit_angle")) st->s1_exit_angle = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_1_distance")) st->s1_dist = nav_to_int(val, 0);
                    else if (!strcmp(key, "slot_1_road"))    snprintf(st->s1_road, sizeof(st->s1_road), "%s", val);
                    else if (!strcmp(key, "lane_active"))    st->lane_active = (!strncmp(val, "true", 4) || val[0] == '1');
                    else if (!strcmp(key, "lane_count"))     st->lane_count = nav_to_int(val, 0);
                    else if (!strncmp(key, "lane_", 5) && key[5] >= '0' && key[5] <= '7' && key[6] == '_') {
                        int li = key[5] - '0';
                        const char *f = key + 7;
                        if (!strcmp(f, "pos"))         st->lanes[li].pos = nav_to_int(val, li);
                        else if (!strcmp(f, "dir"))    st->lanes[li].dir = nav_to_int(val, 1000);
                        else if (!strcmp(f, "status")) st->lanes[li].status = nav_to_int(val, 0);
                        else if (!strcmp(f, "angles")) nav_parse_angles_csv(val, st->lanes[li].angles, &st->lanes[li].nangles);
                    }
                }
            }
        }
        if (!eol) break;
        p = eol + 1;
    }

    if (st->lane_count > NAV_MAX_LANES) st->lane_count = NAV_MAX_LANES;
    if (st->lane_count < 0) st->lane_count = 0;
    st->live = st->session_active && st->route_state >= 1 && st->route_state != 2 && st->src_supports_rg != 0;
}

/* ---- 渲染整张导航信息卡片 ---- */
static void nav_render(const nav_state_t *st) {
    const int W = g_nav_w, H = g_nav_h;
    const int pad_x = 14;
    const int inner_w = W - pad_x * 2;
    const int has_lanes = st->lane_active && st->lane_count > 0;

    /* 1. 背景（与左框完全一致的深灰半透明圆角底） */
    nav_fill_rrect_bg(W, H, 16, 16, 18, 22, g_nav_alpha);

    /* 2. 顶部主转向区（箭头 + 距离 + 动作词） */
    int hero_y = has_lanes ? 12 : 14;
    int icon_s = has_lanes ? 62 : 66;
    int mtype  = (st->slot_count > 0 && st->cur_slot >= 0 && st->s0_type >= 0) ? st->s0_type : 5;
    nav_shape_t icon_sh;
    int rb_num = 0;
    nav_build_turn_icon(&icon_sh, icon_s, mtype, st->s0_junction, st->s0_exit_angle, st->s0_driving_side, &rb_num);
    nav_sh_render(&icon_sh, pad_x - 2, hero_y, icon_s, icon_s,
                  105, 114, 126, 210,
                  255, 255, 255, 255);
    if (rb_num > 0 && rb_num <= 19) {
        char rbs[8];
        snprintf(rbs, sizeof(rbs), "%d", rb_num);
        int rw = nav_text_width(rbs, 18, 0, 1);
        int rx = (pad_x - 2) + icon_s / 2 - rw / 2;
        int ry = hero_y + (int)(icon_s * 28.0f / 64.0f) + 6;
        nav_draw_text(rbs, rx, ry, 18, 0, 1, 255, 255, 255, 255);
    }

    int tx = pad_x + icon_s + 8;
    int tw = W - pad_x - tx;
    /* 距离数字 + 单位 */
    char dnum[16];
    const char *dunit = "";
    nav_fmt_dist(st->dist_to_maneuver, dnum, sizeof(dnum), &dunit);
    int d_px = has_lanes ? 38 : 40;
    int u_px = 20;
    int d_base = hero_y + nav_font_ascent(d_px, 1) + 2;
    int after_d = nav_draw_text(dnum, tx, d_base, d_px, 0, 1, 255, 255, 255, 255);
    if (dunit[0]) {
        nav_draw_text(dunit, after_d + 4, d_base - 1, u_px, 0, 1, 168, 178, 190, 255);
    }

    /* 动作词（如 前方左转 / 右侧出口 / 环岛第2出口） */
    char act[32];
    nav_action_label(mtype, st->s0_junction, st->s0_driving_side, act, sizeof(act));
    int act_px = has_lanes ? 19 : 20;
    int act_base = hero_y + icon_s - 4;
    nav_draw_text_clip(act, tx, act_base, tw, act_px, 1, 0, 108, 200, 255, 255);

    /* 3. 完整路名区（最多 2 行，彻底解决 HUD 5 字显示不全） */
    char road[256], line1[160], line2[160];
    nav_clean_road(st, road, sizeof(road));
    int road_px = has_lanes ? 21 : 23;
    int road_lh = has_lanes ? 24 : 27;
    int nlines = nav_wrap_2lines(road, inner_w, road_px, 1, line1, sizeof(line1), line2, sizeof(line2));
    if (!has_lanes && nlines == 1 && nav_text_width(line1, 25, 1, 0) <= inner_w) {
        road_px = 25;
    }
    int road_top = hero_y + icon_s + (has_lanes ? 6 : 8);
    int road_asc = nav_font_ascent(road_px, 0);
    if (nlines == 1) {
        int cy = road_top + (has_lanes ? 24 : 27) - road_px / 2 + road_asc;
        nav_draw_text_clip(line1, pad_x, cy, inner_w, road_px, 1, 0, 245, 248, 252, 255);
    } else if (nlines >= 2) {
        nav_draw_text_clip(line1, pad_x, road_top + road_asc, inner_w, road_px, 1, 0, 245, 248, 252, 255);
        nav_draw_text_clip(line2, pad_x, road_top + road_lh + road_asc, inner_w, road_px, 1, 0, 232, 238, 245, 255);
    }

    /* 4. 中间动态区：有车道数据时画车道指引条，无车道数据时画下下路口预告 (slot_1) */
    int div_trip_y = H - 124;   /* 底部 3 行行程区预留固定 124px，确保三项永远舒展 */
    if (has_lanes) {
        int bar_top = road_top + (has_lanes ? 52 : 58);
        if (bar_top > div_trip_y - 62) bar_top = div_trip_y - 62;
        nav_draw_hline(pad_x, W - pad_x, bar_top - 4, 255, 255, 255, 36);

        int n = st->lane_count;
        int ord[NAV_MAX_LANES];
        for (int i = 0; i < n; i++) ord[i] = i;
        for (int i = 1; i < n; i++) {
            int k = ord[i], j = i - 1;
            while (j >= 0 && st->lanes[ord[j]].pos > st->lanes[k].pos) { ord[j + 1] = ord[j]; j--; }
            ord[j + 1] = k;
        }
        int bar_h = div_trip_y - bar_top - 6;
        if (bar_h > 58) bar_h = 58;
        if (bar_h < 42) bar_h = 42;
        int slot_w = (inner_w + 4) / n;
        if (slot_w > 46) slot_w = 46;
        int total_w = slot_w * n;
        int start_x = (W - total_w) / 2;

        for (int i = 0; i < n; i++) {
            const nav_lane_t *ln = &st->lanes[ord[i]];
            int lx = start_x + i * slot_w;
            /* 车道间虚线分隔 */
            if (i > 0) {
                for (int dy = 6; dy < bar_h - 8; dy += 6) {
                    nav_blend_px(lx, bar_top + dy, 255, 255, 255, 45);
                    nav_blend_px(lx, bar_top + dy + 1, 255, 255, 255, 45);
                    nav_blend_px(lx, bar_top + dy + 2, 255, 255, 255, 45);
                }
            }
            nav_shape_t lsh;
            lsh.n = 0;
            int has_hi = (ln->status >= 1);
            int hi_bucket = (ln->dir != 1000) ? nav_snap_lane_bucket(ln->dir) : -1;
            int drawn_mask = 0;
            /* 先把全部合法方向画进暗层 */
            for (int k = 0; k < ln->nangles; k++) {
                int b = nav_snap_lane_bucket(ln->angles[k]);
                if (drawn_mask & (1 << b)) continue;
                drawn_mask |= (1 << b);
                if (has_hi && (b == hi_bucket || hi_bucket < 0)) {
                    nav_add_lane_arrow(&lsh, 1, b, (float)slot_w, (float)(bar_h - 4));
                } else {
                    nav_add_lane_arrow(&lsh, 0, b, (float)slot_w, (float)(bar_h - 4));
                }
            }
            /* 若高亮方向不在 angles 里也补画 */
            if (has_hi && hi_bucket >= 0 && !(drawn_mask & (1 << hi_bucket))) {
                nav_add_lane_arrow(&lsh, 1, hi_bucket, (float)slot_w, (float)(bar_h - 4));
            }
            /* status==2 推荐车道：亮翡翠绿 + 底部绿色指示条；status==1 可走车道：纯白 */
            if (ln->status == 2) {
                nav_sh_render(&lsh, lx, bar_top, slot_w, bar_h - 4,
                              105, 114, 126, 180,
                              72, 232, 134, 255);
                nav_overlay_rrect(lx + 6, bar_top + bar_h - 4, slot_w - 12, 3, 1, 72, 232, 134, 235);
            } else if (ln->status == 1) {
                nav_sh_render(&lsh, lx, bar_top, slot_w, bar_h - 4,
                              105, 114, 126, 180,
                              240, 245, 252, 255);
                nav_overlay_rrect(lx + 8, bar_top + bar_h - 4, slot_w - 16, 2, 1, 220, 230, 240, 160);
            } else {
                nav_sh_render(&lsh, lx, bar_top, slot_w, bar_h - 4,
                              96, 104, 114, 170,
                              96, 104, 114, 170);
            }
        }
    } else {
        /* 无车道数据时显示下下路口预告 (slot_1) 或当前道路 / 导航源 */
        int pill_h = 36;
        int pill_y = div_trip_y - pill_h - 10;
        if (st->slot_count >= 2 && st->s1_type >= 0 && st->s1_type != 255) {
            nav_overlay_rrect(pad_x, pill_y, inner_w, pill_h, 9, 255, 255, 255, 20);
            nav_shape_t msh;
            int dummy_rb = 0;
            nav_build_turn_icon(&msh, 24, st->s1_type, st->s1_junction, st->s1_exit_angle, st->s1_driving_side, &dummy_rb);
            nav_sh_render(&msh, pad_x + 8, pill_y + 6, 24, 24,
                          120, 128, 140, 180,
                          235, 242, 250, 240);
            char next_txt[128];
            const char *sa = nav_short_action(st->s1_type, st->s1_driving_side);
            if (st->s1_dist > 0 && st->s1_dist < 200000) {
                char dn[16]; const char *du = "";
                nav_fmt_dist(st->s1_dist, dn, sizeof(dn), &du);
                snprintf(next_txt, sizeof(next_txt), "随后 %s%s %s", dn, du, sa);
            } else {
                snprintf(next_txt, sizeof(next_txt), "随后 %s", sa);
            }
            int np_px = 18;
            int np_base = pill_y + (pill_h - np_px) / 2 + nav_font_ascent(np_px, 0) - 1;
            nav_draw_text_clip(next_txt, pad_x + 38, np_base, inner_w - 46, np_px, 1, 0, 215, 224, 235, 255);
        } else if (st->cur_road[0]) {
            nav_overlay_rrect(pad_x, pill_y, inner_w, pill_h, 9, 255, 255, 255, 18);
            char cur_txt[160];
            snprintf(cur_txt, sizeof(cur_txt), "当前 %s", st->cur_road);
            int np_px = 18;
            int np_base = pill_y + (pill_h - np_px) / 2 + nav_font_ascent(np_px, 0) - 1;
            nav_draw_text_clip(cur_txt, pad_x + 10, np_base, inner_w - 20, np_px, 0, 0, 195, 205, 218, 255);
        } else if (st->source_name[0]) {
            nav_overlay_rrect(pad_x, pill_y, inner_w, pill_h, 9, 255, 255, 255, 16);
            char src_txt[96];
            snprintf(src_txt, sizeof(src_txt), "%s · 导航中", st->source_name);
            int np_px = 17;
            int np_base = pill_y + (pill_h - np_px) / 2 + nav_font_ascent(np_px, 0) - 1;
            nav_draw_text_clip(src_txt, pad_x + 10, np_base, inner_w - 20, np_px, 0, 0, 175, 185, 198, 255);
        }
    }

    /* 5. 底部行程 3 行（剩余路程 / 剩余时间 / 预计到达），样式与左侧数据框统一 */
    nav_draw_hline(pad_x, W - pad_x, div_trip_y, 255, 255, 255, 38);
    int trip_top = div_trip_y + 8;
    int row_h = (H - 10 - trip_top) / 3;
    if (row_h < 32) row_h = 32;
    int lab_px = 21;
    int val_px = 28;
    int unit_px = 18;
    int val_asc = nav_font_ascent(val_px, 1);

    /* 行 0：剩余路程 */
    {
        int ry = trip_top + 0 * row_h;
        int base = ry + (row_h - val_px) / 2 + val_asc;
        nav_draw_text("剩余路程", pad_x, base - 1, lab_px, 0, 0, 165, 172, 180, 255);

        char dn[16]; const char *du = "";
        nav_fmt_dist(st->dist_remaining, dn, sizeof(dn), &du);
        int uw = du[0] ? nav_text_width(du, unit_px, 0, 1) : 0;
        int vw = nav_text_width(dn, val_px, 0, 1);
        int ux = W - pad_x - uw;
        int vx = (du[0] ? (ux - 5) : (W - pad_x)) - vw;
        nav_draw_text(dn, vx, base, val_px, 0, 1, 255, 255, 255, 255);
        if (du[0]) nav_draw_text(du, ux, base - 1, unit_px, 0, 1, 155, 163, 172, 255);
    }

    /* 行 1：剩余时间 */
    {
        int ry = trip_top + 1 * row_h;
        int base = ry + (row_h - val_px) / 2 + val_asc;
        nav_draw_text("剩余时间", pad_x, base - 1, lab_px, 0, 0, 165, 172, 180, 255);

        if (st->time_remaining < 0 || st->time_remaining >= 360000) {
            int vw = nav_text_width("-", val_px, 0, 1);
            nav_draw_text("-", W - pad_x - vw, base, val_px, 0, 1, 255, 255, 255, 255);
        } else {
            int mins = (st->time_remaining + 59) / 60;
            if (mins < 1 && st->time_remaining > 0) mins = 1;
            if (mins < 60) {
                char mn[16];
                snprintf(mn, sizeof(mn), "%d", mins);
                const char *mu = "分钟";
                int uw = nav_text_width(mu, unit_px, 0, 0);
                int vw = nav_text_width(mn, val_px, 0, 1);
                int ux = W - pad_x - uw;
                int vx = ux - 4 - vw;
                nav_draw_text(mn, vx, base, val_px, 0, 1, 255, 255, 255, 255);
                nav_draw_text(mu, ux, base - 1, unit_px, 0, 0, 155, 163, 172, 255);
            } else {
                int hh = mins / 60, mm = mins % 60;
                char hs[12], ms[12];
                snprintf(hs, sizeof(hs), "%d", hh);
                snprintf(ms, sizeof(ms), "%d", mm);
                int w_h = nav_text_width(hs, val_px, 0, 1);
                int w_hu = nav_text_width("时", unit_px, 0, 0);
                int w_m = mm > 0 ? nav_text_width(ms, val_px, 0, 1) : 0;
                int w_mu = mm > 0 ? nav_text_width("分", unit_px, 0, 0) : 0;
                int tot = w_h + 2 + w_hu + (mm > 0 ? (4 + w_m + 2 + w_mu) : 0);
                int cx = W - pad_x - tot;
                cx = nav_draw_text(hs, cx, base, val_px, 0, 1, 255, 255, 255, 255) + 2;
                cx = nav_draw_text("时", cx, base - 1, unit_px, 0, 0, 155, 163, 172, 255);
                if (mm > 0) {
                    cx += 4;
                    cx = nav_draw_text(ms, cx, base, val_px, 0, 1, 255, 255, 255, 255) + 2;
                    nav_draw_text("分", cx, base - 1, unit_px, 0, 0, 155, 163, 172, 255);
                }
            }
        }
    }

    /* 行 2：预计到达 */
    {
        int ry = trip_top + 2 * row_h;
        int base = ry + (row_h - val_px) / 2 + val_asc;
        nav_draw_text("预计到达", pad_x, base - 1, lab_px, 0, 0, 165, 172, 180, 255);

        char eta_s[16];
        nav_fmt_eta(st, eta_s, sizeof(eta_s));
        int vw = nav_text_width(eta_s, val_px, 0, 1);
        nav_draw_text(eta_s, W - pad_x - vw, base, val_px, 0, 1, 255, 255, 255, 255);
    }
}

/* 配置行解析（由 dash_read_conf_line 调用） */
static int nav_read_conf_line(const char *l) {
    int v;
    char s[32];
    while (*l == ' ' || *l == '\t') l++;
    if (*l == '#') return 0;
    if      (sscanf(l, "nav_box = %d", &v) == 1)   { g_nav_conf.enabled = v; return 1; }
    else if (sscanf(l, "nav_side = %31s", s) == 1) {
        if (s[0] == 'l' || s[0] == 'L' || s[0] == '0') g_nav_conf.side = 0;
        else if (s[0] == 'r' || s[0] == 'R' || s[0] == '1') g_nav_conf.side = 1;
        return 1;
    }
    else if (sscanf(l, "nav_x = %d", &v) == 1)     { g_nav_conf.x = v; return 1; }
    else if (sscanf(l, "nav_y = %d", &v) == 1)     { g_nav_conf.y = v; return 1; }
    else if (sscanf(l, "nav_right = %d", &v) == 1) { g_nav_conf.right = v; return 1; }
    else if (sscanf(l, "nav_alpha = %d", &v) == 1) { g_nav_conf.alpha = v < 0 ? 0 : (v > 255 ? 255 : v); return 1; }
    else if (sscanf(l, "nav_tz = %d", &v) == 1)    { if (v >= -12 && v <= 14) g_nav_conf.tz_offset = v; return 1; }
    return 0;
}

/* 轮询 /tmp/cluster_cp.state；返回 1 表示导航框显示状态或内容发生变化 */
static int nav_poll(void) {
    g_nav_dirty = 0;
    if (!g_nav_conf.enabled) {
        if (g_nav_active) { g_nav_active = 0; return 1; }
        return 0;
    }
    /* 运行时临时关闭开关 /tmp/navbox_off */
    FILE *off = fopen(NAV_OFF_FLAG, "r");
    if (off) {
        fclose(off);
        if (g_nav_active) { g_nav_active = 0; return 1; }
        return 0;
    }

    FILE *f = fopen(NAV_STATE_PATH, "r");
    if (!f) {
        if (g_nav_active && ++g_nav_not_live >= 5) {
            g_nav_active = 0;
            g_nav_raw_len = -1;
            return 1;
        }
        return 0;
    }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* 必须读到完整快照（含 current_slot 且以换行结束），防撕裂读 */
    if (n == 0 || buf[n - 1] != '\n' || strstr(buf, "current_slot:") == NULL) {
        if (g_nav_active && ++g_nav_not_live >= 5) {
            g_nav_active = 0;
            g_nav_raw_len = -1;
            return 1;
        }
        return 0;
    }

    if ((int)n == g_nav_raw_len && memcmp(buf, g_nav_raw_prev, n) == 0) {
        if (g_nav_active) g_nav_not_live = 0;
        return 0;
    }

    nav_state_t st;
    nav_parse_state(buf, &st);
    if (!st.live) {
        if (g_nav_active && ++g_nav_not_live >= 5) {
            g_nav_active = 0;
            g_nav_raw_len = (int)n;
            memcpy(g_nav_raw_prev, buf, n + 1);
            return 1;
        }
        return 0;
    }

    g_nav_not_live = 0;
    g_nav_raw_len = (int)n;
    memcpy(g_nav_raw_prev, buf, n + 1);
    nav_render(&st);
    g_nav_active = 1;
    g_nav_dirty = 1;
    return 1;
}
