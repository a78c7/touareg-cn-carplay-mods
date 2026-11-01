/*
 * cl_wired.c — CarPlay route guidance for WIRED CarPlay on MH2p (VW G36, P2878).
 *
 * Wireless CarPlay runs its iAP2 session on VW's libesoiap2 (hooked by
 * fifthBro's dio_cluster.so). Wired CarPlay runs it on the Cinemo NME stack
 * inside the same dio_manager (iap://ffs:///dev/otg-cinemo), which dio_cluster.so
 * cannot see. This module hooks the Cinemo side:
 *
 *   NmeIAP2Message::Encode  (libNmeBaseClasses, called via PLT from libNmeVfs)
 *       - 0x1D01 IdentificationInformation: add 0x5200/0x5203 to "sent",
 *         0x5201/0x5202/0x5204 to "received", append RouteGuidanceDisplayComponent
 *       - 0x5200 StartRouteGuidanceUpdates: add params 1..3 (source name,
 *         source-supports-RG, lane guidance) like dio_cluster.so does
 *   NmeIAP2Message::Decode  (incoming)
 *       - 0x1D02/0x1D03 identification result, 0x5201/0x5202/0x5204 -> parse and
 *         publish /tmp/cluster_cp.state (same format as dio_cluster.so)
 *   CinemoCreateIAP          (libNmeSDK, called via PLT from dio_manager)
 *       - remember the ICinemoIAP*, later call ICinemoIAP_StartRouteGuidanceUpdates
 *
 * Safety: files in /mnt/app/eso/bin/apps/cluster/: wired_off disables everything.
 * If the phone rejects our patched identification (0x1D03) we stop patching for
 * the rest of this dio_manager process and write /tmp/cl_wired_rejected (also
 * honoured by later processes until reboot).
 *
 * Parsing/state code: derived from fifthBro mh2p-cluster dio_manager_preload.c
 * (CC BY-NC-SA 4.0) and luka-dev mib2q-carplay-rgi (rgd_tlv / pps_writer).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "rg_core.inc"

#define EXPORT __attribute__((visibility("default")))
#define OFF_FILE      "/mnt/app/eso/bin/apps/cluster/wired_off"
#define REJECT_FLAG   "/tmp/cl_wired_rejected"

typedef struct { uint8_t *data; int count; int capacity; int grow; } nme_array_t;

static int  g_disabled = -1;          /* -1 unknown, 0 on, 1 off */
static volatile int g_patch_ok = 1;   /* cleared after a 0x1D03 */
static volatile int g_patched = 0;    /* last identification was patched by us */
static void *volatile g_iap = NULL;   /* ICinemoIAP* */
static volatile int g_accept_gen = 0; /* bumped on every 0x1D02 after our patch */
static volatile int g_started_gen = 0;
static volatile int g_saw_fffb = 0;
static volatile int g_thread_running = 0;
static pthread_mutex_t g_thr_mtx = PTHREAD_MUTEX_INITIALIZER;

