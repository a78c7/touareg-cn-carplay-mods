// list windows visible to a DISPLAY_MANAGER (8) context — read-only
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
typedef int (*cc_t)(void **, int);
typedef int (*gp_t)(void *, int, void *);
typedef int (*gc_t)(void *, int, int, char *);
int main(int argc, char **argv) {
  void *h = dlopen("libscreen.so.1", RTLD_NOW); if (!h) return 1;
  cc_t cc = (cc_t)dlsym(h, "screen_create_context");
  gp_t gci = (gp_t)dlsym(h, "screen_get_context_property_iv"), gcp = (gp_t)dlsym(h, "screen_get_context_property_pv");
  gp_t gwi = (gp_t)dlsym(h, "screen_get_window_property_iv"), gwp = (gp_t)dlsym(h, "screen_get_window_property_pv"), gdi = (gp_t)dlsym(h, "screen_get_display_property_iv");
  gc_t gwc = (gc_t)dlsym(h, "screen_get_window_property_cv");
  void *ctx = 0; int r = cc(&ctx, 8); printf("ctx(8)=%d\n", r); if (r) return 1;
  int n = -1; r = gci(ctx, 108, &n); printf("window_count r=%d n=%d\n", r, n); if (r || n <= 0) return 0;
  void **w = calloc(n, sizeof(void *)); r = gcp(ctx, 109, w); if (r) { printf("windows r=%d\n", r); return 0; }
  for (int i = 0; i < n; i++) {
    char ids[64] = ""; int pos[2] = {0}, sz[2] = {0}, bsz[2] = {0}, vis = -1, z = 0, pid = 0, did = -1; void *disp = 0;
    gwc(w[i], 20, sizeof ids, ids); gwi(w[i], 35, pos); gwi(w[i], 40, sz); gwi(w[i], 5, bsz); gwi(w[i], 51, &vis); gwi(w[i], 54, &z); gwi(w[i], 31, &pid);
    if (gwp(w[i], 11, &disp) == 0 && disp) gdi(disp, 87, &did);
    printf("win[%2d] id='%s' disp=%d pos=%d,%d size=%dx%d buf=%dx%d vis=%d z=%d pid=%d\n", i, ids, did, pos[0], pos[1], sz[0], sz[1], bsz[0], bsz[1], vis, z, pid);
  }
  return 0;
}
