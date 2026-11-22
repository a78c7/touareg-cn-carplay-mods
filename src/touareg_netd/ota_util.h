/*
 * ota_util.h — Pure-C SHA-256, allowlisted POSIX tar offline OTA installer/rollback,
 * and sanitized diagnostic bundle helper for touareg_netd (MH2p QNX 6.6 ARMv7).
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#ifndef TOUAREG_OTA_UTIL_H
#define TOUAREG_OTA_UTIL_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ==================== Minimal Pure-C SHA-256 (FIPS 180-4) ==================== */

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t  buf[64];
    size_t   idx;
} t_sha256_t;

static const uint32_t T_SHA_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static inline uint32_t t_rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void t_sha256_block(t_sha256_t *s, const uint8_t b[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16) |
               ((uint32_t)b[i * 4 + 2] << 8) | (uint32_t)b[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = t_rotr32(w[i - 15], 7) ^ t_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = t_rotr32(w[i - 2], 17) ^ t_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], bb = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = t_rotr32(e, 6) ^ t_rotr32(e, 11) ^ t_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = hh + S1 + ch + T_SHA_K[i] + w[i];
        uint32_t S0 = t_rotr32(a, 2) ^ t_rotr32(a, 13) ^ t_rotr32(a, 22);
        uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = bb; bb = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += bb; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += hh;
}

static void t_sha256_init(t_sha256_t *s) {
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u; s->h[2] = 0x3c6ef372u; s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu; s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->bits = 0; s->idx = 0;
}

static void t_sha256_update(t_sha256_t *s, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    s->bits += (uint64_t)len * 8ULL;
    while (len > 0) {
        size_t take = 64 - s->idx;
        if (take > len) take = len;
        memcpy(s->buf + s->idx, p, take);
        s->idx += take; p += take; len -= take;
        if (s->idx == 64) { t_sha256_block(s, s->buf); s->idx = 0; }
    }
}

static void t_sha256_final_hex(t_sha256_t *s, char out_hex[65]) {
    s->buf[s->idx++] = 0x80;
    if (s->idx > 56) {
        memset(s->buf + s->idx, 0, 64 - s->idx);
        t_sha256_block(s, s->buf);
        s->idx = 0;
    }
    memset(s->buf + s->idx, 0, 56 - s->idx);
    for (int i = 0; i < 8; i++) s->buf[56 + i] = (uint8_t)(s->bits >> (56 - i * 8));
    t_sha256_block(s, s->buf);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        for (int j = 7; j >= 0; j--) {
            out_hex[i * 8 + (7 - j)] = hx[(s->h[i] >> (j * 4)) & 0xFu];
        }
    }
    out_hex[64] = '\0';
}

static int t_sha256_file(const char *path, char out_hex[65]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    t_sha256_t st;
    t_sha256_init(&st);
    uint8_t buf[8192];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) t_sha256_update(&st, buf, (size_t)r);
    close(fd);
    if (r < 0) return -1;
    t_sha256_final_hex(&st, out_hex);
    return 0;
}

/* ==================== Allowlisted Offline OTA (.tar) Engine ==================== */

#define OTA_MAX_BYTES (6u * 1024u * 1024u)

typedef struct {
    const char *name;     /* exact entry name inside OTA .tar */
    const char *rel_dst;  /* relative path under app_root (default "/mnt/app") */
    mode_t      mode;
} ota_slot_t;

static const ota_slot_t OTA_ALLOWLIST[] = {
    { "cl_render",          "eso/bin/apps/cluster/cl_render",      0755 },
    { "clusterd",           "eso/bin/apps/cluster/clusterd",       0755 },
    { "cl_probe.so",        "eso/bin/apps/cluster/cl_probe.so",    0755 },
    { "touareg_netd",       "eso/bin/apps/wifi_dev/touareg_netd",  0755 },
    { "VWClusterNav_v6.jar","eso/hmi/lsd/jars/VWClusterNav_v6.jar",0644 },
    { "VWPopupFilter.jar",  "eso/hmi/lsd/jars/VWPopupFilter.jar",  0644 },
    { NULL, NULL, 0 }
};

static int ota_copy_atomic(const char *src, const char *dst, mode_t mode) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.new", dst);
    int dfd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dfd < 0) { close(sfd); return -1; }
    uint8_t buf[8192];
    ssize_t r;
    int ok = 1;
    while ((r = read(sfd, buf, sizeof(buf))) > 0) {
        size_t off = 0;
        while (off < (size_t)r) {
            ssize_t w = write(dfd, buf + off, (size_t)r - off);
            if (w <= 0) { ok = 0; break; }
            off += (size_t)w;
        }
        if (!ok) break;
    }
    if (r < 0) ok = 0;
    if (ok) {
        fchmod(dfd, mode);
        fsync(dfd);
    }
    close(sfd);
    close(dfd);
    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, dst) != 0) { unlink(tmp); return -1; }
    return 0;
}

