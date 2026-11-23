<div align="center">

<img src="docs/img/hero_banner.svg" alt="VW Touareg MH2p CarPlay Mod-Paket & Offenes WLAN-Cockpit" width="100%">

# VW Touareg MH2p CarPlay Mod-Paket & Offenes WLAN-Cockpit

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><img src="docs/img/badges/release.svg" alt="Release v22.1"></a>
  <a href="docs/README.md"><img src="docs/img/badges/platform.svg" alt="MH2p QNX 6.6"></a>
  <a href="#lizenz"><img src="docs/img/badges/license.svg" alt="License PolyForm Noncommercial 1.0"></a>
</p>

**Ein-USB-Stick-Modifikationspaket für Vollbild- & Wireless-CarPlay, HUD-Navigation, digitale Kombiinstrument-Karte & Sport-Infoboxen — inkl. Handy-Web-Konsole (`8080`), Ed25519-signiertem Offline-OTA und WLAN-SSH.**

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><b>📦 Stabile Version (v22.1)</b></a> ·
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/tag/v23.0-beta.5"><b>🧪 Beta-Vorschau (v23.0-beta.5)</b></a> ·
  <a href="#4-schritte-usb-installation"><b>⚡ 4-Schritte-Installation</b></a> ·
  <a href="#11-optionale-module"><b>🧩 Module</b></a> ·
  <a href="README.md"><b>🇨🇳 简体中文</b></a> ·
  <a href="README.en.md"><b>🇺🇸 English</b></a>
</p>

<p><sub>Wenn dir das Projekt hilft, freue ich mich über einen <b>⭐ Star</b> oben rechts oder ein <b>Gefällt mir</b> ohne Login unten; für Release-Benachrichtigungen einfach <b>👀 Watch → Custom → Releases</b> aktivieren.</sub></p>

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases"><img src="https://raw.githubusercontent.com/a78c7/touareg-cn-carplay-mods/refs/heads/badges/downloads.svg" alt="Downloads gesamt"></a>
  &nbsp;
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/stargazers"><img src="https://raw.githubusercontent.com/a78c7/touareg-cn-carplay-mods/refs/heads/badges/stars.svg" alt="GitHub Stars"></a>
  &nbsp;
  <a href="https://touareg-likes.pages.dev/like?lang=de"><img src="https://touareg-likes.pages.dev/badge.svg?lang=de" alt="Gefällt mir (ohne Login)"></a>
</p>

</div>

---

