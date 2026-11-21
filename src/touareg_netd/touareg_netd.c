/*
 * touareg_netd.c - MH2p (QNX 6.6 ARMv7) wireless console & mDNS daemon
 *
 * Features:
 *   1) RFC 6762 mDNS responder on bcm2: touareg.local -> <bcm2 IPv4>
 *   2) Mobile HTTP console bound exclusively to <bcm2 IPv4>:8080 (avoids stock 127.0.0.1:8080, never exposed on ecm0)
 *   3) PIN authentication (/mnt/app/eso/bin/apps/wifi_dev/web.pin), parked-only write lock (/tmp/dash.state spd <= 3),
 *      strict input allowlists, and same-partition atomic file replacement (.new -> rename).
 *
 * Build dependencies: libc + libsocket only.
 */
#include "aeabi_shim.h"
#include "web_ui_data.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define IFACE_DEFAULT     "bcm2"
#define HTTP_PORT_DEFAULT 8080
#define MDNS_PORT         5353
#define MDNS_MCAST_IP     "224.0.0.251"
#define MDNS_HOST_LABEL   "touareg"

#define CLUSTER_DIR       "/mnt/app/eso/bin/apps/cluster"
#define DASH_CONF_PATH    "/mnt/app/eso/bin/apps/cluster/dash.conf"
#define CL_CONF_PATH      "/mnt/app/eso/bin/apps/cluster/cl.conf"
#define WIFI_DIR          "/mnt/app/eso/bin/apps/wifi_dev"
#define WPA_CONF_PATH     "/mnt/app/eso/bin/apps/wifi_dev/wpa.conf"
#define WEB_PIN_PATH      "/mnt/app/eso/bin/apps/wifi_dev/web.pin"
#define DASH_STATE_PATH   "/tmp/dash.state"

static volatile sig_atomic_t g_stop = 0;
static const char *g_iface = IFACE_DEFAULT;
static int g_http_port = HTTP_PORT_DEFAULT;
static int g_dry_test = 0;

static void on_sig(int sig) { (void)sig; g_stop = 1; }

static void nlog(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "[netd %lu] ", (unsigned long)time(NULL));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static uint32_t get_iface_ipv4(const char *ifname) {
    const char *override = getenv("NETD_BIND_ADDR");
    if (override && override[0]) {
        struct in_addr a;
        if (inet_aton(override, &a)) return a.s_addr;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    uint32_t ip = 0;
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
        if (sin->sin_family == AF_INET) ip = sin->sin_addr.s_addr;
    }
    close(fd);
    return ip;
}

static void ip_to_str(uint32_t ip, char *buf, size_t sz) {
    struct in_addr a;
    a.s_addr = ip;
    const char *s = inet_ntoa(a);
    snprintf(buf, sz, "%s", s ? s : "0.0.0.0");
}

/* ==================== mDNS Responder (touareg.local) ==================== */

static int open_mdns_sock(uint32_t if_ip) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(MDNS_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        nlog("mdns bind 5353 failed: errno=%d", errno);
        close(fd);
        return -1;
    }
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_MCAST_IP);
    mreq.imr_interface.s_addr = if_ip;
    (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    struct in_addr out_if;
    out_if.s_addr = if_ip;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &out_if, sizeof(out_if));
    unsigned char ttl = 255, loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    return fd;
}

static void send_mdns_reply(int fd, uint32_t ip_net, uint16_t txid, const struct sockaddr_in *unicast_to) {
    if (fd < 0 || ip_net == 0) return;
    unsigned char pkt[64];
    size_t p = 0;
    pkt[p++] = (unsigned char)(txid >> 8);
    pkt[p++] = (unsigned char)(txid & 0xff);
    pkt[p++] = 0x84; pkt[p++] = 0x00;
    pkt[p++] = 0x00; pkt[p++] = 0x00;
    pkt[p++] = 0x00; pkt[p++] = 0x01;
    pkt[p++] = 0x00; pkt[p++] = 0x00;
    pkt[p++] = 0x00; pkt[p++] = 0x00;
    pkt[p++] = 7; memcpy(pkt + p, "touareg", 7); p += 7;
    pkt[p++] = 5; memcpy(pkt + p, "local", 5); p += 5;
    pkt[p++] = 0;
    pkt[p++] = 0x00; pkt[p++] = 0x01;
    pkt[p++] = 0x80; pkt[p++] = 0x01;
    pkt[p++] = 0x00; pkt[p++] = 0x00; pkt[p++] = 0x00; pkt[p++] = 120;
    pkt[p++] = 0x00; pkt[p++] = 0x04;
    memcpy(pkt + p, &ip_net, 4); p += 4;

    if (unicast_to) {
        sendto(fd, pkt, p, 0, (const struct sockaddr *)unicast_to, sizeof(*unicast_to));
    }
    pkt[0] = 0; pkt[1] = 0;
    struct sockaddr_in mcast;
    memset(&mcast, 0, sizeof(mcast));
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(MDNS_PORT);
    mcast.sin_addr.s_addr = inet_addr(MDNS_MCAST_IP);
    sendto(fd, pkt, p, 0, (const struct sockaddr *)&mcast, sizeof(mcast));
}

