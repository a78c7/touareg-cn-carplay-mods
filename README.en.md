<div align="center">

<img src="docs/img/hero_banner.svg" alt="VW Touareg MH2p CarPlay Mod Suite & Open Wireless Cockpit" width="100%">

# VW Touareg MH2p CarPlay Mod Suite & Open Wireless Cockpit

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><img src="docs/img/badges/release.svg" alt="Release v22.1"></a>
  <a href="docs/README.md"><img src="docs/img/badges/platform.svg" alt="MH2p QNX 6.6"></a>
  <a href="#license"><img src="docs/img/badges/license.svg" alt="License PolyForm Noncommercial 1.0"></a>
</p>

**Zero-disassembly single-USB unlock for full-screen / wireless CarPlay, HUD navigation, digital cluster map & sport telemetry boxes — with mobile `8080` signed OTA and wireless SSH development.**

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><b>📦 Stable Release (v22.1)</b></a> ·
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/tag/v23.0-beta.1"><b>🧪 Beta Preview (v23.0-beta.1)</b></a> ·
  <a href="#4-step-usb-installation"><b>⚡ 4-Step Install</b></a> ·
  <a href="#11-opt-in-modules"><b>🧩 Modules</b></a> ·
  <a href="README.md"><b>🇨🇳 简体中文</b></a> ·
  <a href="README.de.md"><b>🇩🇪 Deutsch</b></a>
</p>

<p><sub>If you find this project helpful, click <b>⭐ Star</b> at the top right or tap the red <b>Like</b> button below (no GitHub account required); for release notifications, click <b>👀 Watch → Custom → Releases</b>.</sub></p>

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases"><img src="https://raw.githubusercontent.com/a78c7/touareg-cn-carplay-mods/refs/heads/badges/downloads.svg" alt="Total Downloads"></a>
  &nbsp;
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/stargazers"><img src="https://raw.githubusercontent.com/a78c7/touareg-cn-carplay-mods/refs/heads/badges/stars.svg" alt="GitHub Stars"></a>
  &nbsp;
  <a href="https://touareg-likes.pages.dev/like?lang=en"><img src="https://touareg-likes.pages.dev/badge.svg?lang=en" alt="Like (no login required)"></a>
</p>

</div>

---