static int disabled(void) {
    if (g_disabled < 0) {
        g_disabled = (access(OFF_FILE, F_OK) == 0) ? 1 : 0;
        if (access(REJECT_FLAG, F_OK) == 0) g_patch_ok = 0;
    }
    return g_disabled;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Pick a component id not used by any component in the Identify. */
static uint16_t pick_component_id(const uint8_t *m, size_t len) {
    uint8_t used[256]; memset(used, 0, sizeof(used));
    size_t io = 6;
    while (io + 4 <= len) {
        uint16_t tl = be16(m + io), tg = be16(m + io + 2);
        if (tl < 4 || io + tl > len) break;
        /* components: TLV whose first inner param is 0x0000 (uint16 id) */
        if (tg >= 0x000E && tl >= 10 && be16(m + io + 4) == 6 && be16(m + io + 6) == 0) {
            uint16_t id = be16(m + io + 8);
            if (id < 256) used[id] = 1;
        }
        io += tl;
    }
    uint16_t c;
    if (!used[0x10]) return 0x10;
    for (c = 0x20; c < 0xff; c++) if (!used[c]) return c;
    return 0x10;
}

static int has_tlv(const uint8_t *m, size_t len, uint16_t tag) {
    size_t io = 6;
    while (io + 4 <= len) {
        uint16_t tl = be16(m + io), tg = be16(m + io + 2);
        if (tl < 4 || io + tl > len) return 0;
        if (tg == tag) return 1;
        io += tl;
    }
    return 0;
}

static int arr_replace(nme_array_t *a, const uint8_t *buf, size_t n) {
    if ((int)n > a->capacity) {
        uint8_t *p = (uint8_t *)realloc(a->data, n);
        if (!p) return -1;
        a->data = p; a->capacity = (int)n;
    }
    memcpy(a->data, buf, n);
    a->count = (int)n;
    return 0;
}

static void *start_thread(void *arg) {
    (void)arg;
    int gen = g_accept_gen;
    int i;
    /* wait for the session to settle: first LocationInformation (0xFFFB) or 6 s */
    for (i = 0; i < 60; i++) {
        if (g_saw_fffb && i >= 10) break;
        usleep(100000);
        if (gen != g_accept_gen) gen = g_accept_gen;
    }
    if (g_iap && g_started_gen != gen) {
        typedef int (*fn_start_t)(void *, int, const uint16_t *);
        fn_start_t fn = (fn_start_t)dlsym(RTLD_DEFAULT, "ICinemoIAP_StartRouteGuidanceUpdates");
        uint16_t id = g_rg_comp_id;
        if (fn) {
            int rc = fn(g_iap, 1, &id);
            log_msg("StartRouteGuidanceUpdates(iap=%p, comp=0x%04x) rc=%d (fffb=%d)", g_iap, id, rc, g_saw_fffb);
            if (rc == 0) g_started_gen = gen;
        } else {
            log_msg("ICinemoIAP_StartRouteGuidanceUpdates not found");
        }
    } else {
        log_msg("start skipped: iap=%p gen=%d started=%d", g_iap, gen, g_started_gen);
    }
    pthread_mutex_lock(&g_thr_mtx); g_thread_running = 0; pthread_mutex_unlock(&g_thr_mtx);
    return NULL;
}

static void kick_start(void) {
    pthread_mutex_lock(&g_thr_mtx);
    if (!g_thread_running) {
        pthread_t t; pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, start_thread, NULL) == 0) g_thread_running = 1;
        pthread_attr_destroy(&at);
    }
    pthread_mutex_unlock(&g_thr_mtx);
}

/* ---------------- NmeIAP2Message::Encode (outgoing) ---------------- */
typedef int (*fn_encode_t)(const void *self, nme_array_t *arr);
static fn_encode_t real_encode = NULL;

EXPORT int _ZNK14NmeIAP2Message6EncodeER8NmeArrayIhE(const void *self, nme_array_t *arr) {
    if (!real_encode) real_encode = (fn_encode_t)dlsym(RTLD_NEXT, "_ZNK14NmeIAP2Message6EncodeER8NmeArrayIhE");
    if (!real_encode) return 12;
    int rc = real_encode(self, arr);
    if (rc != 0 || disabled() || !arr || !arr->data || arr->count < 6) return rc;
    const uint8_t *m = arr->data;
    if (m[0] != 0x40 || m[1] != 0x40) return rc;
    uint16_t id = be16(m + 4);
    size_t len = (size_t)arr->count;

    if (id == 0x1D01) {
        log_tlv_tags("IDENTIFY(cinemo) orig", m, len);
        log_hex("IDENTIFY(cinemo) orig raw", m, len);
        g_patched = 0;
        if (!g_patch_ok) { log_msg("IDENTIFY: not patching (rejected before)"); return rc; }
        if (has_tlv(m, len, IDENT_TLV_RG_COMPONENT)) { log_msg("IDENTIFY: already has RG component"); return rc; }
        g_rg_comp_id = pick_component_id(m, len);
        size_t cap = len + 512;
        uint8_t *out = (uint8_t *)malloc(cap);
        if (!out) return rc;
        size_t nl = rebuild_identify_with_rg(m, len, out, cap);
        if (nl && arr_replace(arr, out, nl) == 0) {
            g_patched = 1;
            log_msg("IDENTIFY(cinemo) PATCHED: %zu -> %zu bytes, RG component id 0x%04x", len, nl, g_rg_comp_id);
            log_tlv_tags("IDENTIFY(cinemo) new", arr->data, nl);
        } else {
            log_msg("IDENTIFY(cinemo) patch FAILED (nl=%zu)", nl);
        }
        free(out);
    } else if (id == 0x5200) {
        /* add params 1,2,3 (zero-length flags) if Cinemo did not */
        uint8_t buf[512];
        if (len + 12 <= sizeof(buf)) {
            size_t io = 6; int have[4] = {0,0,0,0};
            while (io + 4 <= len) {
                uint16_t tl = be16(m + io), tg = be16(m + io + 2);
                if (tl < 4 || io + tl > len) break;
                if (tg < 4) have[tg] = 1;
                io += tl;
            }
            memcpy(buf, m, len);
            size_t o = len; int k;
            for (k = 1; k <= 3; k++) if (!have[k]) { buf[o]=0; buf[o+1]=4; buf[o+2]=0; buf[o+3]=(uint8_t)k; o += 4; }
            buf[2] = (uint8_t)(o >> 8); buf[3] = (uint8_t)o;
            if (o != len) arr_replace(arr, buf, o);
        }
        log_hex("0x5200 out", arr->data, (size_t)arr->count);
    } else if (id == 0xFFFB) {
        if (!g_saw_fffb) log_msg("first LocationInformation (0xFFFB) sent");
        g_saw_fffb = 1;
    } else if ((id & 0xFF00) == 0x5200) {
        log_hex("0x52xx out", m, len);
    }
    return rc;
}

