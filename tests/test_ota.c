/*
 * tests/test_ota.c — Host unit tests for SHA-256, RFC 8032 Ed25519 signature verification,
 * Ed25519-signed allowlisted OTA installation & rollback, and diagnostic redaction.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/touareg_netd/ota_util.h"

static void unhex(const char *h, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        assert(sscanf(h + i * 2, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
}

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void write_file_str(const char *path, const char *data, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, data, len) == (ssize_t)len);
    close(fd);
}

static void read_file_str(const char *path, char *out, size_t cap) {
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    ssize_t n = read(fd, out, cap - 1);
    assert(n >= 0);
    out[n] = '\0';
    close(fd);
}

/* Minimal POSIX ustar builder for unit tests */
static void tar_append_entry(int fd, const char *name, char typeflag, const void *data, size_t len) {
    uint8_t hdr[512];
    memset(hdr, 0, sizeof(hdr));
    snprintf((char *)hdr, 100, "%s", name);
    snprintf((char *)hdr + 100, 8, "%07o", 0644);
    snprintf((char *)hdr + 108, 8, "%07o", 0);
    snprintf((char *)hdr + 116, 8, "%07o", 0);
    snprintf((char *)hdr + 124, 12, "%011lo", (unsigned long)len);
    snprintf((char *)hdr + 136, 12, "%011lo", 1700000000UL);
    memset(hdr + 148, ' ', 8);
    hdr[156] = (uint8_t)typeflag;
    memcpy(hdr + 257, "ustar", 6);
    memcpy(hdr + 263, "00", 2);

    size_t sum = 0;
    for (int i = 0; i < 512; i++) sum += hdr[i];
    snprintf((char *)hdr + 148, 8, "%06lo", (unsigned long)sum);
    hdr[155] = ' ';

    assert(write(fd, hdr, 512) == 512);
    if (len > 0) {
        assert(write(fd, data, len) == (ssize_t)len);
        size_t pad = (512 - (len & 511u)) & 511u;
        if (pad > 0) {
            uint8_t z[512];
            memset(z, 0, pad);
            assert(write(fd, z, pad) == (ssize_t)pad);
        }
    }
}

static void tar_finish(int fd) {
    uint8_t z[1024];
    memset(z, 0, sizeof(z));
    assert(write(fd, z, sizeof(z)) == (ssize_t)sizeof(z));
}

static void test_sha256_and_ed25519_vectors(void) {
    char tmpdir[] = "/tmp/touareg_sha_XXXXXX";
    assert(mkdtemp(tmpdir) != NULL);
    char fpath[256];
    snprintf(fpath, sizeof(fpath), "%s/abc.bin", tmpdir);
    write_file_str(fpath, "abc", 3);
    char hex[65];
    assert(t_sha256_file(fpath, hex) == 0);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    unlink(fpath);
    rmdir(tmpdir);

    /* RFC 8032 Section 7.1 TEST 1 (empty message) */
    uint8_t pk1[32], sig1[64];
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk1, 32);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", sig1, 64);
    assert(t_ed25519_verify(sig1, (const uint8_t *)"", 0, pk1) == 0);
    sig1[10] ^= 0x01;
    assert(t_ed25519_verify(sig1, (const uint8_t *)"", 0, pk1) != 0);

    /* RFC 8032 Section 7.1 TEST 2 (0x72) */
    uint8_t pk2[32], sig2[64], m2[1] = { 0x72 };
    unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", pk2, 32);
    unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
          "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", sig2, 64);
    assert(t_ed25519_verify(sig2, m2, 1, pk2) == 0);
    /* Non-canonical S >= L must be rejected */
    uint8_t bad_s[64];
    memcpy(bad_s, sig2, 64);
    memcpy(bad_s + 32, T_ED_L, 32);
    assert(t_ed25519_verify(bad_s, m2, 1, pk2) != 0);

    /* RFC 8032 Section 7.1 TEST 3 (0xaf, 0x82) */
    uint8_t pk3[32], sig3[64], m3[2] = { 0xaf, 0x82 };
    unhex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", pk3, 32);
    unhex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
          "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a", sig3, 64);
    assert(t_ed25519_verify(sig3, m3, 2, pk3) == 0);
}

