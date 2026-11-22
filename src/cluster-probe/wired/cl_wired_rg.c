/*
 * cl_wired_rg.c — Clean-room iAP2 Route Guidance TLV engine & snapshot writer.
 *
 * Written from scratch for Touareg MH2p wired CarPlay (Cinemo NME iAP2 stack),
 * implementing standard iAP2 wire parsing (0x40 0x40 len[2] msgid[2] {tlv_len[2]
 * tlv_id[2] payload...}*) and emitting the plain-text key/value snapshot expected
 * by VWClusterNav Java bundle (/tmp/cluster_cp.state).
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#include "cl_wired_rg.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define W_LOG_PATH      "/tmp/cl_wired.log"
#define W_LOG_MAX_BYTES 524288
#define W_MAX_MLIST     16
#define W_MAN_SLOTS     16
#define W_MAN_EMIT_MAX  6
#define W_LANE_SETS     16
#define W_MAX_LANES     8
#define W_MAX_LANGLES   16
#define W_SNAP_CAP      16384

/* ---------------- Big-endian wire helpers ---------------- */
static inline uint16_t r_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t r_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static inline uint64_t r_be64(const uint8_t *p) {
    return ((uint64_t)r_be32(p) << 32) | (uint64_t)r_be32(p + 4);
}
static inline void w_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/* ---------------- Lightweight logging ---------------- */
static pthread_mutex_t g_log_lk = PTHREAD_MUTEX_INITIALIZER;
static int g_log_on = 0;

void w_rg_log_init(void) {
    const char *v = getenv("DIO_CLUSTER_LOG");
    const char *w = getenv("CL_WIRED_LOG");
    g_log_on = ((v && *v && *v != '0') || (w && *w && *w != '0')) ? 1 : 0;
}

void w_rg_log_enable(int on) { g_log_on = on ? 1 : 0; }

void w_rg_log(const char *fmt, ...) {
    if (!g_log_on) return;
    pthread_mutex_lock(&g_log_lk);
    struct stat st;
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    if (stat(W_LOG_PATH, &st) == 0 && st.st_size > W_LOG_MAX_BYTES)
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    int fd = open(W_LOG_PATH, flags, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        char line[768];
        time_t now = time(NULL);
        struct tm tm_buf;
        int n = 0;
        if (localtime_r(&now, &tm_buf)) {
            n = snprintf(line, sizeof(line), "[%02d:%02d:%02d] ",
                         tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        }
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 2, fmt, ap);
        va_end(ap);
        if (m > 0) {
            size_t tot = (size_t)n + (size_t)m;
            if (tot > sizeof(line) - 2) tot = sizeof(line) - 2;
            line[tot++] = '\n';
            (void)write(fd, line, tot);
        }
        close(fd);
    }
    pthread_mutex_unlock(&g_log_lk);
}

void w_rg_log_hex(const char *label, const uint8_t *buf, size_t len) {
    if (!g_log_on || !buf) return;
    size_t lim = len > 192 ? 192 : len;
    char hex[192 * 3 + 16];
    size_t o = 0;
    for (size_t i = 0; i < lim && o + 4 < sizeof(hex); i++) {
        o += (size_t)snprintf(hex + o, sizeof(hex) - o, "%02x ", buf[i]);
    }
    if (lim < len && o + 4 < sizeof(hex)) {
        memcpy(hex + o, "...", 4);
    } else {
        hex[o] = '\0';
    }
    w_rg_log("  %s (%zu B): %s", label ? label : "hex", len, hex);
}

/* ---------------- String cleaning helpers ---------------- */

/* Copy UTF-8 bytes stopping at NUL, collapsing \r\n or \n into " / " and
 * dropping ASCII control bytes so QNX key:type:val lines stay intact. */
