/*
 * carlife_probe.c — 途锐 MH2p (QNX 6.6 ARMv7) 原厂 /carlife/carlife LD_PRELOAD 增强探针 v3.0
 *
 * 阶段一核心能力（全量继承）：
 *   1. 原生点对点满屏（零黑边）：在 0x00018052 (FeatureConfigResponse) 追加
 *      DISABLE_RESOLUTION_OPTION=1 与 I_FRAME_INTERVAL=30，并将 cmdVideoEncoderInit 原厂 1024x480
 *      升级为 1664x736（16px 宏块对齐，默认 res.conf 为 1664 736 20）。
 *   2. 真帧率控制 + 实时帧率/码率/触控遥测：下发 0x0001800C (98316) 与 /tmp/carlife_fps_req 热切。
 *   3. 2016 原厂 SDK 防崩护盾（Hook CConnectManager::readCmdData）：包头层安全吸收未知消息与
 *      0x10031/33/34，原生零崩溃 protobuf wire 解析 0x00010030 (NaviNextTurnInfo)。
 *   4. 导航上挡风 HUD & 仪表导航卡：0x00018043 -> 0x00010044 -> 0x00018045 握手，
 *      74 项转向映射原子写入 /tmp/cluster_cp.state。
 *   5. 拔线 1 秒自动清理 HUD（抗 SIGKILL）：SPAWN_SETSID | SPAWN_NOZOMBIE 派生剥离 LD_PRELOAD 的
 *      独立守护子进程监控父进程退出，配合 /proc/self/exefile 进程名门禁。
 *   6. 触控坐标默认直通 + 屏蔽原厂仪表「导航已在移动设备上激活」弹窗。
 *
 * 阶段二新增能力（v3.0，默认安全关闭）：
 *   7. 独立 /carlife_h264_shm 仪表 3D 地图输出（与 CarPlay /cluster_h264_shm 彻底物理隔离）：
 *      受 cluster_3d.conf（默认 mode=off）与热开关门控；mode=off 时零扫描零拷贝零开销；
 *      开启后按 IDR+SPS/PPS 对齐写入 2MB 环缓，同步更新 owner_pid、src_w/src_h、
 *      nav_active 与中控触控时间戳 touch_active_ms，供 carlife_clusterd 低载渲染与避让。
 *   8. 支线 C2（防蓝牙抢焦点开关 bt_audio_lock，默认关闭）：
 *      存在 /mnt/app/eso/bin/apps/carlife_mod/bt_audio_lock 或 /tmp/carlife_bt_audio_lock 时，
 *      掩码 ModuleStatus(moduleID=3, statusID=1 -> 0) 并拦截 mediaRegisterInit/mediaRegisterResume
 *      回调，阻止 CarLife 抢占 RESOURCE_AUDIO_MEDIA 顶掉蓝牙听歌（VW BT），同时保持 USB PCM
 *      导航语音播报（TTS -> ANN_M）100% 直通。
 *
 * Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#ifdef __QNXNTO__
#include <spawn.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __QNXNTO__
#include "aeabi_shim.h"
#endif

#include "carlife_shm.h"

#define EXPORT __attribute__((visibility("default")))

/* ---- 路径与热调开关（支持 HOST_PROBE_TEST 覆写以便本机单元测试）---- */
#ifndef MOD_DIR
#define MOD_DIR                 CARLIFE_MOD_DIR
#endif
#ifndef DISABLE_PERM
#define DISABLE_PERM            MOD_DIR "/disable"
#endif
#ifndef DISABLE_TMP
#define DISABLE_TMP             "/tmp/carlife_probe_off"
#endif
#ifndef RES_CONF_PERM
#define RES_CONF_PERM           MOD_DIR "/res.conf"
#endif
#ifndef RES_CONF_TMP
#define RES_CONF_TMP            "/tmp/carlife_res.conf"
#endif
#ifndef FORCE_TOUCH_SCALE_TMP
#define FORCE_TOUCH_SCALE_TMP   "/tmp/carlife_force_touch_scale"
#endif
#ifndef HUD_OFF_TMP
#define HUD_OFF_TMP             "/tmp/carlife_hud_off"
#endif
#ifndef ALLOW_NAVI_POPUP_TMP
#define ALLOW_NAVI_POPUP_TMP    "/tmp/carlife_allow_navi_popup"
#endif
#ifndef NO_RES_PASSTHROUGH_TMP
#define NO_RES_PASSTHROUGH_TMP  "/tmp/carlife_no_res_passthrough"
#endif
#ifndef NO_REINIT_TMP
#define NO_REINIT_TMP           "/tmp/carlife_no_reinit"
#endif
#ifndef FPS_REQ_TMP
#define FPS_REQ_TMP             "/tmp/carlife_fps_req"
#endif
#ifndef BT_LOCK_PERM
#define BT_LOCK_PERM            CARLIFE_BT_LOCK_PERM
#endif
#ifndef BT_LOCK_TMP
#define BT_LOCK_TMP             CARLIFE_BT_LOCK_TMP
#endif
#ifndef C3D_CONF_PERM
#define C3D_CONF_PERM           CARLIFE_CLUSTER_CONF_PERM
#endif
#ifndef C3D_CONF_TMP
#define C3D_CONF_TMP            CARLIFE_CLUSTER_CONF_TMP
#endif
#ifndef C3D_ON_PERM
#define C3D_ON_PERM             CARLIFE_CLUSTER_ON_PERM
#endif
#ifndef C3D_ON_TMP
#define C3D_ON_TMP              CARLIFE_CLUSTER_ON_TMP
#endif
#ifndef C3D_OFF_TMP
#define C3D_OFF_TMP             CARLIFE_CLUSTER_OFF_TMP
#endif
#ifndef C3D_FUSED_TMP
#define C3D_FUSED_TMP           CARLIFE_CLUSTER_FUSED_TMP
#endif
#ifndef SKIN3_INFO_PATH
#define SKIN3_INFO_PATH         "/mnt/app/eso/hmi/lsd/Resources/skin3/info.txt"
#endif
#ifndef STATE_PATH
#define STATE_PATH              "/tmp/cluster_cp.state"
#endif
#ifndef MEDIA_STATE_PATH
#define MEDIA_STATE_PATH        "/tmp/carlife_media.state"
#endif
#ifndef LOG_PATH
#define LOG_PATH                "/tmp/carlife_probe.log"
#endif

/* CarLife 控制通道消息号（大端 8B 帧头: [len_hi, len_lo, 0, 0, type_b3..b0]） */
#define MSG_CMD_VIDEO_ENCODER_FRAME_RATE_CHANGE 0x0001800Cu
#define MSG_CMD_CARLIFE_DATA_SUBSCRIBE          0x00018043u
#define MSG_CMD_CARLIFE_DATA_SUBSCRIBE_START    0x00018045u
#define MSG_CMD_HU_FEATURE_CONFIG_RESPONSE      0x00018052u
#define MSG_CMD_NAVI_NEXT_TURN_INFO             0x00010030u

/* ---- DWARF / CommonUtil.h 核实的结构体布局 ---- */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frameRate;
} cl_video_enc_t;

typedef struct {
    uint32_t frameRate;
} cl_fps_change_t;

typedef struct {
    uint32_t sampleRate;
    uint32_t channelConfig;
    uint32_t sampleFormat;
} cl_audio_init_t;

typedef struct {
    uint32_t action;
    uint32_t x;
    uint32_t y;
} cl_touch_action_t;

typedef struct {
    uint32_t x;
    uint32_t y;
} cl_touch_pt_t;

/* QNX libcpp-ne.so.5 Dinkumware std::string 真实内存布局 (24B) */
typedef struct {
    union {
        char buf[16];
        const char *ptr;
    } bx;
    uint32_t mysize; /* +0x10 */
    uint32_t myres;  /* +0x14: <= 15 为 SSO 内联 buf, > 15 为堆指针 ptr */
} qnx_dinkum_string_t;

typedef struct cl_feature_config {
    qnx_dinkum_string_t key;
    uint32_t value;
    struct cl_feature_config *pNext;
} cl_feature_config_t;

typedef struct {
    uint32_t cnt;
    cl_feature_config_t *pFeatureConfig;
} cl_feature_config_list_t;

typedef struct cl_mod_status {
    uint32_t moduleID;
    uint32_t statusID;
    struct cl_mod_status *next;
} cl_mod_status_t;

typedef struct {
    uint32_t cnt;
    cl_mod_status_t *moduleStatus;
} cl_mod_status_list_t;

typedef struct cl_sub_info {
    uint32_t moduleID;
    uint8_t supportFlag;
    uint8_t pad[3];
    uint32_t frequency;
    struct cl_sub_info *pNext;
} cl_sub_info_t;

typedef struct {
    uint32_t cnt;
    cl_sub_info_t *pMobileCarLifeInfo;
} cl_sub_info_list_t;

typedef struct {
    uint32_t action;
    uint32_t nextTurn;
    qnx_dinkum_string_t roadName;
    uint32_t totalDistance;
    uint32_t remainDistance;
    uint32_t time;
} cl_navi_turn_t;

typedef struct {
    uint32_t action;
    uint32_t assistantType;
    uint32_t trafficSignType;
    uint32_t totalDistance;
    uint32_t remainDistance;
    uint32_t cameraSpeed;
} cl_navi_assist_t;

typedef struct {
    char source[256];
    char song[128];
    char artist[128];
    char album[128];
    char albumArt[128];
    uint32_t duration;
    uint32_t playlistNum;
    char songId[128];
    uint32_t mode;
} cl_media_info_t;

