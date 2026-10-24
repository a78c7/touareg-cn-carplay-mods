// cl_alt.so — LD_PRELOAD into dio_manager (VW MH2p P2878, libairplay 320.17.1)
// Stage 2: log + (if /tmp/cl_alt_on) advertise a cluster display, negotiate altScreen,
//          accept stream 111 and count/record what the phone sends (no decrypt/decode yet).
// Files: /tmp/cl_probe_on -> logging to /tmp/cl_probe.log ; /tmp/cl_alt_on -> injection.
// Based on the approach of luka-dev/mib2q-carplay-rgi (GPL-3.0).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include "cl_chacha.h"
#include "aeabi_shim.h"

#define EXPORT __attribute__((visibility("default")))
#define CFENC_UTF8 0x08000100

/* VW window "33" (nav cluster map) buffer is 1920x639 -> advertise 1920x640 (even for H.264) */
#define CLUSTER_W 1920
#define CLUSTER_H_STD 640
#define CLUSTER_PHYS_W 292
#define CLUSTER_PHYS_H_STD 97
/* v15: settings from /tmp/cl.conf (testing, wins) or /mnt/app/eso/bin/apps/cluster/cl.conf, read at every /info:
 *   va=N      initial view area (safe area preset below)          default 0
 *   tall=H    advertise a 1920xH cluster (H 640..1080); cl_render shows only the top 639 rows, so the
 *             map centre (where the app puts the car) lands lower on the panel                  default 640
 *   ui=N      cluster scene URL preset (-1 = phone's suggestUI url or URL 0)                   default -1 */
static int g_cfg_va = 0, g_cfg_tall = CLUSTER_H_STD, g_cfg_ui = -1;
static int g_cl_h = CLUSTER_H_STD;          /* height advertised in the current /info */
/* safe-area presets {x, y, w, h} inside the full 1920 x g_cl_h visible frame (car = centre of the safe area
 * if the app honours it). Wide (>= 660 px) keeps the CarPlay ETA card in a side column. */
static const int VA[][4] = {
    { 520,  70,  920, 380 },   /* 0: v14 default, centre y 260 */
    {   0,   0, 1920,   0 },   /* 1: whole frame (h filled in at build time) */
    { 480, 260,  960, 340 },   /* 2: lower centre, centre y 430 */
    { 320, 360, 1280, 260 },   /* 3: bottom band, centre y 490 */
    { 240, 120, 1440, 500 },   /* 4: large, lower, centre y 370 */
    { 560, 440,  800, 180 },   /* 5: low strip, centre y 530 */
    { 320, 320, 1280, 300 },   /* 6: for tall mode: inside the shown rows, centre y 470 */
    /* 7..22: fine grid, car centre x = 960 + dx (dx 0/-80/-160/-240), centre y 550/575/600/620 */
#define G(dx, y, h) { 560 + (dx), y, 800, h }
    G(0, 470, 160), G(0, 510, 130), G(0, 560, 80), G(0, 600, 40),
    G(-80, 470, 160), G(-80, 510, 130), G(-80, 560, 80), G(-80, 600, 40),
    G(-160, 470, 160), G(-160, 510, 130), G(-160, 560, 80), G(-160, 600, 40),
    G(-240, 470, 160), G(-240, 510, 130), G(-240, 560, 80), G(-240, 600, 40),
#undef G
};
#define VA_BUILTIN ((int)(sizeof VA / sizeof VA[0]))
/* "area=x,y,w,h" lines in cl.conf replace the built-in list (h=0: down to the bottom of the frame) */
#define VA_MAX 32
static int g_va[VA_MAX][4], g_va_n;
#define VA_N g_va_n
static const char *UI_URLS[] = {
    "maps:/car/instrumentcluster/map?showETA=no&maneuverLayout=no&showSpeedLimit=no",   /* 0: map only */
    "maps:/car/instrumentcluster/map?showETA=yes&maneuverLayout=no&showSpeedLimit=no",  /* 1: map + ETA card */
    "maps:/car/instrumentcluster?showETA=yes&showSpeedLimit=no",                        /* 2: map + turn card + ETA */
    "maps:/car/instrumentcluster?showETA=no&showSpeedLimit=no",                         /* 3: map + turn card */
};
#define UI_N ((int)(sizeof UI_URLS / sizeof UI_URLS[0]))
static void plog(const char *fmt, ...);
static void read_conf(void) {
    static const char *paths[] = { "/tmp/cl.conf", "/mnt/app/eso/bin/apps/cluster/cl.conf" };
    g_cfg_va = 0; g_cfg_tall = CLUSTER_H_STD; g_cfg_ui = -1;
    g_va_n = VA_BUILTIN; memcpy(g_va, VA, sizeof VA);
    for (unsigned i = 0; i < 2; i++) {
        int fd = open(paths[i], O_RDONLY); if (fd < 0) continue;
        char b[2048]; int n = read(fd, b, sizeof b - 1); close(fd);
        if (n <= 0) continue;
        b[n] = 0;
        int own = 0;
        for (char *l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
            int v;
            if (sscanf(l, " va = %d", &v) == 1) g_cfg_va = v;
            else if (sscanf(l, " tall = %d", &v) == 1) g_cfg_tall = v;
            else if (sscanf(l, " ui = %d", &v) == 1) g_cfg_ui = v;
            else {
                int a[4];
                if (sscanf(l, " area = %d , %d , %d , %d", &a[0], &a[1], &a[2], &a[3]) == 4) {
                    if (!own) { own = 1; g_va_n = 0; }
                    if (g_va_n < VA_MAX) { memcpy(g_va[g_va_n], a, sizeof a); g_va_n++; }
                }
            }
        }
        if (g_cfg_va < 0 || g_cfg_va >= VA_N) g_cfg_va = 0;
        if (g_cfg_tall < CLUSTER_H_STD || g_cfg_tall > 1080) g_cfg_tall = CLUSTER_H_STD;
        g_cfg_tall &= ~15;
        if (g_cfg_ui >= UI_N) g_cfg_ui = -1;
        plog("conf %s: va=%d tall=%d ui=%d areas=%d", paths[i], g_cfg_va, g_cfg_tall, g_cfg_ui, g_va_n);
        return;
    }
    plog("conf: none, defaults va=0 tall=640 ui=-1");
}
#define CLUSTER_UUID "C1057E12-A0D1-4E20-B0C0-1057E12A0D10"
#define CLUSTER_STREAM_UUID "D1057E12-A0D1-4E20-B0C0-1057E12A0D11"