static void send_mdns_nodata(int fd, uint16_t txid, const struct sockaddr_in *unicast_to) {
    if (fd < 0 || !unicast_to) return;
    unsigned char pkt[12] = {
        (unsigned char)(txid >> 8), (unsigned char)(txid & 0xff),
        0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    sendto(fd, pkt, sizeof(pkt), 0, (const struct sockaddr *)unicast_to, sizeof(*unicast_to));
}

static int dns_read_name(const unsigned char *buf, size_t len, size_t *pos, char *out, size_t out_sz) {
    size_t cur = *pos, o = 0;
    int jumped = 0, steps = 0;
    while (cur < len && steps++ < 32) {
        unsigned char c = buf[cur];
        if (c == 0) {
            if (!jumped) *pos = cur + 1;
            if (o > 0 && out[o - 1] == '.') o--;
            out[o] = '\0';
            return 0;
        }
        if ((c & 0xc0) == 0xc0) {
            if (cur + 1 >= len) return -1;
            size_t ptr = (size_t)(((c & 0x3f) << 8) | buf[cur + 1]);
            if (!jumped) { *pos = cur + 2; jumped = 1; }
            cur = ptr;
            continue;
        }
        if (c > 63 || cur + 1 + c > len || o + c + 2 >= out_sz) return -1;
        for (unsigned char i = 0; i < c; i++) out[o++] = (char)tolower(buf[cur + 1 + i]);
        out[o++] = '.';
        cur += 1 + c;
    }
    return -1;
}

static void handle_mdns_pkt(int fd, uint32_t ip_net) {
    unsigned char buf[1500];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
    if (n < 12 || ip_net == 0 || (buf[2] & 0x80)) return;
    uint16_t txid = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
    size_t pos = 12;
    for (uint16_t i = 0; i < qdcount && i < 16; i++) {
        char name[256];
        if (dns_read_name(buf, (size_t)n, &pos, name, sizeof(name)) < 0 || pos + 4 > (size_t)n) return;
        uint16_t qtype = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        uint16_t qclass = (uint16_t)((buf[pos + 2] << 8) | buf[pos + 3]);
        pos += 4;
        if (strcmp(name, "touareg.local") == 0) {
            int want_unicast = (qclass & 0x8000) || (ntohs(from.sin_port) != MDNS_PORT);
            if (qtype == 1 || qtype == 255) {
                send_mdns_reply(fd, ip_net, want_unicast ? txid : 0, want_unicast ? &from : NULL);
                return;
            } else if (qtype == 28) {
                if (want_unicast) send_mdns_nodata(fd, txid, &from);
                send_mdns_reply(fd, ip_net, 0, NULL);
                return;
            }
        }
    }
}

/* ==================== Auth, Safety & Config Helpers ==================== */

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int proc_alive(const char *check_cmd) {
    if (g_dry_test) return 1;
    return system(check_cmd) == 0 ? 1 : 0;
}

static void read_dash_state(double *out_spd, double *out_odo) {
    *out_spd = -1.0;
    *out_odo = -1.0;
    struct stat st;
    if (stat(DASH_STATE_PATH, &st) != 0) return;
    time_t now = time(NULL);
    if (now - st.st_mtime > 5 || st.st_mtime - now > 5) return;
    FILE *fp = fopen(DASH_STATE_PATH, "r");
    if (!fp) return;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    const char *s = buf;
    while (*s) {
        while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
        if (!*s) break;
        const char *eq = strchr(s, '=');
        if (!eq) break;
        size_t kl = (size_t)(eq - s);
        double v = strtod(eq + 1, NULL);
        if (kl == 3 && memcmp(s, "spd", 3) == 0) *out_spd = v;
        else if (kl == 3 && memcmp(s, "odo", 3) == 0) *out_odo = v;
        while (*s && *s != ' ' && *s != '\n' && *s != '\r') s++;
    }
}

static int is_driving_now(void) {
    double spd = -1.0, odo = -1.0;
    read_dash_state(&spd, &odo);
    return (spd > 3.0);
}

static void mount_app_rw(void) {
    if (!g_dry_test) (void)system("mount -uw /mnt/app >/dev/null 2>&1");
}

static void mount_app_ro(void) {
    if (!g_dry_test) {
        sync();
        (void)system("mount -ur /mnt/app >/dev/null 2>&1");
    }
}

static int atomic_write_file(const char *path, const char *data, size_t len, mode_t mode) {
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp)) return -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) { close(fd); unlink(tmp); return -1; }
        off += (size_t)w;
    }
    fsync(fd);
    close(fd);
    chmod(tmp, mode);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *src, size_t slen, char *dst, size_t dsz) {
    size_t di = 0;
    for (size_t i = 0; i < slen && di + 1 < dsz; i++) {
        if (src[i] == '+') dst[di++] = ' ';
        else if (src[i] == '%' && i + 2 < slen) {
            int h1 = hex_val(src[i + 1]), h2 = hex_val(src[i + 2]);
            if (h1 >= 0 && h2 >= 0) { dst[di++] = (char)((h1 << 4) | h2); i += 2; }
        } else dst[di++] = src[i];
    }
    dst[di] = '\0';
}