/* ---- 全局状态 ---- */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cmd_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile uint32_t g_win_w = 1673u;
static volatile uint32_t g_win_h = 748u;
static volatile uint32_t g_req_w = 1664u;
static volatile uint32_t g_req_h = 736u;
static volatile uint32_t g_req_fps = 20u;
static volatile uint32_t g_enc_w = 0u;
static volatile uint32_t g_enc_h = 0u;
static volatile uint32_t g_enc_fps = 0u;

static volatile int g_inited = 0;
static volatile int g_we_own_state = 0;
static volatile int g_reaper_spawned = 0;
static volatile int g_nav_active = 0;
static volatile uint64_t g_last_navi_ms = 0;
static volatile uint64_t g_last_touch_ms = 0;
static volatile int g_we_own_shm = 0;
static volatile int g_feat_sent_passthrough = 0;
static volatile int g_worker_started = 0;
static volatile int g_session_gen = 0;
static volatile int g_sub_done_received = 0;
static volatile uint32_t g_sub_supported_mask = 0x1u; /* 默认只订 module 0 (NaviNextTurnInfo) */
static volatile int g_reinit_done = 0;

/* cluster_3d 模式缓存（避免 20fps 视频回调每帧读盘；每 1s 刷新） */
static volatile int g_c3d_cached_mode = CARLIFE_C3D_OFF;
static uint64_t g_c3d_last_check_ms = 0;

/* H.264 SPS/PPS 缓存（供仪表 3D 地图在任意时刻热开启后 1 秒内秒出画面） */
static uint8_t  g_sps_pps_buf[512];
static uint32_t g_sps_pps_len = 0;
static volatile int g_shm_need_idr = 1;

/* readCmdData 防崩护盾状态（仅由单线程 cmdReceiveThread 访问） */
static uint32_t g_pass_body_rem = 0;
static uint32_t g_shield_log_cnt = 0;
static uint8_t  g_cmd_scratch[40960];

static volatile uint32_t g_touch_log_cnt = 0;
static volatile uint32_t g_navi_log_cnt = 0;
static volatile uint32_t g_assist_log_cnt = 0;
static volatile uint32_t g_video_pkt_cnt = 0;

/* 实时视频/触控采样计数器 */
static volatile uint32_t g_stat_pkts = 0;
static volatile uint32_t g_stat_frames = 0;
static volatile uint32_t g_stat_bytes = 0;
static volatile uint32_t g_stat_moves = 0;
static uint64_t g_stat_last_ms = 0;

static void (*g_orig_enc_done_cb)(cl_video_enc_t *) = NULL;
static void (*g_orig_fps_done_cb)(cl_fps_change_t *) = NULL;
static void (*g_orig_navi_cb)(cl_navi_turn_t *) = NULL;
static void (*g_orig_media_cb)(cl_media_info_t *) = NULL;
static void (*g_orig_mod_status_cb)(cl_mod_status_list_t *) = NULL;
static void (*g_orig_video_cb)(unsigned char *, unsigned int) = NULL;
static void (*g_orig_media_init_cb)(cl_audio_init_t *) = NULL;
static void (*g_orig_media_resume_cb)(void) = NULL;
static void *g_carlife_lib_self = NULL;

static carlife_h264_shm_t *g_shm = NULL;

typedef int (*fn_enc_init_t)(void *, cl_video_enc_t *);
typedef void *(*fn_get_inst_t)(void);
typedef int (*fn_rw_cmd_t)(void *, unsigned char *, unsigned int);
typedef void (*fn_reg_sub_done_t)(void *, void (*)(cl_sub_info_list_t *));
typedef void (*fn_reg_assist_t)(void *, void (*)(cl_navi_assist_t *));
typedef void (*fn_reg_fps_done_t)(void *, void (*)(cl_fps_change_t *));

static fn_enc_init_t g_real_enc_init = NULL;

/* ---- 时间与日志工具 ---- */

static uint64_t mono_ms(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000UL);
}

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int is_disabled(void) {
    return file_exists(DISABLE_PERM) || file_exists(DISABLE_TMP);
}

static int is_bt_audio_locked(void) {
    return file_exists(BT_LOCK_PERM) || file_exists(BT_LOCK_TMP);
}

static void plog(const char *fmt, ...) {
    char buf[512];
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(buf, sizeof(buf), "[%lu.%03lu] ",
                     (unsigned long)ts.tv_sec, (unsigned long)(ts.tv_nsec / 1000000UL));
    if (n < 0) n = 0;
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n - 2u, fmt, ap);
    va_end(ap);
    if (m > 0) n += m;
    if ((size_t)n >= sizeof(buf) - 2u) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';
    buf[n] = '\0';

    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        if (write(fd, buf, (size_t)n) < 0) {}
        close(fd);
    }
}

static void sanitize_str(char *dst, size_t cap, const char *src, size_t max_in) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t o = 0;
    for (size_t i = 0; i < max_in && src[i] != '\0' && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            dst[o++] = ' ';
        } else if (c >= 0x20u) {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

static void read_dinkum_string(const qnx_dinkum_string_t *s, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!s) return;
    uint32_t sz = s->mysize;
    if (sz == 0 || sz > 4096u) return;
    const char *raw = (s->myres <= 15u) ? s->bx.buf : s->bx.ptr;
    if (!raw) return;
    size_t lim = sz;
    if (s->myres <= 15u && lim > 15u) lim = 15u;
    if (lim > cap - 1u) lim = cap - 1u;
    sanitize_str(out, cap, raw, lim);
}

static void load_skin_win_size(void) {
    FILE *f = fopen(SKIN3_INFO_PATH, "r");
    if (!f) return;
    char line[256];
    uint32_t w = 0, h = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned v = 0;
        if (sscanf(line, "Layout.CarLife.Canvas_Dimension.width=%u", &v) == 1 && v >= 320 && v <= 3840) {
            w = v;
        } else if (sscanf(line, "Layout.CarLife.Canvas_Dimension.height=%u", &v) == 1 && v >= 240 && v <= 2160) {
            h = v;
        }
    }
    fclose(f);
    if (w && h) {
        g_win_w = w;
        g_win_h = h;
    }
}

static void load_res_conf(void) {
    const char *paths[2] = { RES_CONF_TMP, RES_CONF_PERM };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        unsigned w = 0, h = 0, fps = 0;
        int n = fscanf(f, "%u %u %u", &w, &h, &fps);
        fclose(f);
        if (n >= 2) {
            g_req_w = w;
            g_req_h = h;
            g_req_fps = (n == 3 && fps >= 3 && fps <= 60) ? fps : 20u;
            return;
        }
    }
}

/* 解析 cluster_3d.conf（默认 CARLIFE_C3D_OFF），优先级与 carlife_clusterd.c 完全一致 */
static int parse_cluster_3d_conf_file(const char *path, int *out_mode) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n') continue;
        if (strncmp(p, "mode=", 5) == 0) {
            char val[32];
            size_t i = 0;
            const char *v = p + 5;
            while (*v == ' ' || *v == '\t') v++;
            while (*v && *v != '\r' && *v != '\n' && *v != ' ' && *v != '\t' && *v != '#' && i + 1 < sizeof(val)) {
                val[i++] = *v++;
            }
            val[i] = '\0';
            if (strcmp(val, "off") == 0 || strcmp(val, "0") == 0) {
                *out_mode = CARLIFE_C3D_OFF;
                found = 1;
            } else if (strcmp(val, "nav") == 0 || strcmp(val, "nav_only") == 0 || strcmp(val, "1") == 0) {
                *out_mode = CARLIFE_C3D_NAV_ONLY;
                found = 1;
            } else if (strcmp(val, "always") == 0 || strcmp(val, "on") == 0 || strcmp(val, "2") == 0) {
                *out_mode = CARLIFE_C3D_ALWAYS;
                found = 1;
            }
        }
    }
    fclose(f);
    return found;
}

static int parse_cluster_3d_on_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return CARLIFE_C3D_ALWAYS;
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    if (strstr(buf, "nav") != NULL || strstr(buf, "NAV") != NULL) {
        return CARLIFE_C3D_NAV_ONLY;
    }
    return CARLIFE_C3D_ALWAYS;
}

static int query_cluster_3d_mode(int force_refresh) {
    uint64_t now = mono_ms();
    if (!force_refresh && g_c3d_last_check_ms > 0 && now < g_c3d_last_check_ms + 1000ULL) {
        return g_c3d_cached_mode;
    }
    g_c3d_last_check_ms = now;

    int mode = CARLIFE_C3D_OFF;
    if (file_exists(C3D_OFF_TMP) || file_exists(C3D_FUSED_TMP)) {
        mode = CARLIFE_C3D_OFF;
    } else if (file_exists(C3D_ON_TMP)) {
        mode = parse_cluster_3d_on_file(C3D_ON_TMP);
    } else if (file_exists(C3D_ON_PERM)) {
        mode = parse_cluster_3d_on_file(C3D_ON_PERM);
    } else {
        int m = CARLIFE_C3D_OFF;
        if (parse_cluster_3d_conf_file(C3D_CONF_TMP, &m) ||
            parse_cluster_3d_conf_file(C3D_CONF_PERM, &m)) {
            mode = m;
        }
    }
    g_c3d_cached_mode = mode;
    return mode;
}