> [!TIP]
> **🧪 Public Beta [`v23.0-beta.1 (Pre-release)`](https://github.com/a78c7/touareg-cn-carplay-mods/releases/tag/v23.0-beta.1) is out** — introduces the opt-in **60fps AI Custom Cluster Theme Engine (GLES2 + QuickJS)**, an embedded **1:1 browser driving simulator**, and the AI-ready [`my_theme.js`](tools/preview/my_theme.js) template (configure & flash via USB without needing the `8080` Wi-Fi console; `v23_mode` is OFF by default and guarded by a 3-layer automatic safety fuse). For daily stability, stick with [`v22.1` Stable](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest).

<div align="center">
  <img src="docs/img/cluster.png" alt="v22 digital cluster map, bold sport telemetry boxes, and symmetrical navigation card" width="92%">
  <br>
  <sub><i>Cluster visual overview: centered CarPlay map flanked by symmetrical <code>295×344px</code> bold sport telemetry boxes and large-font navigation card.</i></sub>
</div>

<br>

https://github.com/user-attachments/assets/618e8fe2-e579-4aed-b9de-ab50b8576374

<div align="center">
  <sub><i>In-car demo video (click for audio; bilingual CN/EN subtitles; the opening custom theme demonstrates an extension built on this repo's <code>1920×639</code> pipeline).</i></sub>
</div>

---

## 11 Opt-In Modules

> All modules ship disabled by default inside `USB/未选模块/`. Use the desktop offline configurator to set each module to **Install / Uninstall / Ignore**, with prerequisite dependencies resolved automatically.

| Category | Module | Core Capability | Compatibility |
| :--- | :--- | :--- | :---: |
| **Infotainment & Projection** | **🖥️ Full-Screen CarPlay** | In-place 38-byte patch removing sidebars for full-width display (verifies skin CRC32, skips safely on mismatch) | CN Touareg `P*` |
| | **📡 Wireless CarPlay** | Enables factory-inactive Wireless CarPlay (integrates upstream LawPaul module) | MH2p Generic |
| | **🤖 Android Auto** | Enables factory-inactive Android Auto (integrates upstream LawPaul module) | MH2p Generic |
| **HUD & Digital Cluster** | **🧭 Navigation to HUD** | Projects Apple Maps / AMap / Baidu turn arrows, distance, street names & lanes to HUD (custom wired iAP2 + wireless dual stack) | CN Touareg `P*` |
| | **🗺️ Cluster Map + Sport Telemetry Boxes** | Projects CarPlay map onto the `1920×639` Innovision Cockpit (vision-based AMap centering, steering-wheel zoom); symmetrical `295px` **46px bold + 10 Hz damped sport gauge bars** (43 real DSI channels), **large-font navigation card** & experimental **60 fps GLES2 pipeline** | CN Touareg `P*` |
| | **🔄 Steering-Wheel `OK` Double-Tap Map Switch** | Double-tap right steering-wheel `OK` within 2s to toggle between CarPlay and stock map (`VW_HUD_4_OkMapSwitch`) | CN Touareg `P*` |
| | **🎙️ Voice Button Short-Press Siri (Experimental)** | Short-press steering-wheel voice button to invoke Siri while CarPlay is active (factory long-press unchanged) | CN Touareg `P*` |
| **Wireless & DevOps** | **📱 Mobile Wireless Console (OTA + Diag + Optional SSH)** | Connects to phone hotspot silently; open `http://touareg.local:8080` to edit cluster layouts, flash **Ed25519-signed offline OTA with rollback**, export sanitized diagnostics & manage safe mode; optional key-only SSH (`:2012`) | MH2p Generic |
| **Fixes & Popup Filters** | **📶 P2878 5 GHz Wireless Region Fix** | Only needed if upgrading legacy firmware (e.g. `P9832`) to `P2878` broke wireless CarPlay while wired still works | CN Touareg `P*` |
| | **🔕 Suppress Bluetooth "Enter Username" Popup** | Shared Bundle `98` filter blocking only Bluetooth OBEX auth prompt (`Busrlo`) without affecting calls, media, or CarPlay | VW MH2p |
| | **🔕 Suppress "SIM for Data Only" Popup** | Shared Bundle `98` filter blocking `PHONE_SIM_DATAONLY` prompt without affecting mobile data or SOS | VW MH2p |

---

<a id="notice"></a>

> [!IMPORTANT]
> **Read Before Installing: Vehicle Compatibility, Safety Fuses, Non-Commercial License & Disclaimer**
> - **Compatibility**: Touareg-specific patches auto-detect China-market P-series firmware (`MH2p_CN_VWG36_P*`, end-to-end verified on `P2878`, auto-probing and safely skipping incompatible sub-patches on other CN `P*` builds and skipping non-CN / EU / other vehicles without modifying files). `VW_WiFi_Dev` and [`docs/`](docs/README.md) apply across **MH2p (MIB2+ High)** platforms (**VW Touareg, Audi, Porsche PCM 5, Bentley, Lamborghini LIS III**).
> - **EU / Non-CN Vehicles (e.g. `P2869`)**: Touareg UI scripts intentionally abort cleanly on EU builds to avoid bricking; the architecture, source code (`src/`), engineering docs (`docs/01–07`, `R1–R5`), local previewer (`tools/preview/index.html`), and `VW_WiFi_Dev` can be used directly as a development foundation (see also the [Touareg-Freunde EU P2869 thread](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/)).
> - **Zero-Wear Safety Fuse & Driving Safety**: Built-in **3-crash zero-wear safe mode** covering J9 HMI, `cl_probe.so`, `clusterd`/`cl_render`, and `io-pkt` (zero eMMC writes during normal boots). Never operate the console or head unit while driving; always rely on factory instruments for speed and warning indicators.
> - **Free, Open-Source & Strictly Non-Commercial**: Licensed under **[PolyForm Noncommercial 1.0.0](LICENSE) + [Additional Terms](LICENSE.ADDITIONAL-TERMS.md)** for personal, non-commercial educational use only. Reselling archives, paid flashing services, or commercial bundling is strictly prohibited — if you paid for this package, demand a refund immediately.
> - **Unofficial & Use at Your Own Risk**: Independent hobbyist research project unaffiliated with VW, Audi, Porsche, Bentley, Lamborghini, Apple, Google, or AMap. Provided "AS IS" with no warranty; you assume all flashing risks. Rights holders may contact via [GitHub Issues](https://github.com/a78c7/touareg-cn-carplay-mods/issues).

## 4-Step USB Installation

1. **Prepare a USB Drive**: **≤ 32 GB**, formatted as **FAT32** (Windows: right-click → Format → FAT32; macOS: Disk Utility → Erase → `MS-DOS (FAT)`).
2. **Extract to the USB Root**: Download the latest release `zip` from **[Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest)** (verify against `SHA256SUMS.txt`) and extract all contents directly to the top level of the USB drive (do **not** wrap inside an extra folder):
   ```text
   USB_ROOT/
   ├── Data/                          ← Must be directly at USB root
   ├── Meta/                          ← Must be directly at USB root
   ├── Mods/                          ← Empty by default; populated by configurator
   ├── 未选模块/                       ← Holds all selectable module folders
   └── 改装设置（Windows）.exe / （Mac）.command
   ```
3. **[Required] Run the Offline Configurator & Save**: Launch **`改装设置（Windows）.exe`** or **`改装设置（Mac）.command`** from the USB root (flashing without running it installs nothing):
   - Choose a preset or toggle individual modules (**Install / Uninstall / Ignore**), and drag-and-drop left/right telemetry boxes and the navigation card;
   - If enabling **Mobile Wireless Console**, configure 1–3 phone hotspot credentials and a 6–16 character PIN, then click **Save to USB** (the macOS build automatically strips `._*` AppleDouble files).
4. **Flash In-Car**: Safely eject the USB drive → **start the engine** → plug into the center-armrest USB port. After ~10 seconds the unit reboots automatically and flashes (several reboots over a few minutes; **do not touch the infotainment during flashing**) → remove USB when prompted.

> [!TIP]
> - **Unsigned Binary Notice**: The desktop configurator has no paid code-signing certificate (Windows: *More info → Run anyway*; macOS: *System Settings → Privacy & Security → Open Anyway*). It is compiled directly from [`tools/configurator`](tools/configurator) by GitHub Actions with `SHA256SUMS.txt`, binds only to `127.0.0.1`, and **never connects to the internet**.
> - **Upgrading from `v22.01` or Earlier**: You must flash `v22.1` **once via USB** (older builds lack the embedded Ed25519 public key and safely reject signed OTA packages; wireless OTA resumes afterward). USB upgrade preserves existing `cl.conf` and `web.pin`.
> - **Black Borders After First Flash**: In iPhone Bluetooth/CarPlay settings, tap *Forget This Car* and reconnect once.

---

## Daily Usage, Wireless Console & Engineering Docs

- **Cluster & HUD**: Press `VIEW` on the steering wheel to switch to the Navigation view; CarPlay projects automatically once connected and syncs with night mode / ACC layout shift.
- **Desktop Layout Previewer**: Open [`tools/preview/index.html`](tools/preview/index.html) locally in any browser to preview `dash.conf` at native `1920×639`, test `smooth=1` damped gauge bars, or replay 10 Hz driving CSV logs.
- **Mobile Wireless Console (`http://touareg.local:8080`)**: Uses the idle `bcm2` STA interface (`dhcp.client -n -R`) concurrently without disrupting 5 GHz RSDB Wireless CarPlay or `ecm0` factory telematics. Protected by fail-closed `X-Pin` auth (with auto-generated on-cluster setup PIN if unset), per-IP exponential backoff, and parked-only lock (`≤ 3 km/h`). Supports live telemetry edits, **Ed25519-signed & SHA-256 verified** offline `.tar` OTA + one-click rollback, sanitized diagnostics export, 3-crash safe-mode reset, and optional key-only **Wireless SSH (`:2012`)**.
- **Engineering Docs & Source Index**: Complete Chinese engineering notes (`docs/01–07`) and MH2p platform reference (`docs/reference/R1–R5`, each note tagged **Touareg-specific** vs **MH2p-generic**) live in **[`docs/README.md`](docs/README.md)** alongside full source under [`src/`](src) and [`tools/`](tools).

---

## Credits

- **[luanqibazao0123-bot](https://github.com/luanqibazao0123-bot)** ([#7](https://github.com/a78c7/touareg-cn-carplay-mods/issues/7)): Inspired the `v21` bold sport gauge redesign and provided real-world `P2878` upgrade testing clues.
- **International Community**: Referenced by the German Touareg-Freunde community in the [EU MH2p P2869 cluster CarPlay thread](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/).
- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)** ([Site](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)): Zero-disassembly USB ModKit framework and optional `AppleCarPlay` / `AndroidAuto` modules ([CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md), incorporating [fifthBro/pcm5-androidauto-connect-fix](https://github.com/fifthBro/pcm5-androidauto-connect-fix)).
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)** & **[mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)**: Upstream `dio_cluster.so` (v0034) and `cl_render` base ([CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md)).
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**: Full-screen geometry reference; **[luka-dev](https://github.com/luka-dev)**: `jxe2jar` and `qnx65-armv7-toolchain`.

---

## License

- **Original Code**: **[PolyForm Noncommercial 1.0.0](LICENSE) + [Additional Non-Commercial Terms](LICENSE.ADDITIONAL-TERMS.md)** (contains no OEM VW/Apple proprietary binaries; see **[Read Before Installing](#notice)**); documentation text under `docs/` is licensed under **CC BY-SA 4.0**.
- **Third-Party Components**: `src/cl_render`, `dio_cluster.so`, ModKit, and optional LawPaul modules follow **[CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md)**. Full attributions and third-party licenses are listed in **[THIRD_PARTY_NOTICES.md](LICENSES/THIRD_PARTY_NOTICES.md)**.
