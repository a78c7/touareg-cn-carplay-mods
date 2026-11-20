# 02 CarPlay 导航上 HUD

[← 目录](README.md)

目标：CarPlay（苹果地图、高德等）导航时，HUD 和仪表上显示转向箭头、到路口距离、路名、车道，就像原车导航一样。对应 U 盘包里的 `VW_HUD_1_RouteGuidance` + `VW_HUD_2_ClusterNav`。

## 1. 原车 CarPlay 为什么不给 HUD 发导航

- 车机 `/mnt/system/etc/eso/production/dio_manager.json` 里没有声明 iAP2 路线指引消息（0x5200–0x5204），所以 iPhone 根本不发转向数据。**【MH2p 通用】**
- 原厂 CarPlay 只向仪表报一个标志"CarPlay 路线指引激活"，仪表据此显示"导航已在移动设备上激活"，没有任何箭头/距离接口（`DSICarplay` 里也没有）。

## 2. 拿到转向数据：dio_cluster.so

- 用 [fifthBro/mh2p-cluster](https://github.com/fifthBro/mh2p-cluster) 公开的 `dio_cluster.so`（CC BY-NC-SA 4.0）。在 `smartphone_integrator.json` 里 carplay 进程的 `envs` 加 `LD_PRELOAD` 注入 `dio_manager`，**不修改 dio_manager 本体**。车机的 enforcer 沙箱不拦 LD_PRELOAD。
- 它在 iAP2 Identify 消息里补上 0x5200 系列声明，iPhone 接受后持续发 RG_Update（0x5201）/ RG_Maneuver（0x5202），写到 `/tmp/cluster_cp.state`。
- 坑：`dio_manager` 同一进程会加载两次钩子，第一次是 root、第二次已降权；第一次建的 0644 文件第二次打不开 → 状态文件一直是空的。解决：开机脚本（ModKit 的 Persist）先建好 0666 的空文件。**【MH2p 通用】**

## 3. 把数据送进大众 HMI：Java 插件

- 反编译 `lsd.jxe`（方法见 [01 篇](01-调试环境与工具.md)）。往仪表/HUD 发导航的是 BAP NavSD（LSG 50）：`RGStatus`、`ManeuverDescriptor`、`DistanceToNextManeuver` 等。它们都实现 `NavigationServiceListener`，数据来自 ASL datapool：
  - 路线指引状态 = datapool int **732**
  - 箭头 = list **751 / 838**
  - 距离 = list **741**（米，unit 0）
  - 路名 = list **744**
- 这几个函数只看上面这些值，不看"手机导航激活"标志（datapool bool 2910019），所以 CarPlay 连着也能用。
- 写一个 OSGi bundle（[`src/hud-java`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/src/hud-java)），读 `/tmp/cluster_cp.state`，往这些 datapool 写值，原车 NavSD 就会自己把箭头、距离、路名发给仪表和 HUD。
- 注册方式：jar 放 `/mnt/app/eso/hmi/lsd/jars/`（在 `-Xbootclasspath/a` 上），`bundles.properties` 加 `Bundle.Name.97 / Bundle.Activator.97`，并把 97 加到 `Bundle.Startup.Lateload` **最后**。加载逻辑在 `de.vw.mib.osgi.internal.BundleRegistry`，GUI 就绪后按顺序 start；顺序启动遇到异常会中断后面所有 bundle，放第一位会连累语音模块。
- 注意：大众的 `lsd/jars/*.jar` 是追加在 `lsd.jxe` 之后，不能像奥迪那样用同名类覆盖原类，只能新增 bundle 去调用服务。

## 4. 大坑：J9 只认 Java 1.4 的 class

- 车机自带的附加 jar class 版本全部 ≤ 48。我们第一次编译成 49 → 类加载静默失败，**一行日志都没有**，排查了好几轮。
- 解决：JDK 8 `javac -source 1.4 -target 1.4`（版本 48），不用 StringBuilder、泛型、自动装箱等 1.5 特性。**【MH2p 通用】**
- 建议插件的 `Activator.start` 第一行就写日志（不依赖 HMI 的自写 Log 类），否则加载失败和逻辑失败分不清。

## 5. 去掉"导航已在移动设备上激活"

- 来源：BAP NavSD InfoStates（FctID 38）在 CarPlay/AA/CarLife 路线指引时发 6。只有它调用 `isCarPlayRouteGuidanceActive()`。
- 插件启动 30 秒后用反射把 `APIFactory.carPlayService` 换成一个包装，开关文件存在时这个函数返回 false。
- 只装 HUD、不装仪表地图时保留这条提示（原车行为）；装了仪表地图才去掉。

## 6. 仪表显示剩余距离和到达时间（原车样式）

- 原车导航的调用方式（反编译 `HsmTargetBap.updateDistanceTime`）：`ASLNavBAPAPI.updateBapDistanceToDestination(米, 255, 类型)`（单位 255 = 由 HMI 自动选 km/m）和 `updateBapTimeToDestination2(小时, 分钟, 时间格式)`，后者同时更新"剩余时间"和"到达时间"（到达时间 = 车机当前时间 + 剩余时间）。
- 数据：`distance_remaining`（米，TLV 0x07）、`time_remaining`（秒，TLV 0x06）。实测 9500 m / 960 s，和仪表显示的 9.5 km、到达时间一致。
- 插件导航中每 2 秒或数值变化时发送，结束导航发无效值。
- 当前道路写进 BAP CurrentPositionInfo（ASL 字符串 datapool 738），但高德不发"当前道路名"，苹果地图才有。

## 7. 断开后 HUD/仪表还挂着旧导航信息

- dio_cluster.so 在 CarPlay 断开后不清空 `/tmp/cluster_cp.state`，插件看到的还是"导航中"，旧箭头一直挂着；下次连上但不导航时还会把旧路线重新激活。
- 修复：自己的钩子在 CarPlay 会话完整 TearDown 和新会话开始时清空这个文件，插件 3 秒后自动撤下导航显示。

## 8. 路名处理

- 路名去掉"进入"等前缀，HUD 才放得下。
- 高德把没名字的路写成"进入 无名道路"。无名/未命名/空路名统一显示"无名道路"（仪表顶部那一行和 HUD 用的是同一个 TurnToInfo 字段）。
- 车道指引只在地图有车道数据的路口出现。

- v19.7：高德的「请行驶到 XX 门」去掉动作前缀后显示「XX 门」。多个路名以空格分隔时（如「A路 B路」），只保留第一个路名。

## 9. 状态文件里有什么

`/tmp/cluster_cp.state` 是 dio_cluster.so 写的 key=value 文本，常用的键：

- `session_active`、`route_state`、`source_supports_rg`：判断是否在导航（`session_active && route_state>=1 && route_state!=2 && source_supports_rg!=0`）
- `slot_0_type`：下一个动作的 iAP2 ManeuverType（1 左转、2 右转、13/14 靠左/靠右、20/21 尽头左/右转、22/23 左/右侧出口……）
- `source_name`：导航 App 名（iAP2 SourceName，TLV 0x13）：`高德地图`、`Apple Maps`（有时 `Apple地图`）、`百度地图`。**只在开始导航时发一次**。
- `distance_remaining`、`time_remaining`、`current_road`

## 10. 有线 CarPlay：另一套 iAP2 协议栈

- 现象：无线 CarPlay 一切正常，**有线**时 HUD 没有箭头，仪表也不知道在导航；仪表上的 CarPlay 地图照常（视频走 AirPlay，和 iAP2 无关）。
- 原因：`dio_manager` 里有两套 iAP2。无线时用大众自己的 `libesoiap2`（`dio_cluster.so` 就挂在它上面）；有线时用 **Cinemo NME** 的 iAP2，走 USB 设备模式（`dio_manager.json` 的 `"device":"iap://ffs:///dev/otg-cinemo"`，二进制里的 `CIAP2ServiceCinemo`），`dio_cluster.so` 一条消息都看不到。`iap2connectionmanager` 只做 USB 主机模式下的引导（认证、Wi-Fi 共享、蓝牙配对），给它注入没用。**【MH2p 通用（推测，同 dio_manager）】**
- Cinemo 本身支持路线指引（`libNmeVfs.so` 有 `StartRouteGuidanceUpdates`、`RouteGuidanceDisplayComponent`），只是识别信息里没声明。
- 做法（不改 `smartphone_integrator.json`，代码编进本来就预加载在 `dio_manager` 里的库）：
  - 拦 `NmeIAP2Message::Encode(NmeArray<uint8_t>&)`（`libNmeBaseClasses.so`，`libNmeVfs.so` 经 PLT 调用）：编码好的 0x1D01 识别信息按字节补上 0x5200 系列消息号和 0x001E 路线指引组件，做法和 `dio_cluster.so` 相同；`NmeArray` 是 `{data, count, capacity, grow}`，用 libc `realloc` 扩容。
  - 拦 `NmeIAP2Message::Decode(const uint8_t*, int)`：拿到 0x5201/0x5202/0x5204 原始消息，用和无线同一套解析写 `/tmp/cluster_cp.state`，HUD/仪表那边不用改。
  - 拦 `CinemoCreateIAP(ICinemoIAP **out)`（`dio_manager` 经 PLT 调用）记下对象；识别被接受、车机发出第一条位置信息（0xFFFB）后，另起线程调 `ICinemoIAP_StartRouteGuidanceUpdates(iap, count, uint16_t *componentIds)`（`libNmeSDK.so` 导出）。0x5200 的参数 1～3（要 App 名等）由 Encode 钩子补上。
- 结果：iPhone 接受修改后的识别信息，有线时也发 0x5201/0x5202，App 名也有。**【途锐实测，iOS 27】**
- 保险：收到 0x1D03（识别被拒）就停止修改，直到下次重启，CarPlay 不受影响。
- 代码：[`src/cluster-probe/wired`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/src/cluster-probe/wired)。
- 顺带：高德的路名有"去往XX"，路名缩短时也要去掉"去往"。
