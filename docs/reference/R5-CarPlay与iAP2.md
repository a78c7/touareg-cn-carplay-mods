# R5 CarPlay 与 iAP2

[← 平台参考](README.md)

测试手机：iPhone，iOS 27。导航 App：高德地图、苹果地图、百度地图。

## 1. 进程结构

- CarPlay 跑在 `dio_manager` 进程里，用车机自带的 `libairplay`（途锐是 **320.17.1**，HomeKit 配对）。【途锐实测】
- `dio_manager.json` 里的车型表同时列了大众 G36、奥迪、保时捷（PAG），说明这几家用的是同一套。【途锐实测】
- 注入方法：在 `smartphone_integrator.json` 里给 CarPlay 进程的 `envs` 加 `LD_PRELOAD`，多个库用冒号分隔。`dio_manager` 本体不用改。【途锐实测】
- 链接注意：自己的 .so 要用 GNU ld `--hash-style=both`。用 lld 链接时 `dlsym(RTLD_NEXT)` 拿不到原函数，CarPlay 会连不上；只有 sysv hash 则根本加载不了。原厂库只有 GNU hash。【途锐实测】

## 2. libairplay 里能拦截的函数

这些都是导出函数，库内部调用也走 PLT，所以 `LD_PRELOAD` 能拦到。【途锐实测】

| 函数 | 用途 |
| --- | --- |
| `AirPlayCopyServerInfo` | 生成 `/info` 回复：声明有几块屏、支持哪些功能 |
| `AirPlayReceiverSessionSetup` | 处理 SETUP：控制通道和各路媒体流 |
| `AirPlayReceiverSessionTearDown` | 会话结束 |
| `AirPlayReceiverSessionSendCommand` | 车机给手机发命令。途锐上是 4 个参数：会话、命令、完成回调、上下文 |
| `AirPlayReceiverSessionPlatformControl` | 手机发给车机的命令（比如 `suggestUI`） |
| `AirPlayReceiverSessionSetNightMode` | 日夜模式切换 |
| `AirPlayReceiverSessionSetHomeKitSecurityContext` | 写入配对会话 |
| `PairingSessionDeriveKey` | 派生各路流的密钥 |

- `AirPlayCopyServerInfo`、`AirPlayReceiverSessionSetup` 实际只用前 3 个参数。【途锐实测】
- 途锐的库里**没有** `AirPlayReceiverSessionScreen_CopyDisplaysInfo`（别的版本的方案用它），要从 `/info` 下手。【途锐实测】
- 库里有 `SCREEN_VIDEODUMPING` 环境变量，看名字是转储主屏 H.264。【未验证】

## 3. 路线指引（iAP2 0x5200 系列）

- 原车 `dio_manager.json` 没有声明 iAP2 路线指引消息 **0x5200–0x5204**，所以 iPhone 根本不发转向数据。在 iAP2 Identify 消息里补上声明，iPhone 就开始发 0x5201（RG_Update）和 0x5202（RG_Maneuver）。【MH2p 通用】
- 原厂 CarPlay 只告诉 HMI 一个"CarPlay 路线指引激活"标志，没有任何箭头、距离接口。

已用到的字段（【途锐实测】）：

| 字段 | 内容 |
| --- | --- |
| 0x06（RG_Update） | 剩余时间，秒 |
| 0x07（RG_Update） | 剩余距离，米。实测 9500 m / 960 s 和仪表显示的 9.5 km、到达时间一致 |
| 0x13（RG_Update，SourceName） | App 名：`高德地图`、`Apple Maps`（有时 `Apple地图`）、`百度地图`。**只在开始导航时发一次**，重连后为空，要自己记住 |
| 当前道路名 | 苹果地图发，高德不发 |
| ManeuverType（RG_Maneuver） | 1 左转、2 右转、13/14 靠左/靠右、20/21 尽头左转/右转、22/23 左侧/右侧出口（其余值没整理） |

"正在导航"的判断：会话有效、路线状态 ≥ 1 且 ≠ 2、App 支持路线指引。【途锐实测】

**有线和无线是两套 iAP2**：无线走大众 `libesoiap2`（类 `CIAP2Link`、`CIAP2ControlSession`）；有线走 Cinemo NME（`/dev/otg-cinemo`，`libNmeSDK.so` / `libNmeVfs.so` / `libNmeBaseClasses.so`）。两边都要补路线指引声明，Cinemo 一侧的拦截点：`NmeIAP2Message::Encode` / `Decode`（经 PLT，可 `LD_PRELOAD`）、`CinemoCreateIAP`、`ICinemoIAP_StartRouteGuidanceUpdates`。Cinemo 自带的路线指引解析只覆盖 0x5201 参数 0–15（没有 SourceName 和车道），所以按原始字节自己解析。详见 [02 篇](../02-CarPlay导航上HUD.md) 第 10 节。【途锐实测】

## 4. 第二屏（仪表地图，altScreen）

【途锐实测】

1. `/info` 的 `displays` 数组里加一块屏（途锐用 1920×640、物理 292×97 mm、和主屏不同的 uuid），`extendedFeatures` 加 `altScreen`、`viewAreas`。
2. 控制通道 SETUP 的回复里加 `enabledFeatures: [altScreen, viewAreas]`。
3. iPhone 会发一个 type **111** 的媒体 SETUP（主屏是 110）。自己开端口接收，端口必须是防火墙放行的（途锐用 6200，见 [R1](R1-系统结构与文件位置.md) 第 7 节）。
4. 收到的包：op=1 是明文 avcC（SPS/PPS，NAL 长度 4 字节），op=0 是加密的视频帧（约 20 帧/秒），op=5 每秒一个（约 370 字节）。
5. 视频是 H.264 High@3.2，1920×640，关键帧很少（38 秒只有 2 个）。发 `forceKeyFrame` 命令能要到一个关键帧。

