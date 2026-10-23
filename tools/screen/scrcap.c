/* scrcap <display_id> <count> <interval_ms> <outprefix> : read QNX display pixels (RGBA) to raw files */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include "aeabi_shim.h"
typedef int (*f_cc)(void **, int); typedef int (*f_gp)(void *, int, void *); typedef int (*f_sp)(void *, int, const void *);
typedef int (*f_cp)(void **, void *); typedef int (*f_cpb)(void *); typedef int (*f_rd)(void *, void *, int, const int *, int);
int main(int argc, char **argv) {
    int want = argc > 1 ? atoi(argv[1]) : 2, cnt = argc > 2 ? atoi(argv[2]) : 1, ms = argc > 3 ? atoi(argv[3]) : 200;
    const char *pre = argc > 4 ? argv[4] : "/tmp/cap";
    void *h = dlopen("libscreen.so.1", RTLD_NOW); if (!h) { printf("no libscreen\n"); return 1; }
    f_cc cc = (f_cc)dlsym(h, "screen_create_context"); f_gp gci = (f_gp)dlsym(h, "screen_get_context_property_iv"), gcp = (f_gp)dlsym(h, "screen_get_context_property_pv");
    f_gp gdi = (f_gp)dlsym(h, "screen_get_display_property_iv"); f_cp cpx = (f_cp)dlsym(h, "screen_create_pixmap"); f_sp spi = (f_sp)dlsym(h, "screen_set_pixmap_property_iv");
    f_cpb cpb = (f_cpb)dlsym(h, "screen_create_pixmap_buffer"); f_gp gpp = (f_gp)dlsym(h, "screen_get_pixmap_property_pv");
    f_gp gbp = (f_gp)dlsym(h, "screen_get_buffer_property_pv"), gbi = (f_gp)dlsym(h, "screen_get_buffer_property_iv"); f_rd rd = (f_rd)dlsym(h, "screen_read_display");
    void *ctx; int r = cc(&ctx, 8); if (r) { printf("ctx %d\n", r); return 1; }
    int n = 0; gci(ctx, 59, &n); void **d = calloc(n, sizeof(void *)); gcp(ctx, 60, d);
    void *disp = NULL; int sz[2] = {0, 0};
    for (int i = 0; i < n; i++) { int id = -1; gdi(d[i], 87, &id); if (id == want) { disp = d[i]; gdi(d[i], 40, sz); } }
    if (!disp) { printf("display %d not found\n", want); return 1; }
    void *pix; cpx(&pix, ctx); int fmt = 8, usage = 2 | 8; /* READ|NATIVE */
    spi(pix, 14, &fmt); spi(pix, 48, &usage); spi(pix, 5, sz);
    if ((r = cpb(pix))) { printf("pixmap buffer %d\n", r); return 1; }
    void *buf = NULL; gpp(pix, 37, &buf); void *ptr = NULL; int stride = 0; gbp(buf, 34, &ptr); gbi(buf, 44, &stride);
    printf("display %d %dx%d stride %d\n", want, sz[0], sz[1], stride);
    for (int k = 0; k < cnt; k++) {
        r = rd(disp, buf, 0, NULL, 0);
        char fn[128]; snprintf(fn, sizeof fn, "%s_%02d.rgba", pre, k);
        FILE *f = fopen(fn, "wb"); for (int y = 0; y < sz[1]; y++) fwrite((char *)ptr + y * stride, 1, sz[0] * 4, f); fclose(f);
        printf("frame %d rc=%d -> %s\n", k, r, fn); usleep(ms * 1000);
    }
    return 0;
}
