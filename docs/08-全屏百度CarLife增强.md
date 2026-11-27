# 08 全屏百度 CarLife 增强

[← 目录](README.md)

国行改款前第三代途锐（CR7，15 英寸 Discover Premium，MH2p / QNX 6.6 ARMv7，`MH2p_CN_VWG36_P*`）原厂自带一套 2016 版百度 CarLife 协议栈（`/carlife/carlife` + `/carlife/lib/libcarlifevehicle.so`），存在四周大黑边、分辨率仅 `1024×480`、对接现代 CarLife+ 容易 `SIGABRT` 黑屏闪退、无挡风玻璃 HUD 导航箭头的痛点。这一篇记录 `VW_FullScreenCarLife` 模块（源码目录 [`src/carlife/`](../src/carlife)）如何原地无损增强原厂有线百度 CarLife，并与 CarPlay 保持 100% 独立共存。

## 1. 增强能力与客观边界（与 CarPlay 差异）

| 维度 | 原厂 2016 版状态 | `VW_FullScreenCarLife` 实测效果 |
| :--- | :--- | :--- |
| **15 寸中控画面** | 居中缩框 `1152×592`，协议仅请求 `1024×480`，黑边大且发糊 | **点对点满屏零黑边**：`skin3` 画布扩至 `1673×748` + 手机端协商 `1664×736`（16px 宏块对齐）直通输出 **【途锐特有】** |
| **帧率与流畅度** | 默认低帧率，手机端往往不响应旧版 `98311` 帧率参数 | 主动下发 `0x0001800C`（`98316`）真帧率控制，默认 `1664×736@20fps`（实测稳定 `19.9~20.0 fps`、`~2.6 Mbps`，手机发热可控；可通过 `res.conf` 调整） **【MH2p 通用】** |
| **现代 CarLife+ 防崩** | 遇新版未知指令或 `13–19 KB` PNG 转向图包会触发 `SIGABRT` 黑屏闪退 | **`CConnectManager::readCmdData` 防崩护盾**：包头层安全抽干未知消息与 `0x10031/33/34` 问题包，原生零崩溃 protobuf wire 解析 `0x00010030` **【MH2p 通用】** |
| **挡风玻璃 HUD 导航** | 原厂无 CarLife HUD 导航支持 | 主动完成 `0x00018043 -> 0x00010044 -> 0x00018045` 订阅握手，74 项转向码映射至 iAP2 写入 `/tmp/cluster_cp.state`（需同时勾选「导航上 HUD」即 `VWClusterNav_v6.jar`） **【途锐特有】** |
| **拔线自动清 HUD** | 拔线时 `smartphone_integrator` 直接 `SIGKILL` 杀进程，状态文件易残留 | `SPAWN_SETSID \| SPAWN_NOZOMBIE` 派生剥离 `LD_PRELOAD` 的独立守护子进程 + `/proc/self/exefile` 门禁，拔线约 1 秒自动写入 `DEACTIVATE` 清 HUD **【MH2p 通用】** |
| **仪表激活弹窗屏蔽** | 导航时原厂仪表反复弹出「导航已在移动设备上激活」 | 掩码 `ModuleStatus` 中 `NAVI=1 -> 0`，不再打断仪表显示 **【MH2p 通用】** |
| **蓝牙听歌防抢焦点** | 手机端上报媒体状态时易强占车机媒体焦点顶掉蓝牙音乐 | 内置可选 `bt_audio_lock` 开关，掩码 `ModuleStatus(MUSIC=1 -> 0)` 并拦截 `mediaRegisterInit/Resume`，保持 `VW BT` 音乐不断线、USB PCM 导航播报照常直通 **【MH2p 通用】** |

**两项硬件/协议客观边界（实事求是说明）**：
1. **仅支持有线 USB 连接**：逆向核实国行 `P2878` 原厂 `smartphone_integrator`、`bluetooth` 与 `carlife` 均未实现无线 CarLife 发现与会话管理（`CDNSServiceDiscoveryService` 写死 `_carplay-ctrl._tcp`，`CarLifeSmartPhoneAppS::wlanDeviceRemoved` 仅为日志空桩），原厂无线 CarLife 入口为空壳。本模块不侵入修改车机无线栈与 `smartphone_integrator` 核心进程。
2. **不支持 12 寸仪表第二屏地图**：与 CarPlay 原生双屏（主屏 + `altScreen` 独立第二路视频流）不同，CarLife 协议仅输出单路视频流；强制把中控单路流复刻裁剪投到仪表盘存在构图受限与观感落差，因此正式版仅保留实车 100% 验通的 **15 寸全屏 + 防崩护盾 + 挡风 HUD 转向箭头 + 蓝牙防抢焦点**，不附带仪表同屏守护进程。

