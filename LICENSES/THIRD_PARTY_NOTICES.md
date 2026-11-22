# Third-Party Notices & License Boundaries

As of **v22.0**, the wired CarPlay iAP2 Route Guidance parser (`src/cluster-probe/wired/cl_wired_rg.c` & `cl_wired.c`) is an independent original implementation in this repository.

All original components in `a78c7/touareg-cn-carplay-mods` are licensed under **PolyForm Noncommercial License 1.0.0 + `LICENSE.ADDITIONAL-TERMS.md`** (personal, non-commercial use; reselling prohibited). Third-party upstream components bundled or derived in this repository retain their respective licenses below:

## 1. `LawPaul/MH2p_SD_ModKit` (CC BY-NC-SA 4.0)
- Upstream: <https://github.com/LawPaul/MH2p_SD_ModKit>
- Components: `USB/Data/`, `USB/Meta/`, optional `AppleCarPlay` and `AndroidAuto` modules under `USB/未选模块/`.
- License text: [`MH2p_SD_ModKit_LICENSE.md`](MH2p_SD_ModKit_LICENSE.md).

## 2. `fifthBro/mh2p-cluster` & `fifthBro/pcm5-androidauto-connect-fix` (CC BY-NC-SA 4.0 / Non-Commercial)
- Upstream: <https://github.com/fifthBro/mh2p-cluster>, <https://github.com/fifthBro/pcm5-androidauto-connect-fix>
- Components: `USB/未选模块/VW_HUD_1_RouteGuidance/Update/dio_cluster.so` (v0034), base `src/cl_render/cluster.c` / `boot_canim_data.h`, and `aafix.jar` / `fix_partition_1008` inside the optional `AndroidAuto` module.
- License text: [`fifthBro_mh2p-cluster_LICENSE.md`](fifthBro_mh2p-cluster_LICENSE.md).

## 3. Noto Sans CJK SC (SIL OFL 1.1)
- Pre-rendered bitmap glyph tables (`src/cl_render/dash_font.h`) generated from Noto Sans CJK SC.
- License text: [`NotoSansCJK_OFL.md`](NotoSansCJK_OFL.md).

## 4. `stb_truetype.h` (Public Domain / MIT)
- Used by `src/cl_render/navbox.c` for runtime vector glyph rasterization.
- License text: [`stb_PUBLIC_DOMAIN.md`](stb_PUBLIC_DOMAIN.md).

## 5. `zlib` 1.3.1 (zlib License)
- Statically linked into `src/vhpatch` for in-place `viewhandler.zip` patching on the head unit.
- License text: [`zlib_LICENSE.md`](zlib_LICENSE.md).
