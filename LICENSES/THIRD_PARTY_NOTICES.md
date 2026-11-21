# Third-Party Attribution: `luka-dev/mib2q-carplay-rgi` & `fifthBro/pcm5-androidauto-connect-fix`

## 1. `luka-dev/mib2q-carplay-rgi` (GPL-3.0-or-later)
- Upstream: <https://github.com/luka-dev/mib2q-carplay-rgi>
- Files in this repository referencing upstream iAP2 TLV / PPS helpers:
  - `src/cluster-probe/wired/framework/` (`common.h`, `logging.c`, `logging.h`, `pps_writer.c`, `pps_writer.h`)
  - `src/cluster-probe/wired/routeguidance/` (`rgd_tlv.c`, `rgd_tlv.h`)
- Full GPL-3.0 license text is included in the repository root [`LICENSE`](../LICENSE).
- Note: These upstream files were originally bundled alongside `fifthBro/mh2p-cluster` (`dio_cluster.so` / `rg_core.inc`). Future releases plan to replace the wired TLV bridge with a clean-room implementation to eliminate any mixed-license linkage inside `cl_probe.so`.

## 2. `fifthBro/pcm5-androidauto-connect-fix` (Non-Commercial)
- Upstream: <https://github.com/fifthBro/pcm5-androidauto-connect-fix>
- Referenced inside the optional `LawPaul/MH2p_SD_ModKit` `AndroidAuto` module (`fix_partition_1008`, `aafix.jar`).
- Free for personal, non-commercial use only; DO NOT SELL.