static void sync_nav_active_to_shm(int active) {
    g_nav_active = active ? 1 : 0;
    if (g_shm) {
        g_shm->nav_active = (uint32_t)g_nav_active;
        carlife_shm_mb();
    }
}

/* ---- 底层控制通道发送（大端 8B 帧头 + Protobuf 载荷）---- */

static int send_raw_cmd(uint32_t msg_type, const uint8_t *payload, uint32_t len) {
    static fn_get_inst_t cm_get_inst = NULL;
    static fn_rw_cmd_t cm_write = NULL;
    if (!cm_get_inst) cm_get_inst = (fn_get_inst_t)dlsym(RTLD_DEFAULT, "_ZN15CConnectManager11getInstanceEv");
    if (!cm_write) cm_write = (fn_rw_cmd_t)dlsym(RTLD_DEFAULT, "_ZN15CConnectManager12writeCmdDataEPhj");
    if (!cm_get_inst || !cm_write) return -1;
    void *cm = cm_get_inst();
    if (!cm) return -1;

    uint8_t frame[512];
    if (len + 8u > sizeof(frame)) return -1;
    frame[0] = (uint8_t)((len >> 8) & 0xFFu);
    frame[1] = (uint8_t)(len & 0xFFu);
    frame[2] = 0;
    frame[3] = 0;
    frame[4] = (uint8_t)((msg_type >> 24) & 0xFFu);
    frame[5] = (uint8_t)((msg_type >> 16) & 0xFFu);
    frame[6] = (uint8_t)((msg_type >> 8) & 0xFFu);
    frame[7] = (uint8_t)(msg_type & 0xFFu);
    if (len > 0 && payload) memcpy(frame + 8, payload, len);

    pthread_mutex_lock(&g_cmd_lock);
    int ok = cm_write(cm, frame, len + 8u);
    pthread_mutex_unlock(&g_cmd_lock);
    return ok ? 0 : -1;
}

static size_t pb_varint(uint8_t *dst, uint32_t v) {
    size_t n = 0;
    do {
        uint8_t b = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        if (v) b |= 0x80u;
        dst[n++] = b;
    } while (v);
    return n;
}

static size_t pb_append_feature_kv(uint8_t *dst, size_t cap, const char *key, uint32_t val) {
    size_t klen = strlen(key);
    uint8_t inner[96];
    size_t il = 0;
    if (klen + 16u >= sizeof(inner)) return 0;
    inner[il++] = 0x0Au;
    il += pb_varint(inner + il, (uint32_t)klen);
    memcpy(inner + il, key, klen);
    il += klen;
    inner[il++] = 0x10u;
    il += pb_varint(inner + il, val);

    if (il + 4u > cap) return 0;
    size_t ol = 0;
    dst[ol++] = 0x12u;
    ol += pb_varint(dst + ol, (uint32_t)il);
    memcpy(dst + ol, inner, il);
    ol += il;
    return ol;
}

static void send_passthrough_feature_config(void) {
    if (file_exists(NO_RES_PASSTHROUGH_TMP)) return;
    uint8_t pb[160];
    size_t n = 0;
    pb[n++] = 0x08u; /* cnt = 2 */
    pb[n++] = 0x02u;
    n += pb_append_feature_kv(pb + n, sizeof(pb) - n, "DISABLE_RESOLUTION_OPTION", 1u);
    n += pb_append_feature_kv(pb + n, sizeof(pb) - n, "I_FRAME_INTERVAL", 30u);
    int r = send_raw_cmd(MSG_CMD_HU_FEATURE_CONFIG_RESPONSE, pb, (uint32_t)n);
    g_feat_sent_passthrough = 1;
    plog("sent FeatureConfig(DISABLE_RESOLUTION_OPTION=1, I_FRAME_INTERVAL=30) ret=%d", r);
}

static int send_hud_subscribe_cmd(uint32_t msg_type, uint32_t mask) {
    uint8_t pb[32];
    uint32_t cnt = 0;
    uint32_t ids[2];
    if (mask & 1u) ids[cnt++] = 0u;
    if (mask & 2u) ids[cnt++] = 1u;
    if (cnt == 0) return 0;

    size_t n = 0;
    pb[n++] = 0x08u;
    pb[n++] = (uint8_t)cnt;
    for (uint32_t i = 0; i < cnt; i++) {
        pb[n++] = 0x12u;
        pb[n++] = 0x04u;
        pb[n++] = 0x08u;
        pb[n++] = (uint8_t)ids[i];
        pb[n++] = 0x10u;
        pb[n++] = 0x01u;
    }
    int r = send_raw_cmd(msg_type, pb, (uint32_t)n);
    plog("sent HUD cmd 0x%08x mask=0x%x (cnt=%u) ret=%d", msg_type, mask, cnt, r);
    return r;
}

static int send_fps_change(uint32_t fps) {
    if (fps < 3u) fps = 3u;
    if (fps > 60u) fps = 60u;
    uint8_t pb[8];
    size_t n = 0;
    pb[n++] = 0x08u;
    n += pb_varint(pb + n, fps);
    int r = send_raw_cmd(MSG_CMD_VIDEO_ENCODER_FRAME_RATE_CHANGE, pb, (uint32_t)n);
    plog("sent VIDEO_ENCODER_FRAME_RATE_CHANGE (98316) fps=%u ret=%d", fps, r);
    return r;
}

/* ---- CarLife raw nextTurn (1..76) -> iAP2 ManeuverType (0..53) 实证映射 ---- */
static void map_carlife_turn_to_iap2(uint32_t raw, uint32_t *out_type, uint32_t *out_jtype, const char **out_desc) {
    uint32_t t = 0u;
    uint32_t jt = 0u;
    const char *desc = "直行";
    switch (raw) {
        case 1: case 13: case 14: case 15: case 16: case 17: case 22:
        case 32: case 33: case 34: case 35: case 36: case 37: case 38:
        case 40: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49:
            t = 0u; desc = "直行"; break;
        case 2: case 12: case 19: case 21: case 41: case 55: case 57: case 58: case 62:
            t = 14u; desc = "靠右"; break;
        case 3: case 56: case 63:
            t = 2u; desc = "右转"; break;
        case 4: case 59:
            t = 48u; desc = "右后方"; break;
        case 5: case 64: case 65: case 66: case 67: case 68:
            t = 4u; desc = "掉头"; break;
        case 6: case 52:
            t = 47u; desc = "左后方"; break;
        case 7: case 50: case 60:
            t = 1u; desc = "左转"; break;
        case 8: case 11: case 18: case 20: case 39: case 51: case 53: case 54: case 61:
            t = 13u; desc = "靠左"; break;
        case 9: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76:
            t = 6u; jt = 1u; desc = "进入环岛"; break;
        case 10:
            t = 7u; jt = 1u; desc = "驶出环岛"; break;
        case 24: case 25: case 26: case 27: case 28: case 31:
            t = 12u; desc = "到达"; break;
        default:
            t = 0u; desc = "直行"; break;
    }
    *out_type = t;
    *out_jtype = jt;
    *out_desc = desc;
}

static void write_state_atomic(const char *path, const char *buf, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        if (write(fd, buf, len) < 0) {}
        close(fd);
    }
}

/* 防止拔线时 smartphone_integrator 直接 SIGKILL 强杀 carlife 导致 HUD 导航残留：
 * 首次进入导航时以 QNX 原生 spawn(SPAWN_SETSID|SPAWN_NOZOMBIE) 启动轻量独立收尾脚本，
 * 仅使用 QNX 内置 kill/sleep/grep/echo，剥离 LD_PRELOAD 避免子进程递归注入。 */
static void ensure_exit_reaper(void) {
#ifdef __QNXNTO__
    if (g_reaper_spawned) return;
    g_reaper_spawned = 1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin; P=%d; "
        "while kill -0 $P 2>/dev/null; do sleep 1; done; "
        "if grep '百度CarLife' /tmp/cluster_cp.state >/dev/null 2>&1; then "
        "(echo '@cluster_cp'; echo 'session_active:b:false'; echo 'route_state:n:0'; "
        "echo 'source_name:s:百度CarLife'; echo 'source_supports_rg:n:1'; "
        "echo 'slot_count:n:0'; echo 'current_slot:n:-1'; echo 'lane_active:b:false') > /tmp/cluster_cp.state; "
        "fi",
        (int)getpid());
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    inh.flags = SPAWN_SETSID | SPAWN_NOZOMBIE;
    char *argv[4] = { (char *)"sh", (char *)"-c", cmd, NULL };
    char *clean_env[2] = { (char *)"PATH=/bin:/usr/bin:/sbin:/usr/sbin", NULL };
    pid_t rp = spawn("/bin/sh", 0, NULL, &inh, argv, clean_env);
    plog("spawned SIGKILL-proof HUD exit reaper pid=%d for carlife=%d", (int)rp, (int)getpid());
#endif
}