static int form_get(const char *body, const char *key, char *out, size_t osz) {
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', seg);
        if (eq && (size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            url_decode(eq + 1, seg - klen - 1, out, osz);
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    out[0] = '\0';
    return 0;
}

static void header_get(const char *req, const char *hdr_end, const char *hname, char *out, size_t osz) {
    out[0] = '\0';
    size_t hl = strlen(hname);
    const char *p = req;
    while (p < hdr_end) {
        const char *nl = strstr(p, "\r\n");
        if (!nl || nl > hdr_end) break;
        if ((size_t)(nl - p) > hl && strncasecmp(p, hname, hl) == 0 && p[hl] == ':') {
            const char *v = p + hl + 1;
            while (v < nl && (*v == ' ' || *v == '\t')) v++;
            size_t vl = (size_t)(nl - v);
            if (vl >= osz) vl = osz - 1;
            memcpy(out, v, vl);
            out[vl] = '\0';
            return;
        }
        p = nl + 2;
    }
}

static int g_pin_fails = 0;
static time_t g_pin_lock_until = 0;

/* 校验管理口令 web.pin（带防爆破限速：连续输错 5 次锁定 30 秒；返回 1=通过, 0=需口令/错误, -1=锁定中） */
static int check_pin_auth(const char *req, const char *hdr_end, const char *query) {
    const char *pin_path = getenv("NETD_PIN_FILE");
    if (!pin_path || !pin_path[0]) pin_path = WEB_PIN_PATH;
    FILE *fp = fopen(pin_path, "r");
    if (!fp) return 1; /* 未配置 PIN 时兼容放行 */
    char expected[64];
    if (!fgets(expected, sizeof(expected), fp)) { fclose(fp); return 1; }
    fclose(fp);
    char *nl = strpbrk(expected, "\r\n ");
    if (nl) *nl = '\0';
    size_t elen = strlen(expected);
    if (elen == 0) return 1;

    time_t now = time(NULL);
    if (g_pin_lock_until > now && now - g_pin_lock_until <= 60) return -1;

    char got[64];
    header_get(req, hdr_end, "X-Pin", got, sizeof(got));
    if (!got[0]) form_get(query, "pin", got, sizeof(got));
    if (!got[0]) return 0;
    size_t glen = strlen(got);
    unsigned char diff = (glen != elen) ? 1 : 0;
    for (size_t i = 0; i < elen && i < glen; i++) diff |= (unsigned char)(got[i] ^ expected[i]);
    if (diff == 0) {
        g_pin_fails = 0;
        g_pin_lock_until = 0;
        return 1;
    }
    if (++g_pin_fails >= 5) {
        g_pin_lock_until = now + 30;
        g_pin_fails = 0;
        return -1;
    }
    return 0;
}

static int valid_items_str(const char *s) {
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == ',')) return 0;
    }
    return 1;
}

static int clamp_int(const char *s, int lo, int hi, int defv) {
    if (!s || !s[0]) return defv;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return defv;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static void json_escape(const char *src, char *dst, size_t dsz) {
    size_t di = 0;
    for (; *src && di + 2 < dsz; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') { dst[di++] = '\\'; dst[di++] = (char)c; }
        else if (c == '\n') { dst[di++] = '\\'; dst[di++] = 'n'; }
        else if (c < 0x20) continue;
        else dst[di++] = (char)c;
    }
    dst[di] = '\0';
}

typedef struct {
    char items[256];
    char r_items[256];
    char nav_side[16];
    int show, r_show, bars, car_up, acc_up, amap_turn, alpha, tank_l, lat_flip, nav_box, nav_tz;
    double trip_base;
} dash_cfg_t;

static void parse_dash_conf(dash_cfg_t *c) {
    memset(c, 0, sizeof(*c));
    strcpy(c->items, "power,torque,g,rpm,oil,gearbox");
    strcpy(c->nav_side, "right");
    c->show = 1; c->r_show = 0; c->bars = 1; c->car_up = 35; c->acc_up = 80;
    c->amap_turn = 3; c->alpha = 200; c->tank_l = 90; c->lat_flip = 0;
    c->nav_box = 1; c->nav_tz = 8;
    c->trip_base = 0.0;
    FILE *fp = fopen(DASH_CONF_PATH, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        char *nl = strpbrk(p, "\r\n");
        if (nl) *nl = '\0';
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = p, *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *ke = eq - 1;
        while (ke >= k && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
        if (strcmp(k, "items") == 0) snprintf(c->items, sizeof(c->items), "%s", v);
        else if (strcmp(k, "r_items") == 0) snprintf(c->r_items, sizeof(c->r_items), "%s", v);
        else if (strcmp(k, "show") == 0) c->show = atoi(v);
        else if (strcmp(k, "r_show") == 0) c->r_show = atoi(v);
        else if (strcmp(k, "bars") == 0) c->bars = atoi(v);
        else if (strcmp(k, "car_up") == 0) c->car_up = atoi(v);
        else if (strcmp(k, "acc_up") == 0) c->acc_up = atoi(v);
        else if (strcmp(k, "amap_turn") == 0) c->amap_turn = atoi(v);
        else if (strcmp(k, "alpha") == 0) c->alpha = atoi(v);
        else if (strcmp(k, "tank_l") == 0) c->tank_l = atoi(v);
        else if (strcmp(k, "lat_flip") == 0) c->lat_flip = atoi(v);
        else if (strcmp(k, "nav_box") == 0) c->nav_box = atoi(v);
        else if (strcmp(k, "nav_side") == 0) strcpy(c->nav_side, (v[0] == 'l' || v[0] == 'L' || v[0] == '0') ? "left" : "right");
        else if (strcmp(k, "nav_tz") == 0) c->nav_tz = atoi(v);
        else if (strcmp(k, "trip_base") == 0) c->trip_base = strtod(v, NULL);
    }
    fclose(fp);
}

static void parse_cl_conf(int *wide_on, int *wide, int *view) {
    *wide_on = 0; *wide = 2688; *view = 85;
    FILE *fp = fopen(CL_CONF_PATH, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "wide=", 5) == 0) { *wide_on = 1; *wide = atoi(p + 5); }
        else if (strncmp(p, "view=", 5) == 0) { *view = atoi(p + 5); }
    }
    fclose(fp);
}

static int count_wifi_networks(void) {
    FILE *fp = fopen(WPA_CONF_PATH, "r");
    if (!fp) return 0;
    char line[256];
    int cnt = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "network={") || strstr(line, "network = {")) cnt++;
    }
    fclose(fp);
    return cnt;
}

