/*
 * vhpatch - 在车机端原地对 MH2p (QNX 6.6) 皮肤 viewhandler.zip 打补丁 / 还原补丁。
 *
 * 全屏 CarPlay 仅修改车机自有 /mnt/app/eso/hmi/lsd/Resources/skin3/viewhandler.zip 中的
 * 两个编译视图（CarPlay 与 AndroidAuto 全屏/边框几何常量，共 38 字节），不随 U 盘包分发任何大众原厂文件。
 *
 * 支持双模式（均要求严格校验、写前整包自检、100% 原地可逆还原）：
 *   1. 精确补丁表模式（优先）：按 fullscreen.patch 逐条核对条目名、大小、CRC-32 与每字节原值（如 P2878）。
 *   2. 跨版本字节码指纹模式（兜底）：面向 P2838 / P2853 / P2869 / P2876 / P2497 / P9832 等其余国行
 *      15 寸 Discover Premium (VWG36) P 版固件；在皮肤 .jxe 内按 J9 字节码唯一指纹自动定位构造矩形与
 *      两个边框 StaticImage，必须全量唯一命中、逐字节核对后动态生成同构补丁表并走同一套安全重写与自检流程；
 *      若任意条件不符或存在歧义，立即返回 unknown 且不触碰任何文件。
 *
 * 用法：
 *   vhpatch check  <zip> <patch>          打印 "stock"、"patched" 或 "unknown"（退出码 0 / 0 / 3）
 *   vhpatch apply  <zip> <patch> <out>    stock   -> patched 写入 <out>（成功退出码 0）
 *   vhpatch revert <zip> <patch> <out>    patched -> stock   写入 <out>
 *   vhpatch probe  <zip> <patch>          只读诊断输出（用于排查未知固件版本，不写任何文件）
 *
 * 除被修改的条目外，其余所有 zip 条目按原始压缩数据逐字节原样拷贝；内嵌 zlib，不依赖车机 libz。
 * Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"
#ifdef __QNXNTO__
#include "aeabi_shim.h"   /* QNX 6.6 libc 缺少 clang 生成的 __aeabi_mem* 辅助函数（zlib 目标文件同样需要） */
#endif

#define MAXP 8            /* 最多补丁条目数 */
#define MAXB 256          /* 单条目最多修改字节数 */
#define GEN_MIN_JXE 4096u
#define GEN_MAX_JXE 524288u

typedef struct {
    char name[128];
    uint32_t size, crc_stock, crc_patched;
    int nb;
    uint32_t off[MAXB];
    uint8_t from[MAXB], to[MAXB];
    int img_linked;       /* 仅诊断用：是否同步改写了 getImage(280 <-> 188) */
} pentry;

/* 跨版本 J9 字节码指纹规格（15 寸 Discover Premium 原厂 1152x592 / 1224x656 布局） */
typedef struct {
    const char *label;
    uint16_t ch_s, ch_p;  /* 构造函数 height */
    uint16_t cx_s, cx_p;  /* 构造函数 x */
    uint8_t  cy_s, cy_p;  /* 构造函数 y */
    uint16_t cw_s, cw_p;  /* 构造函数 width */
    uint16_t bh_s, bh_p;  /* 边框 StaticImage height */
    uint16_t bw_s, bw_p;  /* 边框 StaticImage width */
    uint16_t bx_s, bx_p;  /* 边框 StaticImage x */
    uint8_t  by_s, by_p;  /* 边框 StaticImage y */
} vspec;

static const vspec VSPEC[2] = {
    {
        "CarPlay",
        592u, 748u, 245u, 0u, 72u, 5u, 1152u, 1673u,
        594u, 750u, 1154u, 1675u, 244u, 0u, 71u, 0u
    },
    {
        "AndroidAuto",
        656u, 748u, 209u, 0u, 50u, 0u, 1224u, 1673u,
        658u, 758u, 1226u, 1675u, 208u, 0u, 49u, 0u
    }
};

static pentry P_EXACT[MAXP];
static int NP_EXACT;
static pentry P_GEN[MAXP];
static int NP_GEN;