static void publish_carlife_nav(int active, uint32_t iap_type, uint32_t jtype,
                                const char *desc, const char *road,
                                uint32_t dist_to_man, uint32_t dist_rem, uint32_t time_rem) {
    /* 无论是否关闭 HUD，均同步导航状态给独立 /carlife_h264_shm（供 mode=nav 判定） */
    sync_nav_active_to_shm(active);
    if (file_exists(HUD_OFF_TMP)) return;
    if (active) ensure_exit_reaper();
    char buf[2048];
    int n = 0;
    if (!active) {
        n = snprintf(buf, sizeof(buf),
            "@cluster_cp\n"
            "session_active:b:false\n"
            "route_state:n:0\n"
            "source_name:s:百度CarLife\n"
            "source_supports_rg:n:1\n"
            "slot_count:n:0\n"
            "current_slot:n:-1\n"
            "lane_active:b:false\n");
    } else {
        n = snprintf(buf, sizeof(buf),
            "@cluster_cp\n"
            "session_active:b:true\n"
            "route_state:n:1\n"
            "destination:s:\n"
            "current_road:s:\n"
            "source_name:s:百度CarLife\n"
            "distance_remaining:n:%u\n"
            "time_remaining:n:%u\n"
            "eta:n:0\n"
            "distance_units:n:0\n"
            "source_supports_rg:n:1\n"
            "dist_to_maneuver:n:%u\n"
            "dist_to_maneuver_units:n:0\n"
            "lane_guidance_showing:n:0\n"
            "slot_0_iap_idx:n:0\n"
            "slot_0_type:n:%u\n"
            "slot_0_description:s:%s\n"
            "slot_0_road:s:%s\n"
            "slot_0_distance:n:%u\n"
            "slot_0_driving_side:n:0\n"
            "slot_0_junction_type:n:%u\n"
            "slot_0_exit_angle:n:1000\n"
            "slot_0_exit_info:s:\n"
            "slot_0_junction_angles:s:\n"
            "slot_count:n:1\n"
            "current_slot:n:0\n"
            "lane_active:b:false\n",
            (unsigned)dist_rem, (unsigned)time_rem, (unsigned)dist_to_man,
            (unsigned)iap_type, desc ? desc : "", road ? road : "",
            (unsigned)dist_to_man, (unsigned)jtype);
    }
    if (n > 0 && (size_t)n < sizeof(buf)) {
        write_state_atomic(STATE_PATH, buf, (size_t)n);
        g_we_own_state = active ? 1 : 0;
    }
}

/* ---- 零崩溃原生 Protobuf Wire 解析器：直接解析 0x00010030 (NaviNextTurnInfo) ---- */

static int pb_read_varint(const uint8_t *buf, uint32_t len, uint32_t *pos, uint64_t *out) {
    uint64_t val = 0;
    unsigned shift = 0;
    while (*pos < len && shift < 64u) {
        uint8_t b = buf[(*pos)++];
        val |= (uint64_t)(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) {
            *out = val;
            return 1;
        }
        shift += 7u;
    }
    return 0;
}

static void handle_raw_navi_next_turn(const uint8_t *buf, uint32_t len) {
    uint32_t pos = 0;
    uint32_t action = 0, next_turn = 0, total_dist = 0, rem_dist = 0, time_rem = 0;
    uint32_t field_cnt = 0;
    uint32_t icon_len = 0;
    char road[192];
    road[0] = '\0';

    while (pos < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(buf, len, &pos, &tag)) break;
        uint32_t fnum = (uint32_t)(tag >> 3);
        uint32_t wire = (uint32_t)(tag & 7u);
        field_cnt++;

        if (wire == 0u) {
            uint64_t v = 0;
            if (!pb_read_varint(buf, len, &pos, &v)) break;
            if (fnum == 1u) action = (uint32_t)v;
            else if (fnum == 2u) next_turn = (uint32_t)v;
            else if (fnum == 4u) total_dist = (uint32_t)v;
            else if (fnum == 5u) rem_dist = (uint32_t)v;
            else if (fnum == 6u) time_rem = (uint32_t)v;
        } else if (wire == 2u) {
            uint64_t slen = 0;
            if (!pb_read_varint(buf, len, &pos, &slen)) break;
            if (slen > (uint64_t)(len - pos)) break;
            if (fnum == 3u) {
                sanitize_str(road, sizeof(road), (const char *)(buf + pos), (size_t)slen);
            } else if (fnum == 6u) {
                icon_len = (uint32_t)slen;
            }
            pos += (uint32_t)slen;
        } else if (wire == 1u) {
            if (len - pos < 8u) break;
            pos += 8u;
        } else if (wire == 5u) {
            if (len - pos < 4u) break;
            pos += 4u;
        } else {
            break;
        }
    }

    uint32_t iap_type = 0, jtype = 0;
    const char *desc = "";
    map_carlife_turn_to_iap2(next_turn, &iap_type, &jtype, &desc);

    uint32_t dist_to_man = (rem_dist > 0u) ? rem_dist : total_dist;
    uint32_t dist_rem = (total_dist >= dist_to_man) ? total_dist : dist_to_man;

    if (g_navi_log_cnt < 80u || action == 1u || action == 3u) {
        g_navi_log_cnt++;
        plog("NaviNextTurn(raw %uB): act=%u rawTurn=%u->iap2=%u(jt=%u) road='%s' f4=%um f5=%um t=%us icon=%uB",
             len, action, next_turn, iap_type, jtype, road, total_dist, rem_dist, time_rem, icon_len);
    }

    int ended = (action == 3u && field_cnt == 1u) ||
                (action == 3u && next_turn == 0u && dist_to_man == 0u) ||
                (next_turn == 24u && dist_to_man == 0u);
    if (ended) {
        publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
        g_last_navi_ms = 0;
    } else {
        g_last_navi_ms = mono_ms();
        publish_carlife_nav(1, iap_type, jtype, desc, road, dist_to_man, dist_rem, time_rem);
    }
}

/* 断线/退导航时立即清空本库写入的 HUD 导航状态与独立 /carlife_h264_shm 活跃位 */
static void clear_owned_outputs(const char *reason) {
    if (g_we_own_state || g_nav_active) {
        publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
        g_last_navi_ms = 0;
        plog("cleared HUD/cluster nav state (%s)", reason);
    }
    if (g_we_own_shm && g_shm) {
        g_shm->nav_active = 0u;
        carlife_shm_mb();
        g_shm->flags &= ~CARLIFE_SHM_FLAG_ACTIVE;
        carlife_shm_mb();
        g_we_own_shm = 0;
        g_shm_need_idr = 1;
        plog("cleared carlife_h264_shm active flag (%s)", reason);
    }
}

/* 启动时若 /tmp/cluster_cp.state 里残留上次被 SIGKILL 的 CarLife 导航状态，主动清掉（绝不碰 CarPlay 状态） */
static void clear_stale_carlife_state_on_boot(void) {
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    if (strstr(buf, "百度CarLife") != NULL && strstr(buf, "session_active:b:true") != NULL) {
        publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
        plog("cleared stale CarLife HUD state left from previous session");
    }
}

/* 判断该控制通道消息号是否属于 2016 libcarlifevehicle.so 绝对安全可解析的白名单 */
static int is_safe_legacy_cmd(uint32_t type) {
    switch (type) {
        case 0x00010002u: /* PROTOCOL_VERSION_MATCH_STATUS */
        case 0x00010004u: /* MD_INFO */
        case 0x00010006u: /* MD_BT_PAIR_INFO */
        case 0x00010008u: /* VIDEO_ENCODER_INIT_DONE */
        case 0x0001000Du: /* VIDEO_ENCODER_FRAME_RATE_CHANGE_DONE */
        case 0x00010014u: /* TELE_INCOMING */
        case 0x00010015u: /* TELE_OUTGOING */
        case 0x00010016u: /* TELE_IDLE */
        case 0x00010017u: /* TELE_INCALLING */
        case 0x00010018u: /* SCREEN_ON */
        case 0x00010019u: /* SCREEN_OFF */
        case 0x0001001Au: /* SCREEN_USERPRESENT */
        case 0x0001001Bu: /* FOREGROUND */
        case 0x0001001Cu: /* BACKGROUND */
        case 0x00010021u: /* GO_TO_DESKTOP */
        case 0x00010022u: /* MIC_RECORD_WAKEUP_START */
        case 0x00010023u: /* MIC_RECORD_END */
        case 0x00010024u: /* MIC_RECORD_RECOG_START */
        case 0x00010026u: /* MODULE_STATUS */
        case 0x00010035u: /* MEDIA_INFO */
        case 0x00010036u: /* MEDIA_PROGRESS_BAR */
        case 0x00010037u: /* CONNECT_EXCEPTION */
        case 0x00010038u: /* REQUEST_GO_TO_FOREGROUND */
        case 0x00010039u: /* UI_ACTION_SOUND */
        case 0x00010040u: /* BT_HFP_REQUEST */
        case 0x00010044u: /* CARLIFE_DATA_SUBSCRIBE_DONE */
        case 0x00010047u: /* NAVI_ASSITANTGUIDE_INFO */
        case 0x00010049u: /* MD_AUTHEN_RESPONSE */
        case 0x0001004Bu: /* MD_AUTHEN_RESULT */
        case 0x0001004Cu: /* GO_TO_FOREGROUND_RESPONSE */
        case 0x0001004Du: /* START_BT_AUTOPAIR_REQUEST */
        case 0x0001004Fu: /* BT_HFP_STATUS_REQUEST */
        case 0x00010051u: /* MD_FEATURE_CONFIG_REQUEST */
        case 0x00010054u: /* BT_IDENTIFY_RESULT_IND */
            return 1;
        default:
            return 0;
    }
}

