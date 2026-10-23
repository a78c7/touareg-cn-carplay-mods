// list QNX screen displays: id, size, type, attached
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
typedef int (*cc_t)(void **, int);
typedef int (*gcp_t)(void *, int, void *);
typedef int (*gdi_t)(void *, int, int *);
typedef int (*gdc_t)(void *, int, int, char *);
int main(int argc, char **argv) {
  void *h = dlopen("libscreen.so.1", RTLD_NOW); if (!h) { printf("no libscreen: %s\n", dlerror()); return 1; }
  cc_t cc = (cc_t)dlsym(h, "screen_create_context");
  gcp_t gci = (gcp_t)dlsym(h, "screen_get_context_property_iv");
  gcp_t gcp = (gcp_t)dlsym(h, "screen_get_context_property_pv");
  gdi_t gdi = (gdi_t)dlsym(h, "screen_get_display_property_iv");
  gdc_t gdc = (gdc_t)dlsym(h, "screen_get_display_property_cv");
  void *ctx = 0; int ctype = argc > 1 ? atoi(argv[1]) : 0;
  int r = cc(&ctx, ctype); printf("create_context(%d)=%d\n", ctype, r); if (r) return 1;
  int n = 0; gci(ctx, 59, &n); printf("displays=%d\n", n);
  void **d = calloc(n, sizeof(void *)); gcp(ctx, 60, d);
  for (int i = 0; i < n; i++) {
    int id = -1, sz[2] = {0,0}, type = -1, att = -1; char ids[64] = "";
    gdi(d[i], 87, &id); gdi(d[i], 40, sz); gdi(d[i], 47, &type); gdi(d[i], 64, &att);
    if (gdc) gdc(d[i], 20, sizeof ids, ids);
    printf("display[%d] id=%d idstr='%s' size=%dx%d type=%d attached=%d\n", i, id, ids, sz[0], sz[1], type, att);
  }
  return 0;
}
