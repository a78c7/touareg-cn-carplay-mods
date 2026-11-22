/*
 * tests/test_netd.c — Host unit tests for touareg_netd HTTP auth (fail-closed PIN),
 * per-IP exponential-backoff rate limiting, query-PIN rejection, CSRF header check,
 * download path allowlist, OTA upload rejection, and mDNS name parsing.
 */
#define _GNU_SOURCE
#define TOUAREG_NETD_NO_MAIN

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/touareg_netd/touareg_netd.c"

typedef struct {
    int   fd;
    char *buf;
    size_t cap;
    size_t got;
} reader_arg_t;

static void *reader_thread(void *arg) {
    reader_arg_t *ra = (reader_arg_t *)arg;
    size_t got = 0;
    while (got + 1 < ra->cap) {
        ssize_t r = read(ra->fd, ra->buf + got, ra->cap - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    ra->buf[got] = '\0';
    ra->got = got;
    return NULL;
}

static void run_http_req(const char *raw_req, size_t req_len, uint32_t peer_ip, char *resp, size_t rcap) {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    size_t off = 0;
    while (off < req_len) {
        ssize_t w = write(fds[0], raw_req + off, req_len - off);
        assert(w > 0);
        off += (size_t)w;
    }
    shutdown(fds[0], SHUT_WR);

    reader_arg_t ra = { fds[0], resp, rcap, 0 };
    pthread_t th;
    assert(pthread_create(&th, NULL, reader_thread, &ra) == 0);

    handle_http_client(fds[1], htonl(0xC0A82B01u), peer_ip);
    close(fds[1]);

    pthread_join(th, NULL);
    close(fds[0]);
}

static void write_pin_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    if (content && content[0]) assert(fwrite(content, 1, strlen(content), fp) == strlen(content));
    fclose(fp);
}

static void test_dns_read_name(void) {
    /* Valid: 7 touareg 5 local 0 */
    const unsigned char pkt[] = {
        7, 'T','o','u','a','r','e','g', 5, 'L','O','C','A','L', 0
    };
    size_t pos = 0;
    char out[128];
    assert(dns_read_name(pkt, sizeof(pkt), &pos, out, sizeof(out)) == 0);
    assert(strcmp(out, "touareg.local") == 0);
    assert(pos == sizeof(pkt));

    /* Malicious self-referential compression pointer loop must fail cleanly */
    const unsigned char loop_pkt[] = { 0xc0, 0x00 };
    pos = 0;
    assert(dns_read_name(loop_pkt, sizeof(loop_pkt), &pos, out, sizeof(out)) == -1);
}

static void test_http_security_and_routing(void) {
    g_dry_test = 1;
    char tmpdir[] = "/tmp/touareg_netd_test_XXXXXX";
    assert(mkdtemp(tmpdir) != NULL);

    char pin_path[256], setup_pin_path[256];
    snprintf(pin_path, sizeof(pin_path), "%s/web.pin", tmpdir);
    snprintf(setup_pin_path, sizeof(setup_pin_path), "%s/touareg_setup_pin", tmpdir);
    assert(setenv("NETD_PIN_FILE", pin_path, 1) == 0);
    assert(setenv("NETD_SETUP_PIN_FILE", setup_pin_path, 1) == 0);
    assert(setenv("NETD_APP_ROOT", tmpdir, 1) == 0);

    uint32_t ip_owner    = htonl(0xC0A82B02u); /* 192.168.43.2 */
    uint32_t ip_attacker = htonl(0xC0A82B63u); /* 192.168.43.99 */
    char resp[65536];

    /* 1) Landing page "/" does not require PIN */
    const char *req_home = "GET / HTTP/1.1\r\nHost: touareg.local\r\n\r\n";
    run_http_req(req_home, strlen(req_home), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(resp, "MH2p") != NULL);

    /* 2) Missing web.pin -> MUST fail-closed (503 pin_unconfigured), never allow access */
    pin_reset_rate_limit();
    const char *req_status_nopin = "GET /api/status HTTP/1.1\r\nHost: touareg.local\r\n\r\n";
    run_http_req(req_status_nopin, strlen(req_status_nopin), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 503") != NULL);
    assert(strstr(resp, "\"pin_unconfigured\":true") != NULL);

    /* 3) Empty web.pin -> MUST also fail-closed (503) */
    write_pin_file(pin_path, "\n");
    run_http_req(req_status_nopin, strlen(req_status_nopin), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 503") != NULL);

    /* 3.5) Boot auto-generation: generates random 6-digit PIN + 0600 setup_pin + .unconfirmed,
     *      survives reboot before first login (re-populates setup_pin), then clears both after
     *      first successful login so subsequent boots stay silent with zero flash writes */
    unlink(pin_path);
    unlink(setup_pin_path);
    char unconf_path[384];
    get_pin_unconfirmed_path(pin_path, unconf_path, sizeof(unconf_path));
    unlink(unconf_path);

    assert(ensure_web_pin_boot() == 1);
    char gen_pin[64], screen_pin[64];
    assert(read_valid_pin_file(pin_path, gen_pin, sizeof(gen_pin)));
    assert(read_valid_pin_file(setup_pin_path, screen_pin, sizeof(screen_pin)));
    assert(strlen(gen_pin) == 6 && gen_pin[0] != '0' && strcmp(gen_pin, screen_pin) == 0);
    assert(file_exists(unconf_path));
    struct stat sp_st;
    assert(stat(setup_pin_path, &sp_st) == 0 && (sp_st.st_mode & 0777) == 0600);

    /* Simulate reboot before first login (/tmp cleared): setup_pin must be restored with same PIN */
    unlink(setup_pin_path);
    assert(!file_exists(setup_pin_path));
    assert(ensure_web_pin_boot() == 1);
    assert(read_valid_pin_file(setup_pin_path, screen_pin, sizeof(screen_pin)));
    assert(strcmp(gen_pin, screen_pin) == 0);

    /* First valid login clears both setup_pin and .unconfirmed permanently */
    char req_boot_pin[256];
    snprintf(req_boot_pin, sizeof(req_boot_pin),
        "GET /api/status HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: %s\r\n\r\n", gen_pin);
    run_http_req(req_boot_pin, strlen(req_boot_pin), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    assert(!file_exists(setup_pin_path));
    assert(!file_exists(unconf_path));

    /* Subsequent reboot with confirmed web.pin must be a silent no-op (return 0, no setup_pin) */
    assert(ensure_web_pin_boot() == 0);
    assert(!file_exists(setup_pin_path));

    /* 4) Configure valid PIN; URL query ?pin=... MUST be rejected (401) */
    write_pin_file(pin_path, "839201\n");
    pin_reset_rate_limit();
    const char *req_query_pin = "GET /api/status?pin=839201 HTTP/1.1\r\nHost: touareg.local\r\n\r\n";
    run_http_req(req_query_pin, strlen(req_query_pin), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 401") != NULL);
    assert(strstr(resp, "\"need_pin\":true") != NULL);

    /* 5) Valid X-Pin header -> 200 OK */
    const char *req_ok = "GET /api/status HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n\r\n";
    run_http_req(req_ok, strlen(req_ok), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(resp, "\"ok\":true") != NULL);

    /* 6) Per-IP rate limiting + anti-DoS + exponential backoff (30s -> 120s) */
    pin_reset_rate_limit();
    const char *req_wrong = "GET /api/status HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 000000\r\n\r\n";
    for (int i = 0; i < 4; i++) {
        run_http_req(req_wrong, strlen(req_wrong), ip_attacker, resp, sizeof(resp));
        assert(strstr(resp, "HTTP/1.1 401") != NULL);
    }
    /* 5th failure locks ip_attacker for 30s */
    run_http_req(req_wrong, strlen(req_wrong), ip_attacker, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 429") != NULL);
    assert(strstr(resp, "30") != NULL);

    /* While ip_attacker is locked, even the right PIN from ip_attacker is blocked */
    run_http_req(req_ok, strlen(req_ok), ip_attacker, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 429") != NULL);

    /* Meanwhile, owner on ip_owner MUST NOT be locked out (fixes global DoS) */
    run_http_req(req_ok, strlen(req_ok), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);

    /* Advance simulated clock past 30s; another 5 failures escalates lockout to 120s */
    g_test_now_offset += 31;
    for (int i = 0; i < 4; i++) {
        run_http_req(req_wrong, strlen(req_wrong), ip_attacker, resp, sizeof(resp));
        assert(strstr(resp, "HTTP/1.1 401") != NULL);
    }
    run_http_req(req_wrong, strlen(req_wrong), ip_attacker, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 429") != NULL);
    assert(strstr(resp, "120") != NULL);

    /* After 120s backoff expires, valid PIN succeeds and resets lock level */
    g_test_now_offset += 121;
    run_http_req(req_ok, strlen(req_ok), ip_attacker, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);

    /* 6.5) 16-slot anti-eviction test: when all 16 slots are actively locked, a 17th IP is rejected (429)
     *      and previously locked IP #1 is NOT evicted */
    pin_reset_rate_limit();
    for (int s = 0; s < PIN_MAX_BUCKETS; s++) {
        uint32_t sip = htonl(0x0A000001u + (uint32_t)s);
        for (int k = 0; k < 5; k++) run_http_req(req_wrong, strlen(req_wrong), sip, resp, sizeof(resp));
        assert(strstr(resp, "HTTP/1.1 429") != NULL);
    }
    uint32_t ip_17th = htonl(0x0A000099u);
    run_http_req(req_ok, strlen(req_ok), ip_17th, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 429") != NULL);
    /* IP #1 must still be locked */
    run_http_req(req_ok, strlen(req_ok), htonl(0x0A000001u), resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 429") != NULL);
    /* Reset rate limit table before remaining routing tests */
    pin_reset_rate_limit();

    /* 7) CSRF guard on POST write endpoints */
    const char *req_post_nocsrf =
        "POST /api/action HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n"
        "Content-Length: 14\r\n\r\nact=toggle_map";
    run_http_req(req_post_nocsrf, strlen(req_post_nocsrf), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 403") != NULL);

    const char *req_post_ok =
        "POST /api/action HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n"
        "X-Requested-With: touareg-webui\r\nContent-Length: 14\r\n\r\nact=toggle_map";
    run_http_req(req_post_ok, strlen(req_post_ok), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 200 OK") != NULL);

    /* 8) Download path traversal & non-allowlist rejection */
    const char *req_dl_trav =
        "GET /api/dl?type=log&name=../etc/passwd HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n\r\n";
    run_http_req(req_dl_trav, strlen(req_dl_trav), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 400") != NULL);

    const char *req_dl_bad =
        "GET /api/dl?type=log&name=secret.key HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n\r\n";
    run_http_req(req_dl_bad, strlen(req_dl_bad), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 404") != NULL);

    /* 9) OTA upload with forged/garbage payload rejected cleanly */
    char apps_dir[320];
    snprintf(apps_dir, sizeof(apps_dir), "%s/eso/bin/apps", tmpdir);
    char mkcmd[384];
    snprintf(mkcmd, sizeof(mkcmd), "mkdir -p %s", apps_dir);
    assert(system(mkcmd) == 0);
    const char *req_ota_bad =
        "POST /api/ota/upload HTTP/1.1\r\nHost: touareg.local\r\nX-Pin: 839201\r\n"
        "X-Requested-With: touareg-webui\r\nContent-Length: 16\r\n\r\nNOT_A_VALID_TAR!";
    run_http_req(req_ota_bad, strlen(req_ota_bad), ip_owner, resp, sizeof(resp));
    assert(strstr(resp, "HTTP/1.1 400") != NULL);

    /* Cleanup */
    char rmcmd[384];
    snprintf(rmcmd, sizeof(rmcmd), "rm -rf %s", tmpdir);
    if (system(rmcmd) != 0) {}
}

int main(void) {
    test_dns_read_name();
    test_http_security_and_routing();
    puts("test_netd: ALL PASSED");
    return 0;
}