/* 2016 libcarlifevehicle.so 控制通道防崩护盾 */
EXPORT int _ZN15CConnectManager11readCmdDataEPhj(void *self, unsigned char *buf, unsigned int len) {
    static fn_rw_cmd_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_rw_cmd_t)dlsym(RTLD_NEXT, "_ZN15CConnectManager11readCmdDataEPhj");
    if (!real_fn) return 0;

    if (is_disabled()) {
        return real_fn(self, buf, len);
    }

    if (g_pass_body_rem > 0u) {
        int ok = real_fn(self, buf, len);
        if (!ok) {
            g_pass_body_rem = 0u;
            clear_owned_outputs("cmd body disconnect");
            return 0;
        }
        g_pass_body_rem = (len >= g_pass_body_rem) ? 0u : (g_pass_body_rem - len);
        return 1;
    }

    if (len != 8u || !buf) {
        int ok = real_fn(self, buf, len);
        if (!ok) clear_owned_outputs("cmd raw disconnect");
        return ok;
    }

    for (;;) {
        int ok = real_fn(self, buf, 8u);
        if (!ok) {
            clear_owned_outputs("cmd head disconnect");
            return 0;
        }

        uint32_t sz = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        uint32_t type = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                        ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];

        if (is_safe_legacy_cmd(type) && sz <= sizeof(g_cmd_scratch)) {
            g_pass_body_rem = sz;
            return 1;
        }

        if (sz > 0u) {
            if (sz <= sizeof(g_cmd_scratch)) {
                if (!real_fn(self, g_cmd_scratch, sz)) {
                    clear_owned_outputs("cmd shield body disconnect");
                    return 0;
                }
            } else {
                uint32_t rem = sz;
                while (rem > 0u) {
                    uint32_t chunk = (rem > sizeof(g_cmd_scratch)) ? (uint32_t)sizeof(g_cmd_scratch) : rem;
                    if (!real_fn(self, g_cmd_scratch, chunk)) {
                        clear_owned_outputs("cmd shield chunk disconnect");
                        return 0;
                    }
                    rem -= chunk;
                }
            }
        }

        if (type == MSG_CMD_NAVI_NEXT_TURN_INFO && sz > 0u && sz <= sizeof(g_cmd_scratch)) {
            handle_raw_navi_next_turn(g_cmd_scratch, sz);
        } else if (g_shield_log_cnt < 30u) {
            g_shield_log_cnt++;
            plog("CmdShield absorbed msg=0x%08x len=%u (head=%02x %02x %02x %02x)",
                 type, sz,
                 sz > 0 ? g_cmd_scratch[0] : 0, sz > 1 ? g_cmd_scratch[1] : 0,
                 sz > 2 ? g_cmd_scratch[2] : 0, sz > 3 ? g_cmd_scratch[3] : 0);
        }
    }
}

/* ---- 阶段二：独立 /carlife_h264_shm 写入（默认 mode=off 零额外开销）---- */

/* 从 Annex-B 数据包中提取并缓存 SPS(7)/PPS(8)，并检测是否含 SPS 或 IDR(5) */
static void h264_inspect_and_cache_sps_pps(const unsigned char *data, unsigned int len,
                                           int *has_sps, int *has_idr) {
    *has_sps = 0;
    *has_idr = 0;
    unsigned int scan = (len < 1024u) ? len : 1024u;
    int sps_start = -1;
    unsigned int sps_pps_end = 0;
    for (unsigned int i = 0; i + 4u < scan; i++) {
        unsigned int sc_len = 0;
        if (data[i] == 0x00u && data[i + 1] == 0x00u) {
            if (data[i + 2] == 0x01u) sc_len = 3u;
            else if (data[i + 2] == 0x00u && data[i + 3] == 0x01u) sc_len = 4u;
        }
        if (sc_len == 0u || i + sc_len >= len) continue;
        uint8_t nt = data[i + sc_len] & 0x1Fu;
        if (nt == 7u) {
            *has_sps = 1;
            if (sps_start < 0) sps_start = (int)i;
        } else if (nt == 8u) {
            /* PPS 紧跟在 SPS 之后，继续向后找下一个 NAL 起点作为结束位置 */
        } else {
            if (sps_start >= 0 && sps_pps_end == 0u) {
                sps_pps_end = i;
            }
            if (nt == 5u) *has_idr = 1;
        }
        i += sc_len;
    }
    if (sps_start >= 0) {
        if (sps_pps_end == 0u) sps_pps_end = len;
        unsigned int clen = sps_pps_end - (unsigned int)sps_start;
        if (clen >= 8u && clen <= sizeof(g_sps_pps_buf)) {
            memcpy(g_sps_pps_buf, data + sps_start, clen);
            g_sps_pps_len = clen;
        }
    }
}