static int die(const char *m) { fprintf(stderr, "vhpatch: %s\n", m); return 2; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int load_patch(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return die("cannot open patch file");
    NP_EXACT = 0;
    char l[512];
    while (fgets(l, sizeof l, f)) {
        if (l[0] == '#' || l[0] == '\n' || l[0] == '\r') continue;
        if (!strncmp(l, "entry ", 6)) {
            if (NP_EXACT >= MAXP) { fclose(f); return die("too many entries in patch"); }
            pentry *e = &P_EXACT[NP_EXACT++];
            memset(e, 0, sizeof *e);
            if (sscanf(l + 6, "%127s %u %x %x", e->name, &e->size, &e->crc_stock, &e->crc_patched) != 4) {
                fclose(f); return die("bad entry line");
            }
        } else {
            unsigned o = 0, a = 0, b = 0;
            if (!NP_EXACT || sscanf(l, "%u %x %x", &o, &a, &b) != 3) { fclose(f); return die("bad byte line"); }
            pentry *e = &P_EXACT[NP_EXACT - 1];
            if (e->nb >= MAXB || o >= e->size) { fclose(f); return die("bad byte offset"); }
            e->off[e->nb] = o; e->from[e->nb] = (uint8_t)a; e->to[e->nb] = (uint8_t)b; e->nb++;
        }
    }
    fclose(f);
    return NP_EXACT ? 0 : die("empty patch");
}

static uint8_t *load_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    *len = sz;
    uint8_t *b = malloc(sz > 0 ? (size_t)sz : 1u);
    if (b && sz > 0 && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    return b;
}

/* ZIP 中央目录定位 */
typedef struct { const uint8_t *cd; uint32_t cd_off, cd_size; uint16_t n; long eocd; } zipinfo;

static int zip_open(const uint8_t *z, long len, zipinfo *zi) {
    if (len < 22) return -1;
    long lo = len - 22 - 65535;
    if (lo < 0) lo = 0;
    for (long i = len - 22; i >= lo; i--) {
        if (rd32(z + i) == 0x06054b50u) {
            zi->eocd = i;
            zi->n = rd16(z + i + 10);
            zi->cd_size = rd32(z + i + 12);
            zi->cd_off = rd32(z + i + 16);
            if ((uint64_t)zi->cd_off + (uint64_t)zi->cd_size > (uint64_t)i) return -1;
            zi->cd = z + zi->cd_off;
            return 0;
        }
    }
    return -1;
}

static int find_patch_in(const pentry *tbl, int np, const char *name, int nlen) {
    for (int i = 0; i < np; i++) {
        if ((int)strlen(tbl[i].name) == nlen && !memcmp(tbl[i].name, name, (size_t)nlen)) return i;
    }
    return -1;
}

/* 1 = 补丁表全部条目均为 stock，2 = 全部均为 patched，0 = 其余任意状态 */
static int zip_state_with(const uint8_t *z, long len, const pentry *tbl, int np) {
    zipinfo zi;
    if (np <= 0 || zip_open(z, len, &zi)) return 0;
    int st = -1, found = 0;
    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        if (p + 46 > z + len || rd32(p) != 0x02014b50u) return 0;
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32);
        if (p + 46 + nl + el + cl > z + len) return 0;
        int k = find_patch_in(tbl, np, (const char *)p + 46, nl);
        if (k >= 0) {
            uint32_t crc = rd32(p + 16), us = rd32(p + 24);
            int s = us != tbl[k].size ? 0 : crc == tbl[k].crc_stock ? 1 : crc == tbl[k].crc_patched ? 2 : 0;
            if (!s || (st >= 0 && st != s)) return 0;
            st = s; found++;
        }
        p += 46 + nl + el + cl;
    }
    return found == np ? st : 0;
}

static uint8_t *inflate_raw(const uint8_t *src, uint32_t clen, uint32_t ulen) {
    uint8_t *out = malloc(ulen ? ulen : 1u);
    z_stream s; memset(&s, 0, sizeof s);
    if (!out || inflateInit2(&s, -15) != Z_OK) { free(out); return NULL; }
    s.next_in = (Bytef *)(uintptr_t)src; s.avail_in = clen; s.next_out = out; s.avail_out = ulen;
    int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END || s.total_out != ulen) { free(out); return NULL; }
    return out;
}

