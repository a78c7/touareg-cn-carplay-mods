// axbridge: userspace driver for ASIX AX88772(A/B/C) USB Ethernet on macOS.
// USB (libusb) <-> utun. Answers ARP itself. Init sequence follows Linux drivers/net/usb/asix_devices.c
// build: clang -O2 -o axbridge axbridge.c -I$(brew --prefix)/include/libusb-1.0 -L$(brew --prefix)/lib -lusb-1.0
// run:   sudo ./axbridge            (Ctrl+C to stop; everything is undone)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libusb.h>

static const char *LOCAL_IP = "172.16.250.250";
static const char *PEER_IP  = "172.16.250.248";
static const char *NET      = "172.16.250.0/24";

static libusb_device_handle *h;
static unsigned char ep_in, ep_out;
static int tunfd = -1, phy = 0x10, verbose = 0;
static char tunname[32];
static uint8_t mymac[6];
static uint32_t myip;              // network order
static pthread_mutex_t outlk = PTHREAD_MUTEX_INITIALIZER;
static volatile int running = 1;
static unsigned long rxp, txp, arpreq;

struct arpent { uint32_t ip; uint8_t mac[6]; int valid; };
static struct arpent arpt[64];
static pthread_mutex_t arplk = PTHREAD_MUTEX_INITIALIZER;

static int vw(uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len) {
    int r = libusb_control_transfer(h, 0x40, req, val, idx, d, len, 1000);
    if (r < 0) fprintf(stderr, "ctrl write 0x%02x failed: %s\n", req, libusb_error_name(r));
    return r;
}
static int vr(uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len) {
    int r = libusb_control_transfer(h, 0xC0, req, val, idx, d, len, 1000);
    if (r < 0) fprintf(stderr, "ctrl read 0x%02x failed: %s\n", req, libusb_error_name(r));
    return r;
}
static void mdio_w(int reg, uint16_t v) {
    uint8_t d[2] = { v & 0xff, v >> 8 };
    vw(0x06, 0, 0, NULL, 0); vw(0x08, phy, reg, d, 2); vw(0x0a, 0, 0, NULL, 0);
}
static int mdio_r(int reg) {
    uint8_t d[2] = {0, 0};
    vw(0x06, 0, 0, NULL, 0); int r = vr(0x07, phy, reg, d, 2); vw(0x0a, 0, 0, NULL, 0);
    return r < 0 ? -1 : (d[0] | d[1] << 8);
}