static int save_dash_conf(const char *body) {
    if (!file_exists(DASH_CONF_PATH)) return -3;
    char items[256], r_items[256], bufv[64];
    form_get(body, "items", items, sizeof(items));
    form_get(body, "r_items", r_items, sizeof(r_items));
    if (!valid_items_str(items) || !valid_items_str(r_items)) return -2;

    int show = clamp_int(form_get(body, "show", bufv, sizeof(bufv)) ? bufv : "1", 0, 1, 1);
    int r_show = clamp_int(form_get(body, "r_show", bufv, sizeof(bufv)) ? bufv : "0", 0, 1, 0);
    int bars = clamp_int(form_get(body, "bars", bufv, sizeof(bufv)) ? bufv : "1", 0, 1, 1);
    int car_up = clamp_int(form_get(body, "car_up", bufv, sizeof(bufv)) ? bufv : "35", 0, 90, 35);
    int acc_up = clamp_int(form_get(body, "acc_up", bufv, sizeof(bufv)) ? bufv : "80", 0, 90, 80);
    int amap_turn = clamp_int(form_get(body, "amap_turn", bufv, sizeof(bufv)) ? bufv : "3", 0, 3, 3);
    int alpha = clamp_int(form_get(body, "alpha", bufv, sizeof(bufv)) ? bufv : "200", 0, 255, 200);
    int tank_l = clamp_int(form_get(body, "tank_l", bufv, sizeof(bufv)) ? bufv : "90", 0, 200, 90);
    int lat_flip = clamp_int(form_get(body, "lat_flip", bufv, sizeof(bufv)) ? bufv : "0", 0, 1, 0);
    int nav_box = clamp_int(form_get(body, "nav_box", bufv, sizeof(bufv)) ? bufv : "1", 0, 2, 1);
    const char *nav_side = "right";
    if (form_get(body, "nav_side", bufv, sizeof(bufv)) && (bufv[0] == 'l' || bufv[0] == 'L' || bufv[0] == '0')) {
        nav_side = "left";
    }
    int nav_tz = clamp_int(form_get(body, "nav_tz", bufv, sizeof(bufv)) ? bufv : "8", -12, 14, 8);
    double trip_base = 0.0;
    if (form_get(body, "trip_base", bufv, sizeof(bufv)) && bufv[0]) {
        trip_base = strtod(bufv, NULL);
        if (trip_base < 0.0 || trip_base > 9999999.0) trip_base = 0.0;
    }

    const char *keys[15] = {
        "items", "show", "r_items", "r_show", "bars", "car_up", "acc_up",
        "amap_turn", "alpha", "tank_l", "lat_flip", "trip_base", "nav_box", "nav_side", "nav_tz"
    };
    char vals[15][280];
    snprintf(vals[0], sizeof(vals[0]), "%s", items);
    snprintf(vals[1], sizeof(vals[1]), "%d", show);
    snprintf(vals[2], sizeof(vals[2]), "%s", r_items);
    snprintf(vals[3], sizeof(vals[3]), "%d", r_show);
    snprintf(vals[4], sizeof(vals[4]), "%d", bars);
    snprintf(vals[5], sizeof(vals[5]), "%d", car_up);
    snprintf(vals[6], sizeof(vals[6]), "%d", acc_up);
    snprintf(vals[7], sizeof(vals[7]), "%d", amap_turn);
    snprintf(vals[8], sizeof(vals[8]), "%d", alpha);
    snprintf(vals[9], sizeof(vals[9]), "%d", tank_l);
    snprintf(vals[10], sizeof(vals[10]), "%d", lat_flip);
    snprintf(vals[11], sizeof(vals[11]), "%.1f", trip_base);
    snprintf(vals[12], sizeof(vals[12]), "%d", nav_box);
    snprintf(vals[13], sizeof(vals[13]), "%s", nav_side);
    snprintf(vals[14], sizeof(vals[14]), "%d", nav_tz);
    int seen[15];
    memset(seen, 0, sizeof(seen));

    char *out = malloc(65536);
    if (!out) return -1;
    size_t ol = 0;

    FILE *fp = fopen(DASH_CONF_PATH, "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp) && ol + 600 < 65536) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            int matched = -1;
            if (*p != '#' && *p != '\n' && *p != '\r' && *p) {
                char *eq = strchr(p, '=');
                if (eq) {
                    size_t kl = (size_t)(eq - p);
                    while (kl > 0 && (p[kl - 1] == ' ' || p[kl - 1] == '\t')) kl--;
                    for (int i = 0; i < 15; i++) {
                        if (strlen(keys[i]) == kl && memcmp(p, keys[i], kl) == 0) {
                            matched = i;
                            break;
                        }
                    }
                }
            }
            if (matched >= 0) {
                if (!seen[matched]) {
                    ol += (size_t)snprintf(out + ol, 65536 - ol, "%s=%s\n", keys[matched], vals[matched]);
                    seen[matched] = 1;
                }
            } else {
                size_t ll = strlen(line);
                memcpy(out + ol, line, ll);
                ol += ll;
            }
        }
        fclose(fp);
    }
    for (int i = 0; i < 15 && ol + 320 < 65536; i++) {
        if (!seen[i]) ol += (size_t)snprintf(out + ol, 65536 - ol, "%s=%s\n", keys[i], vals[i]);
    }

    mount_app_rw();
    int rc = atomic_write_file(DASH_CONF_PATH, out, ol, 0644);
    mount_app_ro();
    free(out);
    if (rc == 0 && !g_dry_test) {
        (void)system("slay -f cl_render >/dev/null 2>&1");
    }
    return rc;
}