## 2. 核心架构与修改边界

本模块**不分发任何大众原厂文件**，所有改动在车机本地原地完成且支持一键 100% 无损卸载还原：

1. **皮肤视图补丁（`vhpatch_carlife` + `carlife_fullscreen.patch`）** **【途锐特有】**
   - 仅修改 `/mnt/app/eso/hmi/lsd/Resources/skin3/viewhandler.zip` 内的 `Cm_429C.jxe`（共 19 字节几何与透明边框常量）。
   - `P2878` 优先走精确补丁表核对（条目名、大小、原厂/补丁后 CRC-32 与逐字节比对）；其余国行 15 寸 `P` 版走 `CARLIFE_MAIN` 标识 + J9 字节码唯一指纹兜底，避免与 MirrorLink（`Mam.jxe`）误碰；非 15 寸屏或遇到任何歧义直接中止，绝不硬写。
   - **绝不触碰** CarPlay 对应的 `Am.jxe` / `Cm_0409.jxe`。
2. **皮肤画布配置（`skin3/info.txt`）** **【途锐特有】**
   - 仅将 `Layout.CarLife.Canvas_Dimension.width=1152` / `height=592` 改为 `1673` / `748`，严格保留文件原有的 CRLF 换行与 CarPlay 配置行。
3. **运行时探针（`carlife_probe.so`）** **【MH2p 通用】**
   - 安装位置：`/mnt/app/eso/bin/apps/carlife_mod/carlife_probe.so`。
   - 仅在 `/mnt/system/etc/eso/production/smartphone_integrator.json` 的 `"carlife"` 子块 `"envs"` 头部插入 `LD_PRELOAD=/mnt/app/eso/bin/apps/carlife_mod/carlife_probe.so`。
   - **绝不修改** `"carplay"`、`"alibox"`、`"gal"` 等任何其他配置块；不写入 CarPlay 专属的 `/cluster_h264_shm`。

## 3. 关键技术细节与实车改进

1. **点对点满屏协商与真帧率控制**：
   - 在 `0x00018052`（`FeatureConfigResponse`）中追加 `DISABLE_RESOLUTION_OPTION=1` 与 `I_FRAME_INTERVAL=30`，拦截 `cmdVideoEncoderInit` 将原厂 `1024×480` 替换为 `res.conf` 指定的 16px 宏块对齐分辨率（默认 `1664×736`）；
   - 通过 `0x0001800C`（`98316`）向手机端下发实际帧率上限，并保留 `/tmp/carlife_fps_req` 实时热切接口（按 `st_mtime` 去重避免重复下发）。
2. **H.264 SPS/PPS 参数集缓存修复**：
   - 探针在解析 Annex-B 视频回调时持续提取并缓存最新的 SPS（NAL type `7`）/ PPS（NAL type `8`）参数集，消除流切换或关键帧起帧阶段因缺失参数集导致的解码异常。
3. **蓝牙听歌音频焦点锁（`bt_audio_lock`）**：
   - 实车分析 `/carlife/carlife` 与 `/carlife/audiotable.json` 确认：原厂导航语音（TTS）走 USB PCM `ANN_M` 独立混音轨（`RESOURCE_AUDIO_TTS`），压低背景音后自动恢复，**不会抢占蓝牙音乐**；进出 CarLife 画面仅切 `RESOURCE_VIDEO`，同样不抢音乐焦点。
   - 真正导致顶掉蓝牙音乐的唯一路径是手机端上报 `ModuleStatus(moduleID=3, statusID=1)` 或发送 `MSG_MEDIA_INIT` / `MSG_MEDIA_RESUME`，触发 `DSICarLifeImpl::acquireAudioResource()` 抢占 `RESOURCE_AUDIO_MEDIA` 且从不主动归还。
   - 开启 `bt_audio_lock` 后，探针将 `moduleID == 3 && statusID == 1` 掩码为 `0` 并 Hook 拦截 `CCarLifeLib::mediaRegisterInit` / `mediaRegisterResume`，从根源稳住 `VW BT` 蓝牙听歌，同时保持 USB PCM 导航语音 100% 直通。

## 4. 运行时配置与热调开关

以下开关按需启用，模块卸载时会自动全部清理：