> [!TIP]
> **🧪 Öffentliche Beta [`v23.0-beta.5 (Pre-release)`](https://github.com/a78c7/touareg-cn-carplay-mods/releases/tag/v23.0-beta.5) verfügbar** — im Fahrzeug verifizierte **55–59 fps Hardware-Zero-Copy KI-Design-Engine (GLES2 + QuickJS)**, pixelgenaue **1920×639 Cockpit-Geometriespezifikation** ([`CLUSTER_LAYOUT_SPEC.md`](docs/reference/CLUSTER_LAYOUT_SPEC.md)), Lenkrad-`OK`-Designwechsel (standardmäßig Doppelklick `OK`; bei installiertem Kartenwechsel-Modul Doppelklick Karte / Dreifachklick Design) + `8080`-Designauswahl, 4-stufige Sport-Balkenfarbverläufe und Sofortstart ohne 2-s-Splashscreen (`v23_mode` standardmäßig deaktiviert, 3-stufige Sicherheitsabschaltung). Für den Alltag bleibt [`v22.1` (Stabil)](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) empfohlen.

<div align="center">
  <img src="docs/img/cluster.png" alt="v22 Kombiinstrument-Karte, Sport-Infoboxen und symmetrische Navigationskarte" width="92%">
  <br>
  <sub><i>Ansicht im Digital Cockpit: zentrierte CarPlay-Karte mit symmetrischen <code>295×344px</code> Großschrift-Sport-Infoboxen und Navigationskarte.</i></sub>
</div>

<br>

https://github.com/user-attachments/assets/618e8fe2-e579-4aed-b9de-ab50b8576374

<div align="center">
  <sub><i>Live-Demo im Fahrzeug (Klick für Ton, chinesisch/englische Untertitel; das einleitende Custom-Theme demonstriert eine Erweiterung auf Basis des <code>1920×639</code>-Kanals dieses Repos).</i></sub>
</div>

---

## 11 Optionale Module

> Alle Module liegen standardmäßig deaktiviert unter `USB/未选模块/`. Über den Offline-Konfigurator am PC/Mac lässt sich jedes Modul einzeln auf **Installieren / Deinstallieren / Ignorieren** setzen; Abhängigkeiten werden automatisch aufgelöst.

| Kategorie | Modul | Hauptfunktion | Kompatibilität |
| :--- | :--- | :--- | :---: |
| **Infotainment & Projektion** | **🖥️ Vollbild-CarPlay** | 38-Byte-In-Place-Patch entfernt seitliche Balken (prüft Skin-CRC32, überspringt sicher bei Abweichung) | CN Touareg `P*` |
| | **📡 Wireless CarPlay** | Aktiviert werksseitig inaktives Wireless CarPlay (integriert LawPaul-Modul) | MH2p allgemein |
| | **🤖 Android Auto** | Aktiviert werksseitig inaktives Android Auto (integriert LawPaul-Modul) | MH2p allgemein |
| **HUD & Kombiinstrument** | **🧭 Navigation im HUD** | Projiziert Abbiegepfeile, Entfernung, Straßennamen & Spurhinweise (Apple/AMap/Baidu) ins HUD (eigenes Wired-iAP2 + Wireless-Dual-Stack) | CN Touareg `P*` |
| | **🗺️ Kombiinstrument-Karte + Sport-Infoboxen** | CarPlay-Karte im `1920×639` Innovision Cockpit (AMap-Zentrierung, Lenkrad-Zoom); symmetrische `295px` **46px-Großschrift-Sportbalken mit 10-Hz-Dämpfung** (43 echte DSI-Kanäle), **Großschrift-Navigationskarte** & experimentelle **60-fps-GLES2-Engine** | CN Touareg `P*` |
| | **🔄 Lenkrad-`OK`-Doppelklick Kartenwechsel** | Doppelklick auf rechte `OK`-Taste innerhalb von 2 s schaltet zwischen CarPlay- und Werkskarte um (`VW_HUD_4_OkMapSwitch`) | CN Touareg `P*` |
| | **🎙️ Sprachtaste Kurzdruck für Siri (Experimentell)** | Kurzdruck auf die Lenkrad-Sprachtaste startet Siri bei aktivem CarPlay (Werks-Langdruck unverändert) | CN Touareg `P*` |
| **WLAN & Entwicklung** | **📱 Mobile WLAN-Konsole (OTA + Diagnose + optional SSH)** | Hintergrundverbindung zum Handy-Hotspot (`http://touareg.local:8080`): Infoboxen live anpassen, **Ed25519-signiertes Offline-OTA mit Rollback**, anonymisiertes Diagnosepaket & Safe-Mode-Verwaltung; optional Key-only SSH (`:2012`) | MH2p allgemein |
| **Fixes & Popup-Filter** | **📶 P2878 5-GHz-WLAN-Region-Fix** | Nur erforderlich, falls nach Update von älterer Firmware (z. B. `P9832`) auf `P2878` Kabel-CarPlay läuft, aber Wireless nicht verbindet | CN Touareg `P*` |
| | **🔕 Bluetooth-Popup „Benutzername eingeben“ ausblenden** | Gemeinsamer Bundle-`98`-Filter blockiert nur Bluetooth-OBEX-Auth (`Busrlo`) ohne Einfluss auf Telefonie, Musik oder CarPlay | VW MH2p |
| | **🔕 Popup „SIM nur für Daten“ ausblenden** | Gemeinsamer Bundle-`98`-Filter blockiert `PHONE_SIM_DATAONLY` ohne Einfluss auf Internetverbindung oder SOS | VW MH2p |

---

<a id="notice"></a>

> [!IMPORTANT]
> **Vor der Installation lesen: EU-Hinweis, Fahrzeugkompatibilität, Schutzschaltung & Haftungsausschluss**
> - **Hinweis für EU-Fahrzeuge (z. B. Touareg CR `P2869`)**: Die fertigen Touareg-UI-Skripte prüfen gezielt auf chinesische P-Firmwarestände (`MH2p_CN_VWG36_P*`, vollständig verifiziert auf `P2878`) und **brechen auf EU- oder anderen Ständen sicher ohne Änderungen ab**, um Schäden auszuschließen. Architektur, Quellcode (`src/`), Dokumentation (`docs/01–07`, `R1–R5`), lokaler Browser-Previewer (`tools/preview/index.html`) und `VW_WiFi_Dev` dienen direkt als offene Basis für **MH2p (VW Touareg, Audi, Porsche PCM 5, Bentley, Lamborghini LIS III)** — siehe auch den [Touareg-Freunde-Thread zur EU-P2869-Portierung](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/).
> - **3-Absturz-Schutzschaltung (Zero-Wear Safe Mode) & Fahrsicherheit**: Integrierte Schutzschaltung für J9 HMI, `cl_probe.so`, `clusterd`/`cl_render` und `io-pkt` (keine eMMC-Schreibzugriffe bei normalem Start). Konsole und Einstellungen niemals während der Fahrt bedienen; für Geschwindigkeit und Warnleuchten gilt stets die Werksanzeige.
> - **Kostenlos, Open Source & strikt nicht-kommerziell**: Lizenziert unter **[PolyForm Noncommercial 1.0.0](LICENSE) + [Zusatzbedingungen](LICENSE.ADDITIONAL-TERMS.md)**. Ausschließlich für den privaten, nicht-kommerziellen Gebrauch; Weiterverkauf, kostenpflichtige Codierung/Aufspielung oder kommerzielle Bündelung sind streng untersagt.
> - **Inoffiziell & Nutzung auf eigene Gefahr**: Privates Forschungsprojekt ohne Verbindung zu VW, Audi, Porsche, Bentley, Lamborghini, Apple, Google oder AMap. Bereitstellung ohne Gewährleistung; jedes Flash-Risiko trägt der Nutzer selbst. Rechteinhaber können über [GitHub Issues](https://github.com/a78c7/touareg-cn-carplay-mods/issues) Kontakt aufnehmen.

## 4-Schritte-USB-Installation

1. **USB-Stick vorbereiten**: **≤ 32 GB**, formatiert als **FAT32** (Windows: Rechtsklick → Formatieren → FAT32; macOS: Festplattendienstprogramm → Löschen → `MS-DOS (FAT)`).
2. **Direkt ins Hauptverzeichnis entpacken**: Neuestes Release-`zip` unter **[Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest)** laden (Prüfsumme in `SHA256SUMS.txt`) und den gesamten Inhalt direkt auf die oberste Ebene des USB-Sticks legen (keinen zusätzlichen Unterordner anlegen):
   ```text
   USB-Stammverzeichnis/
   ├── Data/                          ← Muss direkt im Hauptverzeichnis liegen
   ├── Meta/                          ← Muss direkt im Hauptverzeichnis liegen
   ├── Mods/                          ← Standardmäßig leer; wird vom Konfigurator befüllt
   ├── 未选模块/                       ← Enthält alle optionalen Modulordner
   └── 改装设置（Windows）.exe / （Mac）.command
   ```
3. **[Pflicht] Offline-Konfigurator ausführen & speichern**: **`改装设置（Windows）.exe`** oder **`改装设置（Mac）.command`** direkt vom USB-Stick starten (ohne diesen Schritt wird nichts installiert):
   - Preset wählen oder Module einzeln auf **Installieren / Deinstallieren / Ignorieren** setzen sowie Infoboxen/Navigationskarte per Drag & Drop anordnen;
   - Bei aktivierter **WLAN-Konsole** 1–3 Handy-Hotspots und eine 6–16-stellige PIN eintragen und auf **Auf USB speichern** klicken (die Mac-Version entfernt automatisch `._*`-Dateien).
4. **Im Fahrzeug installieren**: USB-Stick sicher auswerfen → **Motor starten** → Stick in den USB-Anschluss der Mittelarmlehne stecken. Nach ca. 10 Sekunden startet das System automatisch neu und flasht (mehrere Neustarts über einige Minuten, **Infotainment dabei nicht bedienen**) → Stick nach Aufforderung abziehen.

> [!TIP]
> - **Hinweis zu unsignierten Binärdateien**: Der Desktop-Konfigurator besitzt kein kostenpflichtiges Code-Signing-Zertifikat (Windows: *Weitere Informationen → Trotzdem ausführen*; macOS: *Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen*). Er wird per GitHub Actions direkt aus [`tools/configurator`](tools/configurator) gebaut, lauscht nur auf `127.0.0.1` und **stellt keinerlei Internetverbindung her**.
> - **Upgrade von `v22.01` oder älter**: Einmaliges USB-Upgrade auf `v22.1` erforderlich (ältere Stände enthalten noch keinen Ed25519-Public-Key und lehnen signierte OTA-Pakete sicher ab). Bestehende `cl.conf` und `web.pin` bleiben erhalten.
> - **Schwarze Ränder nach dem ersten Flash**: In den iPhone Bluetooth-/CarPlay-Einstellungen *Dieses Fahrzeug ignorieren* wählen und einmal neu verbinden.

---

## Alltag, WLAN-Konsole & Technische Dokumentation

- **Kombiinstrument & HUD**: Mit der Lenkradtaste `VIEW` in die Kartenansicht wechseln; bei aktiver CarPlay-Verbindung wird die Karte automatisch eingeblendet und passt sich Nachtmodus sowie ACC-Darstellung an.
- **Lokale Browser-Vorschau**: [`tools/preview/index.html`](tools/preview/index.html) lokal im Browser öffnen, um `dash.conf` in nativen `1920×639` zu prüfen, `smooth=1`-Dämpfungsbalken zu vergleichen oder 10-Hz-CSV-Fahrdaten abzuspielen.
- **Mobile WLAN-Konsole (`http://touareg.local:8080`)**: Nutzt das freie `bcm2`-Interface (`dhcp.client -n -R`) parallel zu 5-GHz-RSDB-Wireless-CarPlay und `ecm0`-Werks-Telematik. Geschützt durch Fail-Closed-`X-Pin`-Auth (inkl. automatisch im Kombiinstrument angezeigter Ersteinrichtungs-PIN), IP-Backoff und Park-Sperre (`≤ 3 km/h`). Ermöglicht Live-Konfiguration, **Ed25519-signiertes & SHA-256-geprüftes** Offline-OTA mit Rollback, anonymisierten Diagnose-Export, Safe-Mode-Reset sowie optionales Key-only **WLAN-SSH (`:2012`)**.
- **Entwicklungsdoku & Quellcode**: Ausführliche Entwicklungsnotizen (`docs/01–07`, auf Chinesisch) und MH2p-Plattformreferenz (`docs/reference/R1–R5`, jeweils markiert als **Touareg-spezifisch** bzw. **MH2p-allgemein**) befinden sich in **[`docs/README.md`](docs/README.md)**, der vollständige Quellcode unter [`src/`](src) und [`tools/`](tools).

---

## Danksagung & Community

- **[luanqibazao0123-bot](https://github.com/luanqibazao0123-bot)** ([#7](https://github.com/a78c7/touareg-cn-carplay-mods/issues/7)): Impulsgeber für das `v21`-Großschrift-Sport-Design sowie die `60fps`-Kombiinstrument-Engine und Praxishinweise zum `P2878`-Upgrade.
- **Touareg-Freunde Community**: Verlinkt und diskutiert im [deutschen EU-MH2p-P2869-Projektthread](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/).
- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)** ([Website](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)): USB-ModKit-Basis sowie optionale `AppleCarPlay`- / `AndroidAuto`-Module ([CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md), inkl. [fifthBro/pcm5-androidauto-connect-fix](https://github.com/fifthBro/pcm5-androidauto-connect-fix)).
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)** & **[mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)**: Upstream-Basis für `dio_cluster.so` (v0034) und `cl_render` ([CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md)).
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**: Vollbild-Geometriereferenz; **[luka-dev](https://github.com/luka-dev)**: `jxe2jar` und `qnx65-armv7-toolchain`.

---

## Lizenz

- **Eigener Code**: **[PolyForm Noncommercial 1.0.0](LICENSE) + [Nicht-kommerzielle Zusatzbedingungen](LICENSE.ADDITIONAL-TERMS.md)** (enthält keine proprietären VW-/Apple-Binärdateien; Details siehe **[Vor der Installation lesen](#notice)**); Dokumentationstexte unter `docs/` stehen unter **CC BY-SA 4.0**.
- **Drittkomponenten**: `src/cl_render`, `dio_cluster.so`, ModKit sowie optionale LawPaul-Module unterliegen **[CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.md)**. Vollständige Nachweise und Lizenztexte in **[THIRD_PARTY_NOTICES.md](LICENSES/THIRD_PARTY_NOTICES.md)**.