static uint8_t *deflate_raw(const uint8_t *src, uint32_t len, uint32_t *clen) {
    z_stream s; memset(&s, 0, sizeof s);
    if (deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return NULL;
    uLong cap = deflateBound(&s, len);
    uint8_t *out = malloc(cap);
    if (!out) { deflateEnd(&s); return NULL; }
    s.next_in = (Bytef *)(uintptr_t)src; s.avail_in = len; s.next_out = out; s.avail_out = (uInt)cap;
    int r = deflate(&s, Z_FINISH);
    *clen = (uint32_t)s.total_out;
    deflateEnd(&s);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    return out;
}

/* ---------- 跨版本 J9 字节码指纹匹配引擎 ---------- */

typedef struct {
    uint32_t pos;
    uint32_t off_bh, off_bw, off_bx, off_by;
    uint8_t  img_op;
    int32_t  img_val;
    uint32_t img_off;
    uint32_t off_alpha;
} border_hit;

static int add_diff_byte(pentry *e, uint32_t off, uint8_t from, uint8_t to) {
    if (from == to) return 0;
    if (e->nb >= MAXB || off >= e->size) return -1;
    e->off[e->nb] = off;
    e->from[e->nb] = from;
    e->to[e->nb] = to;
    e->nb++;
    return 0;
}

static int add_diff_u16(pentry *e, uint32_t off, uint16_t from, uint16_t to) {
    if (add_diff_byte(e, off, (uint8_t)(from & 0xffu), (uint8_t)(to & 0xffu))) return -1;
    if (add_diff_byte(e, off + 1u, (uint8_t)(from >> 8), (uint8_t)(to >> 8))) return -1;
    return 0;
}

/* 匹配构造函数内连续 4 条矩形属性赋值：
 * aload_0; sipush H; putfield ??; aload_0; sipush X; putfield ??;
 * aload_0; bipush Y; putfield ??; aload_0; sipush W; putfield ?? */
static int match_ctor_at(const uint8_t *u, uint32_t len, uint32_t pos, const vspec *sp, int patched) {
    if (pos + 27u > len) return 0;
    uint16_t h = patched ? sp->ch_p : sp->ch_s;
    uint16_t x = patched ? sp->cx_p : sp->cx_s;
    uint8_t  y = patched ? sp->cy_p : sp->cy_s;
    uint16_t w = patched ? sp->cw_p : sp->cw_s;
    const uint8_t *p = u + pos;
    if (p[0] != 0x2au || p[1] != 0x11u || rd16(p + 2) != h || p[4] != 0xb5u) return 0;
    if (p[7] != 0x2au || p[8] != 0x11u || rd16(p + 9) != x || p[11] != 0xb5u) return 0;
    if (p[14] != 0x2au || p[15] != 0x10u || p[16] != y || p[17] != 0xb5u) return 0;
    if (p[20] != 0x2au || p[21] != 0x11u || rd16(p + 22) != w || p[24] != 0xb5u) return 0;
    return 1;
}

/* 匹配边框 StaticImage 调用尾段（兼容 getImage 任意压栈形式及 ldc / ldc_w） */
static int match_border_at(const uint8_t *u, uint32_t len, uint32_t pos, const vspec *sp, int patched, border_hit *bh) {
    if (pos + 45u > len) return 0;
    const uint8_t *p = u + pos;
    if (p[0] != 0x03u || p[1] != 0xbcu || p[2] != 0x06u || p[3] != 0x03u || p[4] != 0xbcu || p[5] != 0x06u) return 0;
    if (p[6] != 0xb7u || p[9] != 0xb2u || p[12] != 0x11u) return 0;
    uint16_t want_h = patched ? sp->bh_p : sp->bh_s;
    if (rd16(p + 13) != want_h) return 0;
    if (p[15] != 0x2au) return 0;

    uint32_t cur = pos + 16u;
    if (cur >= len) return 0;
    uint8_t op = u[cur];
    uint8_t img_op = op;
    int32_t img_val = 0;
    uint32_t img_off = cur;
    if (op >= 0x02u && op <= 0x08u) {
        img_val = (int32_t)op - 3;
        cur += 1u;
    } else if (op == 0x10u) {
        if (cur + 2u > len) return 0;
        img_val = (int8_t)u[cur + 1u];
        img_off = cur + 1u;
        cur += 2u;
    } else if (op == 0x11u) {
        if (cur + 3u > len) return 0;
        img_val = (int16_t)rd16(u + cur + 1u);
        img_off = cur + 1u;
        cur += 3u;
    } else {
        return 0;
    }

    if (cur + 4u > len || u[cur] != 0xb6u || u[cur + 3u] != 0x04u) return 0;
    cur += 4u;

    for (int k = 0; k < 2; k++) {
        if (cur >= len) return 0;
        if (u[cur] == 0x12u) {
            if (cur + 2u > len) return 0;
            cur += 2u;
        } else if (u[cur] == 0x13u) {
            if (cur + 3u > len) return 0;
            cur += 3u;
        } else {
            return 0;
        }
    }

    if (cur + 23u > len) return 0;
    if (u[cur] != 0xbbu || u[cur + 3u] != 0x59u ||
        u[cur + 4u] != 0x03u || u[cur + 5u] != 0x03u || u[cur + 6u] != 0x03u || u[cur + 7u] != 0x03u ||
        u[cur + 8u] != 0xb7u) return 0;
    cur += 11u;

    uint16_t want_w = patched ? sp->bw_p : sp->bw_s;
    uint16_t want_x = patched ? sp->bx_p : sp->bx_s;
    uint8_t  want_y = patched ? sp->by_p : sp->by_s;
    if (u[cur] != 0x03u || u[cur + 1u] != 0x11u || rd16(u + cur + 2u) != want_w) return 0;
    if (u[cur + 4u] != 0x11u || rd16(u + cur + 5u) != want_x) return 0;
    if (u[cur + 7u] != 0x10u || u[cur + 8u] != want_y) return 0;
    if (u[cur + 9u] != 0xb8u) return 0;

    bh->pos = pos;
    bh->off_bh = pos + 13u;
    bh->off_bw = cur + 2u;
    bh->off_bx = cur + 5u;
    bh->off_by = cur + 8u;
    bh->img_op = img_op;
    bh->img_val = img_val;
    bh->img_off = img_off;
    bh->off_alpha = 0;
    return 1;
}

/* 在 border 块前 [pos-160, pos-20] 回溯唯一的 StaticImage alpha 赋值（32 c0 ?? ?? 04 11 alpha 00 04 bb ?? ?? 59 bb ?? ?? 59 02），
 * 且 alpha 与 border 之间不允许跨越另一条 32 c0 控件切换。 */
static int find_alpha_before(const uint8_t *u, uint32_t border_pos, uint8_t want_alpha, uint32_t *out_off) {
    if (border_pos < 20u) return 0;
    uint32_t lo = border_pos > 160u ? border_pos - 160u : 0u;
    uint32_t hi = border_pos - 20u;
    int hits = 0;
    uint32_t found = 0;
    for (uint32_t p = lo; p <= hi; p++) {
        if (p + 18u > border_pos) break;
        const uint8_t *q = u + p;
        if (q[0] == 0x32u && q[1] == 0xc0u && q[4] == 0x04u && q[5] == 0x11u &&
            q[6] == want_alpha && q[7] == 0x00u && q[8] == 0x04u &&
            q[9] == 0xbbu && q[12] == 0x59u && q[13] == 0xbbu && q[16] == 0x59u && q[17] == 0x02u) {
            int crossed = 0;
            for (uint32_t k = p + 4u; k + 1u < border_pos; k++) {
                if (u[k] == 0x32u && u[k + 1u] == 0xc0u) { crossed = 1; break; }
            }
            if (!crossed) {
                found = p + 6u;
                hits++;
            }
        }
    }
    if (hits != 1) return 0;
    *out_off = found;
    return 1;
}

/* 返回：0=无关文件，1=完整 stock，2=完整 patched，-1=半匹配/混合/歧义（危险状态） */
static int analyze_jxe_for_spec(const char *name, const uint8_t *u, uint32_t len, const vspec *sp, pentry *out) {
    int ctor_s = 0, ctor_p = 0;
    uint32_t ctor_s_pos = 0, ctor_p_pos = 0;
    int b_s = 0, b_p = 0, partial = 0;
    border_hit bs[2], bp[2];
    memset(bs, 0, sizeof bs);
    memset(bp, 0, sizeof bp);

    for (uint32_t i = 0; i < len; i++) {
        if (u[i] == 0x2au) {
            if (match_ctor_at(u, len, i, sp, 0)) { ctor_s_pos = i; ctor_s++; }
            if (match_ctor_at(u, len, i, sp, 1)) { ctor_p_pos = i; ctor_p++; }
        } else if (u[i] == 0x03u && i + 5u < len && u[i + 1u] == 0xbcu && u[i + 2u] == 0x06u) {
            border_hit tmp;
            if (match_border_at(u, len, i, sp, 0, &tmp)) {
                if (find_alpha_before(u, i, 0xffu, &tmp.off_alpha)) {
                    if (b_s < 2) bs[b_s] = tmp;
                    b_s++;
                } else {
                    partial++;
                }
            }
            if (match_border_at(u, len, i, sp, 1, &tmp)) {
                if (find_alpha_before(u, i, 0x00u, &tmp.off_alpha)) {
                    if (b_p < 2) bp[b_p] = tmp;
                    b_p++;
                } else {
                    partial++;
                }
            }
        }
    }

    if (ctor_s == 0 && ctor_p == 0 && b_s == 0 && b_p == 0 && partial == 0) return 0;

    int st = 0;
    uint32_t cpos = 0;
    const border_hit *bh = NULL;
    if (ctor_s == 1 && ctor_p == 0 && b_s == 2 && b_p == 0 && partial == 0) {
        st = 1; cpos = ctor_s_pos; bh = bs;
    } else if (ctor_p == 1 && ctor_s == 0 && b_p == 2 && b_s == 0 && partial == 0) {
        st = 2; cpos = ctor_p_pos; bh = bp;
    } else {
        return -1;
    }

    if (!out) return st;

    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name);
    out->size = len;

    /* 按升序记录构造函数与两个边框块的字节差（始终保持 from=stock, to=patched） */
    if (add_diff_u16(out, cpos + 2u,  sp->ch_s, sp->ch_p) ||
        add_diff_u16(out, cpos + 9u,  sp->cx_s, sp->cx_p) ||
        add_diff_byte(out, cpos + 16u, sp->cy_s, sp->cy_p) ||
        add_diff_u16(out, cpos + 22u, sp->cw_s, sp->cw_p)) return -1;

    /* 仅当第一块边框为 getImage(31) 且第二块边框为 getImage(280/188) 时联动改写图片 ID，
     * 防止其它固件皮肤图片表 ID 漂移导致改错图片或不可逆还原（此时 alpha=0 已完全透明）。 */
    int link_img2 = (bh[0].img_op == 0x10u && bh[0].img_val == 31 &&
                     bh[1].img_op == 0x11u && bh[1].img_val == (st == 1 ? 280 : 188));
    out->img_linked = link_img2;

    for (int k = 0; k < 2; k++) {
        if (add_diff_byte(out, bh[k].off_alpha, 0xffu, 0x00u) ||
            add_diff_u16(out, bh[k].off_bh, sp->bh_s, sp->bh_p)) return -1;
        if (k == 1 && link_img2) {
            if (add_diff_u16(out, bh[k].img_off, 280u, 188u)) return -1;
        }
        if (add_diff_u16(out, bh[k].off_bw, sp->bw_s, sp->bw_p) ||
            add_diff_u16(out, bh[k].off_bx, sp->bx_s, sp->bx_p) ||
            add_diff_byte(out, bh[k].off_by, sp->by_s, sp->by_p)) return -1;
    }

    /* 离线模拟另一方向并计算精确 CRC-32 */
    uint8_t *sim = malloc(len);
    if (!sim) return -1;
    memcpy(sim, u, len);
    uint32_t cur_crc = (uint32_t)crc32(0, u, len);
    for (int b = 0; b < out->nb; b++) {
        uint8_t expect = (st == 1) ? out->from[b] : out->to[b];
        uint8_t target = (st == 1) ? out->to[b] : out->from[b];
        if (sim[out->off[b]] != expect) { free(sim); return -1; }
        sim[out->off[b]] = target;
    }
    uint32_t other_crc = (uint32_t)crc32(0, sim, len);
    free(sim);

    if (st == 1) {
        out->crc_stock = cur_crc;
        out->crc_patched = other_crc;
    } else {
        out->crc_patched = cur_crc;
        out->crc_stock = other_crc;
    }
    return st;
}