static void copy_clean_utf8(const uint8_t *src, size_t src_len, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    size_t o = 0;
    for (size_t i = 0; i < src_len && o + 1 < dst_cap; i++) {
        uint8_t c = src[i];
        if (c == 0) break;
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < src_len && src[i + 1] == '\n') i++;
            if (o + 3 < dst_cap) {
                dst[o++] = ' ';
                dst[o++] = '/';
                dst[o++] = ' ';
            }
        } else if (c >= 0x20) {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

/* Strip leading AMap "去往" prefix so short HUD road fields stay readable. */
static void strip_amap_goto(char *s) {
    static const unsigned char prefix[6] = { 0xE5, 0x8E, 0xBB, 0xE5, 0xBE, 0x80 }; /* 去往 */
    if (!s) return;
    char *p = s;
    while (*p == ' ') p++;
    if (memcmp(p, prefix, 6) == 0 && p[6] != '\0') {
        p += 6;
        while (*p == ' ') p++;
        if (*p != '\0') memmove(s, p, strlen(p) + 1);
    }
}

/* ---------------- State cache ---------------- */
typedef struct {
    uint16_t iap_idx;
    uint32_t seq;
    uint8_t  type;
    uint8_t  driving_side;
    uint8_t  junction_type;
    int16_t  exit_angle;
    int16_t  j_angles[W_MAX_MLIST];
    uint16_t j_angle_n;
    uint32_t dist_between;
    char     desc[256];
    char     after_road[256];
    char     exit_info[256];
} w_man_slot_t;

typedef struct {
    uint16_t position;
    int16_t  direction;
    uint8_t  status;
    int16_t  angles[W_MAX_LANGLES];
    uint8_t  angle_n;
} w_lane_t;

typedef struct {
    uint16_t lg_idx;
    uint32_t seq;
    uint8_t  count;
    w_lane_t lanes[W_MAX_LANES];
} w_lane_set_t;

typedef struct {
    int      inited;
    int      have_upd;
    uint8_t  route_state;
    uint8_t  dist_units;
    uint8_t  man_dist_units;
    uint8_t  lane_showing;
    uint8_t  src_supports_rg;
    uint32_t dist_rem;
    uint32_t dist_man;
    uint64_t time_rem;
    uint64_t eta;
    char     dest[256];
    char     cur_road[256];
    char     src_name[64];
    uint16_t mlist[W_MAX_MLIST];
    uint16_t mlist_n;
    int      have_mlist;
    uint16_t lg_index;
    uint32_t man_seq;
    uint32_t lane_seq;
    w_man_slot_t slots[W_MAN_SLOTS];
    w_lane_set_t lsets[W_LANE_SETS];
} w_rg_ctx_t;

static pthread_mutex_t g_rg_lk = PTHREAD_MUTEX_INITIALIZER;
static w_rg_ctx_t g_ctx;
static char g_state_path[256] = "/tmp/cluster_cp.state";

static void ensure_init_locked(void) {
    if (g_ctx.inited) return;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.inited = 1;
    g_ctx.lg_index = 0xFFFF;
    for (int i = 0; i < W_MAN_SLOTS; i++) g_ctx.slots[i].iap_idx = 0xFFFF;
    for (int i = 0; i < W_LANE_SETS; i++) g_ctx.lsets[i].lg_idx = 0xFFFF;
}

void w_rg_set_state_path(const char *path) {
    if (!path || !path[0]) return;
    pthread_mutex_lock(&g_rg_lk);
    snprintf(g_state_path, sizeof(g_state_path), "%s", path);
    pthread_mutex_unlock(&g_rg_lk);
}

void w_rg_reset(void) {
    pthread_mutex_lock(&g_rg_lk);
    g_ctx.inited = 0;
    ensure_init_locked();
    pthread_mutex_unlock(&g_rg_lk);
}

static int find_man_slot(uint16_t idx) {
    for (int i = 0; i < W_MAN_SLOTS; i++)
        if (g_ctx.slots[i].iap_idx == idx) return i;
    return -1;
}

static int is_pinned_in_mlist(uint16_t idx) {
    if (!g_ctx.have_mlist) return 0;
    for (uint16_t i = 0; i < g_ctx.mlist_n; i++)
        if (g_ctx.mlist[i] == idx) return 1;
    return 0;
}

static int alloc_man_slot(uint16_t idx) {
    int hit = find_man_slot(idx);
    if (hit >= 0) return hit;
    int victim = -1;
    for (int i = 0; i < W_MAN_SLOTS; i++) {
        if (g_ctx.slots[i].iap_idx == 0xFFFF) {
            memset(&g_ctx.slots[i], 0, sizeof(g_ctx.slots[i]));
            g_ctx.slots[i].iap_idx = idx;
            return i;
        }
        if (is_pinned_in_mlist(g_ctx.slots[i].iap_idx)) continue;
        if (victim < 0 || g_ctx.slots[i].seq < g_ctx.slots[victim].seq) victim = i;
    }
    if (victim < 0) {
        victim = 0;
        for (int i = 1; i < W_MAN_SLOTS; i++)
            if (g_ctx.slots[i].seq < g_ctx.slots[victim].seq) victim = i;
    }
    memset(&g_ctx.slots[victim], 0, sizeof(g_ctx.slots[victim]));
    g_ctx.slots[victim].iap_idx = idx;
    return victim;
}

static int find_lane_set(uint16_t lg_idx) {
    for (int i = 0; i < W_LANE_SETS; i++)
        if (g_ctx.lsets[i].lg_idx == lg_idx) return i;
    return -1;
}

static int alloc_lane_set(uint16_t lg_idx) {
    int hit = find_lane_set(lg_idx);
    if (hit >= 0) return hit;
    int victim = 0;
    for (int i = 0; i < W_LANE_SETS; i++) {
        if (g_ctx.lsets[i].lg_idx == 0xFFFF) {
            memset(&g_ctx.lsets[i], 0, sizeof(g_ctx.lsets[i]));
            g_ctx.lsets[i].lg_idx = lg_idx;
            return i;
        }
        if (g_ctx.lsets[i].seq < g_ctx.lsets[victim].seq) victim = i;
    }
    memset(&g_ctx.lsets[victim], 0, sizeof(g_ctx.lsets[victim]));
    g_ctx.lsets[victim].lg_idx = lg_idx;
    return victim;
}

/* ---------------- Snapshot serializer (/tmp/cluster_cp.state) ---------------- */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} snap_buf_t;