static void shm_write_h264(const unsigned char *data, unsigned int len) {
    if (!data || len == 0 || len > (CARLIFE_SHM_RING_SIZE / 2u)) return;

    /* 只要尚未缓存本会话 SPS/PPS，先做一次轻量包头扫描，确保运行中热开 cluster_3d 可秒出画面 */
    if (g_sps_pps_len == 0u) {
        int tmp_sps = 0, tmp_idr = 0;
        h264_inspect_and_cache_sps_pps(data, len, &tmp_sps, &tmp_idr);
    }

    int mode = query_cluster_3d_mode(0);
    /* 默认 mode=off 时直接早退：零 SHM 映射、零拷贝 */
    if (mode == CARLIFE_C3D_OFF) {
        if (g_we_own_shm && g_shm) {
            g_shm->flags &= ~CARLIFE_SHM_FLAG_ACTIVE;
            carlife_shm_mb();
            g_we_own_shm = 0;
        }
        g_shm_need_idr = 1;
        return;
    }

    int has_sps = 0, has_idr = 0;
    h264_inspect_and_cache_sps_pps(data, len, &has_sps, &has_idr);

    /* mode=nav 且当前未导航：只缓存 SPS/PPS 并保持 ACTIVE=0，不推送视频帧，零额外负载 */
    if (mode == CARLIFE_C3D_NAV_ONLY && !g_nav_active) {
        if (g_we_own_shm && g_shm) {
            g_shm->nav_active = 0u;
            g_shm->flags &= ~CARLIFE_SHM_FLAG_ACTIVE;
            carlife_shm_mb();
            g_we_own_shm = 0;
        }
        g_shm_need_idr = 1;
        return;
    }

    if (!g_shm) {
#ifndef HOST_PROBE_TEST
        int fd = shm_open(CARLIFE_SHM_NAME, O_RDWR | O_CREAT, 0666);
        if (fd < 0) return;
        fchmod(fd, 0666);
        if (ftruncate(fd, (off_t)sizeof(carlife_h264_shm_t)) != 0) {
            close(fd);
            return;
        }
        void *m = mmap(NULL, sizeof(carlife_h264_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED) return;
        g_shm = (carlife_h264_shm_t *)m;
        if (g_shm->magic != CARLIFE_SHM_MAGIC || g_shm->version != CARLIFE_SHM_VERSION) {
            carlife_shm_init_header(g_shm, (uint32_t)getpid());
        }
        g_shm_need_idr = 1;
#else
        return;
#endif
    }

    /* 刷新元数据头：PID（抗 SIGKILL）、编码宽高、导航标志、最新触控时间戳 */
    g_shm->owner_pid       = (uint32_t)getpid();
    g_shm->src_w           = g_enc_w ? g_enc_w : g_req_w;
    g_shm->src_h           = g_enc_h ? g_enc_h : g_req_h;
    g_shm->nav_active      = (uint32_t)g_nav_active;
    g_shm->touch_active_ms = g_last_touch_ms;

    /* 热开启或刚进导航时等第一个 IDR/SPS 帧，并自动前置缓存 SPS/PPS 以便秒出画面 */
    if (g_shm_need_idr) {
        if (has_idr || has_sps) {
            if (!has_sps && g_sps_pps_len > 0u) {
                carlife_shm_ring_write(g_shm, g_sps_pps_buf, g_sps_pps_len);
            }
            carlife_shm_ring_write(g_shm, data, len);
            g_shm_need_idr = 0;
            plog("Cluster3D SHM synced on %s (mode=%d sps=%uB pkt=%uB src=%ux%u)",
                 has_idr ? "IDR" : "SPS", mode, g_sps_pps_len, len,
                 (unsigned)g_shm->src_w, (unsigned)g_shm->src_h);
        }
    } else {
        if (has_idr && !has_sps && g_sps_pps_len > 0u) {
            carlife_shm_ring_write(g_shm, g_sps_pps_buf, g_sps_pps_len);
        }
        carlife_shm_ring_write(g_shm, data, len);
    }

    carlife_shm_bump_heartbeat(g_shm, 1, mode == CARLIFE_C3D_NAV_ONLY);
    g_we_own_shm = 1;
}

/* ---- 后台会话辅助线程：帧率解锁 + HUD 订阅 + 热调监控 ---- */

static void wrapped_sub_done(cl_sub_info_list_t *list) {
    if (!list) return;
    uint32_t mask = 0;
    cl_sub_info_t *cur = list->pMobileCarLifeInfo;
    for (uint32_t i = 0; i < list->cnt && cur && i < 16u; i++) {
        plog("CarLifeDataSubscribeDone[%u]: moduleID=%u support=%u freq=%u",
             i, cur->moduleID, (unsigned)cur->supportFlag, cur->frequency);
        if (cur->supportFlag && cur->moduleID < 32u) {
            mask |= (1u << cur->moduleID);
        }
        cur = cur->pNext;
    }
    if (mask != 0) g_sub_supported_mask = mask;
    g_sub_done_received = 1;
}

static void wrapped_navi_assist(cl_navi_assist_t *p) {
    if (!p || is_disabled()) return;
    if (g_assist_log_cnt < 30u) {
        g_assist_log_cnt++;
        plog("NaviAssist: action=%u type=%u sign=%u rem=%um tot=%um speed=%u",
             p->action, p->assistantType, p->trafficSignType,
             p->remainDistance, p->totalDistance, p->cameraSpeed);
    }
}

static void wrapped_fps_done(cl_fps_change_t *p) {
    if (p) {
        plog("VideoEncoderFrameRateChangeDone from phone: %ufps", p->frameRate);
    }
    if (g_orig_fps_done_cb) g_orig_fps_done_cb(p);
}

static void register_extra_callbacks(void) {
    static fn_get_inst_t cl_get_inst = NULL;
    static fn_reg_sub_done_t reg_sub = NULL;
    static fn_reg_assist_t reg_assist = NULL;
    static fn_reg_fps_done_t reg_fps = NULL;
    if (!cl_get_inst) cl_get_inst = (fn_get_inst_t)dlsym(RTLD_DEFAULT, "_ZN11CCarLifeLib11getInstanceEv");
    if (!reg_sub) reg_sub = (fn_reg_sub_done_t)dlsym(RTLD_DEFAULT, "_ZN11CCarLifeLib35cmdRegisterCarLifeDataSubscribeDoneEPFvP30SubscribeMobileCarLifeInfoListE");
    if (!reg_assist) reg_assist = (fn_reg_assist_t)dlsym(RTLD_DEFAULT, "_ZN11CCarLifeLib33cmdRegisterNaviAssistantGuideInfoEPFvP21NaviAssitantGuideInfoE");
    if (!reg_fps) reg_fps = (fn_reg_fps_done_t)dlsym(RTLD_DEFAULT, "_ZN11CCarLifeLib42cmdRegisterVideoEncoderFrameRateChangeDoneEPFvP31VideoEncoderFrameRateChangeDoneE");
    void *inst = g_carlife_lib_self ? g_carlife_lib_self : (cl_get_inst ? cl_get_inst() : NULL);
    if (!inst) return;
    if (reg_sub) reg_sub(inst, wrapped_sub_done);
    if (reg_assist) reg_assist(inst, wrapped_navi_assist);
    if (reg_fps && !g_orig_fps_done_cb) reg_fps(inst, wrapped_fps_done);
}

static void *session_worker_thread(void *arg) {
    int my_gen = (int)(intptr_t)arg;
    plog("session_worker started (gen=%d)", my_gen);

    /* 1. 编码器稳定后下发真帧率指令 98316 */
    usleep(500000);
    if (g_session_gen != my_gen || is_disabled()) return NULL;
    if (g_req_fps >= 3u) {
        send_fps_change(g_req_fps);
    }

    /* 2. 发起 HUD 导航数据订阅：0x18043 -> 0x10044 -> 0x18045 */
    if (!file_exists(HUD_OFF_TMP)) {
        usleep(300000);
        if (g_session_gen != my_gen || is_disabled()) return NULL;
        g_sub_done_received = 0;
        g_sub_supported_mask = 0x1u;
        send_hud_subscribe_cmd(MSG_CMD_CARLIFE_DATA_SUBSCRIBE, 0x3u);

        for (int i = 0; i < 20; i++) {
            usleep(100000);
            if (g_session_gen != my_gen || is_disabled()) return NULL;
            if (g_sub_done_received) break;
        }
        plog("HUD subscribe reply received=%d mask=0x%x -> sending START",
             g_sub_done_received, g_sub_supported_mask);
        send_hud_subscribe_cmd(MSG_CMD_CARLIFE_DATA_SUBSCRIBE_START, g_sub_supported_mask);
    }

    /* 3. 若首轮仍被降档则补发一次无痛二次初始化 */
    usleep(1200000);
    if (g_session_gen == my_gen && !is_disabled() && !g_reinit_done && !file_exists(NO_REINIT_TMP)) {
        if (g_req_w >= 640 && g_req_h >= 360 && (g_enc_w != g_req_w || g_enc_h != g_req_h)) {
            g_reinit_done = 1;
            plog("settleReinit: enc=%ux%u != req=%ux%u -> re-sending FeatureConfig + VideoEncoderInit",
                 g_enc_w, g_enc_h, g_req_w, g_req_h);
            send_passthrough_feature_config();
            usleep(350000);
            if (g_session_gen == my_gen && g_real_enc_init && g_carlife_lib_self) {
                cl_video_enc_t mod;
                mod.width = g_req_w;
                mod.height = g_req_h;
                mod.frameRate = g_req_fps;
                pthread_mutex_lock(&g_cmd_lock);
                g_real_enc_init(g_carlife_lib_self, &mod);
                pthread_mutex_unlock(&g_cmd_lock);
                usleep(400000);
                send_fps_change(g_req_fps);
            }
        }
    }

    /* 4. 常驻轮询 /tmp/carlife_fps_req 热切帧率 + 导航心跳超时自动清理 */
    time_t last_fps_mtime = 0;
    while (g_session_gen == my_gen) {
        sleep(1);
        if (is_disabled()) continue;
        if ((g_we_own_state || g_nav_active) && g_last_navi_ms > 0 && mono_ms() > g_last_navi_ms + 10000ULL) {
            publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
            g_last_navi_ms = 0;
            plog("HUD nav watchdog timeout (>10s no update) -> cleared nav state");
        }
        struct stat fst;
        if (stat(FPS_REQ_TMP, &fst) == 0 && fst.st_mtime != last_fps_mtime) {
            last_fps_mtime = fst.st_mtime;
            FILE *f = fopen(FPS_REQ_TMP, "r");
            if (f) {
                unsigned fps = 0;
                int n = fscanf(f, "%u", &fps);
                fclose(f);
                unlink(FPS_REQ_TMP);
                if (n == 1 && fps >= 3 && fps <= 60) {
                    g_req_fps = fps;
                    send_fps_change(fps);
                }
            }
        }
    }
    return NULL;
}

static void launch_session_worker(void) {
    pthread_mutex_lock(&g_lock);
    int gen = ++g_session_gen;
    g_worker_started = 1;
    pthread_mutex_unlock(&g_lock);

    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, session_worker_thread, (void *)(intptr_t)gen) != 0) {
        plog("failed to spawn session_worker");
    }
    pthread_attr_destroy(&attr);
}

/* ---- 包装回调函数 ---- */

static void wrapped_enc_done(cl_video_enc_t *p) {
    if (p) {
        g_enc_w = p->width;
        g_enc_h = p->height;
        g_enc_fps = p->frameRate;
        g_sps_pps_len = 0u;
        if (g_shm) {
            g_shm->src_w = p->width;
            g_shm->src_h = p->height;
        }
        plog("VideoEncoderInitDone from phone: %ux%u@%ufps (HMI win=%ux%u)",
             p->width, p->height, p->frameRate, g_win_w, g_win_h);
    }
    if (g_orig_enc_done_cb) g_orig_enc_done_cb(p);
    if (!is_disabled() && !g_worker_started) {
        register_extra_callbacks();
        launch_session_worker();
    }
}

static void wrapped_navi_turn(cl_navi_turn_t *p) {
    if (p && !is_disabled()) {
        char road[192];
        read_dinkum_string(&p->roadName, road, sizeof(road));
        uint32_t iap_type = 0, jtype = 0;
        const char *desc = "";
        map_carlife_turn_to_iap2(p->nextTurn, &iap_type, &jtype, &desc);
        uint32_t dist_to_man = (p->remainDistance > 0u) ? p->remainDistance : p->totalDistance;
        uint32_t dist_rem = (p->totalDistance >= dist_to_man) ? p->totalDistance : dist_to_man;
        plog("NaviNextTurn(cb): act=%u rawTurn=%u road='%s' rem=%um tot=%um",
             p->action, p->nextTurn, road, p->remainDistance, p->totalDistance);
        int ended = (p->action == 3u && p->nextTurn == 0u && dist_to_man == 0u) ||
                    (p->nextTurn == 24u && dist_to_man == 0u);
        if (ended) {
            publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
            g_last_navi_ms = 0;
        } else {
            g_last_navi_ms = mono_ms();
            publish_carlife_nav(1, iap_type, jtype, desc, road, dist_to_man, dist_rem, p->time);
        }
    }
    if (g_orig_navi_cb) g_orig_navi_cb(p);
}

static void wrapped_mod_status(cl_mod_status_list_t *list) {
    if (list && !is_disabled()) {
        cl_mod_status_t *cur = list->moduleStatus;
        for (uint32_t i = 0; i < list->cnt && cur && i < 16u; i++) {
            plog("ModuleStatus: moduleID=%u statusID=%u", cur->moduleID, cur->statusID);
            if (cur->moduleID == 2u && cur->statusID == 0u) {
                if (g_we_own_state || g_nav_active) {
                    publish_carlife_nav(0, 0, 0, "", "", 0, 0, 0);
                    g_last_navi_ms = 0;
                    plog("ModuleStatus: NAVI=0 -> cleared HUD/cluster nav state");
                }
            } else if (cur->moduleID == 2u && cur->statusID == 1u) {
                /* 无论是否掩码原厂弹窗，均记录真实导航激活状态供仪表 3D 地图门控使用 */
                sync_nav_active_to_shm(1);
                if (!file_exists(ALLOW_NAVI_POPUP_TMP)) {
                    cur->statusID = 0u;
                    plog("ModuleStatus: masked NAVI=1 -> 0 to suppress OEM cluster popup");
                }
            } else if (cur->moduleID == 3u && cur->statusID == 1u && is_bt_audio_locked()) {
                /* 支线 C2：阻止手机端 MUSIC=RUNNING 触发 acquireAudioResource 抢占蓝牙音源 */
                cur->statusID = 0u;
                plog("ModuleStatus: masked MUSIC=1 -> 0 (bt_audio_lock active)");
            }
            cur = cur->next;
        }
    }
    if (g_orig_mod_status_cb) g_orig_mod_status_cb(list);
}