| 文件路径 | 默认值 | 说明 |
| :--- | :--- | :--- |
| `/mnt/app/eso/bin/apps/carlife_mod/res.conf` | `1664 736 20` | 持久化分辨率与帧率，格式 `宽 高 帧率`（宽/高须为 16 的倍数，帧率 `3~60`；设 `0 0 0` 为直通原厂；临时覆盖路径 `/tmp/carlife_res.conf`） |
| `/tmp/carlife_fps_req` | 不存在 | 运行中热切帧率（写入 `3~60` 整数如 `25` 或 `30` 即刻下发 `0x0001800C`） |
| `/mnt/app/eso/bin/apps/carlife_mod/bt_audio_lock` | 不存在（关闭） | 持久化防蓝牙抢焦点开关：存在时拦截 CarLife 抢占 `RESOURCE_AUDIO_MEDIA`，稳住 `VW BT` 听歌（运行中热开关 `/tmp/carlife_bt_audio_lock`） |
| `/mnt/app/eso/bin/apps/carlife_mod/disable` 或 `/tmp/carlife_probe_off` | 不存在 | 存在时停用全部探针改写，仅透传原厂行为 |
| `/tmp/carlife_hud_off` | 不存在 | 存在时停用挡风 HUD 导航状态输出 |
| `/tmp/carlife_allow_navi_popup` | 不存在 | 存在时恢复原厂仪表「导航已在移动设备上激活」弹窗 |
| `/tmp/carlife_force_touch_scale` | 不存在 | 存在时启用额外触控坐标缩放（默认关闭，J9 HMI 已原生按编码分辨率映射触控） |
| `/tmp/carlife_probe.log` | 自动生成 | 运行时诊断日志（记录分辨率协商、实际帧率/码率、防崩护盾拦截、HUD 订阅与 `bt_audio_lock` 状态） |

## 5. 音频通道说明

大众 MH2p 原厂 CarLife 采用双通道音频分离设计：

- **导航语音播报（TTS）**：直接走有线 USB PCM 通道在车机混音播报（压低背景音乐后自动恢复）。
- **第三方音乐 / 播客 / 视频媒体**：受安卓系统音频抓取机制限制，将手机同时连接车机蓝牙（`VW BT`）并切至蓝牙媒体即可通过车机音响播放，与导航播报互不冲突；若发现手机端 CarLife+ 切歌或启动时误抢车机媒体焦点，开启上表 `bt_audio_lock` 即可锁定蓝牙通道。
- **关于歌名显示**：蓝牙音源下原厂仪表媒体页与中控卡片已正常显示 AVRCP 歌名并支持方向盘切歌；为避免触控穿透与倒车/360/空调浮层 Z-order 风险，本模块不在 CarLife 全屏视频上强行叠画悬浮窗。

## 6. 从源码交叉编译

需要 Linux（x86_64）环境、`clang`、`arm-linux-gnueabi-ld` / `arm-linux-gnueabi-strip`、QNX 6.5/6.6 ARMv7 sysroot（`sdp/target/qnx6`）以及 `zlib 1.3.1` 源码目录，全部通过环境变量传入：

```sh
QNX_TARGET=/path/to/sdp/target/qnx6 \
ZLIB_SRC=/path/to/zlib-1.3.1 \
sh src/carlife/build.sh
```

> **链接器红线**：必须使用 GNU `arm-linux-gnueabi-ld` 并携带 `--hash-style=both` 同时生成 `DT_HASH` 与 `DT_GNU_HASH` 段；**切勿改用 `lld`**，否则在 QNX 6.6 动态加载器下 `dlsym(RTLD_NEXT)` 会失效。

## 7. 与 CarPlay 共存及实车验证结论

- **100% 独立共存**：本模块与全屏 CarPlay、无线 CarPlay、仪表大地图及无线控制台完全独立——视图补丁只改 `Cm_429C.jxe`，进程注入只改 `"carlife"` 块，且在桌面「改装设置」中默认关闭、按需勾选，纯 CarPlay 车主不勾选则零影响。
- **实车验证结论（`MH2p_CN_VWG36_P2878`）** **【途锐实测】**：
  - `1664×736@20fps` 点对点铺满 15 寸大屏零黑边，实测稳态 `19.9~20.0 fps`、`~2.6 Mbps`，触控精准跟手；
  - 现代 CarLife+ 导航全程零 `SIGABRT` 闪退，原厂「导航已在移动设备上激活」弹窗彻底静默；
  - 配合 `VWClusterNav_v6.jar` 时挡风玻璃 HUD 转向箭头与距离同步正常，物理拔线约 1 秒内自动清除 HUD 无残留；
  - 开启 `bt_audio_lock` 后蓝牙听歌（`VW BT`）持续播放不被抢焦点，USB 导航语音正常混音播报；
  - U 盘一键卸载后 `viewhandler.zip`、`skin3/info.txt`、`smartphone_integrator.json` 及运行配置 100% 原地还原。