static void sb_appendf(snap_buf_t *sb, const char *fmt, ...) {
    if (!sb || sb->len + 2 >= sb->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t adv = (size_t)n;
        if (sb->len + adv >= sb->cap) sb->len = sb->cap - 1;
        else sb->len += adv;
    }
}

static void sb_str(snap_buf_t *sb, const char *key, const char *val) {
    char clean[320];
    const char *src = val ? val : "";
    size_t o = 0;
    for (size_t i = 0; src[i] != '\0' && o + 1 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 0x20) clean[o++] = (char)c;
    }
    clean[o] = '\0';
    sb_appendf(sb, "%s:s:%s\n", key, clean);
}

static void sb_angles_csv(snap_buf_t *sb, const char *key, const int16_t *vals, size_t n) {
    char csv[256];
    size_t o = 0;
    csv[0] = '\0';
    for (size_t i = 0; i < n && o + 8 < sizeof(csv); i++) {
        int m = snprintf(csv + o, sizeof(csv) - o, "%s%d", i ? "," : "", (int)vals[i]);
        if (m > 0) o += (size_t)m;
    }
    sb_str(sb, key, csv);
}

static void publish_snapshot_locked(void) {
    char *mem = (char *)malloc(W_SNAP_CAP);
    if (!mem) return;
    snap_buf_t sb = { mem, W_SNAP_CAP, 0 };

    sb_appendf(&sb, "@cluster_cp\n");
    sb_appendf(&sb, "session_active:b:%s\n", g_ctx.have_upd ? "true" : "false");
    if (g_ctx.have_upd) {
        sb_appendf(&sb, "route_state:n:%u\n", (unsigned)g_ctx.route_state);
        sb_str(&sb, "destination", g_ctx.dest);
        sb_str(&sb, "current_road", g_ctx.cur_road);
        sb_str(&sb, "source_name", g_ctx.src_name);
        sb_appendf(&sb, "distance_remaining:n:%u\n", (unsigned)g_ctx.dist_rem);
        sb_appendf(&sb, "time_remaining:n:%llu\n", (unsigned long long)g_ctx.time_rem);
        sb_appendf(&sb, "eta:n:%llu\n", (unsigned long long)g_ctx.eta);
        sb_appendf(&sb, "distance_units:n:%u\n", (unsigned)g_ctx.dist_units);
        sb_appendf(&sb, "source_supports_rg:n:%u\n", (unsigned)g_ctx.src_supports_rg);
        sb_appendf(&sb, "dist_to_maneuver:n:%u\n", (unsigned)g_ctx.dist_man);
        sb_appendf(&sb, "dist_to_maneuver_units:n:%u\n", (unsigned)g_ctx.man_dist_units);
        sb_appendf(&sb, "lane_guidance_showing:n:%u\n", (unsigned)g_ctx.lane_showing);
    }

    int emitted = 0;
    int cur_slot = -1;
    char key[48];
    if (g_ctx.have_mlist) {
        for (uint16_t i = 0; i < g_ctx.mlist_n && emitted < W_MAN_EMIT_MAX; i++) {
            int si = find_man_slot(g_ctx.mlist[i]);
            if (si < 0) continue;
            w_man_slot_t *s = &g_ctx.slots[si];
            if (emitted == 0) cur_slot = 0;
            snprintf(key, sizeof(key), "slot_%d_iap_idx", emitted);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)s->iap_idx);
            snprintf(key, sizeof(key), "slot_%d_type", emitted);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)s->type);
            snprintf(key, sizeof(key), "slot_%d_description", emitted);
            sb_str(&sb, key, s->desc);
            snprintf(key, sizeof(key), "slot_%d_road", emitted);
            sb_str(&sb, key, s->after_road);
            snprintf(key, sizeof(key), "slot_%d_distance", emitted);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)s->dist_between);
            snprintf(key, sizeof(key), "slot_%d_driving_side", emitted);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)s->driving_side);
            snprintf(key, sizeof(key), "slot_%d_junction_type", emitted);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)s->junction_type);
            snprintf(key, sizeof(key), "slot_%d_exit_angle", emitted);
            sb_appendf(&sb, "%s:n:%d\n", key, (int)s->exit_angle);
            snprintf(key, sizeof(key), "slot_%d_exit_info", emitted);
            sb_str(&sb, key, s->exit_info);
            snprintf(key, sizeof(key), "slot_%d_junction_angles", emitted);
            sb_angles_csv(&sb, key, s->j_angles, s->j_angle_n);
            emitted++;
        }
    }
    sb_appendf(&sb, "slot_count:n:%d\n", emitted);
    sb_appendf(&sb, "current_slot:n:%d\n", cur_slot);

    int lsi = (g_ctx.lg_index != 0xFFFF) ? find_lane_set(g_ctx.lg_index) : -1;
    if (lsi >= 0 && g_ctx.lsets[lsi].count > 0) {
        w_lane_set_t *ls = &g_ctx.lsets[lsi];
        sb_appendf(&sb, "lane_active:b:true\n");
        sb_appendf(&sb, "lane_count:n:%u\n", (unsigned)ls->count);
        for (uint8_t i = 0; i < ls->count && i < W_MAX_LANES; i++) {
            snprintf(key, sizeof(key), "lane_%u_pos", (unsigned)i);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)ls->lanes[i].position);
            snprintf(key, sizeof(key), "lane_%u_dir", (unsigned)i);
            sb_appendf(&sb, "%s:n:%d\n", key, (int)ls->lanes[i].direction);
            snprintf(key, sizeof(key), "lane_%u_status", (unsigned)i);
            sb_appendf(&sb, "%s:n:%u\n", key, (unsigned)ls->lanes[i].status);
            snprintf(key, sizeof(key), "lane_%u_angles", (unsigned)i);
            sb_angles_csv(&sb, key, ls->lanes[i].angles, ls->lanes[i].angle_n);
        }
    } else {
        sb_appendf(&sb, "lane_active:b:false\n");
    }

    /* QNX /tmp does not support rename(); truncate & write in one write() call */
    int fd = open(g_state_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        (void)write(fd, sb.buf, sb.len);
        close(fd);
    } else {
        w_rg_log("state open %s failed errno=%d", g_state_path, errno);
    }
    free(mem);
}

