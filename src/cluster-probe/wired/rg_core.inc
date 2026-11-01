/*
 * Copyright (c) 2026 fifthBro
 * https://fifthbro.github.io
 *
 * Licensed under CC BY-NC-SA 4.0
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * NOT FOR COMMERCIAL USE
 */

/* Derived from fifthBro dio_manager_preload.c (v0034): logging, RG state, 0x5201/5202/5204 parsing, cluster_cp.state writer, Identify RG patch. Hooks for libesoiap2 removed. */

/* Luka's RG parser + PPS writer (compiled alongside via -I flags). */
#include "rgd_tlv.h"
#include "pps_writer.h"

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* Gated by env var DIO_CLUSTER_LOG (default off). Set to "1" before    */
/* loading the hook to enable. Macros at the bottom of this section     */
/* skip the call entirely (no arg eval) when disabled.                  */
/* ------------------------------------------------------------------ */
#define LOG_FILE "/tmp/cl_wired.log"
static FILE *g_log = NULL;
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_log_enabled = 0;

static void log_init_from_env(void) {
    const char *v = getenv("DIO_CLUSTER_LOG");
    g_log_enabled = (v && *v && *v != '0') ? 1 : 0;
}

static void log_msg_impl(const char *fmt, ...) {
    pthread_mutex_lock(&g_log_mtx);
    if (!g_log) {
        int lfd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (lfd >= 0) { fchmod(lfd, 0666); g_log = fdopen(lfd, "a"); }
        if (!g_log) { if (lfd >= 0) close(lfd); pthread_mutex_unlock(&g_log_mtx); return; }
        setbuf(g_log, NULL);
    }
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) fprintf(g_log, "[%02d:%02d:%02d] ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    pthread_mutex_unlock(&g_log_mtx);
}

static void log_hex_impl(const char *label, const uint8_t *data, size_t len) {
    pthread_mutex_lock(&g_log_mtx);
    if (!g_log) { pthread_mutex_unlock(&g_log_mtx); return; }
    size_t dump = len > 512 ? 512 : len;
    fprintf(g_log, "  %s (%zu bytes): ", label, len);
    size_t i;
    for (i = 0; i < dump; i++) fprintf(g_log, "%02x ", data[i]);
    if (dump < len) fprintf(g_log, "...");
    fputc('\n', g_log);
    pthread_mutex_unlock(&g_log_mtx);
}

/* Walk TLVs inside an iAP2 control session message (40 40 LL LL MM MM ...)
 * and log each top-level TLV's {length, tag}. Helps identify which capabilities
 * the HU declares (tag 0x0006=MessagesSent, 0x0007=MessagesReceived, etc.). */
static void log_tlv_tags_impl(const char *label, const uint8_t *msg, size_t len) {
    if (!msg || len < 8) return;
    if (msg[0] != 0x40 || msg[1] != 0x40) return;
    pthread_mutex_lock(&g_log_mtx);
    if (!g_log) { pthread_mutex_unlock(&g_log_mtx); return; }
    fprintf(g_log, "  %s TLVs:", label);
    size_t off = 6;
    int count = 0;
    while (off + 4 <= len && count < 200) {
        uint16_t tlv_len = (uint16_t)((msg[off] << 8) | msg[off+1]);
        uint16_t tlv_tag = (uint16_t)((msg[off+2] << 8) | msg[off+3]);
        if (tlv_len < 4 || off + tlv_len > len) {
            fprintf(g_log, " [BAD@%zu len=%u]", off, tlv_len);
            break;
        }
        /* For tags 0x0006/0x0007 (msgid lists), show the inner 2-byte value */
        if ((tlv_tag == 0x0006 || tlv_tag == 0x0007) && tlv_len == 6) {
            uint16_t mid = (uint16_t)((msg[off+4] << 8) | msg[off+5]);
            fprintf(g_log, " {t=%04x=%04x}", tlv_tag, mid);
        } else {
            fprintf(g_log, " {t=%04x l=%u}", tlv_tag, tlv_len);
        }
        off += tlv_len;
        count++;
    }
    fputc('\n', g_log);
    pthread_mutex_unlock(&g_log_mtx);
}

/* Gating macros — args are only evaluated when g_log_enabled != 0.
 * Defined AFTER the impls so the function definitions above are not
 * subject to macro expansion. */
#define log_msg(...)                       do { if (g_log_enabled) log_msg_impl(__VA_ARGS__); } while (0)
#define log_hex(label, data, len)          do { if (g_log_enabled) log_hex_impl((label), (data), (len)); } while (0)
#define log_tlv_tags(label, msg, len)      do { if (g_log_enabled) log_tlv_tags_impl((label), (msg), (len)); } while (0)

