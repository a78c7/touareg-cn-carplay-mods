# Third-Party Notices & License Boundaries

As of **v22.0**, the wired CarPlay iAP2 Route Guidance parser (`src/cluster-probe/wired/cl_wired_rg.c` & `cl_wired.c`) is an independent original implementation in this repository.

All independent original components in `a78c7/touareg-cn-carplay-mods` (`src/hud-java`, `src/popup-filter-java`, `src/cluster-probe`, `src/clusterd`, `src/touareg_netd`, `src/vhpatch`, `src/common`, `tools/`, and all `VW_*` module scripts) are licensed under **PolyForm Noncommercial License 1.0.0 + `LICENSE.ADDITIONAL-TERMS.md`** (personal, non-commercial use; reselling prohibited). The adapted `src/cl_render/` codebase and its compiled binary `cl_render` (licensed as a whole under **CC BY-NC-SA 4.0** without imposing `LICENSE.ADDITIONAL-TERMS.md`) and all third-party upstream components bundled or embedded in this repository retain their respective licenses below:

## 1. `LawPaul/MH2p_SD_ModKit` (CC BY-NC-SA 4.0)
- Attribution: LawPaul (<https://github.com/LawPaul/MH2p_SD_ModKit>)
- Components: `USB/Data/`, `USB/Meta/`, optional `AppleCarPlay` and `AndroidAuto` modules under `USB/未选模块/`.
- License text: [`CC-BY-NC-SA-4.0.md`](CC-BY-NC-SA-4.0.md).

## 2. `fifthBro/mh2p-cluster` & `fifthBro/pcm5-androidauto-connect-fix` (CC BY-NC-SA 4.0 / Non-Commercial)
- Attribution: Copyright (c) 2026 fifthBro (<https://fifthbro.github.io>) — <https://github.com/fifthBro/mh2p-cluster>, <https://github.com/fifthBro/pcm5-androidauto-connect-fix>
- Components: `USB/未选模块/VW_HUD_1_RouteGuidance/Update/dio_cluster.so` (v0034), the entire adapted `src/cl_render/` codebase derived from `cluster.c` and its compiled binary `cl_render` (distributed as a whole under **CC BY-NC-SA 4.0** without imposing `LICENSE.ADDITIONAL-TERMS.md`; its embedded third-party files `dash_font.h`, `stb_truetype.h`, and `quickjs/` retain their respective original licenses below), and `aafix.jar` / `fix_partition_1008` inside the optional `AndroidAuto` module.
- License text: [`CC-BY-NC-SA-4.0.md`](CC-BY-NC-SA-4.0.md).

## 3. Noto Sans CJK SC (SIL OFL 1.1)
- Pre-rendered bitmap glyph tables (`src/cl_render/dash_font.h`) generated from Noto Sans CJK SC.
- License text: [`NotoSansCJK_OFL.md`](NotoSansCJK_OFL.md).

## 4. `stb_truetype.h` (Public Domain / MIT)
- Used by `src/cl_render/navbox.c` and `src/cl_render/v23_gles.c` for runtime vector glyph rasterization.
- License text: [`stb_PUBLIC_DOMAIN.md`](stb_PUBLIC_DOMAIN.md).

## 5. `zlib` 1.3.1 (zlib License)
- Statically linked into `src/vhpatch` for in-place `viewhandler.zip` patching on the head unit.
- License text: [`zlib_LICENSE.md`](zlib_LICENSE.md).

## 6. TweetNaCl Ed25519 Field & Curve Arithmetic (Public Domain)
- Upstream: <https://tweetnacl.cr.yp.to/> (Daniel J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers).
- Components: Curve25519/Edwards25519 signature verification arithmetic in `src/touareg_netd/ed25519_verify.h` (Public Domain, adapted with streaming SHA-512 and strict RFC 8032 canonical scalar check).

## 7. QuickJS Javascript Engine (MIT License)
- Attribution: Copyright (c) 2017-2021 Fabrice Bellard, Charlie Gordon (<https://bellard.org/quickjs/>).
- Components: Embedded core QuickJS runtime under `src/cl_render/quickjs/`, statically compiled into `cl_render` for the `v23` sandboxed custom cluster theme engine.
- License text: [`QuickJS-MIT.txt`](QuickJS-MIT.txt).