/* ---------------- NmeIAP2Message::Decode (incoming) ---------------- */
typedef int (*fn_decode_t)(void *self, const uint8_t *buf, int len);
static fn_decode_t real_decode = NULL;
static uint32_t g_rx_count = 0;

EXPORT int _ZN14NmeIAP2Message6DecodeEPKhi(void *self, const uint8_t *buf, int len) {
    if (!real_decode) real_decode = (fn_decode_t)dlsym(RTLD_NEXT, "_ZN14NmeIAP2Message6DecodeEPKhi");
    if (!disabled() && buf && len >= 6 && buf[0] == 0x40 && buf[1] == 0x40) {
        uint16_t id = be16(buf + 4);
        uint32_t n = ++g_rx_count;
        if (n <= 40 || (id & 0xFF00) == 0x5200 || (id & 0xFF00) == 0x1D00)
            log_msg("rx[%u] msgid=0x%04x len=%d", n, id, len);
        if (id == 0x1D02) {
            log_msg("IdentificationAccepted (patched=%d)", g_patched);
            if (g_patched) { g_accept_gen++; g_saw_fffb = 0; kick_start(); }
        } else if (id == 0x1D03) {
            log_hex("IdentificationRejected", buf, (size_t)len);
            if (g_patched) {
                g_patch_ok = 0;
                int fd = open(REJECT_FLAG, O_WRONLY | O_CREAT, 0666);
                if (fd >= 0) close(fd);
                log_msg("rejected after our patch -> patching disabled until reboot");
            }
        } else if (id == 0x5201 || id == 0x5202 || id == 0x5204) {
            if (!g_pps) rg_pps_init();
            if (len <= 4096) {
                log_hex("  in (RG)", buf, (size_t)len);
                if (id == 0x5201)      rg_on_update(buf, (size_t)len);
                else if (id == 0x5202) rg_on_maneuver(buf, (size_t)len);
                else                   rg_on_lane_guidance(buf, (size_t)len);
            }
        }
    }
    return real_decode ? real_decode(self, buf, len) : 12;
}

/* ---------------- CinemoCreateIAP ---------------- */
typedef int (*fn_create_iap_t)(void **out);
static fn_create_iap_t real_create_iap = NULL;

EXPORT int CinemoCreateIAP(void **out) {
    if (!real_create_iap) real_create_iap = (fn_create_iap_t)dlsym(RTLD_NEXT, "CinemoCreateIAP");
    if (!real_create_iap) return 12;
    int rc = real_create_iap(out);
    if (rc == 0 && out && *out) {
        g_iap = *out;
        if (!disabled()) log_msg("CinemoCreateIAP -> %p", *out);
    }
    return rc;
}

__attribute__((constructor)) static void cl_wired_init(void) {
    log_init_from_env();
    if (getenv("CL_WIRED_LOG")) g_log_enabled = 1;
}