/* ---------------- Incoming iAP2 parsers ---------------- */

static void parse_update_locked(const uint8_t *msg, size_t len) {
    g_ctx.have_upd = 1;
    size_t off = 6;
    while (off + 4 <= len) {
        uint16_t tl = r_be16(msg + off);
        uint16_t tid = r_be16(msg + off + 2);
        if (tl < 4 || off + tl > len) break;
        const uint8_t *v = msg + off + 4;
        size_t vl = (size_t)tl - 4;

        switch (tid) {
            case 0x0001: if (vl >= 1) g_ctx.route_state = v[0]; break;
            case 0x0003: copy_clean_utf8(v, vl, g_ctx.cur_road, sizeof(g_ctx.cur_road)); break;
            case 0x0004: copy_clean_utf8(v, vl, g_ctx.dest, sizeof(g_ctx.dest)); break;
            case 0x0005: if (vl >= 8) g_ctx.eta = r_be64(v); break;
            case 0x0006: if (vl >= 8) g_ctx.time_rem = r_be64(v); break;
            case 0x0007: if (vl >= 4) g_ctx.dist_rem = r_be32(v); break;
            case 0x0009: if (vl >= 1) g_ctx.dist_units = v[0]; break;
            case 0x000A: if (vl >= 4) g_ctx.dist_man = r_be32(v); break;
            case 0x000C: if (vl >= 1) g_ctx.man_dist_units = v[0]; break;
            case 0x000D:
                g_ctx.mlist_n = 0;
                for (size_t i = 0; i + 1 < vl && g_ctx.mlist_n < W_MAX_MLIST; i += 2)
                    g_ctx.mlist[g_ctx.mlist_n++] = r_be16(v + i);
                g_ctx.have_mlist = 1;
                break;
            case 0x0010: if (vl >= 2) g_ctx.lg_index = r_be16(v); break;
            case 0x0012: if (vl >= 1) g_ctx.lane_showing = v[0]; break;
            case 0x0013: copy_clean_utf8(v, vl, g_ctx.src_name, sizeof(g_ctx.src_name)); break;
            case 0x0014: if (vl >= 1) g_ctx.src_supports_rg = v[0]; break;
            default: break;
        }
        off += tl;
    }
}

