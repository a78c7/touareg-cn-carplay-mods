/*
 * cl_wired.c — CarPlay route guidance hook for WIRED CarPlay on MH2p (Cinemo NME iAP2).
 *
 * Clean-room implementation using cl_wired_rg.c (no mixed GPL/CC third-party linkage).
 * Hooks:
 *   - NmeIAP2Message::Encode: advertises Route Guidance in 0x1D01, adds flags in 0x5200
 *   - NmeIAP2Message::Decode: handles 0x1D02/0x1D03 and publishes 0x5201/0x5202/0x5204
 *   - CinemoCreateIAP: remembers ICinemoIAP* and triggers StartRouteGuidanceUpdates
 *
 * Safety:
 *   - Disabled if /mnt/app/eso/bin/apps/cluster/wired_off, safe_mode, or /tmp/vw_dio_safe_mode exists.
 *   - If iPhone rejects patched Identify (0x1D03), writes /tmp/cl_wired_rejected and stops patching until reboot.
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cl_wired_rg.h"

#define EXPORT __attribute__((visibility("default")))
#define OFF_FILE        "/mnt/app/eso/bin/apps/cluster/wired_off"
#define SAFE_MODE_FILE  "/mnt/app/eso/bin/apps/cluster/safe_mode"
#define DIO_SAFE_TMP    "/tmp/vw_dio_safe_mode"
#define REJECT_FLAG     "/tmp/cl_wired_rejected"

typedef struct { uint8_t *data; int count; int capacity; int grow; } nme_array_t;

static int  g_disabled = -1;
static volatile int g_patch_ok = 1;
static volatile int g_patched = 0;
static volatile uint16_t g_rg_comp_id = 0x0010;
static void *volatile g_iap = NULL;
static volatile int g_accept_gen = 0;
static volatile int g_started_gen = 0;
static volatile int g_saw_fffb = 0;
static volatile int g_thread_running = 0;
static pthread_mutex_t g_thr_mtx = PTHREAD_MUTEX_INITIALIZER;

static int disabled(void) {
    if (access(SAFE_MODE_FILE, F_OK) == 0 || access(DIO_SAFE_TMP, F_OK) == 0)
        return 1;
    if (g_disabled < 0) {
        g_disabled = (access(OFF_FILE, F_OK) == 0) ? 1 : 0;
        if (access(REJECT_FLAG, F_OK) == 0) g_patch_ok = 0;
    }
    return g_disabled;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static uint16_t pick_component_id(const uint8_t *m, size_t len) {
    uint8_t used[256];
    memset(used, 0, sizeof(used));
    size_t io = 6;
    while (io + 4 <= len) {
        uint16_t tl = be16(m + io), tg = be16(m + io + 2);
        if (tl < 4 || io + tl > len) break;
        if (tg >= 0x000E && tl >= 10 && be16(m + io + 4) == 6 && be16(m + io + 6) == 0) {
            uint16_t id = be16(m + io + 8);
            if (id < 256) used[id] = 1;
        }
        io += tl;
    }
    if (!used[0x10]) return 0x10;
    for (uint16_t c = 0x20; c < 0xFF; c++) if (!used[c]) return c;
    return 0x10;
}

static int arr_replace(nme_array_t *a, const uint8_t *buf, size_t n) {
    if ((int)n > a->capacity) {
        uint8_t *p = (uint8_t *)realloc(a->data, n);
        if (!p) return -1;
        a->data = p;
        a->capacity = (int)n;
    }
    memcpy(a->data, buf, n);
    a->count = (int)n;
    return 0;
}

static void *start_thread(void *arg) {
    (void)arg;
    int gen = g_accept_gen;
    for (int i = 0; i < 60; i++) {
        if (g_saw_fffb && i >= 10) break;
        usleep(100000);
        if (gen != g_accept_gen) gen = g_accept_gen;
    }
    if (!disabled() && g_iap && g_started_gen != gen) {
        typedef int (*fn_start_t)(void *, int, const uint16_t *);
        fn_start_t fn = (fn_start_t)dlsym(RTLD_DEFAULT, "ICinemoIAP_StartRouteGuidanceUpdates");
        uint16_t id = g_rg_comp_id;
        if (fn) {
            int rc = fn(g_iap, 1, &id);
            w_rg_log("StartRouteGuidanceUpdates(iap=%p, comp=0x%04x) rc=%d (fffb=%d)",
                     g_iap, id, rc, g_saw_fffb);
            if (rc == 0) g_started_gen = gen;
        } else {
            w_rg_log("ICinemoIAP_StartRouteGuidanceUpdates not found");
        }
    }
    pthread_mutex_lock(&g_thr_mtx);
    g_thread_running = 0;
    pthread_mutex_unlock(&g_thr_mtx);
    return NULL;
}

static void kick_start(void) {
    pthread_mutex_lock(&g_thr_mtx);
    if (!g_thread_running) {
        pthread_t t;
        pthread_attr_t at;
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

    if (id == W_IAP2_IDENT_INFO) {
        w_rg_log_hex("IDENTIFY(cinemo) orig", m, len);
        g_patched = 0;
        if (!g_patch_ok) { w_rg_log("IDENTIFY: skipped (previously rejected)"); return rc; }
        g_rg_comp_id = pick_component_id(m, len);
        size_t cap = len + 512;
        uint8_t *out = (uint8_t *)malloc(cap);
        if (!out) return rc;
        size_t nl = w_rg_patch_identify(m, len, g_rg_comp_id, out, cap);
        if (nl && arr_replace(arr, out, nl) == 0) {
            g_patched = 1;
            w_rg_log("IDENTIFY(cinemo) patched %zu -> %zu bytes (comp=0x%04x)", len, nl, g_rg_comp_id);
        }
        free(out);
    } else if (id == W_IAP2_RG_START) {
        uint8_t buf[512];
        size_t nl = w_rg_patch_start(m, len, buf, sizeof(buf));
        if (nl && nl != len) (void)arr_replace(arr, buf, nl);
        w_rg_log_hex("0x5200 out", arr->data, (size_t)arr->count);
    } else if (id == W_IAP2_LOC_INFO) {
        if (!g_saw_fffb) w_rg_log("first LocationInformation (0xFFFB) sent");
        g_saw_fffb = 1;
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
        if (n <= 30 || (id & 0xFF00) == 0x5200 || (id & 0xFF00) == 0x1D00)
            w_rg_log("rx[%u] msgid=0x%04x len=%d", n, id, len);
        if (id == W_IAP2_IDENT_ACCEPTED) {
            w_rg_log("IdentificationAccepted (patched=%d)", g_patched);
            if (g_patched) { g_accept_gen++; g_saw_fffb = 0; kick_start(); }
        } else if (id == W_IAP2_IDENT_REJECTED) {
            w_rg_log_hex("IdentificationRejected", buf, (size_t)len);
            if (g_patched) {
                g_patch_ok = 0;
                int fd = open(REJECT_FLAG, O_WRONLY | O_CREAT, 0666);
                if (fd >= 0) close(fd);
                w_rg_log("rejected after our patch -> disabled until reboot");
            }
        } else if (id == W_IAP2_RG_UPDATE || id == W_IAP2_RG_MANEUVER || id == W_IAP2_RG_LANE) {
            w_rg_handle_rx(id, buf, (size_t)len);
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
        if (!disabled()) w_rg_log("CinemoCreateIAP -> %p", *out);
    }
    return rc;
}

__attribute__((constructor)) static void cl_wired_init(void) {
    w_rg_log_init();
}