static void arp_learn(uint32_t ip, const uint8_t *mac) {
    if (!ip) return;
    pthread_mutex_lock(&arplk);
    int free_i = -1;
    for (int i = 0; i < 64; i++) {
        if (arpt[i].valid && arpt[i].ip == ip) { memcpy(arpt[i].mac, mac, 6); pthread_mutex_unlock(&arplk); return; }
        if (!arpt[i].valid && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;
    arpt[free_i].ip = ip; memcpy(arpt[free_i].mac, mac, 6); arpt[free_i].valid = 1;
    struct in_addr a = { ip };
    printf("[arp] %s is %02x:%02x:%02x:%02x:%02x:%02x\n", inet_ntoa(a), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    fflush(stdout);
    pthread_mutex_unlock(&arplk);
}
static int arp_find(uint32_t ip, uint8_t *mac) {
    int ok = 0;
    pthread_mutex_lock(&arplk);
    for (int i = 0; i < 64; i++) if (arpt[i].valid && arpt[i].ip == ip) { memcpy(mac, arpt[i].mac, 6); ok = 1; break; }
    pthread_mutex_unlock(&arplk);
    return ok;
}

static void send_eth(const uint8_t *f, int len) {
    uint8_t buf[2048];
    if (len < 60) { memcpy(buf + 4, f, len); memset(buf + 4 + len, 0, 60 - len); len = 60; }
    else memcpy(buf + 4, f, len);
    uint32_t hdr = (uint32_t)len | ((uint32_t)(~len & 0xffff) << 16);
    buf[0] = hdr; buf[1] = hdr >> 8; buf[2] = hdr >> 16; buf[3] = hdr >> 24;
    int tot = len + 4;
    if (tot % 512 == 0) { buf[tot] = 0; buf[tot+1] = 0; buf[tot+2] = 0xff; buf[tot+3] = 0xff; tot += 4; }
    int n = 0;
    pthread_mutex_lock(&outlk);
    int r = libusb_bulk_transfer(h, ep_out, buf, tot, &n, 1000);
    pthread_mutex_unlock(&outlk);
    if (r < 0) fprintf(stderr, "bulk out: %s\n", libusb_error_name(r)); else txp++;
}

static void send_arp(int op, const uint8_t *tha, uint32_t tpa) {
    uint8_t f[42];
    if (op == 1) memset(f, 0xff, 6); else memcpy(f, tha, 6);
    memcpy(f + 6, mymac, 6); f[12] = 0x08; f[13] = 0x06;
    f[14] = 0; f[15] = 1; f[16] = 0x08; f[17] = 0; f[18] = 6; f[19] = 4; f[20] = 0; f[21] = op;
    memcpy(f + 22, mymac, 6); memcpy(f + 28, &myip, 4);
    if (op == 1) memset(f + 32, 0, 6); else memcpy(f + 32, tha, 6);
    memcpy(f + 38, &tpa, 4);
    send_eth(f, 42);
}

static void handle_rx(const uint8_t *f, int len) {
    if (len < 14) return;
    uint16_t et = f[12] << 8 | f[13];
    if (verbose) printf("[rx] %d bytes type %04x\n", len, et);
    if (et == 0x0806 && len >= 42) {
        uint32_t spa, tpa; memcpy(&spa, f + 28, 4); memcpy(&tpa, f + 38, 4);
        arp_learn(spa, f + 22);
        if (f[21] == 1 && tpa == myip) send_arp(2, f + 22, spa);
    } else if (et == 0x0800 && len >= 34) {
        uint32_t src; memcpy(&src, f + 26, 4);
        arp_learn(src, f + 6);
        int tl = f[16] << 8 | f[17];
        if (tl > len - 14 || tl < 20) tl = len - 14;
        uint8_t out[2048]; uint32_t af = htonl(AF_INET);
        memcpy(out, &af, 4); memcpy(out + 4, f + 14, tl);
        if (write(tunfd, out, tl + 4) < 0) perror("tun write"); else rxp++;
    }
}

static void *rx_thread(void *a) {
    static uint8_t buf[16384];
    while (running) {
        int n = 0;
        int r = libusb_bulk_transfer(h, ep_in, buf, sizeof buf, &n, 500);
        if (r == LIBUSB_ERROR_TIMEOUT && n == 0) continue;
        if (r < 0 && r != LIBUSB_ERROR_TIMEOUT) { fprintf(stderr, "bulk in: %s\n", libusb_error_name(r)); usleep(200000); continue; }
        int off = 0;
        while (off + 4 <= n) {
            uint32_t hdr = buf[off] | buf[off+1] << 8 | buf[off+2] << 16 | (uint32_t)buf[off+3] << 24;
            int l = hdr & 0x7ff;
            if (((hdr ^ (hdr >> 16)) & 0x7ff) != 0x7ff) { if (verbose) fprintf(stderr, "bad rx hdr %08x\n", hdr); break; }
            off += 4;
            if (off + l > n) break;
            handle_rx(buf + off, l);
            off += (l + 1) & ~1;
        }
    }
    return NULL;
}

static void *tun_thread(void *a) {
    uint8_t buf[2048], f[2048];
    while (running) {
        int n = read(tunfd, buf, sizeof buf);
        if (n <= 4) continue;
        uint32_t af; memcpy(&af, buf, 4);
        if (ntohl(af) != AF_INET) continue;
        uint8_t *ip = buf + 4; int iplen = n - 4;
        uint32_t dst; memcpy(&dst, ip + 16, 4);
        uint8_t mac[6];
        if (!arp_find(dst, mac)) { send_arp(1, NULL, dst); arpreq++; usleep(50000); if (!arp_find(dst, mac)) continue; }
        memcpy(f, mac, 6); memcpy(f + 6, mymac, 6); f[12] = 0x08; f[13] = 0x00;
        memcpy(f + 14, ip, iplen);
        send_eth(f, iplen + 14);
    }
    return NULL;
}

static int open_utun(void) {
    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) { perror("utun socket"); return -1; }
    struct ctl_info ci; memset(&ci, 0, sizeof ci);
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof ci.ctl_name);
    if (ioctl(fd, CTLIOCGINFO, &ci) < 0) { perror("CTLIOCGINFO"); return -1; }
    struct sockaddr_ctl sc; memset(&sc, 0, sizeof sc);
    sc.sc_len = sizeof sc; sc.sc_family = AF_SYSTEM; sc.ss_sysaddr = AF_SYS_CONTROL; sc.sc_id = ci.ctl_id; sc.sc_unit = 0;
    if (connect(fd, (struct sockaddr *)&sc, sizeof sc) < 0) { perror("utun connect (need sudo)"); return -1; }
    socklen_t nl = sizeof tunname;
    getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, tunname, &nl);
    return fd;
}

static void on_sig(int s) { running = 0; }