static int save_cl_conf(const char *body) {
    char bufv[64];
    int wide_on = clamp_int(form_get(body, "wide_on", bufv, sizeof(bufv)) ? bufv : "0", 0, 1, 0);
    int wide = clamp_int(form_get(body, "wide", bufv, sizeof(bufv)) ? bufv : "2688", 1920, 2880, 2688);
    int view = clamp_int(form_get(body, "view", bufv, sizeof(bufv)) ? bufv : "85", 60, 100, 85);

    FILE *fp = fopen(CL_CONF_PATH, "r");
    if (!fp) return -3;
    char *out = malloc(65536);
    if (!out) { fclose(fp); return -1; }
    size_t ol = 0;
    int seen_w = 0, seen_v = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp) && ol + 600 < 65536) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "wide=", 5) == 0 || strncmp(p, "#wide=", 6) == 0) {
            if (!seen_w) {
                ol += (size_t)snprintf(out + ol, 65536 - ol, "%swide=%d\n", wide_on ? "" : "#", wide);
                seen_w = 1;
            }
        } else if (strncmp(p, "view=", 5) == 0 || strncmp(p, "#view=", 6) == 0) {
            if (!seen_v) {
                ol += (size_t)snprintf(out + ol, 65536 - ol, "%sview=%d\n", wide_on ? "" : "#", view);
                seen_v = 1;
            }
        } else {
            size_t ll = strlen(line);
            memcpy(out + ol, line, ll);
            ol += ll;
        }
    }
    fclose(fp);
    if (!seen_w) ol += (size_t)snprintf(out + ol, 65536 - ol, "%swide=%d\n", wide_on ? "" : "#", wide);
    if (!seen_v) ol += (size_t)snprintf(out + ol, 65536 - ol, "%sview=%d\n", wide_on ? "" : "#", view);

    mount_app_rw();
    int rc = atomic_write_file(CL_CONF_PATH, out, ol, 0644);
    mount_app_ro();
    free(out);
    return rc;
}

static void bytes_to_hex(const char *s, size_t n, char *hex) {
    for (size_t i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", (unsigned char)s[i]);
    hex[n * 2] = '\0';
}

static int append_wifi_conf(const char *body) {
    char ssid[96], psk[96];
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "psk", psk, sizeof(psk));
    size_t slen = strlen(ssid), plen = strlen(psk);
    if (slen < 1 || slen > 64 || plen < 8 || plen > 63) return -2;
    for (size_t i = 0; i < plen; i++) {
        unsigned char c = (unsigned char)psk[i];
        if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') return -2;
    }
    char hex1[256];
    bytes_to_hex(ssid, slen, hex1);

    char *out = malloc(32768);
    if (!out) return -1;
    size_t ol = 0;
    FILE *fp = fopen(WPA_CONF_PATH, "r");
    if (fp) {
        ol = fread(out, 1, 28000, fp);
        fclose(fp);
    }
    if (ol > 0 && out[ol - 1] != '\n') out[ol++] = '\n';

    mount_app_rw();
    if (ol > 0) {
        char bak[512];
        snprintf(bak, sizeof(bak), "%s.bak", WPA_CONF_PATH);
        (void)atomic_write_file(bak, out, ol, 0600);
    }
    ol += (size_t)snprintf(out + ol, 32768 - ol,
        "\nnetwork={\n    ssid=%s\n    scan_ssid=1\n    psk=\"%s\"\n    key_mgmt=WPA-PSK\n}\n",
        hex1, psk);

    /* 若包含英文直单引号 '，自动补一条 iOS 弯撇号 ’ (\xe2\x80\x99) */
    if (strchr(ssid, '\'') != NULL) {
        char curly[192];
        size_t ci = 0;
        for (size_t i = 0; i < slen && ci + 4 < sizeof(curly); i++) {
            if (ssid[i] == '\'') {
                curly[ci++] = (char)0xe2; curly[ci++] = (char)0x80; curly[ci++] = (char)0x99;
            } else {
                curly[ci++] = ssid[i];
            }
        }
        curly[ci] = '\0';
        char hex2[400];
        bytes_to_hex(curly, ci, hex2);
        ol += (size_t)snprintf(out + ol, 32768 - ol,
            "network={\n    ssid=%s\n    scan_ssid=1\n    psk=\"%s\"\n    key_mgmt=WPA-PSK\n}\n",
            hex2, psk);
    }
    int rc = atomic_write_file(WPA_CONF_PATH, out, ol, 0600);
    mount_app_ro();
    free(out);
    return rc;
}

/* ==================== HTTP Routing ==================== */

static int send_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, p + off, len - off, 0);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static void send_resp(int fd, int code, const char *status, const char *ctype, const void *body, size_t blen) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        code, status, ctype, (unsigned long)blen);
    if (hl > 0 && send_all(fd, hdr, (size_t)hl) == 0 && blen > 0) send_all(fd, body, blen);
}

