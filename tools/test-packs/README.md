# `tools/test-packs` — 社区排障与测试专用下载包

> **说明**：本目录仅存放供 GitHub Issue 排障与实车测试直接下载的小体积纯脚本测试包（不含大众原厂文件、不含大体积二进制）。  
> **原厂正常车辆无需下载本目录任何文件**，请前往 [Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) 下载正式安装包。

| 文件名 | 性质 | 对应源码 | 适用场景 |
| :--- | :---: | :--- | :--- |
| [`VW_ShopAudit_v4_readonly.zip`](VW_ShopAudit_v4_readonly.zip) | **严格只读（推荐）** | [`tools/shop-recovery/VW_ShopAudit`](../shop-recovery/VW_ShopAudit/Update/install.sh) | 商业改装旧包极速深度只读摸底 v4：在 v3 基础上展开排查 `/mnt/system/etc/autorunner/`、`/mnt/app/etc/scripts/`、`/mnt/ota/.boot/`、`/mnt/persist_new/`、`/mnt/misc1/`、`/mnt/app/eso/hmi/lsd/jars/` 及 `/mnt/app/{bin,sbin,armle}` 非原厂时间戳文件，自动脱敏 |
| [`VW_ShopAudit_v3_readonly.zip`](VW_ShopAudit_v3_readonly.zip) | **严格只读（旧版）** | [`tools/shop-recovery/VW_ShopAudit`](../shop-recovery/VW_ShopAudit/Update/install.sh) | 历史版本（已提速防超时，v4 补充展开了 `autorunner` 与 `/mnt/app/etc/scripts`） |
| [`VW_ShopAudit_v2_readonly.zip`](VW_ShopAudit_v2_readonly.zip) | **严格只读（旧版）** | [`tools/shop-recovery/VW_ShopAudit`](../shop-recovery/VW_ShopAudit/Update/install.sh) | 历史版本（慢盘上可能因扫描超时缺 `03/04/REPORT.txt`，请改用上方 `v4`） |