static const uint16_t ids[][2] = {
    {0x2001, 0x1a02}, {0x2001, 0x3c05}, {0x2001, 0x1a00}, {0x0b95, 0x772b}, {0x0b95, 0x7720},
    {0x0b95, 0x772a}, {0x0b95, 0x1720}, {0x13b1, 0x0018}, {0x1557, 0x7720}, {0x07d1, 0x3c05},
};

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    inet_pton(AF_INET, LOCAL_IP, &myip);
    if (libusb_init(NULL) < 0) { fprintf(stderr, "libusb_init failed\n"); return 1; }
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0] && !h; i++) {
        h = libusb_open_device_with_vid_pid(NULL, ids[i][0], ids[i][1]);
        if (h) printf("found device %04x:%04x\n", ids[i][0], ids[i][1]);
    }
    if (!h) { fprintf(stderr, "AX88772 device not found (or no permission)\n"); return 1; }
    libusb_set_auto_detach_kernel_driver(h, 1);
    int r = libusb_claim_interface(h, 0);
    if (r < 0) { fprintf(stderr, "claim interface: %s\n", libusb_error_name(r)); return 1; }

    struct libusb_config_descriptor *cfg;
    libusb_get_active_config_descriptor(libusb_get_device(h), &cfg);
    const struct libusb_interface_descriptor *id = &cfg->interface[0].altsetting[0];
    for (int i = 0; i < id->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *e = &id->endpoint[i];
        if ((e->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK) {
            if (e->bEndpointAddress & 0x80) ep_in = e->bEndpointAddress; else ep_out = e->bEndpointAddress;
        }
    }
    libusb_free_config_descriptor(cfg);
    printf("endpoints in=0x%02x out=0x%02x\n", ep_in, ep_out);
    if (!ep_in || !ep_out) { fprintf(stderr, "bulk endpoints not found\n"); return 1; }

    // ---- chip init (asix_devices.c ax88772_hw_reset) ----
    uint8_t b[8] = {0};
    vw(0x1f, 0xB0, 0, NULL, 0); usleep(5000);                 // GPIO: RSE|GPO2|GPO2EN
    vr(0x19, 0, 0, b, 2); phy = b[1] & 0x1f;                   // internal PHY addr
    int embd = (phy == 0x10);
    printf("phy addr 0x%02x (embedded=%d)\n", phy, embd);
    vw(0x22, embd ? 1 : 0, 0, NULL, 0);                        // SW_PHY_SELECT
    vw(0x20, 0x48, 0, NULL, 0); usleep(150000);                // IPPD|PRL
    vw(0x20, 0x00, 0, NULL, 0); usleep(150000);
    vw(0x20, embd ? 0x20 : 0x04, 0, NULL, 0); usleep(150000);  // IPRL / PRTE
    vw(0x10, 0, 0, NULL, 0);                                   // RX off
    vr(0x13, 0, 0, mymac, 6);
    printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n", mymac[0], mymac[1], mymac[2], mymac[3], mymac[4], mymac[5]);
    mdio_w(0, 0x8000); usleep(100000);                         // BMCR reset
    mdio_w(4, 0x01E1);                                         // advertise all + CSMA
    mdio_w(0, 0x1200);                                         // autoneg enable+restart
    vw(0x1b, 0x0336, 0, NULL, 0);                              // medium: 100 FD
    vw(0x12, 0x0c15, 0x12, NULL, 0);                           // IPG
    vw(0x10, 0x0088, 0, NULL, 0);                              // RX on: SO|AB

    tunfd = open_utun();
    if (tunfd < 0) return 1;
    char cmd[256];
    snprintf(cmd, sizeof cmd, "ifconfig %s inet %s %s up", tunname, LOCAL_IP, PEER_IP); system(cmd);
    snprintf(cmd, sizeof cmd, "route -q -n delete -net %s >/dev/null 2>&1; route -q -n add -net %s -interface %s", NET, NET, tunname); system(cmd);
    printf("interface %s up: %s -> %s (route %s)\n", tunname, LOCAL_IP, PEER_IP, NET);

    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    pthread_t t1, t2;
    pthread_create(&t1, NULL, rx_thread, NULL);
    pthread_create(&t2, NULL, tun_thread, NULL);

    int lastlink = -1, tick = 0; uint32_t peer; inet_pton(AF_INET, PEER_IP, &peer);
    while (running) {
        int bmsr = mdio_r(1); bmsr = mdio_r(1);
        int link = bmsr >= 0 && (bmsr & 4);
        if (link != lastlink) {
            if (link) {
                int lpa = mdio_r(5) & mdio_r(4);
                int s100 = (lpa & 0x180) != 0, fd = (lpa & 0x100) || (!(lpa & 0x80) && (lpa & 0x40));
                uint16_t med = 0x0134 | (fd ? 0x02 : 0) | (s100 ? 0x200 : 0);
                vw(0x1b, med, 0, NULL, 0);
                printf("LINK UP %s %s\n", s100 ? "100M" : "10M", fd ? "full" : "half");
                send_arp(1, NULL, peer);
            } else printf("link down (cable not connected / car unit off?)\n");
            lastlink = link;
        }
        if (++tick % 10 == 0) printf("stats: rx %lu  tx %lu  arp-req %lu\n", rxp, txp, arpreq);
        sleep(1);
    }
    printf("stopping...\n");
    snprintf(cmd, sizeof cmd, "route -q -n delete -net %s >/dev/null 2>&1", NET); system(cmd);
    close(tunfd);
    vw(0x10, 0, 0, NULL, 0);
    libusb_release_interface(h, 0); libusb_close(h); libusb_exit(NULL);
    return 0;
}