static int ota_tar_octal(const uint8_t *p, size_t n, size_t *out) {
    size_t v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\0')) i++;
    if (i == n) { *out = 0; return 0; }
    for (; i < n && p[i] != '\0' && p[i] != ' '; i++) {
        if (p[i] < '0' || p[i] > '7') return -1;
        v = (v << 3) | (size_t)(p[i] - '0');
    }
    *out = v;
    return 0;
}

static int ota_tar_chksum_ok(const uint8_t hdr[512]) {
    size_t want = 0;
    if (ota_tar_octal(hdr + 148, 8, &want) != 0) return 0;
    size_t usum = 0;
    for (int i = 0; i < 512; i++) usum += (i >= 148 && i < 156) ? ' ' : (size_t)hdr[i];
    return usum == want;
}

static int ota_manifest_lookup(const char *manifest, const char *name, char want_hex[65]) {
    const char *p = manifest;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        if (ll > 65) {
            int okhex = 1;
            for (int i = 0; i < 64; i++) {
                if (!isxdigit((unsigned char)p[i])) { okhex = 0; break; }
            }
            if (okhex && (p[64] == ' ' || p[64] == '\t')) {
                const char *fn = p + 64;
                while (fn < p + ll && (*fn == ' ' || *fn == '\t' || *fn == '*')) fn++;
                size_t fnl = (size_t)(p + ll - fn);
                while (fnl > 0 && (fn[fnl - 1] == '\r' || fn[fnl - 1] == ' ')) fnl--;
                if (fnl == strlen(name) && memcmp(fn, name, fnl) == 0) {
                    for (int i = 0; i < 64; i++) want_hex[i] = (char)tolower((unsigned char)p[i]);
                    want_hex[64] = '\0';
                    return 1;
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* Verify and atomically install an allowlisted OTA .tar package staged at tar_path.
 * app_root is "/mnt/app" on the head unit (or a temp dir in host unit tests).
 * Caller is responsible for mounting app_root rw before and ro after.
 * Returns 0 on success (writes summary to msg), non-zero on failure without modifying targets. */
static int ota_apply_tar(const char *app_root, const char *tar_path, char *msg, size_t msz) {
    char stage_dir[320], bak_dir[320];
    snprintf(stage_dir, sizeof(stage_dir), "%s/eso/bin/apps/.ota_stage", app_root);
    snprintf(bak_dir, sizeof(bak_dir), "%s/eso/bin/apps/.ota_backup", app_root);
    mkdir(stage_dir, 0755);

    int fd = open(tar_path, O_RDONLY);
    if (fd < 0) { snprintf(msg, msz, "无法打开升级包"); return -1; }

    char manifest[8192];
    manifest[0] = '\0';
    char ver_txt[64];
    ver_txt[0] = '\0';
    int present[16];
    memset(present, 0, sizeof(present));
    int count_files = 0;

    uint8_t hdr[512];
    uint8_t io_buf[8192];
    int err = 0;

    for (;;) {
        ssize_t r = read(fd, hdr, 512);
        if (r == 0) break;
        if (r != 512) { snprintf(msg, msz, "升级包格式损坏（头截断）"); err = -2; break; }
        int all_zero = 1;
        for (int i = 0; i < 512; i++) if (hdr[i] != 0) { all_zero = 0; break; }
        if (all_zero) break;
        if (!ota_tar_chksum_ok(hdr)) { snprintf(msg, msz, "升级包 tar 校验和无效"); err = -3; break; }

        char raw_name[101];
        memcpy(raw_name, hdr, 100);
        raw_name[100] = '\0';
        const char *name = raw_name;
        if (strncmp(name, "./", 2) == 0) name += 2;
        uint8_t tf = hdr[156];
        size_t fsz = 0;
        if (ota_tar_octal(hdr + 124, 12, &fsz) != 0 || fsz > OTA_MAX_BYTES) {
            snprintf(msg, msz, "升级包文件大小非法"); err = -4; break;
        }
        if (tf == '5') {
            /* allow harmless top-level "." directory entry only */
            if (strcmp(name, ".") != 0 && strcmp(name, "./") != 0 && name[0] != '\0') {
                snprintf(msg, msz, "升级包不允许包含子目录：%s", name); err = -5; break;
            }
            continue;
        }
        if (tf != '0' && tf != '\0') {
            snprintf(msg, msz, "升级包拒绝非普通文件（禁止符号链接/特殊节点）"); err = -6; break;
        }
        if (strchr(name, '/') != NULL || strstr(name, "..") != NULL) {
            snprintf(msg, msz, "升级包包含非法路径：%s", name); err = -7; break;
        }

        int slot_idx = -1;
        int is_manifest = (strcmp(name, "manifest.sha256") == 0);
        int is_ver = (strcmp(name, "version.txt") == 0);
        if (!is_manifest && !is_ver) {
            for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
                if (strcmp(OTA_ALLOWLIST[i].name, name) == 0) { slot_idx = i; break; }
            }
            if (slot_idx < 0) {
                snprintf(msg, msz, "升级包包含未授权白名单外文件：%s", name); err = -8; break;
            }
        }

        size_t pad = (512 - (fsz & 511u)) & 511u;
        if (is_manifest || is_ver) {
            char *dstb = is_manifest ? manifest : ver_txt;
            size_t cap = is_manifest ? sizeof(manifest) : sizeof(ver_txt);
            if (fsz >= cap) { snprintf(msg, msz, "清单文件过大"); err = -9; break; }
            size_t got = 0;
            while (got < fsz) {
                ssize_t k = read(fd, dstb + got, fsz - got);
                if (k <= 0) { err = -10; break; }
                got += (size_t)k;
            }
            dstb[got] = '\0';
            if (err) { snprintf(msg, msz, "读取清单失败"); break; }
        } else {
            char outp[512];
            snprintf(outp, sizeof(outp), "%s/%s", stage_dir, name);
            int ofd = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (ofd < 0) { snprintf(msg, msz, "无法创建暂存文件"); err = -11; break; }
            size_t rem = fsz;
            while (rem > 0) {
                size_t want = rem < sizeof(io_buf) ? rem : sizeof(io_buf);
                ssize_t k = read(fd, io_buf, want);
                if (k <= 0) { err = -12; break; }
                if (write(ofd, io_buf, (size_t)k) != k) { err = -13; break; }
                rem -= (size_t)k;
            }
            close(ofd);
            if (err) { unlink(outp); snprintf(msg, msz, "解包暂存失败"); break; }
            present[slot_idx] = 1;
            count_files++;
        }

        if (pad > 0) {
            uint8_t skip[512];
            if (read(fd, skip, pad) != (ssize_t)pad) { snprintf(msg, msz, "升级包截断"); err = -14; break; }
        }
    }
    close(fd);

    if (!err && count_files == 0) { snprintf(msg, msz, "升级包中无任何可更新组件"); err = -15; }
    if (!err && !manifest[0]) { snprintf(msg, msz, "升级包缺少 manifest.sha256 校验清单"); err = -16; }

    /* Phase 2: strict SHA-256 verification for every staged component BEFORE touching any live file */
    if (!err) {
        for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
            if (!present[i]) continue;
            char want_hex[65], got_hex[65], sp[512];
            if (!ota_manifest_lookup(manifest, OTA_ALLOWLIST[i].name, want_hex)) {
                snprintf(msg, msz, "manifest.sha256 缺少 %s 哈希", OTA_ALLOWLIST[i].name);
                err = -17; break;
            }
            snprintf(sp, sizeof(sp), "%s/%s", stage_dir, OTA_ALLOWLIST[i].name);
            if (t_sha256_file(sp, got_hex) != 0 || strcmp(want_hex, got_hex) != 0) {
                snprintf(msg, msz, "%s SHA-256 校验不匹配（包可能损坏或被篡改）", OTA_ALLOWLIST[i].name);
                err = -18; break;
            }
        }
    }

    /* Phase 3: backup existing targets & atomically install */
    int installed = 0;
    if (!err) {
        mkdir(bak_dir, 0755);
        for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
            if (!present[i]) continue;
            char src[512], dst[512], bak[512];
            snprintf(src, sizeof(src), "%s/%s", stage_dir, OTA_ALLOWLIST[i].name);
            snprintf(dst, sizeof(dst), "%s/%s", app_root, OTA_ALLOWLIST[i].rel_dst);
            snprintf(bak, sizeof(bak), "%s/%s", bak_dir, OTA_ALLOWLIST[i].name);
            struct stat st;
            if (stat(dst, &st) == 0 && S_ISREG(st.st_mode)) {
                if (ota_copy_atomic(dst, bak, OTA_ALLOWLIST[i].mode) != 0) {
                    snprintf(msg, msz, "备份旧版 %s 失败，未改动任何文件", OTA_ALLOWLIST[i].name);
                    err = -19; break;
                }
            }
        }
    }
    if (!err) {
        for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
            if (!present[i]) continue;
            char src[512], dst[512];
            snprintf(src, sizeof(src), "%s/%s", stage_dir, OTA_ALLOWLIST[i].name);
            snprintf(dst, sizeof(dst), "%s/%s", app_root, OTA_ALLOWLIST[i].rel_dst);
            /* Only upgrade components already installed on this unit (safe for cross-brand MH2p units with only VW_WiFi_Dev) */
            struct stat dst_st;
            if (stat(dst, &dst_st) != 0 || !S_ISREG(dst_st.st_mode)) continue;
            if (ota_copy_atomic(src, dst, OTA_ALLOWLIST[i].mode) != 0) {
                snprintf(msg, msz, "替换 %s 失败（可用一键回滚还原）", OTA_ALLOWLIST[i].name);
                err = -20; break;
            }
            installed++;
        }
    }
    if (!err && ver_txt[0]) {
        char *nl = strpbrk(ver_txt, "\r\n");
        if (nl) *nl = '\0';
        char vp[512];
        snprintf(vp, sizeof(vp), "%s/eso/bin/apps/wifi_dev/ota_version", app_root);
        int vfd = open(vp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (vfd >= 0) { (void)write(vfd, ver_txt, strlen(ver_txt)); fsync(vfd); close(vfd); }
    }

    /* Cleanup staged files */
    for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
        char sp[512];
        snprintf(sp, sizeof(sp), "%s/%s", stage_dir, OTA_ALLOWLIST[i].name);
        unlink(sp);
    }
    unlink(tar_path);

    if (!err) {
        snprintf(msg, msz, "升级成功（更新 %d 项%s%s），原版已自动备份，请点「软重启车机」生效",
                 installed, ver_txt[0] ? " · 版本 " : "", ver_txt);
    }
    return err;
}

