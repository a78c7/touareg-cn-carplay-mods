/*
 * carlife_shm.h — 大众途锐 CR7 (MH2p / QNX 6.6 ARMv7) 百度 CarLife 阶段二
 *                 12 寸仪表 3D 地图独立共享内存协议与公共配置定义
 *
 * 设计要点：
 *   1. 专属共享内存 /carlife_h264_shm（Magic = 0x434C3344 "CL3D"），与 CarPlay 的
 *      /cluster_h264_shm (0x48323634) 完全物理隔离，绝不相互干扰或误触发。
 *   2. 头部全部采用定宽无符号整数与显式填充，确保 x86_64/arm64 主机单测与
 *      QNX 6.6 ARMv7 32-bit 车机端结构体尺寸与字段偏移 100% 一致。
 *   3. 除 2MB Annex-B H.264 环形缓冲外，携带 owner_pid（抗 SIGKILL 瞬时检测）、
 *      src_w/src_h（源编码分辨率）、touch_active_ms（中控触控拖动避让时间戳）、
 *      nav_active（导航状态）及 last_pkt_bytes（静止小 P 帧启发式降频）。
 *
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.
 */
#ifndef CARLIFE_SHM_H
#define CARLIFE_SHM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- 共享内存名称、魔数与环缓容量 ---- */
#define CARLIFE_SHM_NAME        "/carlife_h264_shm"
#define CARLIFE_SHM_MAGIC       0x434C3344u /* "CL3D" */
#define CARLIFE_SHM_VERSION     1u
#define CARLIFE_SHM_RING_SIZE   (2u * 1024u * 1024u) /* 2 MB 环形缓冲 */

/* CarPlay 独立 SHM（仅用于只读互锁：CarPlay 活跃时 CarLife 无条件让位） */
#define CARPLAY_SHM_NAME        "/cluster_h264_shm"
#define CARPLAY_SHM_MAGIC       0x48323634u /* "H264" */

/* flags 位定义 */
#define CARLIFE_SHM_FLAG_ACTIVE     (1u << 0) /* CarLife H.264 投屏流活跃 */
#define CARLIFE_SHM_FLAG_NAV_ONLY   (1u << 1) /* 写入端指示当前配置为仅导航模式 */
#define CARLIFE_SHM_FLAG_HB_SHIFT   8u        /* bits 8..31: 单调递增心跳计数 */
#define CARLIFE_SHM_FLAG_HB_MASK    0x00FFFFFFu

/* ---- 车机运行路径与开关约定（严格遵守 QNX /tmp 单层无子目录红线）---- */
#define CARLIFE_MOD_DIR             "/mnt/app/eso/bin/apps/carlife_mod"
#define CARLIFE_CLUSTER_CONF_PERM   CARLIFE_MOD_DIR "/cluster_3d.conf"
#define CARLIFE_CLUSTER_CONF_TMP    "/tmp/carlife_cluster_3d.conf"
#define CARLIFE_CLUSTER_ON_PERM     CARLIFE_MOD_DIR "/cluster_3d_on"
#define CARLIFE_CLUSTER_ON_TMP      "/tmp/carlife_cluster_3d_on"
#define CARLIFE_CLUSTER_OFF_TMP     "/tmp/carlife_cluster_3d_off"
#define CARLIFE_CLUSTER_FUSED_TMP   "/tmp/carlife_cluster_fused"
#define CARLIFE_CLUSTER_READY_TMP   "/tmp/carlife_cluster_ready"
#define CARLIFE_CLUSTER_LOG_TMP     "/tmp/carlife_clusterd.log"
#define CARLIFE_ACTIVE_FLAG_TMP     "/tmp/carlife_active"
#define CARLIFE_BT_LOCK_PERM        CARLIFE_MOD_DIR "/bt_audio_lock"
#define CARLIFE_BT_LOCK_TMP         "/tmp/carlife_bt_audio_lock"
#define VWCN_REBIND_PATH            "/tmp/vwcn_rebind"
#define VWCN_USER_NATIVE_PATH       "/tmp/vwcn_user_native"

/* 12 寸全液晶仪表 Window 58 固定输出分辨率 */
#define CARLIFE_CLUSTER_OUT_W       1920
#define CARLIFE_CLUSTER_OUT_H       639