static void parse_maneuver_locked(const uint8_t *msg, size_t len) {
    uint16_t idx = 0;
    int have_idx = 0;
    int have_exit_ang = 0;
    uint8_t m_type = 0, m_side = 0, m_jtype = 0;
    int has_type = 0, has_side = 0, has_jtype = 0, has_dist = 0;
    int has_desc = 0, has_road = 0, has_exit_info = 0;
    uint32_t m_dist = 0;
    int16_t m_exit_ang = 1000;
    int16_t m_jangs[W_MAX_MLIST];
    uint16_t m_jang_n = 0;
    char m_desc[256] = {0}, m_road[256] = {0}, m_einfo[256] = {0};

    size_t off = 6;
    while (off + 4 <= len) {
        uint16_t tl = r_be16(msg + off);
        uint16_t tid = r_be16(msg + off + 2);
        if (tl < 4 || off + tl > len) break;
        const uint8_t *v = msg + off + 4;
        size_t vl = (size_t)tl - 4;

        switch (tid) {
            case 0x0001: if (vl >= 2) { idx = r_be16(v); have_idx = 1; } break;
            case 0x0002: copy_clean_utf8(v, vl, m_desc, sizeof(m_desc)); strip_amap_goto(m_desc); has_desc = 1; break;
            case 0x0003: if (vl >= 1) { m_type = v[0]; has_type = 1; } break;
            case 0x0004: copy_clean_utf8(v, vl, m_road, sizeof(m_road)); strip_amap_goto(m_road); has_road = 1; break;
            case 0x0005: if (vl >= 4) { m_dist = r_be32(v); has_dist = 1; } break;
            case 0x0008: if (vl >= 1) { m_side = v[0]; has_side = 1; } break;
            case 0x0009: if (vl >= 1) { m_jtype = v[0]; has_jtype = 1; } break;
            case 0x000A:
                for (size_t i = 0; i + 1 < vl && m_jang_n < W_MAX_MLIST; i += 2)
                    m_jangs[m_jang_n++] = (int16_t)r_be16(v + i);
                break;
            case 0x000B: if (vl >= 2) { m_exit_ang = (int16_t)r_be16(v); have_exit_ang = 1; } break;
            case 0x000D: copy_clean_utf8(v, vl, m_einfo, sizeof(m_einfo)); has_exit_info = 1; break;
            default: break;
        }
        off += tl;
    }

    if (!have_idx) return;
    int si = alloc_man_slot(idx);
    if (si < 0) return;
    w_man_slot_t *s = &g_ctx.slots[si];
    s->seq = ++g_ctx.man_seq;
    if (has_type) s->type = m_type;
    if (has_side) s->driving_side = m_side;
    if (has_jtype) s->junction_type = m_jtype;
    s->exit_angle = have_exit_ang ? m_exit_ang : 1000;
    if (has_dist) s->dist_between = m_dist;
    if (has_desc) snprintf(s->desc, sizeof(s->desc), "%s", m_desc);
    if (has_road) snprintf(s->after_road, sizeof(s->after_road), "%s", m_road);
    s->j_angle_n = m_jang_n;
    if (m_jang_n > 0) memcpy(s->j_angles, m_jangs, m_jang_n * sizeof(int16_t));
    if (has_exit_info) snprintf(s->exit_info, sizeof(s->exit_info), "%s", m_einfo);
    else s->exit_info[0] = '\0';
}