EXPORT void *AirPlayCopyServerInfo(void *a, void *b, void *c, void *d);

static int g_log, g_alt, shm_init_early;
static int g_fd = -1;
static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
typedef int (*asprintf_t)(char **, const char *, ...);
static asprintf_t p_asprintf;

/* CFLite */
typedef void *(*cf_str_t)(void *, const char *, unsigned);
typedef void *(*cf_arrm_t)(void *, long, const void *);
typedef void *(*cf_arrmcopy_t)(void *, long, const void *);
typedef void  (*cf_arrapp_t)(void *, const void *);
typedef long  (*cf_arrcount_t)(const void *);
typedef void *(*cf_arrget_t)(const void *, long);
typedef void  (*cf_rel_t)(const void *);
typedef void *(*cf_ret_t)(const void *);
typedef void  (*cf_dictset_t)(void *, const void *, const void *);
typedef void *(*cf_dictm_t)(void *, long, const void *, const void *);
typedef void *(*cf_dictmcopy_t)(void *, long, const void *);
typedef int   (*cf_dictseti_t)(void *, const void *, int64_t);
typedef int64_t (*cf_dictgeti_t)(const void *, const void *, int *);
typedef void *(*cf_dictget_t)(const void *, const void *);
typedef unsigned long (*cf_typeid_t)(const void *);
typedef unsigned long (*cf_tid_t)(void);
static cf_str_t cf_str; static cf_arrm_t cf_arrm; static cf_arrmcopy_t cf_arrmcopy; static cf_arrapp_t cf_arrapp;
static cf_arrcount_t cf_arrcount; static cf_arrget_t cf_arrget; static cf_rel_t cf_rel; static cf_ret_t cf_ret;
static cf_dictset_t cf_dictset; static cf_dictm_t cf_dictm; static cf_dictmcopy_t cf_dictmcopy; static cf_dictseti_t cf_dictseti;
static cf_dictgeti_t cf_dictgeti; static cf_dictget_t cf_dictget; static cf_typeid_t cf_typeid; static cf_tid_t cf_arrtid, cf_dicttid;
static const void *arr_cb, *dkey_cb, *dval_cb;
static int cf_ready;