static uint8_t *extract_zip_entry(const uint8_t *z, long len, const uint8_t *cd_rec, uint32_t *out_usize) {
    uint16_t method = rd16(cd_rec + 10);
    uint32_t csize = rd32(cd_rec + 20), usize = rd32(cd_rec + 24), lho = rd32(cd_rec + 42), crc = rd32(cd_rec + 16);
    if ((method != 8u && method != 0u) || usize < GEN_MIN_JXE || usize > GEN_MAX_JXE) return NULL;
    if ((uint64_t)lho + 30u > (uint64_t)len) return NULL;
    const uint8_t *lh = z + lho;
    if (rd32(lh) != 0x04034b50u) return NULL;
    uint16_t lnl = rd16(lh + 26), lel = rd16(lh + 28);
    uint64_t data_off = (uint64_t)lho + 30u + lnl + lel;
    if (data_off + csize > (uint64_t)len) return NULL;
    const uint8_t *data = z + data_off;

    uint8_t *u = NULL;
    if (method == 8u) {
        u = inflate_raw(data, csize, usize);
    } else {
        if (csize != usize) return NULL;
        u = malloc(usize);
        if (u) memcpy(u, data, usize);
    }
    if (!u) return NULL;
    if ((uint32_t)crc32(0, u, usize) != crc) { free(u); return NULL; }
    *out_usize = usize;
    return u;
}