static void parse_lane_item(const uint8_t *v, size_t vl, w_lane_set_t *ls) {
    size_t off = 0;
    int cur = -1;
    int has_hl = 0;
    while (off + 4 <= vl) {
        uint16_t sl = r_be16(v + off);
        uint16_t sid = r_be16(v + off + 2);
        if (sl < 4 || off + sl > vl) break;
        const uint8_t *sv = v + off + 4;
        size_t svl = (size_t)sl - 4;

        if (sid == 0x0000) {
            if (ls->count >= W_MAX_LANES) break;
            cur = ls->count++;
            memset(&ls->lanes[cur], 0, sizeof(ls->lanes[cur]));
            ls->lanes[cur].position = (svl >= 2) ? r_be16(sv) : (svl >= 1 ? sv[0] : (uint16_t)cur);
            ls->lanes[cur].direction = 1000;
            has_hl = 0;
        } else if (sid == 0x0001 || sid == 0x0002 || sid == 0x0003) {
            if (cur < 0) {
                if (ls->count >= W_MAX_LANES) break;
                cur = ls->count++;
                memset(&ls->lanes[cur], 0, sizeof(ls->lanes[cur]));
                ls->lanes[cur].position = (uint16_t)cur;
                ls->lanes[cur].direction = 1000;
                has_hl = 0;
            }
            if (sid == 0x0001 && svl >= 1) {
                ls->lanes[cur].status = sv[0];
            } else if (sid == 0x0002) {
                size_t p = 0;
                while (p < svl && ls->lanes[cur].angle_n < W_MAX_LANGLES) {
                    int16_t ang = (p + 1 < svl) ? (int16_t)r_be16(sv + p) : (int16_t)sv[p];
                    p += (p + 1 < svl) ? 2 : 1;
                    ls->lanes[cur].angles[ls->lanes[cur].angle_n++] = ang;
                    if (!has_hl && ls->lanes[cur].direction == 1000)
                        ls->lanes[cur].direction = ang;
                }
            } else if (sid == 0x0003 && svl >= 1) {
                ls->lanes[cur].direction = (svl >= 2) ? (int16_t)r_be16(sv) : (int16_t)sv[0];
                has_hl = 1;
            }
        }
        off += sl;
    }
}