static void test_diag_sanitize(void) {
    char san[256];
    diag_sanitize_line("psk=\"MySecretPass123\" pin=889900\n", san, sizeof(san));
    assert(strstr(san, "MySecretPass123") == NULL && strstr(san, "889900") == NULL);
    diag_sanitize_line("X-Pin: 987654\n", san, sizeof(san));
    assert(strstr(san, "987654") == NULL);
    diag_sanitize_line("-----BEGIN OPENSSH PRIVATE KEY-----\n", san, sizeof(san));
    assert(strstr(san, "OPENSSH") == NULL);
    diag_sanitize_line("vin=WVGZZZCRZPD012345\n", san, sizeof(san));
    assert(strstr(san, "WVGZZZCRZPD012345") == NULL);
    diag_sanitize_line("car WVGZZZCRZPD012345 tel 13812345678 ok\n", san, sizeof(san));
    assert(strstr(san, "WVGZZZCRZPD012345") == NULL && strstr(san, "13812345678") == NULL);
    diag_sanitize_line("t=1700000000 spd=86.5 rpm=2100\n", san, sizeof(san));
    assert(strcmp(san, "t=1700000000 spd=86.5 rpm=2100\n") == 0);
}

static void test_ota_signature_and_security(void) {
    /* Throwaway test-only Ed25519 public key and precomputed valid signature over manifest */
    uint8_t test_pk[32];
    unhex("f0229a72ad95331515c1e27ca0703473e6bd96b8bf316c669312bda4977430a8", test_pk, 32);
    const char *good_sig =
        "cbf522000025b4d834cb1f9e1a3d66f0b280bcc7aef42fca81cc22f0b7354fbf"
        "fd04b5bbec57c8fcdc1b0afe044f5cea4847efad9a057148514a93b2a9082405\n";
    const char *payload_netd = "NEW_NETD_BINARY_V2202";
    const char *payload_ver  = "v22.02-test\n";
    const char *good_manifest =
        "8e18e5ba4fc1dfc95a39c4e7ee1a6364a76b5f992aeb34118435e27b9a4fdf9e  touareg_netd\n"
        "1e6426cb1099ac94b420d32a61c53c5d621f95b2d5932cb703cc0f2c51f376e4  version.txt\n";

    char root[] = "/tmp/touareg_ota_test_XXXXXX";
    assert(mkdtemp(root) != NULL);

    char wifi_dir[384], live_netd[448], ver_path[448], tar_path[448], msg[256], buf[128];
    snprintf(wifi_dir, sizeof(wifi_dir), "%s/eso/bin/apps/wifi_dev", root);
    mkdir_p(wifi_dir);
    snprintf(live_netd, sizeof(live_netd), "%s/touareg_netd", wifi_dir);
    snprintf(ver_path, sizeof(ver_path), "%s/ota_version", wifi_dir);
    snprintf(tar_path, sizeof(tar_path), "%s/pkg.tar", root);

    write_file_str(live_netd, "OLD_NETD_V2201", strlen("OLD_NETD_V2201"));

    /* 1) Unsigned package MUST be rejected by default */
    int tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(tfd >= 0);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "version.txt", '0', payload_ver, strlen(payload_ver));
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -21);
    read_file_str(live_netd, buf, sizeof(buf));
    assert(strcmp(buf, "OLD_NETD_V2201") == 0);

    /* 2) Forged manifest + forged signature (attacker recomputes sha256) MUST be rejected */
    char bad_sig[140];
    snprintf(bad_sig, sizeof(bad_sig), "%s", good_sig);
    bad_sig[0] = (bad_sig[0] == 'c') ? 'd' : 'c';
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(tfd >= 0);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "version.txt", '0', payload_ver, strlen(payload_ver));
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_append_entry(tfd, "manifest.sha256.sig", '0', bad_sig, strlen(bad_sig));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -23);
    read_file_str(live_netd, buf, sizeof(buf));
    assert(strcmp(buf, "OLD_NETD_V2201") == 0);

    /* 3) Valid signature over manifest, but tampered binary -> SHA-256 mismatch MUST be rejected */
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(tfd >= 0);
    tar_append_entry(tfd, "touareg_netd", '0', "EVIL_BINARY_TAMPERED!", 21);
    tar_append_entry(tfd, "version.txt", '0', payload_ver, strlen(payload_ver));
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_append_entry(tfd, "manifest.sha256.sig", '0', good_sig, strlen(good_sig));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -18);

    /* 4) Valid signature, tampered version.txt -> MUST be rejected */
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(tfd >= 0);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "version.txt", '0', "v99.99-fake\n", 12);
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_append_entry(tfd, "manifest.sha256.sig", '0', good_sig, strlen(good_sig));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -18);

    /* 5) Path traversal, symlink, duplicate entry, non-allowlisted file MUST be rejected */
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    tar_append_entry(tfd, "../pwn", '0', "x", 1);
    tar_finish(tfd); close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -7);

    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    tar_append_entry(tfd, "touareg_netd", '2', "", 0);
    tar_finish(tfd); close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -6);

    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    tar_append_entry(tfd, "evil.sh", '0', "rm -rf /", 8);
    tar_finish(tfd); close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -8);

    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "touareg_netd", '0', "DUP", 3);
    tar_finish(tfd); close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == -8);

    /* 6) Valid signed OTA package -> succeeds, updates binary & version, backs up old file, rollback restores old */
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(tfd >= 0);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "version.txt", '0', payload_ver, strlen(payload_ver));
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_append_entry(tfd, "manifest.sha256.sig", '0', good_sig, strlen(good_sig));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == 0);
    read_file_str(live_netd, buf, sizeof(buf));
    assert(strcmp(buf, payload_netd) == 0);
    read_file_str(ver_path, buf, sizeof(buf));
    assert(strcmp(buf, "v22.02-test") == 0);

    assert(ota_rollback_backup(root, msg, sizeof(msg)) == 0);
    read_file_str(live_netd, buf, sizeof(buf));
    assert(strcmp(buf, "OLD_NETD_V2201") == 0);

    /* 7) Local SSH opt-in flag allow_unsigned_ota allows unsigned package with valid SHA-256 */
    char dev_flag[448];
    snprintf(dev_flag, sizeof(dev_flag), "%s/allow_unsigned_ota", wifi_dir);
    write_file_str(dev_flag, "", 0);
    tfd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    tar_append_entry(tfd, "touareg_netd", '0', payload_netd, strlen(payload_netd));
    tar_append_entry(tfd, "version.txt", '0', payload_ver, strlen(payload_ver));
    tar_append_entry(tfd, "manifest.sha256", '0', good_manifest, strlen(good_manifest));
    tar_finish(tfd);
    close(tfd);
    assert(ota_apply_tar_with_key(root, tar_path, test_pk, msg, sizeof(msg)) == 0);
    read_file_str(live_netd, buf, sizeof(buf));
    assert(strcmp(buf, payload_netd) == 0);

    /* 8) Nonexistent root & rollback errors */
    assert(ota_apply_tar("/tmp/nonexistent_ota_root_xyz", "/tmp/no_such.tar", msg, sizeof(msg)) != 0);
    assert(ota_rollback_backup("/tmp/nonexistent_ota_root_xyz", msg, sizeof(msg)) != 0);

    /* Best-effort cleanup */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    if (system(cmd) != 0) {}
}

int main(void) {
    test_sha256_and_ed25519_vectors();
    test_diag_sanitize();
    test_ota_signature_and_security();
    puts("test_ota: ALL PASSED");
    return 0;
}