static void plog(const char *fmt, ...) {
    if (g_fd < 0) return;
    char buf[1024]; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(buf, sizeof buf, "[%ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap; va_start(ap, fmt); n += vsnprintf(buf + n, sizeof buf - n, fmt, ap); va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    pthread_mutex_lock(&g_lk); write(g_fd, buf, n); pthread_mutex_unlock(&g_lk);
}
static void plog_cf(const char *tag, const void *obj, size_t maxlen) {
    if (g_fd < 0) return;
    if (!obj) { plog("%s: (null)", tag); return; }
    char *s = NULL;
    if (p_asprintf && p_asprintf(&s, "%@", obj) >= 0 && s) {
        size_t l = strlen(s);
        plog("%s: (%u bytes)", tag, (unsigned)l);
        if (l > maxlen) l = maxlen;
        pthread_mutex_lock(&g_lk); write(g_fd, s, l); write(g_fd, "\n", 1); pthread_mutex_unlock(&g_lk);
        free(s);
    } else plog("%s: %p", tag, obj);
}

#define R(v, n) v = (void *)dlsym(RTLD_DEFAULT, n)
static void init_once(void) {
    struct stat st;
    g_log = (stat("/tmp/cl_probe_on", &st) == 0);
    g_alt = (stat("/tmp/cl_alt_on", &st) == 0);
    if (g_log || g_alt) {
        g_fd = open("/tmp/cl_probe.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (g_fd >= 0) fchmod(g_fd, 0666);
    }
    p_asprintf = (asprintf_t)dlsym(RTLD_DEFAULT, "ASPrintF");
    if (g_alt) shm_init_early = 1;
    plog("cl_alt v19.3 loaded pid=%d uid=%d log=%d alt=%d", getpid(), getuid(), g_log, g_alt);
}
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
#define INIT() pthread_once(&g_once, init_once)
__attribute__((constructor)) static void ctor(void) { INIT(); }

static int cf_init(void) {
    if (cf_ready) return cf_ready > 0;
    R(cf_str, "CFStringCreateWithCString"); R(cf_arrm, "CFArrayCreateMutable"); R(cf_arrmcopy, "CFArrayCreateMutableCopy");
    R(cf_arrapp, "CFArrayAppendValue"); R(cf_arrcount, "CFArrayGetCount"); R(cf_arrget, "CFArrayGetValueAtIndex");
    R(cf_rel, "CFRelease"); R(cf_ret, "CFRetain"); R(cf_dictset, "CFDictionarySetValue"); R(cf_dictm, "CFDictionaryCreateMutable");
    R(cf_dictmcopy, "CFDictionaryCreateMutableCopy"); R(cf_dictseti, "CFDictionarySetInt64"); R(cf_dictgeti, "CFDictionaryGetInt64");
    R(cf_dictget, "CFDictionaryGetValue"); R(cf_typeid, "CFGetTypeID"); R(cf_arrtid, "CFArrayGetTypeID"); R(cf_dicttid, "CFDictionaryGetTypeID");
    R(arr_cb, "kCFLArrayCallBacksCFLTypes"); R(dkey_cb, "kCFLDictionaryKeyCallBacksCFLTypes"); R(dval_cb, "kCFLDictionaryValueCallBacksCFLTypes");
    cf_ready = (cf_str && cf_arrm && cf_arrmcopy && cf_arrapp && cf_arrcount && cf_arrget && cf_rel && cf_ret && cf_dictset && cf_dictm &&
                cf_dictmcopy && cf_dictseti && cf_dictgeti && cf_dictget && cf_typeid && cf_arrtid && cf_dicttid && arr_cb && dkey_cb && dval_cb) ? 1 : -1;
    plog("cf_init %s", cf_ready > 0 ? "ok" : "FAILED");
    return cf_ready > 0;
}

/* helpers */
static void *K(const char *s) { return cf_str(0, s, CFENC_UTF8); }
static void set_str(void *d, const char *k, const char *v) { void *kk = K(k), *vv = K(v); cf_dictset(d, kk, vv); cf_rel(vv); cf_rel(kk); }
static void set_i64(void *d, const char *k, int64_t v) { void *kk = K(k); cf_dictseti(d, kk, v); cf_rel(kk); }
static void set_obj(void *d, const char *k, void *v) { void *kk = K(k); cf_dictset(d, kk, v); cf_rel(kk); }
static void *get_obj(const void *d, const char *k) { void *kk = K(k); void *v = cf_dictget(d, kk); cf_rel(kk); return v; }
static int64_t get_i64(const void *d, const char *k) { void *kk = K(k); int e = 0; int64_t v = cf_dictgeti(d, kk, &e); cf_rel(kk); return v; }
static int is_arr(const void *o) { return o && cf_typeid(o) == cf_arrtid(); }
static int is_dict(const void *o) { return o && cf_typeid(o) == cf_dicttid(); }
static void arr_add_str(void *a, const char *s) { void *v = K(s); cf_arrapp(a, v); cf_rel(v); }
static void arr_add_obj(void *a, void *o) { cf_arrapp(a, o); cf_rel(o); }

static void *rect(int64_t x, int64_t y, int64_t w, int64_t h) {
    void *d = cf_dictm(0, 0, dkey_cb, dval_cb);
    set_i64(d, "originXPixels", x); set_i64(d, "originYPixels", y); set_i64(d, "widthPixels", w); set_i64(d, "heightPixels", h);
    return d;
}
static void *viewarea(int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    void *va = rect(0, 0, CLUSTER_W, g_cl_h);
    void *s = rect(sx, sy, sw, sh); set_obj(va, "safeArea", s); cf_rel(s);
    return va;
}
static void *stream_arr(void *uuidCF, const char *uuidStr) {
    void *a = cf_arrm(0, 1, arr_cb), *d = cf_dictm(0, 0, dkey_cb, dval_cb);
    set_i64(d, "originXPixels", 0); set_i64(d, "originYPixels", 0); set_i64(d, "zIndex", 0);
    if (uuidCF) set_obj(d, "uuid", uuidCF); else set_str(d, "uuid", uuidStr);
    arr_add_obj(a, d);
    return a;
}
static void *build_cluster(int night) {
    void *d = cf_dictm(0, 0, dkey_cb, dval_cb), *a;
    g_cl_h = g_cfg_tall;
    set_str(d, "name", "Instrument Cluster");
    set_i64(d, "type", 111);
    set_i64(d, "maxFPS", 30);
    set_i64(d, "widthPixels", CLUSTER_W); set_i64(d, "heightPixels", g_cl_h);
    /* keep the pixel pitch: physical height grows with the pixel height, so iOS keeps the same map scale */
    set_i64(d, "widthPhysical", CLUSTER_PHYS_W); set_i64(d, "heightPhysical", (CLUSTER_PHYS_H_STD * g_cl_h + CLUSTER_H_STD / 2) / CLUSTER_H_STD);
    set_str(d, "uid", CLUSTER_UUID); set_str(d, "uuid", CLUSTER_UUID);
    a = cf_arrm(0, 1, arr_cb); arr_add_str(a, "Instrument Cluster"); set_obj(d, "properties", a); cf_rel(a);
    a = cf_arrm(0, VA_N, arr_cb);
    for (int i = 0; i < VA_N; i++) {
        /* a safe area outside the frame makes the iPhone drop the whole CarPlay session: clamp */
        int x = g_va[i][0], y = g_va[i][1], w = g_va[i][2], h = g_va[i][3];
        if (x < 0) x = 0; if (x > CLUSTER_W - 64) x = CLUSTER_W - 64;
        if (y < 0) y = 0; if (y > g_cl_h - 32) y = g_cl_h - 32;
        if (h <= 0) h = g_cl_h - y;
        if (w < 64) w = 64; if (x + w > CLUSTER_W) w = CLUSTER_W - x;
        if (h < 32) h = 32; if (y + h > g_cl_h) h = g_cl_h - y;
        arr_add_obj(a, viewarea(x, y, w, h));
    }
    set_obj(d, "viewAreas", a); cf_rel(a);
    set_i64(d, "initialViewArea", g_cfg_va);
    a = cf_arrm(0, 0, arr_cb); set_obj(d, "adjacentViewAreas", a); cf_rel(a);
    set_i64(d, "cornerMasks", 0); set_i64(d, "nightMode", night); set_i64(d, "limitedUI", 0);
    a = cf_arrm(0, 0, arr_cb); set_obj(d, "limitedUIElements", a); cf_rel(a);
    a = stream_arr(NULL, CLUSTER_STREAM_UUID); set_obj(d, "initialVideoStreams", a); cf_rel(a);
    return d;
}
static int modernize_main(void *m, int night) {
    void *uuid = get_obj(m, "uuid");
    if (!uuid) return -1;
    set_obj(m, "uid", uuid);
    set_i64(m, "cornerMasks", 0); set_i64(m, "nightMode", night); set_i64(m, "limitedUI", 0);
    void *a = cf_arrm(0, 0, arr_cb); set_obj(m, "limitedUIElements", a); cf_rel(a);
    a = stream_arr(uuid, NULL); set_obj(m, "initialVideoStreams", a); cf_rel(a);
    return 0;
}

/* ---------- session commands ---------- */
static void *volatile g_session;
typedef int (*fn_sendcmd_t)(void *, void *, void *, void *);
static void cmd_done(int status, void *resp, void *ctx) { plog("cmd '%s' completion status=%d", (const char *)ctx, status); }
static int send_cmd_ex(const char *type, const char *url, const char *ik1, int64_t iv1, const char *ik2, int64_t iv2, int adj_empty) {
    static fn_sendcmd_t sc; if (!sc) sc = (fn_sendcmd_t)dlsym(RTLD_DEFAULT, "AirPlayReceiverSessionSendCommand");
    void *sess = g_session;
    if (!sc || !sess || cf_ready <= 0) return -1;
    void *cmd = cf_dictm(0, 0, dkey_cb, dval_cb), *p = cf_dictm(0, 0, dkey_cb, dval_cb);
    set_str(cmd, "type", type);
    set_str(p, "uuid", CLUSTER_UUID);
    if (url) set_str(p, "url", url);
    if (ik1) set_i64(p, ik1, iv1);
    if (ik2) set_i64(p, ik2, iv2);
    if (adj_empty) { void *a = cf_arrm(0, 0, arr_cb); set_obj(p, "adjacentViewAreas", a); cf_rel(a); }
    set_obj(cmd, "params", p); cf_rel(p);
    int rc = sc(sess, cmd, (void *)cmd_done, (void *)type);
    cf_rel(cmd);
    plog("cmd '%s' sent rc=%d%s%s", type, rc, url ? " url=" : "", url ? url : "");
    return rc;
}
static int send_cmd(const char *type, const char *url) { return send_cmd_ex(type, url, NULL, 0, NULL, 0, 0); }
/* night mode: HU (dio_manager) calls AirPlayReceiverSessionSetNightMode(session, bool) which only targets the main
 * display; mirror the same value to the cluster display (params uuid + CFBoolean nightMode). */
static volatile int g_night = -1;
static int send_cluster_night(int night) {
    static fn_sendcmd_t sc; if (!sc) sc = (fn_sendcmd_t)dlsym(RTLD_DEFAULT, "AirPlayReceiverSessionSendCommand");
    void **bt = (void **)dlsym(RTLD_DEFAULT, "kCFLBooleanTrue"), **bf = (void **)dlsym(RTLD_DEFAULT, "kCFLBooleanFalse");
    void *sess = g_session;
    if (!sc || !sess || !bt || !bf || cf_ready <= 0 || night < 0) return -1;
    void *cmd = cf_dictm(0, 0, dkey_cb, dval_cb), *p = cf_dictm(0, 0, dkey_cb, dval_cb);
    set_str(cmd, "type", "setNightMode");
    set_str(p, "uuid", CLUSTER_UUID);
    set_obj(p, "nightMode", night ? *bt : *bf);
    set_obj(cmd, "params", p); cf_rel(p);
    int rc = sc(sess, cmd, (void *)cmd_done, (void *)"setNightMode(cluster)");
    cf_rel(cmd);
    plog("cluster setNightMode %d rc=%d", night, rc);
    return rc;
}
static char g_cluster_url[512];      /* from the phone's suggestUI (AMap sends none) */
static volatile int g_ui = -1;        /* runtime override ("ui N" command), else g_cfg_ui */
static const char *cluster_url(void) {
    int u = g_ui >= 0 ? g_ui : g_cfg_ui;
    if (u >= 0 && u < UI_N) return UI_URLS[u];
    return g_cluster_url[0] ? g_cluster_url : UI_URLS[0];
}
#define CLUSTER_URL cluster_url()
/* v15: one-line status for quick checks: cat /tmp/cl_status */
static volatile int st_info, st_ctl, st_s111, st_rx, st_ui; static volatile unsigned long st_frames, st_idr;
static void write_status(const char *ev) {
    char b[256]; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(b, sizeof b, "t=%ld ev=%s info=%d ctl=%d s111=%d rx=%d frames=%lu idr=%lu ui=%d h=%d va=%d url=%d\n",
                     (long)ts.tv_sec, ev, st_info, st_ctl, st_s111, st_rx, st_frames, st_idr, st_ui, g_cl_h, g_cfg_va,
                     g_ui >= 0 ? g_ui : g_cfg_ui);
    int fd = open("/tmp/cl_status", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { write(fd, b, n); close(fd); }
}

/* ---------- crypto (VW 320.17.1: master key at session+0x1F8) ---------- */
#define SESSION_KEY_OFF 0x1F8
typedef void (*fn_derive_t)(const uint8_t *, int, uint64_t, uint8_t *, uint8_t *);
typedef int (*fn_ctr_init_t)(void *, const uint8_t *, const uint8_t *);
typedef int (*fn_ctr_upd_t)(void *, const uint8_t *, uint32_t, uint8_t *);
static uint8_t g_key[16], g_iv[16]; static int g_have_key;
static void hex16(char *o, const uint8_t *b) { for (int i = 0; i < 16; i++) sprintf(o + 2 * i, "%02x", b[i]); }
/* HomeKit pairing session at session+0x3E4 (AirPlayReceiverSessionSetHomeKitSecurityContext) */
#define SESSION_PAIRING_OFF 0x3E4
typedef int (*fn_pderive_t)(void *, const void *, size_t, const void *, size_t, size_t, uint8_t *);
static uint8_t g_ckey[32]; static int g_have_ckey;
static int derive_keys(void *session, int64_t connID) {
    g_have_key = 0; g_have_ckey = 0;
    void *pairing = session ? *(void **)((uint8_t *)session + SESSION_PAIRING_OFF) : NULL;
    fn_pderive_t pd = (fn_pderive_t)dlsym(RTLD_DEFAULT, "PairingSessionDeriveKey");
    plog("crypto: pairing=%p derive=%p connID=%llu", pairing, (void *)pd, (unsigned long long)connID);
    if (pairing && pd) {
        char salt[64]; int sl = snprintf(salt, sizeof salt, "DataStream-Salt%llu", (unsigned long long)connID);
        static const char info[] = "DataStream-Output-Encryption-Key";
        int r = pd(pairing, salt, sl, info, sizeof info - 1, 32, g_ckey);
        char a[33], b[33]; hex16(a, g_ckey); hex16(b, g_ckey + 16);
        plog("crypto: chacha key r=%d key=%s%s", r, a, b);
        if (r == 0) g_have_ckey = 1;
    }
    return g_have_ckey ? 0 : -1;
}

/* ---------- shared memory ring for the cluster renderer (fifthBro cluster_h264_shm format) ---------- */
#define SHM_NAME "/cluster_h264_shm"
#define SHM_MAGIC 0x48323634u
#define SHM_RING (2 * 1024 * 1024)
typedef struct { volatile uint32_t magic, write_pos, total_bytes, flags; uint8_t ring[SHM_RING]; } h264_shm_t;
static h264_shm_t *g_shm;
static void shm_init(void) {
    if (g_shm) return;
    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { plog("shm: open errno=%d", errno); return; }
    fchmod(fd, 0666);
    if (ftruncate(fd, sizeof(h264_shm_t)) < 0) plog("shm: ftruncate errno=%d", errno);
    void *p = mmap(NULL, sizeof(h264_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { plog("shm: mmap errno=%d", errno); return; }
    g_shm = (h264_shm_t *)p;
    g_shm->magic = SHM_MAGIC;
    plog("shm: ready %s (wp=%u total=%u)", SHM_NAME, g_shm->write_pos, g_shm->total_bytes);
}
#define GOP_MAX (8u << 20)
static uint8_t *g_gop; static uint32_t g_gopn; static int g_gop_valid;
static void gop_reset(void) { g_gopn = 0; g_gop_valid = 1; }
static void gop_add(const uint8_t *d, uint32_t n) {
    if (!g_gop_valid) return;
    if (!g_gop) g_gop = malloc(GOP_MAX);
    if (!g_gop || g_gopn + n > GOP_MAX) { g_gop_valid = 0; return; }
    memcpy(g_gop + g_gopn, d, n); g_gopn += n;
}
static void shm_write(const uint8_t *d, uint32_t n) {
    if (!g_shm || !n || n >= SHM_RING) return;
    uint32_t wp = g_shm->write_pos, first = SHM_RING - wp;
    if (n <= first) memcpy(g_shm->ring + wp, d, n);
    else { memcpy(g_shm->ring + wp, d, first); memcpy(g_shm->ring, d + first, n - first); }
    __sync_synchronize();
    g_shm->write_pos = (wp + n) % SHM_RING;
    __sync_synchronize();
    g_shm->total_bytes += n;
}

/* ---------- stream 111 receiver ---------- */
static int g_lsock = -1, g_port;
static volatile int g_rx_stop, g_rx_running;
static volatile long long g_last_rx_ms;   /* CLOCK_MONOTONIC ms of the last packet on stream 111 (keep-alive hold) */
static long long mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static pthread_mutex_t g_rx_lk = PTHREAD_MUTEX_INITIALIZER;
static int read_exact(int fd, uint8_t *p, size_t n) {
    while (n) {
        struct pollfd pf = { fd, POLLIN, 0 };
        int r = poll(&pf, 1, 500);
        if (g_rx_stop) return -1;
        if (r <= 0) { if (r < 0 && errno != EINTR) return -1; continue; }
        ssize_t k = read(fd, p, n);
        if (k <= 0) { if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue; return -1; }
        p += k; n -= k;
    }
    return 0;
}
static void *rx_thread(void *arg) {
    int ls = (int)(long)arg, fd = -1;
    plog("rx: waiting for phone on port %d", g_port);
    for (int i = 0; i < 120 && !g_rx_stop; i++) {
        struct pollfd pf = { ls, POLLIN, 0 };
        if (poll(&pf, 1, 500) > 0) { fd = accept(ls, NULL, NULL); break; }
    }
    if (fd < 0) { plog("rx: no connection"); st_rx = -1; write_status("rx_timeout"); goto out; }
    plog("rx: phone connected fd=%d chacha=%d", fd, g_have_ckey);
    st_rx = 1; st_frames = 0; st_idr = 0; st_ui = 0; write_status("rx_connected");
    shm_init();
    if (g_shm) g_shm->flags |= 1;
    uint8_t hdr[128]; uint8_t *body = NULL, *ab = NULL; size_t cap = 0, abcap = 0;
    uint8_t ps[512]; size_t psn = 0; int lensz = 4;
    unsigned long frames = 0, bytes = 0, idr = 0, badnal = 0, cfg = 0, opc[256] = {0}, small0 = 0; uint64_t nonce = 0;
    static const uint8_t sc[4] = {0, 0, 0, 1};
    struct timespec t0, t; clock_gettime(CLOCK_MONOTONIC, &t0); time_t lastrep = t0.tv_sec;
    int kf1 = 0, kf2 = 0, ui_sent = 0, cmd_seen = 0, kf_retry = 0; time_t last_cmd_mtime = 0, last_status = 0;
    long long last_cmd_poll_ms = 0; char last_cb[64] = "";
    while (!g_rx_stop) {
        if (read_exact(fd, hdr, 128)) break;
        uint32_t bs = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
        unsigned op = hdr[4];
        if (bs > 4u << 20) { plog("rx: bodySize %u too big, abort", bs); break; }
        if (bs > cap) { body = realloc(body, bs); cap = bs; }
        if (bs && read_exact(fd, body, bs)) break;
        bytes += bs; opc[op & 255]++;
        if ((op != 0 && op != 5 && opc[op] <= 5) || (op == 0 && bs <= 16 && ++small0 <= 5))
            plog("rx: op=%u body=%u hdr=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", op, bs, hdr[4],hdr[5],hdr[6],hdr[7],hdr[8],hdr[9],hdr[10],hdr[11],hdr[12],hdr[13],hdr[14],hdr[15]);
        if (op == 1 && bs >= 7) {           /* avcC: keep SPS/PPS as Annex-B */
            cfg++; lensz = (body[4] & 3) + 1; psn = 0;
            size_t p = 5; int ns = body[p++] & 0x1f;
            for (int k = 0; k < ns && p + 2 <= bs; k++) { int n = body[p] << 8 | body[p + 1]; p += 2; if (p + n <= bs && psn + 4 + n <= sizeof ps) { memcpy(ps + psn, sc, 4); memcpy(ps + psn + 4, body + p, n); psn += 4 + n; } p += n; }
            if (p < bs) { int np = body[p++]; for (int k = 0; k < np && p + 2 <= bs; k++) { int n = body[p] << 8 | body[p + 1]; p += 2; if (p + n <= bs && psn + 4 + n <= sizeof ps) { memcpy(ps + psn, sc, 4); memcpy(ps + psn + 4, body + p, n); psn += 4 + n; } p += n; } }
            if (cfg <= 3) plog("rx: avcC lensz=%d ps=%u bytes", lensz, (unsigned)psn);
        } else if (op == 0 && bs > 16 && g_have_ckey) {
            uint32_t n = bs - 16;
            cc_aead_decrypt(g_ckey, nonce++, body, n);
            frames++;
            if (abcap < n + psn + 64) { abcap = n + psn + 64; ab = realloc(ab, abcap); }
            size_t p = 0, w = 0; int isidr = 0, good = 1;
            while (p + lensz <= n) {
                uint32_t L = 0; for (int i = 0; i < lensz; i++) L = (L << 8) | body[p + i];
                p += lensz;
                if (!L || p + L > n) { good = 0; break; }
                if ((body[p] & 0x1f) == 5) isidr = 1;
                memcpy(ab + w, sc, 4); memcpy(ab + w + 4, body + p, L); w += 4 + L; p += L;
            }
            if (!good) { badnal++; if (badnal <= 3) plog("rx: bad NAL structure frame %lu (nonce desync?)", frames); continue; }
            if (isidr) {
                idr++; st_idr = idr;
                gop_reset();
                if (psn) { shm_write(ps, psn); gop_add(ps, psn); }
                plog("rx: IDR #%lu at frame %lu", idr, frames);
            }
            shm_write(ab, w); gop_add(ab, w);
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        g_last_rx_ms = (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
        long el = t.tv_sec - t0.tv_sec;
        st_frames = frames;
        if (!ui_sent && idr > 0) {
            send_cmd("showUI", CLUSTER_URL);
            send_cluster_night(g_night < 0 ? 0 : g_night);
            send_cmd_ex("updateViewArea", NULL, "viewAreaIndex", g_cfg_va, "animationDurationMillis", 0, 1);
            ui_sent = 1; st_ui = 1; write_status("ui_sent");
        }
        /* v15: frames but no key frame yet (decoder cannot start): ask again at 3 s, 6 s, 9 s - never more
         * (a forceKeyFrame flood made the phone stop sending frames in v8/v9) */
        if (idr == 0 && kf_retry < 3 && el >= 3 * (kf_retry + 1)) {
            kf_retry++; plog("rx: no IDR after %lds (frames=%lu), forceKeyFrame #%d", el, frames, kf_retry);
            send_cmd("forceKeyFrame", NULL);
        }
        if (t.tv_sec - last_status >= 2) { last_status = t.tv_sec; write_status("rx"); }
        (void)kf1; (void)kf2;
        /* command file: written by root / the HMI plug-in, never deleted by us (uid 1037 cannot).
         * v19.2: polled every 100 ms and acted on when its content or mtime changes (writers add "#<seq>" so repeated
         * commands differ); before, once a second on mtime only (1 s resolution), which made steering-wheel zoom laggy */
        long long now_ms = (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
        if (now_ms - last_cmd_poll_ms >= 100) {
            struct stat st; last_cmd_poll_ms = now_ms;
            char cb[64] = "";
            if (stat("/tmp/cl_alt_cmd", &st) == 0) {
                int cf = open("/tmp/cl_alt_cmd", O_RDONLY);
                if (cf >= 0) { int k = read(cf, cb, sizeof cb - 1); if (k > 0) cb[k] = 0; close(cf); }
            }
            if (cb[0] && (st.st_mtime != last_cmd_mtime || strcmp(cb, last_cb))) {
                int first = (last_cmd_mtime == 0 && cmd_seen == 0);
                last_cmd_mtime = st.st_mtime; cmd_seen = 1;
                strncpy(last_cb, cb, sizeof last_cb - 1);
                if (first && el < 2) { plog("cmd file present at start, ignoring: %s", cb); }
                else {
                    int n = 1; char w[16] = ""; sscanf(cb, "%15s %d", w, &n); if (n < 1) n = 1; if (n > 10) n = 10;
                    plog("cmd file: %s %d", w, n);
                    if (!strcmp(w, "idr")) send_cmd("forceKeyFrame", NULL);
                    else if (!strcmp(w, "va")) {      /* "va N": switch safe-area preset live */
                        int v = 0; sscanf(cb, "%*s %d", &v);
                        if (v >= 0 && v < VA_N) { g_cfg_va = v; send_cmd_ex("updateViewArea", NULL, "viewAreaIndex", v, "animationDurationMillis", 0, 1); write_status("va"); }
                    } else if (!strcmp(w, "ui")) {    /* "ui N": switch cluster scene URL live (-1 = suggested/default) */
                        int v = -1; sscanf(cb, "%*s %d", &v);
                        g_ui = (v >= 0 && v < UI_N) ? v : -1;
                        send_cmd("showUI", CLUSTER_URL); write_status("ui");
                    }
                    else if (!strcmp(w, "zoomin") || !strcmp(w, "zoomout"))
                        for (int z = 0; z < n; z++) {   /* v19.2: no 300 ms stall of this receive loop per step */
                            if (z) usleep(60000);
                            send_cmd_ex("changeMapZoomLevel", NULL, "zoomDirection", w[4] == 'i' ? 0 : 1, NULL, 0, 0);
                        }
                    else if (!strcmp(w, "replay")) {
                        if (g_gop_valid && g_gopn) {
                            uint32_t off = 0; while (off < g_gopn) { uint32_t c = g_gopn - off > 65536 ? 65536 : g_gopn - off; shm_write(g_gop + off, c); off += c; }
                            plog("rx: replayed GOP %u bytes", g_gopn);
                        } else plog("rx: replay unavailable (valid=%d n=%u)", g_gop_valid, g_gopn);
                    }
                }
            }
        }
        if (t.tv_sec - lastrep >= 10) {
            lastrep = t.tv_sec;
            plog("rx: %lds frames=%lu idr=%lu badnal=%lu bytes=%lu shm_total=%u op0=%lu op1=%lu op2=%lu op3=%lu op4=%lu op5=%lu other=%lu small0=%lu", el, frames, idr, badnal, bytes, g_shm ? g_shm->total_bytes : 0, opc[0], opc[1], opc[2], opc[3], opc[4], opc[5], bytes ? 0UL : 0UL, small0);
        }
    }
    plog("rx: ended frames=%lu idr=%lu badnal=%lu", frames, idr, badnal);
    st_rx = 0; write_status("rx_ended");
    if (g_shm) g_shm->flags &= ~1u;
    free(body); free(ab);
    close(fd);
out:
    close(ls);
    pthread_mutex_lock(&g_rx_lk); g_rx_running = 0; if (g_lsock == ls) g_lsock = -1; pthread_mutex_unlock(&g_rx_lk);
    return NULL;
}
static void rx_stop(const char *why) {
    if (g_rx_running) { plog("rx: stop (%s)", why); g_rx_stop = 1; }
}
static int rx_start(void) {
    rx_stop("restart");
    for (int i = 0; i < 40 && g_rx_running; i++) usleep(50000);
    if (g_rx_running) { plog("rx: old receiver still running"); return -1; }
    int s = socket(AF_INET6, SOCK_STREAM, 0);
    if (s < 0) { plog("rx: socket errno=%d", errno); return -1; }
    int on = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    /* head unit pf only passes fixed ports on carplay0/bcm0; 6030 is stock 110, 7000 is RTSP */
    static const int ports[] = { 6200, 6100, 7100, 5010 };
    struct sockaddr_in6 a; int bound = 0;
    for (unsigned i = 0; i < sizeof ports / sizeof ports[0] && !bound; i++) {
        memset(&a, 0, sizeof a); a.sin6_len = sizeof a; a.sin6_family = AF_INET6; a.sin6_port = htons(ports[i]);
        if (bind(s, (struct sockaddr *)&a, sizeof a) == 0) bound = 1; else plog("rx: bind %d errno=%d", ports[i], errno);
    }
    if (!bound) { close(s); return -1; }
    socklen_t l = sizeof a; getsockname(s, (struct sockaddr *)&a, &l);
    if (listen(s, 2) < 0) { plog("rx: listen errno=%d", errno); close(s); return -1; }
    g_port = ntohs(a.sin6_port);
    g_rx_stop = 0; g_rx_running = 1; g_lsock = s;
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, rx_thread, (void *)(long)s) != 0) { g_rx_running = 0; close(s); return -1; }
    return g_port;
}

/* v15: dio_cluster.so keeps the last route in /tmp/cluster_cp.state after CarPlay goes away, so VWClusterNav kept
 * the old arrow / distance / ETA on the HUD and cluster. Empty it when a session ends or a new one starts; the
 * phone's next route update rewrites it. */
static void clear_rg_state(const char *why) {
    int fd = open("/tmp/cluster_cp.state", O_WRONLY | O_TRUNC);
    plog("route state cleared (%s) fd=%d", why, fd);
    if (fd >= 0) close(fd);
}

/* ---------- interposers ---------- */

/* v19.3: CarPlay dropped every 30 s while the head unit showed its own screen.
 * When the main screen stream is torn down ("Modes changed: screen accessory"), libairplay starts
 * _IdleStateKeepAliveThread (thread name "AirPlayKeepAliveReceiver"): select() on the keep-alive socket with a
 * 30 s timeout; on timeout it ends the session (kTimeoutErr -6722, "session ended: Dur=30 seconds") and
 * dio_manager reconnects. The phone sends those beacons only when it has no stream running - but the cluster
 * stream 111 (handled here, unknown to libairplay) keeps running, so no beacon ever came. Seen on the car:
 * frames kept arriving on 111 for the whole 30 s before each drop.
 * Fix: in that thread only, wait in 5 s slices and restart the countdown while stream 111 packets arrive
 * (the phone is demonstrably alive). Without cluster packets the original timeout runs as before, so a phone
 * that really left is still detected. Every other select() caller goes straight through. */
typedef int (*fn_select_t)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
EXPORT int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv) {
    static fn_select_t real;
    if (!real) real = (fn_select_t)dlsym(RTLD_NEXT, "select");
    if (!real) { errno = ENOSYS; return -1; }
    if (!tv || tv->tv_sec < 20 || !g_alt) return real(n, r, w, e, tv);
    char nm[48] = "";
    if (pthread_getname_np(pthread_self(), nm, sizeof nm) != 0 || strcmp(nm, "AirPlayKeepAliveReceiver")) return real(n, r, w, e, tv);
    if (access("/mnt/app/eso/bin/apps/cluster/ka_off", F_OK) == 0) return real(n, r, w, e, tv);   /* switch: old behaviour */
    fd_set r0, w0, e0;
    if (r) r0 = *r;
    if (w) w0 = *w;
    if (e) e0 = *e;
    long long total = (long long)tv->tv_sec * 1000 + tv->tv_usec / 1000, left = total, start = mono_ms();
    int held = 0;
    for (;;) {
        long long slice = left < 5000 ? left : 5000;
        struct timeval t = { (time_t)(slice / 1000), (long)(slice % 1000) * 1000 };
        if (r) *r = r0;
        if (w) *w = w0;
        if (e) *e = e0;
        int rc = real(n, r, w, e, &t);
        if (rc != 0) {
            if (held) plog("keepalive: idle wait ended rc=%d after %lld ms (held by cluster stream)", rc, mono_ms() - start);
            return rc;
        }
        left -= slice;
        if (g_rx_running && mono_ms() - g_last_rx_ms < 3000) {
            if (!held) plog("keepalive: no beacon from the phone, cluster stream alive -> idle timeout held");
            held = 1; left = total;
        }
        if (left <= 0) {
            plog("keepalive: no beacon and no cluster packets for %lld ms -> timeout (session ends)", total);
            return 0;
        }
    }
}
typedef void *(*fn4_t)(void *, void *, void *, void *);
static void *resolve(const char *name) { return dlsym(RTLD_NEXT, name); }

EXPORT void *AirPlayCopyServerInfo(void *a, void *b, void *c, void *d) {
    INIT();
    static fn4_t real; if (!real) real = (fn4_t)resolve("AirPlayCopyServerInfo");
    if (shm_init_early) { shm_init_early = 0; shm_init(); }
    void *info = real ? real(a, b, c, d) : NULL;
    if (info && g_alt && cf_init() && is_dict(info)) {
        read_conf();
        st_info++;
        int night = (int)get_i64(info, "nightMode");
        g_night = night ? 1 : 0;
        void *ext = get_obj(info, "extendedFeatures");
        void *ext2 = is_arr(ext) ? cf_arrmcopy(0, 0, ext) : cf_arrm(0, 2, arr_cb);
        arr_add_str(ext2, "altScreen"); arr_add_str(ext2, "viewAreas");
        void *disp = get_obj(info, "displays");
        if (is_arr(disp) && cf_arrcount(disp) >= 1 && is_dict(cf_arrget(disp, 0))) {
            void *main0 = cf_dictmcopy(0, 0, cf_arrget(disp, 0));
            if (modernize_main(main0, night) == 0) {
                void *nd = cf_arrm(0, 2, arr_cb);
                arr_add_obj(nd, main0);
                arr_add_obj(nd, build_cluster(night));
                set_obj(info, "displays", nd);
                set_obj(info, "displayPanels", nd);
                cf_rel(nd);
                set_obj(info, "extendedFeatures", ext2);
                plog("/info: injected cluster display + extendedFeatures");
            } else { cf_rel(main0); plog("/info: main uuid missing, no inject"); }
        } else plog("/info: no displays array, no inject");
        cf_rel(ext2);
    }
    if (g_log) {
        plog("CopyServerInfo session=%p", a);
        if (info && cf_init() && is_dict(info)) { void *dd = get_obj(info, "displays"); plog_cf("  displays", dd, 8000); plog_cf("  extendedFeatures", get_obj(info, "extendedFeatures"), 2000); }
    }
    return info;
}

EXPORT int AirPlayReceiverSessionSetup(void *session, void *req, void **resp, void *d) {
    INIT();
    static fn4_t real; if (!real) real = (fn4_t)resolve("AirPlayReceiverSessionSetup");
    int want111 = 0; int64_t connID = 0; int isControl = 0;
    if (g_log || g_alt) { cf_init(); plog("SessionSetup session=%p", session); plog_cf("  request", req, 4000); }
    if (cf_ready > 0 && is_dict(req)) {
        void *st = get_obj(req, "streams");
        if (!st) isControl = 1;
        else if (is_arr(st)) {
            for (long i = 0; i < cf_arrcount(st); i++) {
                void *s = cf_arrget(st, i);
                if (is_dict(s) && get_i64(s, "type") == 111) { want111 = 1; connID = get_i64(s, "streamConnectionID"); }
            }
        }
    }
    int r = real ? (int)(long)real(session, req, (void *)resp, d) : -6700;
    if (g_alt && cf_ready > 0 && r == 0 && resp) {
        if (isControl) {
            void *rd = *resp ? cf_dictmcopy(0, 0, *resp) : cf_dictm(0, 0, dkey_cb, dval_cb);
            void *ef = get_obj(rd, "enabledFeatures");
            void *ef2 = is_arr(ef) ? cf_arrmcopy(0, 0, ef) : cf_arrm(0, 2, arr_cb);
            arr_add_str(ef2, "altScreen"); arr_add_str(ef2, "viewAreas");
            set_obj(rd, "enabledFeatures", ef2); cf_rel(ef2);
            if (*resp) cf_rel(*resp);
            *resp = rd;
            plog("control SETUP: enabledFeatures += altScreen, viewAreas");
            clear_rg_state("new session");
            st_ctl++; st_s111 = 0; st_rx = 0; write_status("control_setup");
        } else if (want111) {
            g_session = session;
            st_s111++; write_status("setup111");
            derive_keys(session, connID);
            int port = rx_start();
            plog("media SETUP: stream 111 requested connID=%lld -> listener port %d", (long long)connID, port);
            if (port > 0) {
                void *rd = *resp ? cf_dictmcopy(0, 0, *resp) : cf_dictm(0, 0, dkey_cb, dval_cb);
                void *st = get_obj(rd, "streams");
                void *st2 = is_arr(st) ? cf_arrmcopy(0, 0, st) : cf_arrm(0, 1, arr_cb);
                void *sd = cf_dictm(0, 0, dkey_cb, dval_cb);
                set_i64(sd, "type", 111); set_i64(sd, "dataPort", port);
                arr_add_obj(st2, sd);
                set_obj(rd, "streams", st2); cf_rel(st2);
                if (*resp) cf_rel(*resp);
                *resp = rd;
            }
        }
    }
    if (g_log || g_alt) { plog("  -> result %d", r); plog_cf("  response", resp ? *resp : NULL, 4000); }
    return r;
}

typedef int (*fn6_t)(void *, uint32_t, void *, void *, void *, void **);
typedef int (*fn_cfgetcstr_t)(const void *, char *, long, unsigned);
EXPORT int AirPlayReceiverSessionPlatformControl(void *session, uint32_t flags, void *command, void *qualifier, void *params, void **outParams) {
    INIT();
    static fn6_t real; if (!real) real = (fn6_t)resolve("AirPlayReceiverSessionPlatformControl");
    static fn_cfgetcstr_t gcs; if (!gcs) gcs = (fn_cfgetcstr_t)dlsym(RTLD_DEFAULT, "CFStringGetCString");
    char cmd[64] = "";
    if (command && gcs) gcs(command, cmd, sizeof cmd, CFENC_UTF8);
    if ((g_log || g_alt) && strcmp(cmd, "setUpStreams") && strcmp(cmd, "tearDownStreams")) {
        plog("PlatformControl '%s' flags=%u", cmd, flags);
        if (params) plog_cf("  params", params, 3000);
    }
    if (g_alt && !strcmp(cmd, "suggestUI") && params && cf_init() && is_dict(params)) {
        /* find the first string URL anywhere in params["urls"] (array) or params["url"] */
        void *u = get_obj(params, "urls"), *first = NULL;
        if (is_arr(u) && cf_arrcount(u) > 0) first = cf_arrget(u, 0);
        if (!first) first = get_obj(params, "url");
        if (first && gcs && gcs(first, g_cluster_url, sizeof g_cluster_url, CFENC_UTF8)) plog("suggestUI: will showUI url=%s", g_cluster_url);
        else { g_cluster_url[0] = 0; plog("suggestUI: no url found"); }
    }
    return real ? real(session, flags, command, qualifier, params, outParams) : -6714;
}

typedef int (*fn_setnight_t)(void *, int, void *, void *);
EXPORT int AirPlayReceiverSessionSetNightMode(void *session, int night, void *cb, void *ctx) {
    INIT();
    static fn_setnight_t real; if (!real) real = (fn_setnight_t)resolve("AirPlayReceiverSessionSetNightMode");
    int rc = real ? real(session, night, cb, ctx) : -6714;
    g_night = (night & 0xff) ? 1 : 0;
    plog("HU setNightMode %d rc=%d", g_night, rc);
    if (g_alt) send_cluster_night(g_night);
    return rc;
}

EXPORT int AirPlayReceiverSessionTearDown(void *session, void *req, void *c, void *d) {
    INIT();
    static fn4_t real; if (!real) real = (fn4_t)resolve("AirPlayReceiverSessionTearDown");
    if (g_log || g_alt) { plog("TearDown session=%p", session); plog_cf("  request", req, 2000); }
    if (g_alt && cf_ready > 0) {
        int full = 1, has111 = 0;
        if (is_dict(req)) {
            void *st = get_obj(req, "streams");
            if (is_arr(st) && cf_arrcount(st) > 0) {
                full = 0;
                for (long i = 0; i < cf_arrcount(st); i++) { void *s = cf_arrget(st, i); if (is_dict(s) && get_i64(s, "type") == 111) has111 = 1; }
            }
        }
        if (full || has111) rx_stop(full ? "full teardown" : "111 teardown");
        if (full) clear_rg_state("teardown");
    }
    return real ? (int)(long)real(session, req, c, d) : 0;
}