static void parse_lane_locked(const uint8_t *msg, size_t len) {
    uint16_t lg_idx = 0;
    w_lane_set_t tmp;
    memset(&tmp, 0, sizeof(tmp));

    size_t off = 6;
    while (off + 4 <= len) {
        uint16_t tl = r_be16(msg + off);
        uint16_t tid = r_be16(msg + off + 2);
        if (tl < 4 || off + tl > len) break;
        const uint8_t *v = msg + off + 4;
        size_t vl = (size_t)tl - 4;
        if (tid == 0x0001) {
            lg_idx = (vl >= 2) ? r_be16(v) : (vl >= 1 ? v[0] : 0);
        } else if (tid == 0x0002 && vl >= 4) {
            parse_lane_item(v, vl, &tmp);
        }
        off += tl;
    }

    int si = alloc_lane_set(lg_idx);
    if (si < 0) return;
    tmp.lg_idx = lg_idx;
    tmp.seq = ++g_ctx.lane_seq;
    g_ctx.lsets[si] = tmp;
}

void w_rg_handle_rx(uint16_t msgid, const uint8_t *msg, size_t len) {
    if (!msg || len < 6 || len > 4096) return;
    pthread_mutex_lock(&g_rg_lk);
    ensure_init_locked();
    if (msgid == W_IAP2_RG_UPDATE)        parse_update_locked(msg, len);
    else if (msgid == W_IAP2_RG_MANEUVER) parse_maneuver_locked(msg, len);
    else if (msgid == W_IAP2_RG_LANE)     parse_lane_locked(msg, len);
    publish_snapshot_locked();
    pthread_mutex_unlock(&g_rg_lk);
}

/* ---------------- Outgoing Identify (0x1D01) & Start (0x5200) patchers ---------------- */

static int list_has_be16(const uint8_t *p, size_t n, uint16_t target) {
    for (size_t i = 0; i + 1 < n; i += 2)
        if (r_be16(p + i) == target) return 1;
    return 0;
}

static size_t write_u16_tlv(uint8_t *dst, uint16_t tag, uint16_t val) {
    w_be16(dst, 6);
    w_be16(dst + 2, tag);
    w_be16(dst + 4, val);
    return 6;
}

static size_t append_rg_component(uint8_t *out, size_t cap, uint16_t comp_id) {
    static const char cname[] = "RouteGuidanceDisplayComponent";
    const size_t nlen = sizeof(cname); /* includes NUL */
    const size_t inner = 6 + (4 + nlen) + 6 * 7;
    const size_t total = 4 + inner;
    if (cap < total) return 0;

    size_t o = 0;
    w_be16(out + o, (uint16_t)total); o += 2;
    w_be16(out + o, W_IDENT_TAG_RG_COMPONENT); o += 2;
    o += write_u16_tlv(out + o, 0x0000, comp_id);
    w_be16(out + o, (uint16_t)(4 + nlen)); o += 2;
    w_be16(out + o, 0x0001); o += 2;
    memcpy(out + o, cname, nlen); o += nlen;
    o += write_u16_tlv(out + o, 0x0002, 256); /* MaxCurrentRoadNameLength */
    o += write_u16_tlv(out + o, 0x0003, 256); /* MaxDestinationNameLength */
    o += write_u16_tlv(out + o, 0x0004, 256); /* MaxAfterManeuverRoadNameLength */
    o += write_u16_tlv(out + o, 0x0005, 256); /* MaxManeuverDescriptionLength */
    o += write_u16_tlv(out + o, 0x0006, 6);   /* MaxGuidanceManeuverStorageCapacity */
    o += write_u16_tlv(out + o, 0x0007, 256); /* MaxLaneGuidanceDescriptionLength */
    o += write_u16_tlv(out + o, 0x0008, 6);   /* MaxLaneGuidanceStorageCapacity */
    return o;
}