/* 仪表同屏模式枚举 */
typedef enum {
    CARLIFE_C3D_OFF      = 0, /* 默认关闭：零解码/零窗口，仪表 100% 保持原厂地图 */
    CARLIFE_C3D_NAV_ONLY = 1, /* 仅百度导航激活时上仪表，退导航/拔线秒级回原厂地图 */
    CARLIFE_C3D_ALWAYS   = 2  /* 连上 CarLife 即上仪表，拔线秒级回原厂地图 */
} carlife_c3d_mode_t;

/*
 * 共享内存头部固定为 64 字节（8 字节自然对齐，无跨架构 padding 歧义）：
 *   +0x00 magic          (4B)  0x434C3344
 *   +0x04 version        (4B)  1
 *   +0x08 flags          (4B)  bit0=ACTIVE, bit1=NAV_ONLY, bits8..31=heartbeat
 *   +0x0C owner_pid      (4B)  /carlife/carlife 进程 PID（用于抗 SIGKILL 存活探测）
 *   +0x10 src_w          (4B)  手机端实际编码宽（如 1664）
 *   +0x14 src_h          (4B)  手机端实际编码高（如 736）
 *   +0x18 nav_active     (4B)  1=百度导航进行中, 0=未导航/已退出
 *   +0x1C reserved0      (4B)  对齐填充，保留为 0
 *   +0x20 touch_active_ms(8B)  最近一次中控触控事件单调时间戳 (ms, CLOCK_MONOTONIC)
 *   +0x28 write_pos      (4B)  生产者写入偏移 [0, CARLIFE_SHM_RING_SIZE)
 *   +0x2C read_pos       (4B)  消费者读取偏移 [0, CARLIFE_SHM_RING_SIZE)
 *   +0x30 total_bytes    (4B)  累计写入字节数（无符号单调递增，允许自然回绕）
 *   +0x34 last_pkt_bytes (4B)  最近一包视频载荷长度（供静止小 P 帧启发式降频）
 *   +0x38 reserved1      (4B)  保留（0）
 *   +0x3C reserved2      (4B)  保留（0）
 *   +0x40 ring[2MB]            Annex-B H.264 环形缓冲区
 */
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t flags;
    volatile uint32_t owner_pid;
    volatile uint32_t src_w;
    volatile uint32_t src_h;
    volatile uint32_t nav_active;
    volatile uint32_t reserved0;
    volatile uint64_t touch_active_ms;
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    volatile uint32_t total_bytes;
    volatile uint32_t last_pkt_bytes;
    volatile uint32_t reserved1;
    volatile uint32_t reserved2;
    uint8_t ring[CARLIFE_SHM_RING_SIZE];
} carlife_h264_shm_t;

/* CarPlay 最小只读头部视图（仅用于检测 CarPlay 仪表流是否活跃） */
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t write_pos;
    volatile uint32_t total_bytes;
    volatile uint32_t flags;
} carplay_shm_head_t;

/* C99 兼容的编译期布局断言：确保跨平台 ABI 完全一致 */
typedef char carlife_shm_check_touch_off[(offsetof(carlife_h264_shm_t, touch_active_ms) == 0x20u) ? 1 : -1];
typedef char carlife_shm_check_ring_off[(offsetof(carlife_h264_shm_t, ring) == 0x40u) ? 1 : -1];

/* ---- 内存屏障与单生产/单消费环缓辅助函数 ---- */

static inline void carlife_shm_mb(void) {
    __sync_synchronize();
}

/* 初始化共享内存头（生产者首次创建或 magic 不匹配时调用） */
static inline void carlife_shm_init_header(carlife_h264_shm_t *shm, uint32_t owner_pid) {
    if (!shm) return;
    shm->version         = CARLIFE_SHM_VERSION;
    shm->flags           = 0u;
    shm->owner_pid       = owner_pid;
    shm->src_w           = 0u;
    shm->src_h           = 0u;
    shm->nav_active      = 0u;
    shm->reserved0       = 0u;
    shm->touch_active_ms = 0ULL;
    shm->write_pos       = 0u;
    shm->read_pos        = 0u;
    shm->total_bytes     = 0u;
    shm->last_pkt_bytes  = 0u;
    shm->reserved1       = 0u;
    shm->reserved2       = 0u;
    carlife_shm_mb();
    shm->magic           = CARLIFE_SHM_MAGIC;
}