加密：途锐这版 libairplay 是 **ChaCha20-Poly1305**，不是网上旧版本的 AES-CTR。密钥派生和 nonce 格式见 [03 篇](../03-CarPlay地图上仪表.md) 第 3 节。【MH2p 通用（同版本 libairplay）】

## 5. 车机能发的命令

用 `AirPlayReceiverSessionSendCommand` 发，iPhone 直接执行，不需要 App 配合。【途锐实测】

| 命令 | 作用 |
| --- | --- |
| `showUI`（`maps:/car/instrumentcluster/map?...`） | 让手机在仪表屏上显示地图 |
| `forceKeyFrame` | 要一个关键帧。**不要反复发**：每帧都发会让手机不停重置编码器，只发 op=1 不发画面 |
| `setNightMode`（带仪表屏 uuid） | 仪表屏日夜模式。原车的日夜切换只对主屏生效，要自己给仪表屏再发一次 |
| `updateViewArea {uuid, viewAreaIndex}` | 切换安全区，毫秒级生效 |
| `changeMapZoomLevel {uuid, zoomDirection}` | 0 放大、1 缩小，以屏幕中心缩放。车机没法让地图"回到车位置" |

手机发来的 `suggestUI`：高德的 urls 是空的，仪表场景完全由高德自己画。【途锐实测】

## 6. 安全区（viewAreas / safeArea）

- 仪表地图是手机 App 自己画的，车机能影响的只有安全区：`/info` 里仪表屏的 `viewAreas` 数组，每项是"整个可见区域 + 一个 safeArea 子矩形"。**可见区域必须保持整屏**，只改 safeArea。【途锐实测】
- **安全区超出画面，iPhone 会拒绝整个 CarPlay**：会话在 `/info` 之后几十毫秒就被拆掉，中控 CarPlay 也连不上，手机连续失败几次后会停一阵不再重连。生成时一定要把安全区限制在画面内。【MH2p 通用】
- 三个 App 对安全区的理解完全不同：【途锐实测，iOS 27】

| App | 车标位置 |
| --- | --- |
| 苹果地图 | 严格在安全区中心（导航和巡航都是） |
| 高德巡航 | 安全区中心 |
| 高德导航 | 纵向固定在整个画面约 85% 高度，不受安全区影响；横向在安全区中心，或往左/右偏**约 213 像素**（固定像素，跟安全区宽度无关），偏向跟着目的地方向走（见下） |
| 百度导航 | 安全区中心右边约 165 像素 |
| 百度巡航 | 完全不理安全区 |

- 高德导航的横向偏移【途锐实测，一次导航 53 张截图，自动找车标位置并量出"指向目的地的红线"方向】：
  - 车标只出现在三个位置：安全区中心、中心左边约 213 像素、中心右边约 213 像素。
  - 偏向跟**目的地方向**一致：红线偏右时偏右，偏左时偏左，跟下一个机动是左转、右转还是直行无关。目的地几乎正对车头时，往哪边偏不确定。
  - CarPlay 重连后导航还在继续时，经常完全不偏（4 次里 3 次）；开了"车道级导航"时也观察到不偏。
  - 车机拿不到目的地方向，iAP2 路线指引里没有这项，所以**按路口方向补偿是错的**，碰巧对上时看起来有效，反向时车标会偏约 420 像素。早期的"往转弯方向挪"就是被少量样本误导的。要每次都居中，只能从画面里判断车标在三个位置中的哪一个，再切换对应的安全区。
- 判断是哪个 App 要用 SourceName，但它只在开始导航时发一次：重连、车机重启后导航还在继续，就拿不到了。要把上次的 App 名存在重启不清空的地方（途锐用 `/mnt/ota`）。
- 想让车标更低：在 `/info` 里把仪表屏报成 1920×960（物理高度按比例放大，像素密度不变），显示时只取中间 640 行。手机按 960 高的画面放车标，落到仪表上就靠下。【途锐实测】

## 7. 空闲心跳（加了第二屏以后要处理）

【途锐实测，libairplay 320.17.1】

- 中控视频流被拆掉时（手机把画面交回车机，日志 `Modes changed: screen accessory`），libairplay 启动 `_IdleStateKeepAliveThread`（线程名 `AirPlayKeepAliveReceiver`），等手机发心跳。**30 秒收不到**就 `kTimeoutErr -6722`、`AirPlay session ended: Dur=30 seconds`，`dio_manager` 重启、重连。
- 手机只在**没有任何流**时才发心跳。第二屏（stream 111）是自己的钩子接收的，libairplay 不知道它存在：仪表视频一直在传，手机不发心跳，libairplay 却以为没流了，于是车机一显示原车界面，CarPlay 就每 30 秒断一次；重连后还停在原车界面，就一直循环。
- 这个线程就是一个 `select()`（读集合：心跳 socket + 退出管道，超时 30 秒，返回 0 判超时）。`select` 从 libc 导入，`LD_PRELOAD` 能拦：只处理这个线程名、超时 ≥ 20 秒的调用，分段等待，第二屏还在收包就重置倒计时；第二屏也停了，按原来的 30 秒超时。
- 另一种断开 `CarPlayControlClientSTALeft` 是手机 Wi-Fi 离开了车机热点，属于无线链路问题，和这里无关。

## 8. CarPlay Ultra

纸面调研的结论：在这个平台上做不了完整的 Ultra。主题资产由苹果按车厂的 MFi 证书和资产编号下发，本地渲染器和资产格式不公开，新的同步、车辆状态协议也没有公开文档；这版 libairplay 也不包含这些功能。能做的只是一些子集，比如在仪表地图上叠加自绘的车辆数据。【未验证：没上车试】