static void send_json(int fd, int code, int ok, const char *msg) {
    char esc[256], body[384];
    json_escape(msg, esc, sizeof(esc));
    int n = snprintf(body, sizeof(body), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", esc);
    send_resp(fd, code, code == 200 ? "OK" : "Error", "application/json; charset=utf-8", body, (size_t)n);
}

static void send_need_pin(int fd) {
    const char *body = "{\"ok\":false,\"need_pin\":true,\"msg\":\"需要验证管理口令\"}";
    send_resp(fd, 401, "Unauthorized", "application/json; charset=utf-8", body, strlen(body));
}

static const char *USB_MOUNTS[] = {
    "/fs/usb0_0/drive_logs", "/fs/usb1_0/drive_logs", "/fs/sda0/drive_logs", "/fs/sdb0/drive_logs", NULL
};

static const char *SYS_LOG_ALLOW[] = {
    "cl_probe.log", "clusterd.log", "vwcn.log", "vwcn_persist.log", "dio_cluster.log",
    "dash.state", "cluster_cp.state", "cl_render_dbg", "cl_car_dbg", "vw_popup_filter.log",
    "wpa_bcm2.log", "hostapd_bcm0.log", "dhcp_bcm2.log", "touareg_netd.log", "wifi_dev_persist.log", NULL
};

static void handle_status(int fd, uint32_t bcm2_ip) {
    char *j = malloc(32768);
    if (!j) { send_json(fd, 500, 0, "内存不足"); return; }
    size_t l = 0;
    char ipstr[32], esc1[320], esc2[320];
    ip_to_str(bcm2_ip, ipstr, sizeof(ipstr));
    double spd = -1.0, odo = -1.0;
    read_dash_state(&spd, &odo);
    int has_cl = g_dry_test ? 1 : (file_exists(DASH_CONF_PATH) ? 1 : 0);
    int ssh_on = g_dry_test ? 1 : (file_exists(WIFI_DIR "/no_ssh") ? 0 : 1);
    dash_cfg_t dc;
    parse_dash_conf(&dc);
    int wide_on = 0, wide = 2688, view = 85;
    parse_cl_conf(&wide_on, &wide, &view);

    json_escape(dc.items, esc1, sizeof(esc1));
    json_escape(dc.r_items, esc2, sizeof(esc2));

    l += (size_t)snprintf(j + l, 32768 - l,
        "{\"ok\":true,\"bcm2_ip\":\"%s\",\"speed\":%.1f,\"odo\":%.1f,"
        "\"has_cluster_mod\":%d,\"ssh_enabled\":%d,"
        "\"clusterd\":%d,\"cl_render\":%d,\"dio_manager\":%d,\"wifi_count\":%d,"
        "\"dash\":{\"items\":\"%s\",\"show\":%d,\"r_items\":\"%s\",\"r_show\":%d,\"bars\":%d,"
        "\"car_up\":%d,\"acc_up\":%d,\"amap_turn\":%d,\"alpha\":%d,\"tank_l\":%d,\"lat_flip\":%d,\"trip_base\":%.1f,"
        "\"nav_box\":%d,\"nav_side\":\"%s\",\"nav_tz\":%d},"
        "\"cl\":{\"wide_on\":%d,\"wide\":%d,\"view\":%d},"
        "\"toggles\":{\"disable\":%d,\"ka_off\":%d,\"zoom_flip\":%d,\"switch_off\":%d},\"drive_logs\":[",
        ipstr, spd, odo, has_cl, ssh_on,
        proc_alive("pidin -p clusterd 2>/dev/null | grep -q clusterd"),
        proc_alive("pidin -p cl_render 2>/dev/null | grep -q cl_render"),
        proc_alive("pidin -p dio_manager 2>/dev/null | grep -q dio_manager"),
        count_wifi_networks(),
        esc1, dc.show, esc2, dc.r_show, dc.bars,
        dc.car_up, dc.acc_up, dc.amap_turn, dc.alpha, dc.tank_l, dc.lat_flip, dc.trip_base,
        dc.nav_box, dc.nav_side, dc.nav_tz,
        wide_on, wide, view,
        file_exists(CLUSTER_DIR "/disable"),
        file_exists(CLUSTER_DIR "/ka_off"),
        file_exists(CLUSTER_DIR "/zoom_flip"),
        file_exists(CLUSTER_DIR "/switch_off"));

    /* 扫描 U 盘 drive_logs CSV 文件 */
    int first = 1;
    for (int m = 0; USB_MOUNTS[m]; m++) {
        DIR *dir = opendir(USB_MOUNTS[m]);
        if (!dir) continue;
        struct dirent *de;
        int cnt = 0;
        while ((de = readdir(dir)) != NULL && cnt < 40 && l + 256 < 30000) {
            size_t nl = strlen(de->d_name);
            if (nl < 5 || strcmp(de->d_name + nl - 4, ".csv") != 0) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", USB_MOUNTS[m], de->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            char enm[256];
            json_escape(de->d_name, enm, sizeof(enm));
            l += (size_t)snprintf(j + l, 32768 - l, "%s{\"name\":\"%s\",\"size\":%lu}",
                first ? "" : ",", enm, (unsigned long)st.st_size);
            first = 0;
            cnt++;
        }
        closedir(dir);
        if (!first) break;
    }

    l += (size_t)snprintf(j + l, 32768 - l, "],\"sys_logs\":[");
    first = 1;
    for (int i = 0; SYS_LOG_ALLOW[i] && l + 256 < 32700; i++) {
        char full[256];
        snprintf(full, sizeof(full), "/tmp/%s", SYS_LOG_ALLOW[i]);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            l += (size_t)snprintf(j + l, 32768 - l, "%s{\"name\":\"%s\",\"size\":%lu}",
                first ? "" : ",", SYS_LOG_ALLOW[i], (unsigned long)st.st_size);
            first = 0;
        }
    }
    l += (size_t)snprintf(j + l, 32768 - l, "]}");
    send_resp(fd, 200, "OK", "application/json; charset=utf-8", j, l);
    free(j);
}

static int valid_filename(const char *s) {
    if (!s || !s[0] || strlen(s) > 120 || strstr(s, "..")) return 0;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-')) return 0;
    }
    return 1;
}

