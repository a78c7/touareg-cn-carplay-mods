# 大众 MH2p 车机改装开发笔记（非官方）

在一台 2023 款国行途锐（中国进口版，车机 MH2p_CN_VWG36_P2878）上，从零做出下面几项改装的完整开发过程：每一步遇到什么问题、怎么查到原因、怎么解决。写给想在别的 MH2p 车上自己动手的人作参考。

MH2p（MIB2+ High）是大众集团多个品牌共用的车机平台。据 [MH2p SD ModKit 网站](https://lawpaul.github.io/MH2p_SD_ModKit_Site/) 的列表并结合中欧改款边界，用这个平台的有：

- 大众：第三代途锐改款前（CR7，欧版 2018–2023.05 / 中规 2019–2023 款，配备 15 英寸 Discover Premium MH2p；不含改款前低配 9.2 英寸模拟仪表领航版、欧版 MY2024+ 中期改款 MIB3 Discover Pro Max 及中规 2025–2026 改款 15.05 英寸 Asterix 新途锐）
- 奥迪（MIB2+ High）：A1、A6、A7、A8、Q3、Q7、Q8、e-tron（各自有年份范围）
- 保时捷（PCM 5）：911（992）、Cayenne（E3）、Macan（95B）、Panamera（976）
- 宾利（MIB2+ High）：添越 Bentayga、飞驰 Flying Spur、欧陆 Continental GT（对应年份车型）
- 兰博基尼（LIS III）：Urus（2018–2023）

本笔记只在途锐上验证过。系统层（QNX、J9 Java、DSI 数据接口、CarPlay 库、显示管理、无线网卡客户端 `bcm2`）是平台共用的，换品牌大概率能参考；界面皮肤、仪表的显示编号和坐标、补丁偏移是各车各版本自己的，要重新找。每条结论都标了适用范围（见下面"标注说明"）。

成品（下载即刷的 U 盘包和全部源码）就在本仓库根目录 **[a78c7/touareg-cn-carplay-mods](../README.md)**。本 `docs/` 目录只放技术说明文字，不含任何大众原厂文件。

> 本项目与大众汽车、苹果公司无关，不是官方资料。改装车机有风险，可能影响保修，后果自负。

## 目录

### 开发过程（按做的顺序）

| 篇 | 内容 |
| --- | --- |
| [01 调试环境与工具](01-调试环境与工具.md) | SSH、Mac 用的 USB 网卡驱动、QNX 交叉编译的坑、不导出车机文件编译插件（桩类）、车机自带的 dmdt 调试工具、车机 shell 和时钟的限制、无效值 |
| [02 CarPlay 导航上 HUD](02-CarPlay导航上HUD.md) | 让 iPhone 发转向数据、把数据送进大众 HMI（Java 插件）、剩余距离/到达时间、断开清理、有线 CarPlay（Cinemo iAP2） |
| [03 CarPlay 地图上仪表](03-CarPlay地图上仪表.md) | 让 iPhone 发第二路视频、ChaCha20 解密、H.264 解码上屏、断开恢复原车地图、车标位置校准 |
| [04 仪表信息框](04-仪表信息框.md) | 从车机读功率/扭矩/G 值/油温等实时数据画在仪表地图上；0–100 计时、最大值、扭矩口径实测、行车记录到 U 盘、左右两个框 |
| [05 全屏 CarPlay 与 U 盘包](05-全屏CarPlay与U盘包.md) | 在车上就地打补丁（不分发大众文件）、「改装设置」程序 |
| [06 给其他车型的建议](06-给其他车型的建议.md) | 换一台 MH2p 车（大众、奥迪、保时捷、宾利、兰博基尼），先查哪些东西、哪些能复用 |
| [07 无线控制台与 SSH 开发](07-无线控制台与SSH开发.md) | 利用闲置 `bcm2` 连手机个人热点、避开开机 `io-pkt` 冲突、iOS 弯撇号与休眠兼容、Ed25519 主机密钥、`touareg.local` 与 `8080` 手机网页控制台 |

### 平台参考（按系统整理，查资料用）

| 篇 | 内容 |
| --- | --- |
| [平台参考说明](reference/README.md) | 标注方法、这里不写什么（不放大众文件、不涉及任何保护机制的绕过） |
| [R1 系统结构与文件位置](reference/R1-系统结构与文件位置.md) | 分区和挂载、主要进程、配置文件、ModKit 脚本机制、重启方法、网络和防火墙 |
| [R2 HMI（J9 Java）](reference/R2-HMI-J9-Java.md) | 插件加载、编译要求、读车辆数据、datapool / ASL 列表编号、BAP NavSD、显示管理接口 |
| [R3 车辆数据接口](reference/R3-车辆数据接口.md) | DSICarVehicleStates、DSICarKombi、DSICarComfort、DSICarDrivingCharacteristics、DSINavigation 属性编号、字段、单位、刷新率、无效值（实测表） |
| [R4 显示系统](reference/R4-显示系统.md) | 屏幕和 displayable 编号、仪表上谁画什么、画到屏上的方法、dmdt |
| [R5 CarPlay 与 iAP2](reference/R5-CarPlay与iAP2.md) | 可拦截的 libairplay 函数、路线指引字段、第二屏、车机命令、安全区 |
| [仪表几何规范](reference/CLUSTER_LAYOUT_SPEC.md) | 途锐 12 英寸数字驾驶舱 `1920×639` 实车像素标定遮挡区与安全布局坐标真相源 |

## 标注说明

- **【途锐特有】**：只在这台车、这个软件版本上验证过，别的车多半要重新找（编号、端口、坐标等）。
- **【MH2p 通用】**：平台层面的结论，同平台的车大概率一样（大众、奥迪、保时捷、宾利、兰博基尼的 MH2p 车机都有参考价值，但只在途锐上验证过）。

## 测试环境

- 车机：MH2p_CN_VWG36_P2878，QNX 6.6.0，NVIDIA Tegra K1（armle-v7），HMI 是 J9 Java（`/ifs/lsd.jxe`）
- 手机：iPhone（iOS 27）；导航 App 测试过高德地图、苹果地图、百度地图
- 刷机：[MH2p SD ModKit](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)（U 盘刷入）

## 致谢

- [LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)：MH2p 用 U 盘改装的基础。
- [fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)、[mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)：MH2p 仪表投屏的开拓者，`dio_cluster.so` 和 `cluster.c` 是本项目的起点。
- [luka-dev](https://github.com/luka-dev)：mib2q-carplay-rgi（CarPlay 路线指引和第二屏协议研究）、jxe2jar、qnx65-armv7-toolchain。
- [davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)：英国途锐全屏方案。

## 许可

本目录下的技术笔记文字内容采用 **CC BY-SA 4.0**：可以转载、修改，需署名并以相同方式共享；源码与 U 盘包的许可证见仓库根目录 [README.md](../README.md#许可证)。

---

# VW MH2p head-unit modding notes (unofficial)

Development notes (Chinese) from building these mods on a 2023 China-market imported VW Touareg (`MH2p_CN_VWG36_P2878`, QNX 6.6, Tegra K1, J9 Java HMI), written as a reference for other cars on the MH2p (MIB2+ High) platform — pre-facelift 3rd-gen VW Touareg CR7 (EU 2018–05/2023, CN 2019–2023 with 15" Discover Premium; excluding base 9.2" analog-gauge trims, EU MY2024+ MIB3 Discover Pro Max, and CN 2025–2026 Asterix facelifts), Audi (A1/A6/A7/A8/Q3/Q7/Q8/e-tron), Porsche PCM 5 (992, Cayenne E3, Macan 95B, Panamera 976), Bentley (Bentayga, Flying Spur, Continental GT), Lamborghini Urus (LIS III). Only verified on the Touareg:

- **CarPlay turn-by-turn on the HUD**: `LD_PRELOAD` hook adds iAP2 route-guidance (0x5200–0x5204) to Identify; a J9 OSGi bundle feeds BAP NavSD through ASL datapool 732 / lists 741, 744, 751, 838. J9 only loads class files ≤ version 48. Wired CarPlay uses a different iAP2 stack (Cinemo NME over `/dev/otg-cinemo`, not VW `libesoiap2`): hook `NmeIAP2Message::Encode`/`Decode` and call `ICinemoIAP_StartRouteGuidanceUpdates`.
- **CarPlay map on the instrument cluster**: advertise a second display (`altScreen`, `viewAreas`) in `/info`; stream 111 is ChaCha20-Poly1305 (key from `PairingSessionDeriveKey` with `DataStream-Salt<connID>`); receive on a port the pf firewall allows (6200); NvMedia H.264 decode; show on displayable 58 with the cluster list `[58, 33]` so the stock map (33) stays bound underneath and returns on disconnect.
- **Live info box** (power, torque, G, rpm, oil temperatures, board-computer values) from `DSICarVehicleStates` / `DSICarKombi`, blended over the cluster map; 0–100 km/h timing interpolated between samples, session maxima, a CSV drive log to a USB drive (Java writes chunks to `/tmp`, a root boot loop appends them to the drive), invalid sentinel values (-3276.5, 32770), two boxes sharing one renderer.
- **Building without dumping the head unit**: compile-only stubs generated from the constant pool of an already-published plugin jar; rebuilt classes are byte-identical.
- **Full-screen CarPlay** patched on the car (38 bytes in two entries of `skin3/viewhandler.zip`), so no VW file is distributed.
- **Wireless console & SSH over phone hotspot (`bcm2`)**: `wpa_supplicant -D qwdi` on the idle `bcm2` STA interface (`dhcp.client -n -R` so stock `ecm0` telematics is untouched), delayed startup to avoid early `io-pkt` crashes, per-car Ed25519 keys, and `touareg_netd` serving `touareg.local` mDNS + PIN-protected HTTP console on `<bcm2_ip>:8080`.

A system-organized platform reference (partitions, processes, HMI plug-in loading, datapool / BAP numbers, DSI vehicle-data tables with measured units and invalid values, display / displayable ids, libairplay hooks, iAP2 route-guidance fields) is in [`reference/`](reference/README.md). No VW files, nothing about bypassing any protection (component protection, feature activation, signatures).

Binaries and sources: [a78c7/touareg-cn-carplay-mods](https://github.com/a78c7/touareg-cn-carplay-mods). Not affiliated with Volkswagen or Apple. Text licensed CC BY-SA 4.0.