size_t w_rg_patch_identify(const uint8_t *in, size_t in_len,
                           uint16_t component_id,
                           uint8_t *out, size_t out_cap) {
    static const uint16_t add_sent[] = { W_IAP2_RG_START, W_IAP2_RG_STOP };
    static const uint16_t add_recv[] = { W_IAP2_RG_UPDATE, W_IAP2_RG_MANEUVER, W_IAP2_RG_LANE };

    if (!in || !out || in_len < 6 || out_cap < in_len + 160) return 0;
    if (in[0] != 0x40 || in[1] != 0x40) return 0;

    memcpy(out, in, 6);
    size_t io = 6, oo = 6;
    while (io + 4 <= in_len) {
        uint16_t tl = r_be16(in + io);
        uint16_t tid = r_be16(in + io + 2);
        if (tl < 4 || io + tl > in_len) return 0;
        if (tid == W_IDENT_TAG_RG_COMPONENT) return 0; /* already present */

        if (tid == W_IDENT_TAG_SENT_MSGS || tid == W_IDENT_TAG_RECV_MSGS) {
            const uint16_t *extras = (tid == W_IDENT_TAG_SENT_MSGS) ? add_sent : add_recv;
            size_t en = (tid == W_IDENT_TAG_SENT_MSGS) ? 2 : 3;
            const uint8_t *payload = in + io + 4;
            size_t plen = (size_t)tl - 4;
            uint16_t to_add[3];
            size_t add_n = 0;
            for (size_t k = 0; k < en; k++) {
                if (!list_has_be16(payload, plen, extras[k])) to_add[add_n++] = extras[k];
            }
            size_t new_tl = (size_t)tl + 2 * add_n;
            if (oo + new_tl > out_cap) return 0;
            w_be16(out + oo, (uint16_t)new_tl);
            w_be16(out + oo + 2, tid);
            memcpy(out + oo + 4, payload, plen);
            for (size_t k = 0; k < add_n; k++)
                w_be16(out + oo + tl + 2 * k, to_add[k]);
            oo += new_tl;
        } else {
            if (oo + tl > out_cap) return 0;
            memcpy(out + oo, in + io, tl);
            oo += tl;
        }
        io += tl;
    }

    size_t clen = append_rg_component(out + oo, out_cap - oo, component_id);
    if (!clen) return 0;
    oo += clen;
    if (oo > 0xFFFF) return 0;
    w_be16(out + 2, (uint16_t)oo);
    return oo;
}

size_t w_rg_patch_start(const uint8_t *in, size_t in_len,
                        uint8_t *out, size_t out_cap) {
    if (!in || !out || in_len < 6 || in_len + 12 > out_cap) return 0;
    int seen[4] = {0, 0, 0, 0};
    size_t io = 6;
    while (io + 4 <= in_len) {
        uint16_t tl = r_be16(in + io);
        uint16_t tid = r_be16(in + io + 2);
        if (tl < 4 || io + tl > in_len) break;
        if (tid < 4) seen[tid] = 1;
        io += tl;
    }
    memcpy(out, in, in_len);
    size_t oo = in_len;
    for (uint16_t k = 1; k <= 3; k++) {
        if (!seen[k]) {
            w_be16(out + oo, 4);
            w_be16(out + oo + 2, k);
            oo += 4;
        }
    }
    w_be16(out + 2, (uint16_t)oo);
    return oo;
}
