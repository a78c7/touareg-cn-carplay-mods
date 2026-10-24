/* clusterd: run the cluster renderer only while the phone streams the cluster video.
 * Watches flags bit0 of /cluster_h264_shm (set by cl_alt while stream 111 is live).
 * usage: clusterd <renderer path> [renderer args...] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "aeabi_shim.h"

typedef struct { volatile uint32_t magic, write_pos, total_bytes, flags; } shm_head_t;

static void logm(const char *m, int v) {
    FILE *f = fopen("/tmp/clusterd.log", "a");
    if (f) { fprintf(f, "[%ld] %s %d\n", (long)time(NULL), m, v); fclose(f); }
}
/* v15: every request gets a ";#<n>" suffix so VWClusterNav sees a new string even when two requests land in
 * the same second or before it started (it ignores '#' groups). */
static void rebind(const char *seq) {
    static unsigned n; char b[160];
    int l = snprintf(b, sizeof b, "%s;#%ld-%u", seq, (long)time(NULL), ++n);
    int fd = open("/tmp/vwcn_rebind", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { write(fd, b, l); close(fd); chmod("/tmp/vwcn_rebind", 0666); }
}
static void send_cmd(const char *c) {
    int fd = open("/tmp/cl_alt_cmd", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { write(fd, c, strlen(c)); close(fd); chmod("/tmp/cl_alt_cmd", 0666); }
}

/* v15 auto view area: rules in cl.conf ("rule=<app name part or *>,<1 navigating|0 not|*>,<area index>",
 * first match wins, else "va="). The app comes from source_name in /tmp/cluster_cp.state (e.g. 高德地图, 地图, 百度地图). */
static int read_file(const char *p, char *b, int cap) {
    int fd = open(p, O_RDONLY); if (fd < 0) return -1;
    int n = read(fd, b, cap - 1); close(fd);
    if (n < 0) n = 0; b[n] = 0; return n;
}
/* iAP2 ManeuverType of the next maneuver: -1 left-hand, 1 right-hand, 0 other (AMap shifts the car ~210 px
 * toward the side of the next turn while navigating; "turn=<app>,<area if left>,<area if right>" compensates) */
static int turn_side(int t) {
    switch (t) { case 1: case 4: case 13: case 20: case 22: case 24: return -1;
                 case 2: case 14: case 21: case 23: case 25: return 1; }
    return 0;
}
static void logs(const char *m) {
    FILE *f = fopen("/tmp/clusterd.log", "a");
    if (f) { fprintf(f, "[%ld] %s\n", (long)time(NULL), m); fclose(f); }
}
/* v19.3: AMap's turn shift is not reliable after a reconnect. Seen on the car (same spot, same right turn, same
 * area 14): when CarPlay reconnected while a guidance was already running, AMap sometimes kept the car centred
 * (no shift toward the turn), so the compensation pushed it ~210 px off; after the guidance was restarted on the
 * phone AMap shifted again and the compensation was right. Both cases send the same data to the head unit.
 * So the compensation applies only to a guidance that started during the current CarPlay session (app name
 * received, or guidance went from off to on more than 10 s after the session started); a guidance that was
 * already running when CarPlay connected gets none until a new guidance starts. Maneuver changes in that state
 * are logged to learn whether AMap starts shifting again by itself. */
static time_t g_sess_t;          /* start of the current cluster stream (set by main) */
static int g_fresh = -1;         /* -1 no guidance yet, 0 guidance was already running at connect, 1 started here */
static int g_prev_nav;
static char g_last_road[96];
static int pick_va(char *app, int appcap, int *nav) {
    char st[4096], cf[2048];
    int side = 0, lanes = 0;
    app[0] = 0; *nav = 0;
    if (read_file("/tmp/cluster_cp.state", st, sizeof st) > 0) {
        int sess = strstr(st, "session_active:b:true") != NULL, rs = -1, rg = -1;
        char *p = strstr(st, "\nroute_state:n:"); if (p) rs = atoi(p + 15);
        p = strstr(st, "\nsource_supports_rg:n:"); if (p) rg = atoi(p + 22);
        *nav = sess && rs >= 1 && rs != 2 && rg != 0;
        p = strstr(st, "\nslot_0_type:n:"); if (p) side = turn_side(atoi(p + 15));
        lanes = strstr(st, "\nlane_active:b:true") || strstr(st, "\nlane_active:n:1");
        p = strstr(st, "\nsource_name:s:");
        if (p) { p += 15; int i = 0; while (p[i] && p[i] != '\n' && i < appcap - 1) { app[i] = p[i]; i++; } app[i] = 0; }
        p = strstr(st, "\nslot_0_road:s:");
        if (p && g_fresh == 0 && *nav) {
            char r[96]; int i = 0; p += 15;
            while (p[i] && p[i] != '\n' && i < (int)sizeof r - 1) { r[i] = p[i]; i++; } r[i] = 0;
            if (strcmp(r, g_last_road)) {
                char m[160]; snprintf(m, sizeof m, "resumed guidance: next maneuver now \"%s\" side %d", r, side);
                if (g_last_road[0]) logs(m);
                strcpy(g_last_road, r);
            }
        }
    }
    if (!*nav) {
        if (g_fresh != -1) logs("guidance ended");
        g_fresh = -1; g_last_road[0] = 0;
    } else if (g_fresh != 1 && (app[0] || (g_fresh == -1 && !g_prev_nav && time(NULL) - g_sess_t > 10))) {
        g_fresh = 1; logs(app[0] ? "guidance started in this session (app name received): turn compensation allowed" : "guidance started in this session: turn compensation allowed");
    } else if (g_fresh == -1) {
        g_fresh = 0; logs("guidance was already running when CarPlay connected: no turn compensation until a new guidance starts");
    }
    g_prev_nav = *nav;
    /* the phone sends the app name only at the start of a guidance session: keep the last one we saw.
     * v19.2: also across head-unit restarts (/mnt/ota survives them, /tmp does not): after a restart CarPlay
     * reconnects and a guidance that was already running continues without a name, so the app rules (and the AMap
     * turn compensation) did not apply until the guidance was restarted. Written only when the app changes. */
    static char last_app[64];
    if (!last_app[0] && read_file("/tmp/cl_last_app", last_app, sizeof last_app) < 0)
        read_file("/mnt/ota/modkit/cl_last_app", last_app, sizeof last_app);
    if (app[0] && strcmp(app, last_app)) {
        strncpy(last_app, app, sizeof last_app - 1);
        int fd = open("/tmp/cl_last_app", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) { write(fd, last_app, strlen(last_app)); close(fd); }
        fd = open("/mnt/ota/modkit/cl_last_app", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { write(fd, last_app, strlen(last_app)); close(fd); }
    }
    else if (last_app[0]) { strncpy(app, last_app, appcap - 1); app[appcap - 1] = 0; }
    if (read_file("/tmp/cl.conf", cf, sizeof cf) < 0 && read_file("/mnt/app/eso/bin/apps/cluster/cl.conf", cf, sizeof cf) < 0) return -1;
    int def = -1;
    /* v19.2: amap_turn in dash.conf (改装设置): 0 off, 1 on, 2 auto (default). AMap lane-level guidance does not move the
     * car toward the next turn, so the compensation would push it off-centre. The phone does not tell us that setting;
     * the clue is lane guidance data: two cars so far, the AMap that sends lanes is the one that does not shift the car.
     * Auto: once lanes were seen during the current guidance, compensation stays off until guidance ends. */
    char dc[8192];
    int mode = 2;
    if (read_file("/mnt/app/eso/bin/apps/cluster/dash.conf", dc, sizeof dc) > 0) {
        char *m = strstr(dc, "\namap_turn="); if (m) mode = atoi(m + 11);
    }
    static int lanes_seen = 0;
    if (!*nav) lanes_seen = 0;
    else if (lanes && !lanes_seen) { lanes_seen = 1; FILE *lf = fopen("/tmp/clusterd.log", "a"); if (lf) { fprintf(lf, "[%ld] lane data in this guidance (amap_turn=%d): turn compensation %s\n", (long)time(NULL), mode, mode == 2 ? "off" : "unchanged"); fclose(lf); } }
    int turn_on = mode == 1 || (mode == 2 && !lanes_seen && g_fresh == 1);
    for (char *l = strtok(cf, "\n"); l; l = strtok(NULL, "\n")) {
        while (*l == ' ') l++;
        if (!strncmp(l, "va=", 3)) { def = atoi(l + 3); continue; }
        /* v19.4: "turn=<app>,<left>,<right>[,<other>]". Seen on the car: with a straight-on next maneuver (type 3,
         * 6.6 km ahead) AMap also kept the car ~210 px right of the centre (area 0: car at x 1170; area 14: x 960),
         * so maneuvers that are neither left nor right use <other>, default the right-turn area. */
        if (!strncmp(l, "turn=", 5) && *nav && turn_on) {
            char *a = l + 5, *c1 = strchr(a, ','); if (!c1) continue;
            char *c2 = strchr(c1 + 1, ','); if (!c2) continue;
            char *c3 = strchr(c2 + 1, ',');
            *c1 = 0;
            if (app[0] && strstr(app, a)) return side < 0 ? atoi(c1 + 1) : side > 0 ? atoi(c2 + 1) : atoi(c3 ? c3 + 1 : c2 + 1);
            continue;
        }
        if (strncmp(l, "rule=", 5)) continue;
        char *a = l + 5, *c1 = strchr(a, ','); if (!c1) continue;
        char *c2 = strchr(c1 + 1, ','); if (!c2) continue;
        *c1 = 0;
        int nv = (c1[1] == '*') ? -1 : atoi(c1 + 1);
        int ok_app = !strcmp(a, "*") || (app[0] && strstr(app, a));
        if (ok_app && (nv < 0 || nv == *nav)) return atoi(c2 + 1);
    }
    return def;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    signal(SIGHUP, SIG_IGN);
    shm_head_t *s = NULL;
    pid_t child = 0;
    uint32_t last_total = 0; int stall = 0;
    int inactive = 0, zoom_n = getenv("CL_ZOOM") ? atoi(getenv("CL_ZOOM")) : 3;
    time_t started = 0, last_auto = 0; int auto_va = -1; char app[64];
    /* v19.3: the map sometimes stayed hidden (stock map on the cluster although the list was [58,33]), after boot
     * and after CarPlay reconnects; restarting the renderer fixed it. Cause, from the logs: the renderer creates
     * its window only when the first key frame arrives, and the cluster list was set at a fixed 2 s after start.
     * Every hidden case had the list set before the window existed (boot: list 34.618 s, key frame 34.313 s;
     * reconnect: list 1762.205 s, key frame 1762.257 s); every good case had the key frame ~0.5-1 s earlier.
     * displaymanager resolves the ids when the list is set, and re-setting the same list does not pick up a
     * window created later. So the list is now set only after the renderer reports its window
     * (/tmp/cl_render_ready = its pid); a renderer that never reports within 12 s is restarted. */
    int new_stream = 1, stage = 0, zoomed = 0;
    time_t shown_t = 0;
    const char *did = getenv("CL_DISPID");
    if (!did || !*did) { setenv("CL_DISPID", "58", 1); did = "58"; }
    char show_seq[64]; snprintf(show_seq, sizeof show_seq, "hold%s,33;rate10;st", did);   /* ours on top, native 33 kept underneath */
    const char *back_seq = getenv("CL_BACKSEQ") ? getenv("CL_BACKSEQ") : "unhold;33;vis1;rate10;st";
    logm("clusterd start", getpid());
    for (;;) {
        if (!s) {
            int fd = shm_open("/cluster_h264_shm", O_RDONLY, 0);
            if (fd >= 0) {
                void *p = mmap(NULL, sizeof(shm_head_t), PROT_READ, MAP_SHARED, fd, 0);
                close(fd);
                if (p != MAP_FAILED) { s = (shm_head_t *)p; logm("shm attached", 0); }
            }
        }
        int active = s && s->magic == 0x48323634u && (s->flags & 1);
        /* fallback: flag stuck (dio_manager died) -> treat >3 s without new bytes as inactive */
        if (s) { uint32_t t = s->total_bytes; if (t == last_total) stall++; else { stall = 0; last_total = t; } }
        if (active && stall >= 6) active = 0;
        if (child > 0 && waitpid(child, NULL, WNOHANG) == child) { logm("renderer exited", child); child = 0; }
        if (active) {
            inactive = 0;
            if (child <= 0) {
                child = fork();
                if (child == 0) { execv(argv[1], argv + 1); _exit(127); }
                started = time(NULL); stage = 0;
                if (new_stream) { new_stream = 0; zoomed = 0; g_sess_t = started; g_fresh = -1; g_prev_nav = 0; g_last_road[0] = 0; }
                logm("renderer started", child);
            } else if (stage == 0 && time(NULL) - started >= 2) {
                send_cmd("idr");              /* get a fresh key frame: the renderer creates its window on it */
                stage = 1;
                logm("idr requested", 0);
            } else if (stage == 1) {
                char rb[32]; int ready = read_file("/tmp/cl_render_ready", rb, sizeof rb) > 0 && atoi(rb) == child;
                if (ready) {
                    if (strcmp(did, "33") != 0) rebind(show_seq);   /* show our own displayable on the cluster */
                    stage = 2; shown_t = time(NULL);
                    logm("renderer window ready, cluster list set (s after start)", (int)(shown_t - started));
                } else if (time(NULL) - started >= 12) {
                    kill(child, SIGTERM); usleep(500000); kill(child, SIGKILL);
                    waitpid(child, NULL, 0); logm("renderer window not ready after 12 s, restarting", child); child = 0;
                }
            } else if (stage == 2 && time(NULL) - shown_t >= 2) {
                /* the phone keeps its zoom for the whole connection: zoom in once per connection, not per renderer */
                if (zoom_n > 0 && !zoomed) { char b[32]; snprintf(b, sizeof b, "zoomin %d", zoom_n); send_cmd(b); }
                /* set the list once more now that frames are being posted (cheap; the same list, no blink) */
                if (strcmp(did, "33") != 0) rebind(show_seq);
                logm(zoomed ? "zoom kept (same connection)" : "zoom requested", zoom_n);
                zoomed = 1; stage = 3;
                auto_va = -1; last_auto = time(NULL);
            } else if (stage == 3 && time(NULL) - last_auto >= 1) {
                int nav, v = pick_va(app, sizeof app, &nav);
                last_auto = time(NULL);
                if (v >= 0 && v != auto_va) {
                    char b[32]; snprintf(b, sizeof b, "va %d", v); send_cmd(b);
                    FILE *f = fopen("/tmp/clusterd.log", "a");
                    if (f) { fprintf(f, "[%ld] auto va %d (app=%s nav=%d)\n", (long)time(NULL), v, app, nav); fclose(f); }
                    auto_va = v;
                }
            }
        } else if (child > 0 && ++inactive >= 6) {   /* ~3 s without stream: release window 33 */
            kill(child, SIGTERM); usleep(500000); kill(child, SIGKILL);
            waitpid(child, NULL, 0); logm("renderer stopped", child); child = 0;
            new_stream = 1;
            usleep(500000);
            rebind(back_seq);                 /* back to native map 33, same steps as the VIEW key */
            logm("rebind requested", 0);
        }
        usleep(500000);
    }
}