static void wrapped_media_init(cl_audio_init_t *p) {
    if (!is_disabled() && is_bt_audio_locked()) {
        plog("bt_audio_lock: blocked mediaInit (sr=%u ch=%u bits=%u) to protect VW BT",
             p ? (unsigned)p->sampleRate : 0u,
             p ? (unsigned)p->channelConfig : 0u,
             p ? (unsigned)p->sampleFormat : 0u);
        return;
    }
    if (g_orig_media_init_cb) g_orig_media_init_cb(p);
}

static void wrapped_media_resume(void) {
    if (!is_disabled() && is_bt_audio_locked()) {
        plog("bt_audio_lock: blocked mediaResume to protect VW BT");
        return;
    }
    if (g_orig_media_resume_cb) g_orig_media_resume_cb();
}

static void wrapped_media_info(cl_media_info_t *p) {
    if (p && !is_disabled()) {
        char src[128], song[128], artist[128], album[128];
        sanitize_str(src, sizeof(src), p->source, sizeof(p->source));
        sanitize_str(song, sizeof(song), p->song, sizeof(p->song));
        sanitize_str(artist, sizeof(artist), p->artist, sizeof(p->artist));
        sanitize_str(album, sizeof(album), p->album, sizeof(p->album));
        char buf[768];
        int n = snprintf(buf, sizeof(buf),
                         "source=%s\nsong=%s\nartist=%s\nalbum=%s\nduration=%u\nmode=%u\n",
                         src, song, artist, album, (unsigned)p->duration, (unsigned)p->mode);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            write_state_atomic(MEDIA_STATE_PATH, buf, (size_t)n);
        }
        plog("MediaInfo: song='%s' artist='%s' src='%s' dur=%u", song, artist, src, (unsigned)p->duration);
    }
    if (g_orig_media_cb) g_orig_media_cb(p);
}

static int h264_has_vcl_frame(const unsigned char *data, unsigned int len) {
    unsigned int scan = (len < 256u) ? len : 256u;
    for (unsigned int i = 0; i + 4u < scan; i++) {
        if (data[i] == 0x00u && data[i + 1] == 0x00u) {
            if (data[i + 2] == 0x01u) {
                uint8_t nt = data[i + 3] & 0x1Fu;
                if (nt == 1u || nt == 5u) return 1;
            } else if (data[i + 2] == 0x00u && data[i + 3] == 0x01u && i + 4u < scan) {
                uint8_t nt = data[i + 4] & 0x1Fu;
                if (nt == 1u || nt == 5u) return 1;
            }
        }
    }
    return (len > 256u) ? 1 : 0;
}

static void wrapped_video_data(unsigned char *data, unsigned int len) {
    if (data && len > 0 && !is_disabled()) {
        if (g_video_pkt_cnt == 0u) {
            plog("first H.264 video packet received: len=%u head=%02x %02x %02x %02x %02x",
                 len, data[0], len > 1 ? data[1] : 0, len > 2 ? data[2] : 0,
                 len > 3 ? data[3] : 0, len > 4 ? data[4] : 0);
            g_stat_last_ms = mono_ms();
        }
        g_video_pkt_cnt++;
        g_stat_pkts++;
        g_stat_bytes += len;
        if (h264_has_vcl_frame(data, len)) g_stat_frames++;

        uint64_t now = mono_ms();
        if (g_stat_last_ms > 0 && now >= g_stat_last_ms + 2000ULL) {
            uint64_t dt = now - g_stat_last_ms;
            uint32_t pkts = g_stat_pkts;
            uint32_t frms = g_stat_frames;
            uint32_t bytes = g_stat_bytes;
            uint32_t moves = g_stat_moves;
            g_stat_pkts = 0;
            g_stat_frames = 0;
            g_stat_bytes = 0;
            g_stat_moves = 0;
            g_stat_last_ms = now;
            uint32_t fps10 = (uint32_t)((uint64_t)frms * 10000ULL / dt);
            uint32_t pkt10 = (uint32_t)((uint64_t)pkts * 10000ULL / dt);
            uint32_t kbps = (uint32_t)((uint64_t)bytes * 8ULL / dt);
            uint32_t mv10 = (uint32_t)((uint64_t)moves * 10000ULL / dt);
            plog("VideoStats: rx_fps=%u.%u pkt_hz=%u.%u kbps=%u enc=%ux%u@%u move_hz=%u.%u",
                 fps10 / 10u, fps10 % 10u, pkt10 / 10u, pkt10 % 10u,
                 kbps, g_enc_w, g_enc_h, g_enc_fps, mv10 / 10u, mv10 % 10u);
        }

        shm_write_h264(data, len);
    }
    if (g_orig_video_cb) g_orig_video_cb(data, len);
}

/* ---- 触控坐标（默认直通，同时向 /carlife_h264_shm 写入触控活跃时间戳）---- */

static void scale_touch_xy(const char *tag, uint32_t *px, uint32_t *py) {
    if (tag[0] == 'M') g_stat_moves++;
    if (!is_disabled()) {
        uint64_t now = mono_ms();
        g_last_touch_ms = now;
        if (g_shm) {
            g_shm->touch_active_ms = now;
        }
    }
    if (is_disabled() || !file_exists(FORCE_TOUCH_SCALE_TMP)) return;
    uint32_t ew = g_enc_w, eh = g_enc_h;
    uint32_t ww = g_win_w, wh = g_win_h;
    if (ew == 0 || eh == 0 || ww == 0 || wh == 0) return;
    if (ew == ww && eh == wh) return;
    uint32_t rx = *px, ry = *py;
    uint32_t sx = (uint32_t)(((uint64_t)rx * (uint64_t)ew + (uint64_t)(ww / 2u)) / (uint64_t)ww);
    uint32_t sy = (uint32_t)(((uint64_t)ry * (uint64_t)eh + (uint64_t)(wh / 2u)) / (uint64_t)wh);
    if (sx >= ew) sx = ew - 1u;
    if (sy >= eh) sy = eh - 1u;
    *px = sx;
    *py = sy;
    if (tag[0] != 'M' && g_touch_log_cnt < 20u) {
        g_touch_log_cnt++;
        plog("Touch %s: raw=(%u,%u) -> scaled=(%u,%u)", tag, rx, ry, sx, sy);
    }
}

/* ---- PLT Hook 导出函数（共 15 个，含支线 C2 新增 2 个媒体音频注册 Hook）---- */