static int has_jxe_suffix(const char *s, int nlen) {
    return nlen > 4 && !memcmp(s + nlen - 4, ".jxe", 4);
}

/* 通用指纹扫描：全量扫描 zip 内全部 .jxe 条目，严格要求 CarPlay 全局唯一（恰好 1 个），
 * AndroidAuto 全局 0 或 1 个且状态与 CarPlay 一致，任何歧义/半匹配/重复命中一律返回 0 (unknown)。
 * 返回：1=stock，2=patched，0=unknown */
static int scan_generic(const uint8_t *z, long len, pentry *out_tbl, int *out_np) {
    zipinfo zi;
    if (zip_open(z, len, &zi)) return 0;

    int cnt[2] = {0, 0};
    int st[2] = {0, 0};
    pentry hit[2];
    memset(hit, 0, sizeof hit);

    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        if (p + 46 > z + len || rd32(p) != 0x02014b50u) return 0;
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32);
        if (p + 46 + nl + el + cl > z + len) return 0;
        const char *nm = (const char *)p + 46;

        if (nl > 0 && nl < 128u && has_jxe_suffix(nm, nl)) {
            uint32_t usize = 0;
            uint8_t *u = extract_zip_entry(z, len, p, &usize);
            if (u) {
                char nbuf[128];
                memcpy(nbuf, nm, nl);
                nbuf[nl] = '\0';
                int matched_specs = 0;
                int bad = 0;
                for (int s = 0; s < 2; s++) {
                    pentry cand;
                    int r = analyze_jxe_for_spec(nbuf, u, usize, &VSPEC[s], &cand);
                    if (r < 0) {
                        bad = 1;
                    } else if (r > 0) {
                        matched_specs++;
                        if (cnt[s] == 0) { hit[s] = cand; st[s] = r; }
                        cnt[s]++;
                    }
                }
                free(u);
                if (bad || matched_specs > 1 || cnt[0] > 1 || cnt[1] > 1) return 0;
            }
        }
        p += 46 + nl + el + cl;
    }

    /* 严格要求：CarPlay 恰好 1 个；AndroidAuto 为 0 或 1 个且状态与 CarPlay 一致 */
    if (cnt[0] == 1 && (cnt[1] == 0 || (cnt[1] == 1 && st[1] == st[0]))) {
        int n = 0;
        if (cnt[1] == 1) out_tbl[n++] = hit[1]; /* 保持 Am 在前、Cm 在后的经典顺序 */
        out_tbl[n++] = hit[0];
        *out_np = n;
        return st[0];
    }
    return 0;
}