static void handle_download(int fd, const char *query) {
    char type[32], name[128];
    form_get(query, "type", type, sizeof(type));
    form_get(query, "name", name, sizeof(name));
    if (!valid_filename(name)) { send_json(fd, 400, 0, "非法文件名"); return; }

    char full[512];
    full[0] = '\0';
    if (strcmp(type, "drive") == 0) {
        for (int m = 0; USB_MOUNTS[m]; m++) {
            char cand[512];
            snprintf(cand, sizeof(cand), "%s/%s", USB_MOUNTS[m], name);
            if (file_exists(cand)) { snprintf(full, sizeof(full), "%s", cand); break; }
        }
    } else if (strcmp(type, "log") == 0) {
        for (int i = 0; SYS_LOG_ALLOW[i]; i++) {
            if (strcmp(SYS_LOG_ALLOW[i], name) == 0) {
                snprintf(full, sizeof(full), "/tmp/%s", name);
                break;
            }
        }
    }
    if (!full[0]) { send_json(fd, 404, 0, "文件不存在"); return; }

    struct stat st;
    int ffd = open(full, O_RDONLY);
    if (ffd < 0 || fstat(ffd, &st) != 0) {
        if (ffd >= 0) close(ffd);
        send_json(fd, 404, 0, "无法读取文件");
        return;
    }

    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n",
        name, (unsigned long)st.st_size);
    if (send_all(fd, hdr, (size_t)hl) == 0) {
        char buf[16384];
        ssize_t r;
        while ((r = read(ffd, buf, sizeof(buf))) > 0) {
            if (send_all(fd, buf, (size_t)r) != 0) break;
        }
    }
    close(ffd);
}

static void handle_http_client(int cfd, uint32_t bcm2_ip) {
    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = 30; tv.tv_usec = 0;
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char req[8192];
    size_t got = 0;
    char *hdr_end = NULL;
    while (got + 1 < sizeof(req)) {
        ssize_t r = recv(cfd, req + got, sizeof(req) - 1 - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
        req[got] = '\0';
        hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end) break;
    }
    if (!hdr_end) return;

    size_t hdr_len = (size_t)(hdr_end - req) + 4;
    size_t clen = 0;
    const char *p = req;
    while (p < hdr_end) {
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            clen = (size_t)strtoul(p + 15, NULL, 10);
            break;
        }
        const char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 2;
    }
    if (clen > 4096) { send_json(cfd, 413, 0, "请求过大"); return; }
    while (got < hdr_len + clen && got + 1 < sizeof(req)) {
        ssize_t r = recv(cfd, req + got, hdr_len + clen - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    req[got] = '\0';
    const char *body = req + hdr_len;

    char method[16], target[512];
    if (sscanf(req, "%15s %511s", method, target) != 2) return;
    char *q = strchr(target, '?');
    const char *query = "";
    if (q) { *q = '\0'; query = q + 1; }

    /* 首页外壳免口令下发，其余全部 /api/ 接口强制校验 web.pin */
    if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) &&
        (strcmp(target, "/") == 0 || strcmp(target, "/index.html") == 0)) {
        if (method[0] == 'H') {
            char hdr[256];
            int hl = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                (unsigned long)WEB_UI_HTML_LEN);
            send_all(cfd, hdr, (size_t)hl);
        } else {
            send_resp(cfd, 200, "OK", "text/html; charset=utf-8", WEB_UI_HTML, WEB_UI_HTML_LEN);
        }
        return;
    }

    int auth = check_pin_auth(req, hdr_end, query);
    if (auth == -1) {
        send_json(cfd, 429, 0, "口令连续输错次数过多，请等待 30 秒后再试");
        return;
    }
    if (auth != 1) {
        send_need_pin(cfd);
        return;
    }

    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        if (strcmp(target, "/api/status") == 0) handle_status(cfd, bcm2_ip);
        else if (strcmp(target, "/api/dl") == 0) handle_download(cfd, query);
        else send_json(cfd, 404, 0, "未找到路径");
        return;
    }

    if (strcmp(method, "POST") == 0) {
        char xrw[64];
        header_get(req, hdr_end, "X-Requested-With", xrw, sizeof(xrw));
        if (strcmp(xrw, "touareg-webui") != 0) {
            send_json(cfd, 403, 0, "非法跨站请求（缺少安全头）");
            return;
        }
        if (is_driving_now()) {
            send_json(cfd, 403, 0, "车辆行驶中，请停车后再修改车机设置");
            return;
        }
        if (strcmp(target, "/api/dash") == 0) {
            int rc = save_dash_conf(body);
            if (rc == 0) send_json(cfd, 200, 1, "已保存 dash.conf 并刷新仪表");
            else if (rc == -2) send_json(cfd, 400, 0, "参数格式不合法");
            else if (rc == -3) send_json(cfd, 404, 0, "当前车机未安装途锐仪表信息框模块");
            else send_json(cfd, 500, 0, "写入 dash.conf 失败");
        } else if (strcmp(target, "/api/cl") == 0) {
            int rc = save_cl_conf(body);
            if (rc == 0) send_json(cfd, 200, 1, "已保存 cl.conf，请点「重连 CarPlay」生效");
            else if (rc == -3) send_json(cfd, 404, 0, "未检测到 cl.conf");
            else send_json(cfd, 500, 0, "写入 cl.conf 失败");
        } else if (strcmp(target, "/api/toggle") == 0) {
            char name[32], val[8];
            form_get(body, "name", name, sizeof(name));
            form_get(body, "val", val, sizeof(val));
            if (strcmp(name, "disable") != 0 && strcmp(name, "ka_off") != 0 &&
                strcmp(name, "zoom_flip") != 0 && strcmp(name, "switch_off") != 0) {
                send_json(cfd, 400, 0, "不支持的开关");
                return;
            }
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", CLUSTER_DIR, name);
            mount_app_rw();
            int ok = 0;
            if (atoi(val)) {
                int tf = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (tf >= 0) { close(tf); ok = 1; }
            } else {
                ok = (unlink(path) == 0 || errno == ENOENT);
            }
            mount_app_ro();
            send_json(cfd, ok ? 200 : 500, ok, ok ? "开关已更新" : "开关写入失败");
        } else if (strcmp(target, "/api/action") == 0) {
            char act[32];
            form_get(body, "act", act, sizeof(act));
            if (strcmp(act, "toggle_map") == 0) {
                if (!g_dry_test) {
                    int tf = open("/tmp/vwcn_toggle", O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (tf >= 0) close(tf);
                }
                send_json(cfd, 200, 1, "已触发仪表地图切换（CarPlay ↔ 原车）");
            } else if (strcmp(act, "restart_render") == 0) {
                if (!g_dry_test) (void)system("slay -f cl_render >/dev/null 2>&1");
                send_json(cfd, 200, 1, "已刷新仪表渲染进程");
            } else if (strcmp(act, "restart_carplay") == 0) {
                if (!g_dry_test) (void)system("slay -f dio_manager >/dev/null 2>&1");
                send_json(cfd, 200, 1, "已重启 CarPlay 进程，几秒后自动重连");
            } else if (strcmp(act, "reboot_hu") == 0) {
                send_json(cfd, 200, 1, "正在软重启车机...");
                if (!g_dry_test) {
                    sync();
                    int ofd = open("/dev/ooc/system", O_WRONLY);
                    if (ofd >= 0) { (void)write(ofd, "gem-reset\n", 10); close(ofd); }
                }
            } else send_json(cfd, 400, 0, "未知操作");
        } else if (strcmp(target, "/api/wifi") == 0) {
            int rc = append_wifi_conf(body);
            if (rc == 0) send_json(cfd, 200, 1, "热点已追加并备份原配置，下次切换/重启后生效");
            else if (rc == -2) send_json(cfd, 400, 0, "热点名或密码格式不合法（密码需 8~63 位可见 ASCII 且不含引号）");
            else send_json(cfd, 500, 0, "写入 wpa.conf 失败");
        } else send_json(cfd, 404, 0, "未知接口");
        return;
    }
    send_json(cfd, 405, 0, "Method Not Allowed");
}