static int ota_rollback_backup(const char *app_root, char *msg, size_t msz) {
    char bak_dir[320];
    snprintf(bak_dir, sizeof(bak_dir), "%s/eso/bin/apps/.ota_backup", app_root);
    int restored = 0;
    for (int i = 0; OTA_ALLOWLIST[i].name; i++) {
        char bak[512], dst[512];
        snprintf(bak, sizeof(bak), "%s/%s", bak_dir, OTA_ALLOWLIST[i].name);
        snprintf(dst, sizeof(dst), "%s/%s", app_root, OTA_ALLOWLIST[i].rel_dst);
        struct stat st;
        if (stat(bak, &st) == 0 && S_ISREG(st.st_mode)) {
            if (ota_copy_atomic(bak, dst, OTA_ALLOWLIST[i].mode) != 0) {
                snprintf(msg, msz, "回滚 %s 失败", OTA_ALLOWLIST[i].name);
                return -1;
            }
            restored++;
        }
    }
    if (restored == 0) {
        snprintf(msg, msz, "未找到可回滚的 OTA 备份");
        return -2;
    }
    snprintf(msg, msz, "已回滚 %d 个核心组件，请点「软重启车机」生效", restored);
    return 0;
}

/* ==================== Sanitized Diagnostic Line Filter ==================== */

/* Redact sensitive substrings (Wi-Fi PSK/SSID, navigation destination/road names, keys)
 * in-place into dst so exported diagnostic bundles are safe to post on GitHub Issues. */
static void diag_sanitize_line(const char *line, char *dst, size_t cap) {
    if (!cap) return;
    static const char *redact_prefixes[] = {
        "destination:s:", "current_road:s:", "slot_0_road:s:", "slot_0_description:s:",
        "psk=", "ssid=", "password=", NULL
    };
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    for (int i = 0; redact_prefixes[i]; i++) {
        size_t pl = strlen(redact_prefixes[i]);
        if (strncasecmp(p, redact_prefixes[i], pl) == 0) {
            snprintf(dst, cap, "%.*s%s[REDACTED]\n", (int)(p - line), line, redact_prefixes[i]);
            return;
        }
    }
    /* Also redact inline psk="..." or ChaCha hex strings */
    if (strstr(line, "psk=\"") || strstr(line, "PIN=")) {
        snprintf(dst, cap, "[REDACTED SENSITIVE LINE]\n");
        return;
    }
    snprintf(dst, cap, "%s", line);
}

#endif /* TOUAREG_OTA_UTIL_H */