EXPORT int _ZN11CCarLifeLib19cmdVideoEncoderInitEP16VideoEncoderInit(void *self, cl_video_enc_t *p) {
    if (!g_real_enc_init) {
        g_real_enc_init = (fn_enc_init_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib19cmdVideoEncoderInitEP16VideoEncoderInit");
    }
    if (!g_real_enc_init) return -1;
    g_carlife_lib_self = self;

    if (p && !is_disabled()) {
        load_res_conf();
        if (!g_feat_sent_passthrough && !file_exists(NO_RES_PASSTHROUGH_TMP)) {
            send_passthrough_feature_config();
            usleep(200000);
        }
        cl_video_enc_t mod = *p;
        if (g_req_w >= 640 && g_req_h >= 360) {
            mod.width = g_req_w;
            mod.height = g_req_h;
            mod.frameRate = g_req_fps;
            plog("cmdVideoEncoderInit override: stock=%ux%u@%u -> req=%ux%u@%u",
                 p->width, p->height, p->frameRate, mod.width, mod.height, mod.frameRate);
            pthread_mutex_lock(&g_cmd_lock);
            int r = g_real_enc_init(self, &mod);
            pthread_mutex_unlock(&g_cmd_lock);
            return r;
        }
        plog("cmdVideoEncoderInit passthrough (res.conf=0): %ux%u@%u", p->width, p->height, p->frameRate);
    }
    return g_real_enc_init(self, p);
}

typedef int (*fn_feat_resp_t)(void *, cl_feature_config_list_t *);
EXPORT int _ZN11CCarLifeLib24cmdFeatureConfigResponseEP17FeatureConfigList(void *self, cl_feature_config_list_t *list) {
    static fn_feat_resp_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_feat_resp_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib24cmdFeatureConfigResponseEP17FeatureConfigList");
    if (!real_fn) return -1;
    g_carlife_lib_self = self;

    if (!list || is_disabled() || file_exists(NO_RES_PASSTHROUGH_TMP)) {
        return real_fn(self, list);
    }

    uint8_t pb[480];
    size_t body_off = 2u;
    uint32_t total_cnt = 0;
    int has_disable_res = 0;
    int has_iframe = 0;

    cl_feature_config_t *cur = list->pFeatureConfig;
    for (uint32_t i = 0; i < list->cnt && cur && i < 24u; i++) {
        char kbuf[64];
        read_dinkum_string(&cur->key, kbuf, sizeof(kbuf));
        if (kbuf[0] != '\0') {
            plog("FeatureConfig(OEM): %s=%u", kbuf, cur->value);
            if (strcmp(kbuf, "DISABLE_RESOLUTION_OPTION") == 0) has_disable_res = 1;
            if (strcmp(kbuf, "I_FRAME_INTERVAL") == 0) has_iframe = 1;
            size_t added = pb_append_feature_kv(pb + body_off, sizeof(pb) - body_off, kbuf, cur->value);
            if (added > 0) { body_off += added; total_cnt++; }
        }
        cur = cur->pNext;
    }
    if (!has_disable_res) {
        size_t added = pb_append_feature_kv(pb + body_off, sizeof(pb) - body_off, "DISABLE_RESOLUTION_OPTION", 1u);
        if (added > 0) { body_off += added; total_cnt++; }
    }
    if (!has_iframe) {
        size_t added = pb_append_feature_kv(pb + body_off, sizeof(pb) - body_off, "I_FRAME_INTERVAL", 30u);
        if (added > 0) { body_off += added; total_cnt++; }
    }
    pb[0] = 0x08u;
    pb[1] = (uint8_t)total_cnt;
    g_feat_sent_passthrough = 1;
    int r = send_raw_cmd(MSG_CMD_HU_FEATURE_CONFIG_RESPONSE, pb, (uint32_t)body_off);
    plog("cmdFeatureConfigResponse augmented: total=%u (DISABLE_RESOLUTION_OPTION=1, I_FRAME_INTERVAL=30) ret=%d", total_cnt, r);
    return r;
}

typedef void (*fn_reg_enc_done_t)(void *, void (*)(cl_video_enc_t *));
EXPORT void _ZN11CCarLifeLib31cmdRegisterVideoEncoderInitDoneEPFvP20VideoEncoderInitDoneE(
    void *self, void (*cb)(cl_video_enc_t *)) {
    static fn_reg_enc_done_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_enc_done_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib31cmdRegisterVideoEncoderInitDoneEPFvP20VideoEncoderInitDoneE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_enc_done_cb = cb;
    real_fn(self, cb ? wrapped_enc_done : NULL);
}

EXPORT void _ZN11CCarLifeLib42cmdRegisterVideoEncoderFrameRateChangeDoneEPFvP31VideoEncoderFrameRateChangeDoneE(
    void *self, void (*cb)(cl_fps_change_t *)) {
    static fn_reg_fps_done_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_fps_done_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib42cmdRegisterVideoEncoderFrameRateChangeDoneEPFvP31VideoEncoderFrameRateChangeDoneE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_fps_done_cb = cb;
    real_fn(self, wrapped_fps_done);
}

typedef void (*fn_reg_mod_status_t)(void *, void (*)(cl_mod_status_list_t *));
EXPORT void _ZN11CCarLifeLib23cmdRegisterModuleStatusEPFvP22ModuleStatusListMobileE(
    void *self, void (*cb)(cl_mod_status_list_t *)) {
    static fn_reg_mod_status_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_mod_status_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib23cmdRegisterModuleStatusEPFvP22ModuleStatusListMobileE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_mod_status_cb = cb;
    real_fn(self, wrapped_mod_status);
}

typedef void (*fn_reg_navi_t)(void *, void (*)(cl_navi_turn_t *));
EXPORT void _ZN11CCarLifeLib27cmdRegisterNaviNextTurnInfoEPFvP16NaviNextTurnInfoE(
    void *self, void (*cb)(cl_navi_turn_t *)) {
    static fn_reg_navi_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_navi_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib27cmdRegisterNaviNextTurnInfoEPFvP16NaviNextTurnInfoE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_navi_cb = cb;
    plog("hooked cmdRegisterNaviNextTurnInfo (orig_cb=%p)", (void *)cb);
    real_fn(self, wrapped_navi_turn);
}

typedef void (*fn_reg_media_t)(void *, void (*)(cl_media_info_t *));
EXPORT void _ZN11CCarLifeLib20cmdRegisterMediaInfoEPFvP9MediaInfoE(
    void *self, void (*cb)(cl_media_info_t *)) {
    static fn_reg_media_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_media_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib20cmdRegisterMediaInfoEPFvP9MediaInfoE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_media_cb = cb;
    real_fn(self, wrapped_media_info);
}

typedef void (*fn_reg_media_init_t)(void *, void (*)(cl_audio_init_t *));
EXPORT void _ZN11CCarLifeLib17mediaRegisterInitEPFvP18AudioInitParameterE(
    void *self, void (*cb)(cl_audio_init_t *)) {
    static fn_reg_media_init_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_media_init_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib17mediaRegisterInitEPFvP18AudioInitParameterE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_media_init_cb = cb;
    real_fn(self, cb ? wrapped_media_init : NULL);
}

typedef void (*fn_reg_media_resume_t)(void *, void (*)(void));
EXPORT void _ZN11CCarLifeLib19mediaRegisterResumeEPFvvE(
    void *self, void (*cb)(void)) {
    static fn_reg_media_resume_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_media_resume_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib19mediaRegisterResumeEPFvvE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_media_resume_cb = cb;
    real_fn(self, cb ? wrapped_media_resume : NULL);
}

typedef void (*fn_reg_video_t)(void *, void (*)(unsigned char *, unsigned int));
EXPORT void _ZN11CCarLifeLib24videoRegisterDataReceiveEPFvPhjE(
    void *self, void (*cb)(unsigned char *, unsigned int)) {
    static fn_reg_video_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_reg_video_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib24videoRegisterDataReceiveEPFvPhjE");
    if (!real_fn) return;
    g_carlife_lib_self = self;
    g_orig_video_cb = cb;
    real_fn(self, cb ? wrapped_video_data : NULL);
}

typedef int (*fn_touch_pt_t)(void *, cl_touch_pt_t *);
EXPORT int _ZN11CCarLifeLib19ctrlTouchActionDownEP15TouchActionDown(void *self, cl_touch_pt_t *p) {
    static fn_touch_pt_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_touch_pt_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib19ctrlTouchActionDownEP15TouchActionDown");
    if (!real_fn) return -1;
    if (!p) return real_fn(self, p);
    cl_touch_pt_t mod = *p;
    scale_touch_xy("DOWN", &mod.x, &mod.y);
    return real_fn(self, &mod);
}

EXPORT int _ZN11CCarLifeLib19ctrlTouchActionMoveEP15TouchActionMove(void *self, cl_touch_pt_t *p) {
    static fn_touch_pt_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_touch_pt_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib19ctrlTouchActionMoveEP15TouchActionMove");
    if (!real_fn) return -1;
    if (!p) return real_fn(self, p);
    cl_touch_pt_t mod = *p;
    scale_touch_xy("MOVE", &mod.x, &mod.y);
    return real_fn(self, &mod);
}

EXPORT int _ZN11CCarLifeLib17ctrlTouchActionUpEP13TouchActionUp(void *self, cl_touch_pt_t *p) {
    static fn_touch_pt_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_touch_pt_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib17ctrlTouchActionUpEP13TouchActionUp");
    if (!real_fn) return -1;
    if (!p) return real_fn(self, p);
    cl_touch_pt_t mod = *p;
    scale_touch_xy("UP", &mod.x, &mod.y);
    return real_fn(self, &mod);
}

typedef int (*fn_touch_act_t)(void *, cl_touch_action_t *);
EXPORT int _ZN11CCarLifeLib15ctrlTouchActionEP11TouchAction(void *self, cl_touch_action_t *p) {
    static fn_touch_act_t real_fn = NULL;
    if (!real_fn) real_fn = (fn_touch_act_t)dlsym(RTLD_NEXT, "_ZN11CCarLifeLib15ctrlTouchActionEP11TouchAction");
    if (!real_fn) return -1;
    if (!p) return real_fn(self, p);
    cl_touch_action_t mod = *p;
    scale_touch_xy("ACT", &mod.x, &mod.y);
    return real_fn(self, &mod);
}

/* ---- 生命周期初始化与清理 ---- */

#ifndef HOST_PROBE_TEST
/* 检查当前进程是否为 /carlife/carlife（防止 LD_PRELOAD 意外在子进程中执行） */
static int is_carlife_process(void) {
    char exe[256];
    int fd = open("/proc/self/exefile", O_RDONLY);
    if (fd < 0) return 1;
    ssize_t n = read(fd, exe, sizeof(exe) - 1u);
    close(fd);
    if (n <= 0) return 1;
    exe[n] = '\0';
    const char *base = strrchr(exe, '/');
    base = base ? (base + 1) : exe;
    return strcmp(base, "carlife") == 0;
}

__attribute__((constructor))
static void carlife_probe_init(void) {
    if (!is_carlife_process()) return;
    pthread_mutex_lock(&g_lock);
    if (!g_inited) {
        g_inited = 1;
        load_skin_win_size();
        load_res_conf();
        clear_stale_carlife_state_on_boot();
        int c3d = query_cluster_3d_mode(1);
        plog("carlife_probe v3.0 loaded (pid=%d disabled=%d win=%ux%u req=%ux%u@%u c3d=%d bt_lock=%d shield=1)",
             (int)getpid(), is_disabled(), g_win_w, g_win_h, g_req_w, g_req_h, g_req_fps,
             c3d, is_bt_audio_locked());
    }
    pthread_mutex_unlock(&g_lock);
}

__attribute__((destructor))
static void carlife_probe_fini(void) {
    if (!g_inited) return;
    g_session_gen++;
    clear_owned_outputs("process exit");
}
#endif