/* 统一解析：优先精确表（P2878），未命中则回退跨版本字节码指纹引擎 */
static int resolve_zip(const uint8_t *z, long len, const pentry **out_tbl, int *out_np, int *out_is_generic) {
    int st = zip_state_with(z, len, P_EXACT, NP_EXACT);
    if (st) {
        if (out_tbl) *out_tbl = P_EXACT;
        if (out_np) *out_np = NP_EXACT;
        if (out_is_generic) *out_is_generic = 0;
        return st;
    }
    NP_GEN = 0;
    st = scan_generic(z, len, P_GEN, &NP_GEN);
    if (st) {
        if (out_tbl) *out_tbl = P_GEN;
        if (out_np) *out_np = NP_GEN;
        if (out_is_generic) *out_is_generic = 1;
        return st;
    }
    return 0;
}

/* ---------- 写前双向自检的 ZIP 原地重写 ---------- */

static int rewrite(const uint8_t *z, long len, const pentry *tbl, int np, int is_generic,
                   int to_patched, const char *outpath) {
    zipinfo zi;
    if (zip_open(z, len, &zi)) return die("not a zip file");
    size_t out_cap = (size_t)len + 65536u + (size_t)np * 65536u;
    uint8_t *o = malloc(out_cap);
    uint8_t *cd = malloc((size_t)zi.cd_size + 64u);
    if (!o || !cd) { free(o); free(cd); return die("out of memory"); }

    long w = 0, cw = 0, done = 0;
    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        if (p + 46 > z + len || rd32(p) != 0x02014b50u) { free(o); free(cd); return die("bad central directory"); }
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32), flags = rd16(p + 8), method = rd16(p + 10);
        uint32_t csize = rd32(p + 20), usize = rd32(p + 24), lho = rd32(p + 42), crc = rd32(p + 16);
        if (p + 46 + nl + el + cl > z + len) { free(o); free(cd); return die("truncated central directory"); }
        if ((uint64_t)lho + 30u > (uint64_t)len) { free(o); free(cd); return die("bad local header"); }
        const uint8_t *lh = z + lho;
        if (rd32(lh) != 0x04034b50u) { free(o); free(cd); return die("bad local header"); }
        uint16_t lnl = rd16(lh + 26), lel = rd16(lh + 28);
        const uint8_t *data = lh + 30 + lnl + lel;
        if (data + csize > z + len) { free(o); free(cd); return die("entry beyond end of file"); }

        uint32_t newoff = (uint32_t)w;
        int k = find_patch_in(tbl, np, (const char *)p + 46, nl);
        if (k < 0) {
            /* 其余条目：本地头 + 压缩数据（+ 可选 data descriptor）逐字节原样拷贝 */
            long end = (long)(data - z) + (long)csize;
            if (flags & 8u) end += (end + 4 <= len && rd32(z + end) == 0x08074b50u) ? 16 : 12;
            if (end > len || (size_t)w + (size_t)(end - (long)lho) > out_cap) {
                free(o); free(cd); return die("copy overflow");
            }
            memcpy(o + w, lh, (size_t)(end - (long)lho));
            w += end - (long)lho;
        } else {
            const pentry *e = &tbl[k];
            if ((method != 8u && method != 0u) || usize != e->size) {
                free(o); free(cd); return die("unexpected entry format");
            }
            uint8_t *u = NULL;
            if (method == 8u) {
                u = inflate_raw(data, csize, usize);
            } else if (csize == usize) {
                u = malloc(usize);
                if (u) memcpy(u, data, usize);
            }
            if (!u) { free(o); free(cd); return die("inflate failed"); }

            uint32_t want_before = to_patched ? e->crc_stock : e->crc_patched;
            uint32_t want_after  = to_patched ? e->crc_patched : e->crc_stock;
            if ((uint32_t)crc32(0, u, usize) != want_before || crc != want_before) {
                free(u); free(o); free(cd); return die("entry CRC is not the expected version - nothing written");
            }
            for (int b = 0; b < e->nb; b++) {
                uint8_t from = to_patched ? e->from[b] : e->to[b];
                uint8_t to   = to_patched ? e->to[b]   : e->from[b];
                if (u[e->off[b]] != from) {
                    free(u); free(o); free(cd); return die("byte check failed - nothing written");
                }
                u[e->off[b]] = to;
            }
            uint32_t ncrc = (uint32_t)crc32(0, u, usize);
            if (ncrc != want_after) {
                free(u); free(o); free(cd); return die("result CRC mismatch - nothing written");
            }

            uint32_t nc = 0;
            uint8_t *c = NULL;
            if (method == 8u) {
                c = deflate_raw(u, usize, &nc);
                free(u);
                if (!c) { free(o); free(cd); return die("deflate failed"); }
            } else {
                c = u;
                nc = usize;
            }

            if ((size_t)w + 30u + lnl + nc > out_cap) {
                free(c); free(o); free(cd); return die("output buffer overflow");
            }
            uint8_t *h = o + w;
            memcpy(h, lh, 30);
            wr16(h + 6, rd16(lh + 6) & ~8u); /* 清除 data descriptor 标志 */
            wr32(h + 14, ncrc); wr32(h + 18, nc); wr32(h + 22, usize);
            wr16(h + 26, lnl); wr16(h + 28, 0);
            memcpy(h + 30, lh + 30, lnl);
            w += 30 + lnl;
            memcpy(o + w, c, nc); w += (long)nc;
            free(c);
            crc = ncrc; csize = nc; flags &= ~8u; done++;
        }
        /* 更新中央目录条目偏移及补丁后校验信息 */
        memcpy(cd + cw, p, (size_t)(46 + nl + el + cl));
        wr16(cd + cw + 8, flags); wr32(cd + cw + 16, crc); wr32(cd + cw + 20, csize); wr32(cd + cw + 42, newoff);
        cw += 46 + nl + el + cl;
        p += 46 + nl + el + cl;
    }

    if (done != np) { free(o); free(cd); return die("not all patch entries found"); }
    if ((size_t)w + (size_t)cw + (size_t)(len - zi.eocd) > out_cap) {
        free(o); free(cd); return die("final buffer overflow");
    }
    uint32_t cdoff = (uint32_t)w;
    memcpy(o + w, cd, (size_t)cw); w += cw;
    memcpy(o + w, z + zi.eocd, (size_t)(len - zi.eocd)); /* EOCD + 注释 */
    wr32(o + w + 12, (uint32_t)cw); wr32(o + w + 16, cdoff);
    w += len - zi.eocd;

    /* 落盘前双重自检：既校验当前补丁表，又在通用模式下独立重扫一次结果 zip */
    int want_state = to_patched ? 2 : 1;
    if (zip_state_with(o, w, tbl, np) != want_state) {
        free(o); free(cd); return die("self check of the result failed - nothing written");
    }
    if (is_generic) {
        pentry chk_tbl[MAXP];
        int chk_np = 0;
        if (scan_generic(o, w, chk_tbl, &chk_np) != want_state || chk_np != np) {
            free(o); free(cd); return die("generic round-trip self check failed - nothing written");
        }
    }

    FILE *f = fopen(outpath, "wb");
    if (!f) { free(o); free(cd); return die("cannot create output file"); }
    int wr_ok = (fwrite(o, 1, (size_t)w, f) == (size_t)w);
    int cl_ok = (fclose(f) == 0);
    free(o); free(cd);
    if (!wr_ok || !cl_ok) return die("write failed");
    printf("%s: %s (%s, %d entries), %ld bytes\n",
           outpath, to_patched ? "patched" : "stock",
           is_generic ? "generic" : "exact", np, w);
    return 0;
}

