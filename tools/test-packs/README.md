# `tools/test-packs` — 社区排障与测试专用下载包

> **说明**：本目录仅存放供 GitHub Issue 排障与实车测试直接下载的小体积纯脚本测试包（不含大众原厂文件、不含大体积二进制）。  
> **原厂正常车辆无需下载本目录任何文件**，请前往 [Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) 下载正式安装包。

| 文件名 | 性质 | 对应源码 | 适用场景 |
| :--- | :---: | :--- | :--- |
| [`VW_ShopAudit_v3_readonly.zip`](VW_ShopAudit_v3_readonly.zip) | **严格只读（推荐）** | [`tools/shop-recovery/VW_ShopAudit`](../shop-recovery/VW_ShopAudit/Update/install.sh) | 商业改装旧包极速深度只读摸底（单次目录预筛提速 ~20 倍防 SWDL 看门狗超时、`01/03/04` 优先落盘、覆盖变体 A 定时还原寻源含 `.pers`/`lsd.sh`/`config/production` 与变体 B `dio_manager.org` 体检，自动脱敏） |
| [`VW_ShopAudit_v2_readonly.zip`](VW_ShopAudit_v2_readonly.zip) | **严格只读（旧版）** | [`tools/shop-recovery/VW_ShopAudit`](../shop-recovery/VW_ShopAudit/Update/install.sh) | 历史版本（慢盘上可能因扫描超时缺 `03/04/REPORT.txt`，请改用上方 `v3`） |