/* ------------------------------------------------------------------ */
/* iAP2 message IDs of interest                                        */
/* ------------------------------------------------------------------ */
#define IAP2_MSG_IDENTIFY               0x1D00
#define IAP2_MSG_IDENTIFY_ACCEPTED      0x1D02
#define IAP2_MSG_IDENTIFY_REJECTED      0x1D03
#define IAP2_MSG_RG_START               0x5200
#define IAP2_MSG_RG_UPDATE              0x5201
#define IAP2_MSG_RG_MANEUVER            0x5202
#define IAP2_MSG_RG_STOP                0x5203
#define IAP2_MSG_RG_LANE                0x5204

static const char *msgid_name(uint16_t id) {
    switch (id) {
        case IAP2_MSG_IDENTIFY:          return "Identify";
        case IAP2_MSG_IDENTIFY_ACCEPTED: return "IdentifyAccepted";
        case IAP2_MSG_IDENTIFY_REJECTED: return "IdentifyRejected";
        case IAP2_MSG_RG_START:          return "RG_Start";
        case IAP2_MSG_RG_UPDATE:         return "RG_Update";
        case IAP2_MSG_RG_MANEUVER:       return "RG_Maneuver";
        case IAP2_MSG_RG_STOP:           return "RG_Stop";
        case IAP2_MSG_RG_LANE:           return "RG_Lane";
        default:                         return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* No runtime gate. LD_PRELOAD is only wired for dio_manager via       */
/* smartphone_integrator.json's "carplay" child envs[], so the hook    */
/* only loads there in the first place. And dio_manager itself only    */
/* runs after the user accepts CarPlay, which is the natural trigger.  */
/* ------------------------------------------------------------------ */

#define IDENT_TLV_RG_COMPONENT   0x001E
static uint16_t g_rg_comp_id = 0x0010;
#define RGD_COMPONENT_ID g_rg_comp_id

/* ------------------------------------------------------------------ */
/* PPS bridge — hook side: parse RG, publish to /pps/...               */
/* Java side (CarPlayClusterIntegration) subscribes and pushes BAP.    */
/* ------------------------------------------------------------------ */
/* Plain file in /tmp — matches the convention for our cluster IPC on MH2P
 * (mirror FIFO is /tmp/cluster_ctl). Java polls the same path. */
#define PPS_RG_PATH   "/tmp/cluster_cp.state"
#define PPS_RG_OBJECT "cluster_cp"

static pps_handle_t *g_pps = NULL;
static pthread_mutex_t g_pps_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Latest-known route state. 0x5201 updates multiple fields; 0x5202 with
 * index=0 updates the current maneuver. We cache and republish all fields
 * together so Java sees a coherent snapshot. */
static struct {
    int     have_update;       /* got at least one 0x5201 */
    uint8_t route_state;
    uint8_t distance_units;          /* TLV 0x0009 — TOTAL trip (0=km,1=mi,2=m,3=yd,4=ft) */
    uint8_t dist_to_maneuver_units;  /* TLV 0x000C — NEXT TURN (same encoding) */
    uint8_t lane_guidance_showing;   /* TLV 0x0012 — 0=hide lanes, 1=show */
    uint8_t source_supports_rg;      /* TLV 0x0014 */
    uint32_t distance_remaining;     /* TLV 0x0007 — total trip, in units above */
    uint32_t dist_to_maneuver;       /* TLV 0x000A — distance to next turn (live-updating) */
    uint64_t time_remaining;
    uint64_t eta;
    char    destination[256];
    char    current_road[256];
    char    source_name[64];

    /* Maneuver list from 0x5201 TLV 0x000D — iAP indexes of upcoming maneuvers
     * in order. maneuver_list[0] is the CURRENT next turn. iPhone sends the
     * full list of 0x5202 maneuvers once upfront, then advances the list in
     * 0x5201 updates as the driver passes each turn. */
    uint16_t maneuver_list[MAX_MANEUVER_LIST];
    uint16_t maneuver_list_count;
    int      have_maneuver_list;   /* at least one 0x5201 with list */

    /* Active lane-guidance set, signalled in 0x5201 TLV 0x0010
     * (LaneGuidanceIndex). This is the lookup key into g_lane_sets[] for
     * what the cluster should currently display — NOT the maneuver iap_idx
     * and not the MAN_TLV_LINKED_LANE_GUIDANCE link from 5202 (which is
     * never present in observed Apple/Google traffic). Apple/Google update
     * this index as the route progresses to point at the lane set whose
     * lanes apply right now. 0xFFFF = no current lane set. */
    uint16_t lane_guidance_index;
} g_rg_state;

/* Slot cache — LRU-replaced maneuver slots keyed by iAP index.
 * Mirrors Luka's slot_cache, sized for BAP's 6-slot MaxGuidanceManeuverStorageCapacity. */
/* Cache capacity. Apple re-broadcasts slot[cur] every tick AND injects
 * phantom indices (6/7/8) around reroutes — with only 6 slots the LRU
 * evicts upcoming turns before ML advances to them, stranding the cluster
 * on the last known maneuver. 16 gives headroom for a typical route. */
#define MAN_SLOT_COUNT 16
/* BAP spec renders at most 6 upcoming maneuvers; cap PPS emission regardless
 * of cache size so Java's MAX_SLOTS=6 array stays in sync. */
#define MAN_SLOT_PPS_MAX 6
typedef struct {
    uint16_t iap_index;          /* 0xFFFF = empty */
    uint32_t last_seq;           /* LRU ordering */
    uint8_t  type;
    uint8_t  driving_side;
    uint8_t  junction_type;
    int16_t  exit_angle;
    int16_t  junction_angles[MAX_MANEUVER_LIST];
    uint16_t junction_angle_count;
    uint32_t distance_between;
    char     description[256];
    char     after_road[256];
    char     exit_info[RGD_EXIT_INFO_MAX + 1];
} man_slot_t;

static man_slot_t g_slots[MAN_SLOT_COUNT];
static uint32_t   g_slot_seq_counter = 0;

/* Lane-set cache, parallel to slot cache. Apple/Google send 0x5204
 * messages keyed by `LaneGuidanceIndex` and use the same numeric space as
 * iap_idx (verified 2026-04-26: a burst of 5204s with indices 0..5
 * arrived BEFORE the matching 5202s with iap_idx 0..5 — so the linkage is
 * implicit, MAN_TLV_LINKED_LANE_GUIDANCE was not present in any 5202).
 * We cache lane sets keyed by lane_guidance_index and look up by current
 * maneuver's iap_idx at publish time. */
#define LANE_SET_COUNT 16
typedef struct {
    uint16_t lg_index;             /* 0xFFFF = empty */
    uint32_t last_seq;
    uint8_t  lane_count;
    uint16_t lane_pos[MAX_LANE_GUIDANCE];
    int16_t  lane_dir[MAX_LANE_GUIDANCE];
    uint8_t  lane_status[MAX_LANE_GUIDANCE];
    int16_t  lane_angles[MAX_LANE_GUIDANCE][MAX_LANE_ANGLES];
    uint8_t  lane_angle_count[MAX_LANE_GUIDANCE];
} lane_set_t;

static lane_set_t g_lane_sets[LANE_SET_COUNT];
static uint32_t   g_lane_seq_counter = 0;

static void slots_init_once(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    int i;
    for (i = 0; i < MAN_SLOT_COUNT; i++) g_slots[i].iap_index = 0xFFFF;
    for (i = 0; i < LANE_SET_COUNT; i++) g_lane_sets[i].lg_index = 0xFFFF;
    g_rg_state.lane_guidance_index = 0xFFFF;
}

static int lane_set_find(uint16_t lg_idx) {
    int i;
    for (i = 0; i < LANE_SET_COUNT; i++)
        if (g_lane_sets[i].lg_index == lg_idx) return i;
    return -1;
}

static int lane_set_allocate(uint16_t lg_idx) {
    int existing = lane_set_find(lg_idx);
    if (existing >= 0) return existing;
    int i, oldest = 0;
    for (i = 0; i < LANE_SET_COUNT; i++) {
        if (g_lane_sets[i].lg_index == 0xFFFF) {
            g_lane_sets[i].lg_index = lg_idx;
            return i;
        }
        if (g_lane_sets[i].last_seq < g_lane_sets[oldest].last_seq) oldest = i;
    }
    memset(&g_lane_sets[oldest], 0, sizeof(g_lane_sets[oldest]));
    g_lane_sets[oldest].lg_index = lg_idx;
    return oldest;
}

/* Find the slot that currently holds iap_idx, or -1 if none. */
static int slot_find(uint16_t iap_idx) {
    int i;
    for (i = 0; i < MAN_SLOT_COUNT; i++)
        if (g_slots[i].iap_index == iap_idx) return i;
    return -1;
}

/* True if iap_idx is referenced by the current maneuver_list (upcoming turn).
 * Used to pin live entries against LRU eviction — Apple's re-broadcast pattern
 * otherwise rotates past-turn slots to the top and evicts the ones ML is
 * about to advance to. */
static int slot_pinned_by_ml(uint16_t iap_idx) {
    uint16_t i;
    if (!g_rg_state.have_maneuver_list) return 0;
    for (i = 0; i < g_rg_state.maneuver_list_count; i++)
        if (g_rg_state.maneuver_list[i] == iap_idx) return 1;
    return 0;
}

/* Allocate a slot for iap_idx (reuse existing or LRU-evict).
 * Eviction refuses slots whose index is in the current maneuver_list —
 * those are upcoming turns the driver has not passed yet. */
static int slot_allocate(uint16_t iap_idx) {
    int existing = slot_find(iap_idx);
    if (existing >= 0) return existing;
    /* find empty */
    int i;
    int oldest = -1;
    for (i = 0; i < MAN_SLOT_COUNT; i++) {
        if (g_slots[i].iap_index == 0xFFFF) {
            g_slots[i].iap_index = iap_idx;
            return i;
        }
        if (slot_pinned_by_ml(g_slots[i].iap_index)) continue;
        if (oldest < 0 || g_slots[i].last_seq < g_slots[oldest].last_seq) oldest = i;
    }
    /* All unpinned slots exhausted — fall back to global LRU so we still
     * make progress (shouldn't happen with 16 slots and a normal route). */
    if (oldest < 0) {
        oldest = 0;
        for (i = 1; i < MAN_SLOT_COUNT; i++)
            if (g_slots[i].last_seq < g_slots[oldest].last_seq) oldest = i;
    }
    memset(&g_slots[oldest], 0, sizeof(g_slots[oldest]));
    g_slots[oldest].iap_index = iap_idx;
    return oldest;
}

/* Build a CSV from int16_t array: "a,b,c". Returns bytes written (excl NUL). */
static size_t angles_to_csv(const int16_t *vals, uint16_t n, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    size_t off = 0; out[0] = '\0';
    for (uint16_t i = 0; i < n && off + 8 < out_sz; i++) {
        int written = snprintf(out + off, out_sz - off, "%s%d", i > 0 ? "," : "", (int)vals[i]);
        if (written > 0) off += (size_t)written;
    }
    return off;
}

static void pps_publish_state(void) {
    if (!g_pps) return;
    pthread_mutex_lock(&g_pps_mtx);
    pps_begin(g_pps);
    pps_write_header(g_pps);
    pps_write_bool(g_pps, "session_active", g_rg_state.have_update);
    if (g_rg_state.have_update) {
        pps_write_int(g_pps, "route_state", g_rg_state.route_state);
        pps_write_string(g_pps, "destination", g_rg_state.destination);
        pps_write_string(g_pps, "current_road", g_rg_state.current_road);
        pps_write_string(g_pps, "source_name", g_rg_state.source_name);
        pps_write_uint(g_pps, "distance_remaining", g_rg_state.distance_remaining);
        pps_write_uint(g_pps, "time_remaining", g_rg_state.time_remaining);
        pps_write_uint(g_pps, "eta", g_rg_state.eta);
        pps_write_int(g_pps, "distance_units", g_rg_state.distance_units);
        pps_write_int(g_pps, "source_supports_rg", g_rg_state.source_supports_rg);
        pps_write_uint(g_pps, "dist_to_maneuver", g_rg_state.dist_to_maneuver);
        pps_write_int(g_pps, "dist_to_maneuver_units", g_rg_state.dist_to_maneuver_units);
        pps_write_int(g_pps, "lane_guidance_showing", g_rg_state.lane_guidance_showing);
    }
    /* Stage 2 — emit slot array + current-slot pointer.
     * Slot ordering: walk the maneuver_list in order so Java gets slots
     * in turn order (slot_0 = next turn, slot_1 = one after, ...). If no
     * maneuver_list yet, just dump populated slots in their native order. */
    int emitted = 0;
    uint16_t i;
    char key[40];
    int current_slot_out = -1;   /* which emitted slot is "current next turn" */

    if (g_rg_state.have_maneuver_list) {
        for (i = 0; i < g_rg_state.maneuver_list_count && emitted < MAN_SLOT_PPS_MAX; i++) {
            int si = slot_find(g_rg_state.maneuver_list[i]);
            if (si < 0) continue;     /* list ref before 0x5202 arrived */
            man_slot_t *s = &g_slots[si];

            if (emitted == 0) current_slot_out = emitted;

            snprintf(key, sizeof(key), "slot_%d_iap_idx", emitted);
            pps_write_int(g_pps, key, s->iap_index);
            snprintf(key, sizeof(key), "slot_%d_type", emitted);
            pps_write_int(g_pps, key, s->type);
            snprintf(key, sizeof(key), "slot_%d_description", emitted);
            pps_write_string(g_pps, key, s->description);
            snprintf(key, sizeof(key), "slot_%d_road", emitted);
            pps_write_string(g_pps, key, s->after_road);
            snprintf(key, sizeof(key), "slot_%d_distance", emitted);
            pps_write_uint(g_pps, key, s->distance_between);
            snprintf(key, sizeof(key), "slot_%d_driving_side", emitted);
            pps_write_int(g_pps, key, s->driving_side);
            snprintf(key, sizeof(key), "slot_%d_junction_type", emitted);
            pps_write_int(g_pps, key, s->junction_type);
            snprintf(key, sizeof(key), "slot_%d_exit_angle", emitted);
            pps_write_int(g_pps, key, s->exit_angle);
            snprintf(key, sizeof(key), "slot_%d_exit_info", emitted);
            pps_write_string(g_pps, key, s->exit_info);
            {
                char csv[256];
                angles_to_csv(s->junction_angles, s->junction_angle_count, csv, sizeof(csv));
                snprintf(key, sizeof(key), "slot_%d_junction_angles", emitted);
                pps_write_string(g_pps, key, csv);
            }
            emitted++;
        }
    }
    pps_write_int(g_pps, "slot_count", emitted);
    pps_write_int(g_pps, "current_slot", current_slot_out);
    /* Stage 3: lane guidance. The cluster should display the lane set
     * Apple/Google explicitly point at via 0x5201 TLV 0x0010
     * (LaneGuidanceIndex), NOT the lane set whose lg_idx happens to equal
     * the current maneuver's iap_idx. Verified 2026-04-27 against direct
     * user observation: at iap_idx=3 (Scotland Rd) Apple sent 5201.lgi=0,
     * the iPhone screen showed lg_idx=0's content — 5202 / 5204 indices
     * are independent number spaces, the 5201 explicitly binds them. */
    {
        int lane_set_idx = -1;
        if (g_rg_state.lane_guidance_index != 0xFFFF) {
            lane_set_idx = lane_set_find(g_rg_state.lane_guidance_index);
        }
        if (lane_set_idx >= 0 && g_lane_sets[lane_set_idx].lane_count > 0) {
            lane_set_t *ls = &g_lane_sets[lane_set_idx];
            pps_write_bool(g_pps, "lane_active", 1);
            pps_write_int(g_pps, "lane_count", ls->lane_count);
            char key[32];
            uint8_t i;
            for (i = 0; i < ls->lane_count && i < MAX_LANE_GUIDANCE; i++) {
                snprintf(key, sizeof(key), "lane_%u_pos", (unsigned)i);
                pps_write_int(g_pps, key, ls->lane_pos[i]);
                snprintf(key, sizeof(key), "lane_%u_dir", (unsigned)i);
                pps_write_int(g_pps, key, ls->lane_dir[i]);
                snprintf(key, sizeof(key), "lane_%u_status", (unsigned)i);
                pps_write_int(g_pps, key, ls->lane_status[i]);
                {
                    char csv[256];
                    angles_to_csv(ls->lane_angles[i],
                                  ls->lane_angle_count[i],
                                  csv, sizeof(csv));
                    snprintf(key, sizeof(key), "lane_%u_angles", (unsigned)i);
                    pps_write_string(g_pps, key, csv);
                }
            }
        } else {
            pps_write_bool(g_pps, "lane_active", 0);
        }
    }
    pps_end(g_pps);
    pthread_mutex_unlock(&g_pps_mtx);
}

/* Called from sendToSession when we see 0x5201 RouteGuidanceUpdate.
 * buf/len is the full payload "40 40 LL LL 52 01 <TLVs>"; rgd_parse_update
 * expects TLV bytes only, so we skip the 6-byte message header. */
static void rg_on_update(const uint8_t *msg_payload, size_t msg_len) {
    if (msg_len < 8) return;
    rgd_update_t u;
    memset(&u, 0, sizeof(u));
    rgd_parse_update(msg_payload + 6, msg_len - 6, &u);

    g_rg_state.have_update = 1;
    if (u.present & RGD_UPD_ROUTE_STATE)        g_rg_state.route_state        = u.route_state;
    if (u.present & RGD_UPD_DISTANCE_REMAINING) g_rg_state.distance_remaining = u.distance_remaining;
    if (u.present & RGD_UPD_TIME_REMAINING)     g_rg_state.time_remaining     = u.time_remaining;
    if (u.present & RGD_UPD_ETA)                g_rg_state.eta                = u.eta;
    if (u.present & RGD_UPD_DISTANCE_UNITS)     g_rg_state.distance_units     = u.distance_units;
    if (u.present & RGD_UPD_SOURCE_SUPPORTS_RG) g_rg_state.source_supports_rg = u.source_supports_route_guidance;
    if (u.present & RGD_UPD_DIST_TO_MANEUVER)     g_rg_state.dist_to_maneuver       = u.dist_to_maneuver;
    if (u.present & RGD_UPD_DIST_TO_MANEUVER_UNI) g_rg_state.dist_to_maneuver_units = u.dist_to_maneuver_units;
    if (u.present & RGD_UPD_LANE_SHOWING)         g_rg_state.lane_guidance_showing  = u.lane_guidance_showing;
    if (u.present & RGD_UPD_LANE_INDEX)            g_rg_state.lane_guidance_index    = u.lane_guidance_index;

    /* Track the maneuver_list — which iAP indexes are the upcoming turns, in order.
     * list[0] is the CURRENT next turn. As the driver passes maneuvers, iPhone
     * advances the list (drops the head). */
    if (u.present & RGD_UPD_MANEUVER_LIST) {
        uint16_t n = u.maneuver_list_count;
        if (n > MAX_MANEUVER_LIST) n = MAX_MANEUVER_LIST;
        memcpy(g_rg_state.maneuver_list, u.maneuver_list, (size_t)n * sizeof(uint16_t));
        g_rg_state.maneuver_list_count = n;
        g_rg_state.have_maneuver_list = 1;
    }
    if (u.present & RGD_UPD_DESTINATION) {
        strncpy(g_rg_state.destination, u.destination, sizeof(g_rg_state.destination) - 1);
        g_rg_state.destination[sizeof(g_rg_state.destination) - 1] = '\0';
    }
    if (u.present & RGD_UPD_CURRENT_ROAD) {
        strncpy(g_rg_state.current_road, u.current_road, sizeof(g_rg_state.current_road) - 1);
        g_rg_state.current_road[sizeof(g_rg_state.current_road) - 1] = '\0';
    }
    if (u.present & RGD_UPD_SOURCE_NAME) {
        strncpy(g_rg_state.source_name, u.source_name, sizeof(g_rg_state.source_name) - 1);
        g_rg_state.source_name[sizeof(g_rg_state.source_name) - 1] = '\0';
    }
    pps_publish_state();
}

/* Called on 0x5202 RouteGuidanceManeuverUpdate.
 * Stage 2: cache every maneuver by iAP index in a slot. pps_publish_state()
 * then publishes all populated slots + which slot is the current turn based
 * on the 0x5201 maneuver_list. */
/* AMap: "去往XX路" -> "XX路" (the HUD only fits ~5 chars). */
static void strip_go_to(char *p) {
    static const char *pre[] = { "\xe5\x8e\xbb\xe5\xbe\x80" /* 去往 */ };
    size_t i;
    if (!p) return;
    for (i = 0; i < sizeof(pre)/sizeof(pre[0]); i++) {
        char *q = p; size_t n = strlen(pre[i]);
        while (*q == ' ') q++;
        if (strncmp(q, pre[i], n) == 0 && q[n]) {
            q += n;
            while (*q == ' ') q++;
            if (*q) memmove(p, q, strlen(q) + 1);
            return;
        }
    }
}

static void rg_on_maneuver(const uint8_t *msg_payload, size_t msg_len) {
    if (msg_len < 8) return;
    slots_init_once();
    rgd_maneuver_t m;
    memset(&m, 0, sizeof(m));
    rgd_parse_maneuver(msg_payload + 6, msg_len - 6, &m);
    if (!(m.present & RGD_MAN_INDEX)) return;

    int si = slot_allocate(m.index);
    if (si < 0) return;
    man_slot_t *s = &g_slots[si];
    s->last_seq = ++g_slot_seq_counter;
    if (m.present & RGD_MAN_TYPE)             s->type             = m.maneuver_type;
    if (m.present & RGD_MAN_DRIVING_SIDE)     s->driving_side     = m.driving_side;
    if (m.present & RGD_MAN_JUNCTION_TYPE)    s->junction_type    = m.junction_type;
    if (m.present & RGD_MAN_EXIT_ANGLE)       s->exit_angle       = m.exit_angle;
    if (m.present & RGD_MAN_DISTANCE_BETWEEN) s->distance_between = m.distance_between;
    strip_go_to(m.description); strip_go_to(m.after_road_name);
    if (m.present & RGD_MAN_DESCRIPTION) {
        strncpy(s->description, m.description, sizeof(s->description) - 1);
        s->description[sizeof(s->description) - 1] = '\0';
    }
    if (m.present & RGD_MAN_AFTER_ROAD) {
        strncpy(s->after_road, m.after_road_name, sizeof(s->after_road) - 1);
        s->after_road[sizeof(s->after_road) - 1] = '\0';
    }
    if (m.present & RGD_MAN_JUNCTION_ANGLES) {
        uint16_t nj = m.junction_angle_count;
        if (nj > MAX_MANEUVER_LIST) nj = MAX_MANEUVER_LIST;
        memcpy(s->junction_angles, m.junction_angles, (size_t)nj * sizeof(int16_t));
        s->junction_angle_count = nj;
    } else {
        s->junction_angle_count = 0;
    }
    if (m.present & RGD_MAN_EXIT_INFO_STR) {
        strncpy(s->exit_info, m.exit_info_str, sizeof(s->exit_info) - 1);
        s->exit_info[sizeof(s->exit_info) - 1] = '\0';
    } else {
        s->exit_info[0] = '\0';
    }
    pps_publish_state();
}

/* Called on 0x5204 RouteGuidanceLaneGuidanceInformation.
 * Stage 3: cache lanes BY lane_guidance_index. Apple/Google use that index
 * as an implicit reference to the matching maneuver iap_idx (no
 * MAN_TLV_LINKED_LANE_GUIDANCE on 5202s in observed runs), so storing the
 * latest 5204 globally collapsed all upcoming maneuvers' lane data onto
 * the last-received one. pps_publish_state now looks up the *current*
 * maneuver's iap_idx in g_lane_sets[]. */
static void rg_on_lane_guidance(const uint8_t *msg_payload, size_t msg_len) {
    if (msg_len < 8) return;
    slots_init_once();
    rgd_lane_guidance_t lg;
    memset(&lg, 0, sizeof(lg));
    rgd_parse_lane_guidance(msg_payload + 6, msg_len - 6, &lg);

    /* Empty lane set for this index means "this maneuver has no lanes" —
     * still record it so we can report lane_active=false when current. */
    int si = lane_set_allocate(lg.lane_guidance_index);
    if (si < 0) { pps_publish_state(); return; }
    lane_set_t *ls = &g_lane_sets[si];
    ls->last_seq = ++g_lane_seq_counter;
    uint8_t n = lg.lane_count;
    if (n > MAX_LANE_GUIDANCE) n = MAX_LANE_GUIDANCE;
    ls->lane_count = n;
    uint8_t i;
    for (i = 0; i < n; i++) {
        ls->lane_pos[i]    = lg.lanes[i].position;
        ls->lane_dir[i]    = lg.lanes[i].direction;
        ls->lane_status[i] = lg.lanes[i].status;
        uint8_t ac = lg.lanes[i].angle_count;
        if (ac > MAX_LANE_ANGLES) ac = MAX_LANE_ANGLES;
        ls->lane_angle_count[i] = ac;
        for (uint8_t k = 0; k < ac; k++)
            ls->lane_angles[i][k] = lg.lanes[i].angles[k];
    }
    pps_publish_state();
}

static void rg_pps_init(void) {
    pps_config_t cfg = PPS_CONFIG_DEFAULT;
    cfg.path          = PPS_RG_PATH;
    cfg.object_name   = PPS_RG_OBJECT;
    cfg.mode          = PPS_MODE_TEXT;
    cfg.create_dirs   = true;
    cfg.keep_open     = true;
    cfg.truncate_on_write = true;
    g_pps = pps_open(&cfg);
    if (g_pps) {
        log_msg("PPS: opened %s", PPS_RG_PATH);
    } else {
        log_msg("PPS: FAILED to open %s", PPS_RG_PATH);
    }
}

/* write_be16 / read_be16 come from Luka's common.h (via rgd_tlv.h). */

static size_t build_rg_component_tlv(uint8_t *out, size_t max_len);

/* Rebuild the Identify message, extending existing 0x0006/0x0007 TLVs in place
 * with extra msgids. Apple rejects duplicate occurrences of 0x0006 or 0x0007
 * (test 20260419_194655: 0x1D03 payload {tag=0006}{tag=0007}), so we must
 * merge into the existing TLVs rather than append new ones.
 *
 * Extends 0x0006 with: 0x5200, 0x5203 (accessory-sent RG msgs)
 * Extends 0x0007 with: 0x5201, 0x5202, 0x5204 (accessory-received RG msgs)
 *
 * in/in_len: original 40 40 LL LL 1D 01 <tlvs> message.
 * out/out_max: destination buffer.
 * Returns new message length (including 40 40 header and updated length field),
 * or 0 on failure. */
/* v15: drop 0xFFFB/0xFFFA/0xFFFC from Identify patch.
 * iap2connectionmanager has no Location module registered. Advertising
 * Location caused phone to send 0xFFFA StartLocationInformation which the
 * library then couldn't route (no handler) and SIGSEGV'd in the control
 * session dispatch path. Location must only be advertised in a process
 * that actually has the Location module wired (dio_manager). */
static const uint16_t RG_SENT_MSGIDS[] = { 0x5200, 0x5203 };
static const uint16_t RG_RECV_MSGIDS[] = { 0x5201, 0x5202, 0x5204 };

static size_t rebuild_identify_with_rg(const uint8_t *in, size_t in_len,
                                       uint8_t *out, size_t out_max) {
    if (in_len < 6 || out_max < in_len + 256) return 0;
    if (in[0] != 0x40 || in[1] != 0x40) return 0;

    /* Copy 40 40 LL LL MM MM header (length fixed up at end). */
    memcpy(out, in, 6);
    size_t oo = 6;
    size_t io = 6;

    while (io + 4 <= in_len) {
        uint16_t tlv_len = (uint16_t)((in[io] << 8) | in[io+1]);
        uint16_t tlv_tag = (uint16_t)((in[io+2] << 8) | in[io+3]);
        if (tlv_len < 4 || io + tlv_len > in_len) return 0;

        if (tlv_tag == 0x0006 || tlv_tag == 0x0007) {
            const uint16_t *extra = (tlv_tag == 0x0006) ? RG_SENT_MSGIDS : RG_RECV_MSGIDS;
            size_t extra_n = (tlv_tag == 0x0006) ? (sizeof(RG_SENT_MSGIDS)/2)
                                                 : (sizeof(RG_RECV_MSGIDS)/2);
            size_t new_tlv_len = tlv_len + 2 * extra_n;
            if (oo + new_tlv_len > out_max) return 0;
            write_be16(out + oo, (uint16_t)new_tlv_len);
            write_be16(out + oo + 2, tlv_tag);
            /* Copy original msgid payload (skip 4-byte TLV header). */
            memcpy(out + oo + 4, in + io + 4, tlv_len - 4);
            /* Append new msgids (big-endian). */
            size_t eoff = oo + tlv_len;
            size_t i;
            for (i = 0; i < extra_n; i++) {
                write_be16(out + eoff + 2*i, extra[i]);
            }
            oo += new_tlv_len;
        } else {
            if (oo + tlv_len > out_max) return 0;
            memcpy(out + oo, in + io, tlv_len);
            oo += tlv_len;
        }
        io += tlv_len;
    }

    /* Append 0x001E RouteGuidanceDisplayComponent TLV. */
    size_t rg_len = build_rg_component_tlv(out + oo, out_max - oo);
    if (rg_len == 0) return 0;
    oo += rg_len;

    /* Fix up top-level length field (bytes 2-3). */
    write_be16(out + 2, (uint16_t)oo);
    return oo;
}

/*
 * Build the RouteGuidanceDisplayComponent TLV for Identify patching.
 * Matches MHI3 libesoiap2.so layout (9 inner TLVs).
 * Returns bytes written, or 0 on failure.
 */
static size_t build_rg_component_tlv(uint8_t *out, size_t max_len) {
    const char *name = "RouteGuidanceDisplayComponent";
    size_t name_len = strlen(name) + 1;  /* include null terminator */
    size_t inner_len = 6 + (4 + name_len) + 6*7;  /* id + name + 7 uint16 TLVs */
    size_t outer_len = 4 + inner_len;

    if (max_len < outer_len) return 0;

    size_t off = 0;
    /* Outer header */
    write_be16(out + off, (uint16_t)outer_len); off += 2;
    write_be16(out + off, IDENT_TLV_RG_COMPONENT); off += 2;

    /* 0x0000 Identifier */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0000); off += 2;
    write_be16(out + off, RGD_COMPONENT_ID); off += 2;

    /* 0x0001 Name */
    write_be16(out + off, (uint16_t)(4 + name_len)); off += 2;
    write_be16(out + off, 0x0001); off += 2;
    memcpy(out + off, name, name_len); off += name_len;

    /* 0x0002 MaxCurrentRoadNameLength = 256 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0002); off += 2;
    write_be16(out + off, 0x0100); off += 2;

    /* 0x0003 MaxDestinationNameLength = 256 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0003); off += 2;
    write_be16(out + off, 0x0100); off += 2;

    /* 0x0004 MaxAfterManeuverRoadNameLength = 256 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0004); off += 2;
    write_be16(out + off, 0x0100); off += 2;

    /* 0x0005 MaxManeuverDescriptionLength = 256 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0005); off += 2;
    write_be16(out + off, 0x0100); off += 2;

    /* 0x0006 MaxGuidanceManeuverStorageCapacity = 6 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0006); off += 2;
    write_be16(out + off, 0x0006); off += 2;

    /* 0x0007 MaxLaneGuidanceDescriptionLength = 256 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0007); off += 2;
    write_be16(out + off, 0x0100); off += 2;

    /* 0x0008 MaxLaneGuidanceStorageCapacity = 6 */
    write_be16(out + off, 6); off += 2;
    write_be16(out + off, 0x0008); off += 2;
    write_be16(out + off, 0x0006); off += 2;

    return off;
}

/* Patched identify buffer — persists for the session */
