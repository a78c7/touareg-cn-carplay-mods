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
- v15: tall-stream crop (`croptop` in cl.conf: the phone renders a 1920x960 cluster, rows croptop..+639 are shown).
- v16: info box (`dash.c`, built with `-DUSE_DASH`): power / torque / G / rpm / engine oil / gearbox oil drawn on the CPU
  into an RGBA pixmap and `screen_blit`ed source-over onto every cluster frame; data from `/tmp/dash.state` written by the
  Java bundle (`vwcn.Dash`). Glyphs in `dash_font.h` are pre-rendered from Noto Sans CJK SC (SIL OFL 1.1, see
  [../../LICENSES/NotoSansCJK_OFL.md](../../LICENSES/NotoSansCJK_OFL.md)); regenerate with `python3 gen_dash_font.py > dash_font.h`.
  `dash.c` / `dash_font.h` / `gen_dash_font.py` are new code by this repository, distributed with cl_render under the same terms.
- v17: rows and order from `dash.conf` (`items=`, 30 items, keys or Chinese labels, max 10 rows), box size computed
  from the chosen rows, three font sizes (1-6 / 7-8 / 9-10 rows); board-computer values use the VW names as two-line
  labels (memory on top: 自启动起 / 自上次加油起 / 累计); `/tmp/dash.conf` overrides the installed one for testing.
- v21: 43 real physical DSI items (incl. direct TPMS, pitch/roll, GNSS altitude, ZF 8HP65A gear calibration),
  symmetrical 295px bold info boxes with inline sport gauge bars (`bars=1`), and symmetrical 295×344 large-font nav
  card (`navbox.c`, `nav_box=1/2`).
- v22.0:
  - **10Hz telemetry + exponential damping (`smooth=1` in `dash.c`)**: `Dash.java` publishes `/tmp/dash.state` at 10Hz;
    when `smooth=1`, `dash.c` applies exponential damping to fast-moving sport gauge bars (rpm, torque, power, pedal,
    brake, G, steer) and accelerates idle redraw to 33ms (~30fps) while values are converging, dropping back to 300ms
    once settled (zero extra CPU load at steady cruise).
  - **Independent 60fps OpenGL ES 2.0 animation engine (`gles60.c`)**: Run via `capture=gles60` on the same window `58`
    (still composited as `[58, 33]` above the stock map, NO rewrite of the `v21.1` `capture=h264` map pipeline).
    Resolves `gl*` entry points dynamically via `eglGetProcAddress` (never links `-lGLESv2`), renders a VSync-locked
    60fps GPU-accelerated arc/bar gauge scene while compositing the pre-rendered CPU text/bar textures from
    `g_dash_pxbuf[0..1]` cleanly over GPU shaders.
  - **Hardware & rendering boundaries (Tegra K1 + QNX Screen, 2 GB RAM)**:
    - Existing `capture=h264` mode composites CarPlay H.264 video via NVDEC + `screen_blit`, while `dash.c` CPU-renders
      small 295×344 RGBA boxes (fast for numbers & small bars, NOT meant for full-screen 1920×639 CPU animations).
    - Full-screen 60fps animations / custom needles / particles MUST use the `gles60.c` OpenGL ES 2.0 shader path
      (`eglSwapInterval(dpy, 1)`), never full-screen CPU loops in `dash.c`.

Run:
- CarPlay map + telemetry overlay (default): `CL_DISPID=58 cl_render capture=h264 xres=1920 yres=639 mode=stretch`
- Standalone 60fps GLES2 animated cluster (experimental, toggled via `gles60_mode`): `CL_DISPID=58 cl_render capture=gles60 xres=1920 yres=639`
