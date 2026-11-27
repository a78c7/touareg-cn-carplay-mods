# `tools/shop-recovery` — 商业改装旧包排查与安全接管工具集

> **重要提醒**：本目录工具**仅供此前花钱刷过商业改装包 / 购买二手改装车、且安装本开源包时遇到 `error: unknown patch state` 或 `WARN: suspected shop` 告警的进阶排查使用**。  
> **原厂未改装车辆绝对不需要使用本目录任何模块**（故刻意不放入 `USB/未选模块/` 与「改装设置」界面，避免普通车主误操作）。

完整背景与实车案例见 [`docs/07-无线控制台与SSH开发.md` 第 12 节](../../docs/07-无线控制台与SSH开发.md#12-已改装车与二手车冲突排查改装店锁死入口与残留公钥)、[Issue #10](https://github.com/a78c7/touareg-cn-carplay-mods/issues/10)、[Issue #16](https://github.com/a78c7/touareg-cn-carplay-mods/issues/16) 与 [Issue #17](https://github.com/a78c7/touareg-cn-carplay-mods/issues/17)。

---

## 工具清单

| 目录 | 性质 | 用途与适用范围 |
| :--- | :---: | :--- |
| **`VW_ShopAudit/`** | **严格只读** | 深度摸底体检工具：不改/不删车机任何文件，自动脱敏并导出 `ShopAudit/audit_N/REPORT.txt`，一次看清启动入口、定时还原暗桩（含 `/mnt/app/eso/bin` 全目录、`runMMX/runHMI/runLSD/java.sh`、`/mnt/system/etc/autorunner`、`/mnt/app/etc/scripts`、`/mnt/ota/{app,system,ot,.boot}`、`/mnt/persist_new` 与 `/mnt/app/{bin,sbin,armle}` 非原厂文件）、`dio_manager.org` 双件套替换、`.j`/`autorunner`/`challenge.pub` 与陌生 SSH 公钥。 |
| **`VW_ShopTakeover/`** | **变体 B 接管** | 针对 **变体 B 商业双件套**（`servicemgrmibhigh_orgapp` + `dio_manager.org` + `/eso/hmi/lsd/.j` + 3 秒死循环杀 `sshd`/`challenge`）实车验证通过（#16 `MH2p_CN_VWG36_P2838`）。先强校验原厂 ELF 并完整备份商业原件到 U 盘 `ShopBackup/orig/`，再原子恢复原厂 `dio_manager`、清理陌生公钥 `rsa-key-20210318`、建立 `servicemgrmibhigh0` 交由 ModKit 接管；车机原 `.org` / `_orgapp` / `.j` 均不删除。 |
| **`VW_ShopRollback/`** | **变体 B 回滚** | `VW_ShopTakeover` 配套后悔药：保留同一根存有 `ShopBackup/orig/` 的 U 盘，清空 `Mods/` 后单独刷入即可还原旧商业版 `dio_manager` 与启动脚本，并移除 `servicemgrmibhigh0` 与 `modkit_persist.sh`（末尾触发 `error: unknown patch state` 属刻意保护，防止 ModKit 再次覆写还原后的脚本）。 |
| **`VW_ShopBypassA/`** | **变体 A 绕过** | 针对 **变体 A 底层定时还原**（109B `servicemgrmibhigh` + `del`/`shadow` 被底层 `/ifs` 或 RCC 开机覆写，但 `servicemgrmibhigh0` 与 `dio_manager` 均为纯原厂 ELF，见 #10 / #17）。先备份校验清单到 U 盘 `ShopBackupA/orig/` 并将原厂 `servicemgrmibhigh0` 原子备份为 `servicemgrmibhigh0.real`，再在 `servicemgrmibhigh0` 放置轻量防重入 wrapper（立即 `exec servicemgrmibhigh0.real` 并在后台拉起 `modkit_persist.sh`）。**注**：末尾触发 `error: unknown patch state` 属预期；后续升级可走手机 `8080` 离线 OTA，真需 U 盘刷机前用下方回滚包还原即可。 |
| **`VW_ShopBypassARollback/`** | **变体 A 回滚** | `VW_ShopBypassA` 配套后悔药：校验车机 `servicemgrmibhigh0.real` 为原厂 ELF 后原子还原回 `servicemgrmibhigh0`，30 秒恢复标准 U 盘 ModKit 刷机兼容。 |

---

## 使用原则（务必遵守）

1. **先只读摸底，再决定是否接管**：永远先单独刷 `VW_ShopAudit`，确认整机无定时还原暗桩、且 `servicemgrmibhigh_orgapp` 与 `dio_manager.org` 均完好后再使用 `VW_ShopTakeover`。
2. **每次只放一个模块进 `Mods/`**：用已准备好的 ModKit U 盘（根目录保留 `Data/`、`Meta/`），**清空 `Mods/`** 后只放入需要运行的那一个目录（例如 `Mods/VW_ShopAudit/Update/install.sh`）。**严禁同时把 `VW_ShopTakeover` 和 `VW_ShopRollback` 放进 `Mods/`**（脚本也会自动拦截中止）。
3. **妥善保管 `ShopBackup/orig/`**：接管成功后将 U 盘根目录生成的 `ShopBackup/orig/` 拷贝一份到电脑留档，并切勿将该备份目录或任何车机 `backup/` 公开上传。
4. **本地沙盒自测**：上车前或修改脚本后，可在 macOS / Linux 下运行内置沙盒测试验证全部 8 个场景（正常接管、幂等防覆写、双包防呆、损坏 ELF 拦截、缺 `dio_manager.org` 拦截、一键回滚与防反覆写、回滚后再接管、以及 `VW_ShopAudit` 只读扫描）：
   ```sh
   sh tools/shop-recovery/test_shop_recovery.sh
   ```