/* ---------- 只读诊断（vhpatch probe）：协助排查未知老版本 ---------- */

static int contains_bytes(const uint8_t *hay, uint32_t hlen, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) return 0;
    for (uint32_t i = 0; i + nlen <= hlen; i++) {
        if (hay[i] == (uint8_t)needle[0] && !memcmp(hay + i, needle, nlen)) return 1;
    }
    return 0;
}

static void dump_ctor_candidates(const uint8_t *u, uint32_t len) {
    int shown = 0;
    for (uint32_t i = 0; i + 26u < len && shown < 4; i++) {
        if (u[i] != 0x2au) continue;
        /* 检查连续 4 条 aload_0 + (bipush|sipush) + putfield */
        uint32_t cur = i;
        int vals[4];
        int ok = 1;
        for (int k = 0; k < 4; k++) {
            if (cur + 5u > len || u[cur] != 0x2au) { ok = 0; break; }
            if (u[cur + 1u] == 0x10u) {
                if (cur + 6u > len || u[cur + 3u] != 0xb5u) { ok = 0; break; }
                vals[k] = (int8_t)u[cur + 2u];
                cur += 6u;
            } else if (u[cur + 1u] == 0x11u) {
                if (cur + 7u > len || u[cur + 4u] != 0xb5u) { ok = 0; break; }
                vals[k] = (int16_t)rd16(u + cur + 2u);
                cur += 7u;
            } else {
                ok = 0; break;
            }
        }
        if (ok && vals[0] >= 300 && vals[0] <= 1200 && vals[3] >= 600 && vals[3] <= 2000) {
            printf("  ctor_cand @%u: h=%d x=%d y=%d w=%d\n", i, vals[0], vals[1], vals[2], vals[3]);
            shown++;
            i = cur - 1u;
        }
    }
}