/* 生产者向环缓追加字节（调用方保证 len <= CARLIFE_SHM_RING_SIZE / 2） */
static inline int carlife_shm_ring_write(carlife_h264_shm_t *shm,
                                         const uint8_t *data,
                                         uint32_t len) {
    if (!shm || !data || len == 0u || len > (CARLIFE_SHM_RING_SIZE / 2u)) {
        return -1;
    }
    uint32_t wp = shm->write_pos % CARLIFE_SHM_RING_SIZE;
    uint32_t first = CARLIFE_SHM_RING_SIZE - wp;
    if (len <= first) {
        memcpy(shm->ring + wp, data, (size_t)len);
    } else {
        memcpy(shm->ring + wp, data, (size_t)first);
        memcpy(shm->ring, data + first, (size_t)(len - first));
    }
    carlife_shm_mb();
    shm->write_pos      = (wp + len) % CARLIFE_SHM_RING_SIZE;
    shm->last_pkt_bytes = len;
    carlife_shm_mb();
    shm->total_bytes   += len;
    return 0;
}

/* 生产者推进心跳计数并设置活跃/模式位 */
static inline void carlife_shm_bump_heartbeat(carlife_h264_shm_t *shm,
                                              int active,
                                              int nav_only) {
    if (!shm) return;
    uint32_t f  = shm->flags;
    uint32_t hb = ((f >> CARLIFE_SHM_FLAG_HB_SHIFT) + 1u) & CARLIFE_SHM_FLAG_HB_MASK;
    uint32_t low = (active ? CARLIFE_SHM_FLAG_ACTIVE : 0u) |
                   (nav_only ? CARLIFE_SHM_FLAG_NAV_ONLY : 0u);
    shm->flags = (hb << CARLIFE_SHM_FLAG_HB_SHIFT) | low;
}

/*
 * 消费者读取增量字节到调用方提供的连续缓冲 out（容量 out_cap）：
 *   writable != 0 时同步回写 shm->read_pos（PROT_READ 只读映射时传 0 防段错误）。
 * 返回：
 *   > 0 : 实际读出的字节数（同步推进 *io_read_pos 与 *io_last_total）
 *     0 : 暂无新数据
 *    -1 : 发生环缓覆盖溢出（已自动快进到最新 write_pos，调用方应等待下一个 SPS/IDR 关键帧）
 */
static inline int carlife_shm_ring_read(carlife_h264_shm_t *shm,
                                        uint32_t *io_read_pos,
                                        uint32_t *io_last_total,
                                        uint8_t *out,
                                        uint32_t out_cap,
                                        int writable) {
    if (!shm || !io_read_pos || !io_last_total || !out || out_cap == 0u) {
        return 0;
    }
    uint32_t total = shm->total_bytes;
    if (total == *io_last_total) {
        return 0;
    }
    carlife_shm_mb();

    uint32_t delta = total - *io_last_total;
    if (delta > (CARLIFE_SHM_RING_SIZE - 65536u)) {
        uint32_t wp = shm->write_pos % CARLIFE_SHM_RING_SIZE;
        carlife_shm_mb();
        uint32_t t2 = shm->total_bytes;
        *io_read_pos   = wp;
        *io_last_total = t2;
        if (writable) shm->read_pos = wp;
        return -1;
    }

    uint32_t n = (delta > out_cap) ? out_cap : delta;
    uint32_t rp = *io_read_pos % CARLIFE_SHM_RING_SIZE;
    if (rp + n <= CARLIFE_SHM_RING_SIZE) {
        memcpy(out, shm->ring + rp, (size_t)n);
    } else {
        uint32_t first = CARLIFE_SHM_RING_SIZE - rp;
        memcpy(out, shm->ring + rp, (size_t)first);
        memcpy(out + first, shm->ring, (size_t)(n - first));
    }
    carlife_shm_mb();

    /* 二次溢出复检：防止读拷贝期间生产者套圈覆盖未读区间 */
    if ((uint32_t)(shm->total_bytes - *io_last_total) > (CARLIFE_SHM_RING_SIZE - 16384u)) {
        uint32_t wp = shm->write_pos % CARLIFE_SHM_RING_SIZE;
        carlife_shm_mb();
        uint32_t t2 = shm->total_bytes;
        *io_read_pos   = wp;
        *io_last_total = t2;
        if (writable) shm->read_pos = wp;
        return -1;
    }

    *io_read_pos   = (rp + n) % CARLIFE_SHM_RING_SIZE;
    *io_last_total = *io_last_total + n;
    if (writable) shm->read_pos = *io_read_pos;
    return (int)n;
}

#endif /* CARLIFE_SHM_H */