static int open_http_sock(uint32_t bind_ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = bind_ip;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        nlog("http bind port %d failed: errno=%d", port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);

    const char *env_if = getenv("NETD_IFACE");
    if (env_if && env_if[0]) g_iface = env_if;
    const char *env_port = getenv("NETD_PORT");
    if (env_port && atoi(env_port) > 0) g_http_port = atoi(env_port);
    if (getenv("NETD_DRY_TEST")) g_dry_test = 1;

    nlog("touareg_netd v21.0 started (iface=%s port=%d)", g_iface, g_http_port);

    uint32_t cur_ip = 0;
    int http_fd = -1, mdns_fd = -1;
    time_t last_check = 0, last_ann = 0;

    while (!g_stop) {
        time_t now = time(NULL);
        if (now - last_check >= 3) {
            last_check = now;
            uint32_t ip = get_iface_ipv4(g_iface);
            if (ip != cur_ip) {
                char s_old[32], s_new[32];
                ip_to_str(cur_ip, s_old, sizeof(s_old));
                ip_to_str(ip, s_new, sizeof(s_new));
                nlog("%s ip changed: %s -> %s", g_iface, s_old, s_new);
                if (http_fd >= 0) { close(http_fd); http_fd = -1; }
                if (mdns_fd >= 0) { close(mdns_fd); mdns_fd = -1; }
                cur_ip = ip;
                if (cur_ip != 0) {
                    http_fd = open_http_sock(cur_ip, g_http_port);
                    mdns_fd = open_mdns_sock(cur_ip);
                    if (http_fd >= 0) nlog("http listening on %s:%d", s_new, g_http_port);
                    if (mdns_fd >= 0) {
                        send_mdns_reply(mdns_fd, cur_ip, 0, NULL);
                        last_ann = now;
                        nlog("mdns advertising %s.local -> %s", MDNS_HOST_LABEL, s_new);
                    }
                }
            } else if (cur_ip != 0 && mdns_fd >= 0 && now - last_ann >= 60) {
                send_mdns_reply(mdns_fd, cur_ip, 0, NULL);
                last_ann = now;
            }
        }

        struct pollfd pfds[2];
        int nfds = 0, idx_http = -1, idx_mdns = -1;
        if (http_fd >= 0) { idx_http = nfds; pfds[nfds].fd = http_fd; pfds[nfds].events = POLLIN; nfds++; }
        if (mdns_fd >= 0) { idx_mdns = nfds; pfds[nfds].fd = mdns_fd; pfds[nfds].events = POLLIN; nfds++; }

        if (nfds == 0) { sleep(2); continue; }
        int r = poll(pfds, (nfds_t)nfds, 1000);
        if (r <= 0) continue;

        if (idx_mdns >= 0 && (pfds[idx_mdns].revents & POLLIN)) handle_mdns_pkt(mdns_fd, cur_ip);
        if (idx_http >= 0 && (pfds[idx_http].revents & POLLIN)) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int cfd = accept(http_fd, (struct sockaddr *)&peer, &plen);
            if (cfd >= 0) {
                handle_http_client(cfd, cur_ip);
                close(cfd);
            }
        }
    }

    if (http_fd >= 0) close(http_fd);
    if (mdns_fd >= 0) close(mdns_fd);
    return 0;
}