static int probe_zip(const uint8_t *z, long len) {
    const pentry *tbl = NULL;
    int np = 0, is_gen = 0;
    int st = resolve_zip(z, len, &tbl, &np, &is_gen);
    printf("probe: state=%s mode=%s entries=%d\n",
           st == 1 ? "stock" : st == 2 ? "patched" : "unknown",
           st ? (is_gen ? "generic" : "exact") : "none", np);
    if (st && tbl) {
        for (int i = 0; i < np; i++) {
            printf("  match[%d]: %s size=%u stock=%08x patched=%08x bytes=%d%s\n",
                   i, tbl[i].name, tbl[i].size, tbl[i].crc_stock, tbl[i].crc_patched, tbl[i].nb,
                   is_gen ? (tbl[i].img_linked ? " img280=linked" : " img280=skip") : "");
        }
        return 0;
    }

    zipinfo zi;
    if (zip_open(z, len, &zi)) {
        printf("probe: invalid zip\n");
        return 3;
    }
    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        if (p + 46 > z + len || rd32(p) != 0x02014b50u) break;
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32);
        if (p + 46 + nl + el + cl > z + len) break;
        const char *nm = (const char *)p + 46;
        if (nl > 0 && nl < 128u && has_jxe_suffix(nm, nl)) {
            uint32_t usize = 0;
            uint8_t *u = extract_zip_entry(z, len, p, &usize);
            if (u) {
                int is_known_name = ((nl == 6 && !memcmp(nm, "Am.jxe", 6)) ||
                                     (nl == 11 && !memcmp(nm, "Cm_0409.jxe", 11)));
                int has_cp = contains_bytes(u, usize, "CARPLAY_MAIN");
                int has_aa = contains_bytes(u, usize, "ANDROIDAUTO");
                if (is_known_name || has_cp || has_aa) {
                    char nbuf[128];
                    memcpy(nbuf, nm, nl); nbuf[nl] = '\0';
                    printf("probe candidate: %s size=%u crc=%08x%s%s\n",
                           nbuf, usize, rd32(p + 16),
                           has_cp ? " [CARPLAY_MAIN]" : "",
                           has_aa ? " [ANDROIDAUTO]" : "");
                    for (int s = 0; s < 2; s++) {
                        int r = analyze_jxe_for_spec(nbuf, u, usize, &VSPEC[s], NULL);
                        if (r != 0) printf("  spec[%s]: result=%d\n", VSPEC[s].label, r);
                    }
                    dump_ctor_candidates(u, usize);
                }
                free(u);
            }
        }
        p += 46 + nl + el + cl;
    }
    return 3;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: vhpatch check|probe <zip> <patch> | apply|revert <zip> <patch> <out>\n");
        return 2;
    }
    if (load_patch(argv[3])) return 2;
    long len = 0;
    uint8_t *z = load_file(argv[2], &len);
    if (!z) return die("cannot read zip");

    if (!strcmp(argv[1], "probe")) {
        int rc = probe_zip(z, len);
        free(z);
        return rc;
    }

    const pentry *tbl = NULL;
    int np = 0, is_gen = 0;
    int st = resolve_zip(z, len, &tbl, &np, &is_gen);

    if (!strcmp(argv[1], "check")) {
        puts(st == 1 ? "stock" : st == 2 ? "patched" : "unknown");
        free(z);
        return st ? 0 : 3;
    }
    if (argc < 5) { free(z); return die("missing output file"); }
    if (!strcmp(argv[1], "apply")) {
        if (st != 1) {
            free(z);
            return die(st == 2 ? "already patched" : "zip is not a supported stock version - nothing written");
        }
        int rc = rewrite(z, len, tbl, np, is_gen, 1, argv[4]);
        free(z);
        return rc;
    }
    if (!strcmp(argv[1], "revert")) {
        if (st != 2) {
            free(z);
            return die(st == 1 ? "already stock" : "zip is not a supported patched version - nothing written");
        }
        int rc = rewrite(z, len, tbl, np, is_gen, 0, argv[4]);
        free(z);
        return rc;
    }
    free(z);
    return die("unknown command");
}
