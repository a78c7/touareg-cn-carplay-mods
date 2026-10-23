# cl_render

H.264 decoder/renderer for the instrument cluster, based on **fifthBro / mh2p-cluster `cluster.c`**
(Copyright (c) 2026 fifthBro, https://fifthbro.github.io), licensed **CC BY-NC-SA 4.0** — see
[../../LICENSES/fifthBro_mh2p-cluster_LICENSE.md](../../LICENSES/fifthBro_mh2p-cluster_LICENSE.md). This modified version is
distributed under the same license (attribution, non-commercial, share-alike).

Changes in this copy (VW Touareg MH2p_CN_VWG36_P2878):

- `cb_alloc_picture`: round-robin surface allocation (the original always took the first free surface,
  which overwrote the reference frame when the DPB is 1 frame -> corrupted picture).
- Window id string configurable with env `CL_DISPID` (default "33"); the Touareg build runs it as "58"
  and the cluster display list is set to `[58, 33]` so the stock map stays bound underneath.
- Built with `-DUSE_SCREEN_BLIT` (NvMedia Mixer returns an error on this unit).
- `glchar.h` / `shim.c` / `aeabi_shim.h`: build fixes for clang + QNX 6.6 libc.

Run: `CL_DISPID=58 cl_render capture=h264 xres=1920 yres=639 mode=stretch` (started by `clusterd`).
