# R2 HMI（J9 Java）

[← 平台参考](README.md)

车机的整个界面、和仪表/HUD 的通信、车辆数据的订阅，都在一个 J9 Java 进程里。自己写一个插件放进去，就能读车辆数据、往仪表和 HUD 发内容，不用改大众原有的程序。

## 1. 程序组成

| 项 | 说明 | 范围 |
| --- | --- | --- |
| `/ifs/lsd.jxe` | 主程序，J9 的 ROM 镜像格式（途锐约 88 MB） | 【MH2p 通用】 |
| 转换 | [luka-dev/jxe2jar](https://github.com/luka-dev/jxe2jar) 转成普通 jar，约 4 分钟；途锐是 47135 个类，**类名没有混淆**，用 CFR 反编译能直接读 | 【途锐实测】 |
| 界面皮肤 | `Resources/skin3/viewhandler.zip`（国行大众），每个界面一个 jxe。各品牌、各地区的皮肤不同 | 【途锐实测】 |
| 附加 jar | `/mnt/app/eso/hmi/lsd/jars/*.jar`，用 `-Xbootclasspath/a` 加在 `lsd.jxe` **后面**，所以只能新增类，不能覆盖原有的类 | 【途锐实测】 |
| 调试映射 | 车机带的 `MIBTestDebug-*.jar` 里只有各模块的调试符号映射，没有代码；遇到名字被混淆的界面 jxe 时可以用来对名字 | 【途锐实测】 |

## 2. 插件怎么加载

- 注册表是 `/mnt/app/eso/hmi/lsd/config/bundles.properties`。加一个插件需要三处：
  - `Bundle.Name.<编号> = <名字>`
  - `Bundle.Activator.<编号> = <激活器类的全名>`
  - 把编号加进 `Bundle.Startup.Lateload = ...` 列表
- 界面准备好以后，启动线程按列表**顺序**逐个启动。某个插件启动时抛异常，后面的就都不启动了。所以自己的插件一定放在 Lateload 列表**最后**，否则会连累原车的模块（途锐上放第一位时，语音模块没起来）。【途锐实测】
- 途锐原厂 Lateload 是 `61,62,95`，97 号没被占用。别的车先查一下自己的文件，别撞号。【途锐实测】
- 激活器用 `Class.forName` 加载，附加 jar 在 bootclasspath 上，能找到。【途锐实测】

## 3. 编译要求

- **class 文件版本必须 ≤ 48（Java 1.4）**。车机自带的附加 jar 全部 ≤ 48。版本 49 的类会**静默**加载失败，日志里一行都没有。【MH2p 通用】
- 用 JDK 8：`javac -source 1.4 -target 1.4 -bootclasspath <lsd.jar 或 J9 的 jcl.jar>`。不能用 `StringBuilder`、泛型、自动装箱等 1.5 以后的语法。
- 没有导出的 `lsd.jar` 时，可以从一个已有插件 jar 的常量池生成桩类来编译（[01 篇](../01-调试环境与工具.md) 第 7 节）。
- 桩类里没有的字段，用反射按名字读：读不到就返回空，不会崩。

## 4. 启动时机

- 插件启动后，CarPlay 无线连接可能在开机 30 秒左右就完成，各种服务也未必都准备好了。不要一启动就长时间 sleep 再开始工作，要尽早开始，服务没就绪就隔一秒重试。【途锐实测】
- 原生程序和插件之间用 `/tmp` 里的文件通信时，**比较文件内容，不要比较修改时间**：QNX 的修改时间精度是 1 秒，同一秒写两次会漏掉后一次。【途锐实测】

## 5. 读车辆数据（DSI）

- 获取服务：`DSIProxyFactory.getDSIProxyAPI().getDSIProxy().getService(null, <接口>.class)`。
- 订阅：`setNotification(int[] 属性编号, 监听器)`；取消：`clearNotification(...)`。监听器可以继承大众自带的 `...Adapter` 类，只重写需要的回调。
- 回调的第二个参数是有效标志，`1` 才是有效数据。
- 各属性的编号、字段、单位见 [R3](R3-车辆数据接口.md)。

## 6. ASL datapool 和列表（导航相关）

大众 HMI 内部用"datapool"（按编号存的值）和"ASL 列表"在模块之间传数据。往仪表和 HUD 发导航的 BAP 模块，就是从这些编号读数据的。

| 编号 | 类型 | 内容 | 范围 |
| --- | --- | --- | --- |
| 732 | int datapool | 路线指引状态 | 【途锐实测】 |
| 738 | 字符串 datapool | 当前道路名（BAP NavSD CurrentPositionInfo 用） | 【途锐实测】 |
| 741 | ASL 列表 | 到下一个路口的距离 | 【途锐实测】 |
| 744 | ASL 列表 | 路名 | 【途锐实测】 |
| 751、838 | ASL 列表 | 转向箭头（原车有这两个列表，途锐上两个都写） | 【途锐实测】 |
| 2910019 | bool datapool | "手机导航激活"，由 CarPlay 模块设置 | 【途锐实测】 |
| 10394、10406、10397、10401、10403 | datapool | 大众"运动显示"页用的功率、最大功率、增压、G 值×100、G 角度。国行皮肤没有这个页面，这些编号也没人显示 | 【未验证：只在程序里读到】 |

列表的读写：`ListManager.getGenericASLList(编号).updateList(Object[])`。

## 7. BAP NavSD（往仪表和 HUD 发导航）

- 仪表和 HUD 的导航信息走 BAP 协议，导航功能单元是 **LSG 50（NavSD）**。【MH2p 通用】
- 各功能（FctID）从上面的 datapool 取数据，数据一变它们自己就发给仪表。也可以直接调用导航 BAP 接口（`ASLNavBAPFactory.getNavBAPApi()`）：

| 方法 | 作用 | 范围 |
| --- | --- | --- |
| `updateBapDistanceToNextManeuver` | 到下一个路口的距离 | 【途锐实测】 |
| `updateBapTurnToInfo` | 转入的路名（仪表顶部那一行和 HUD 用同一个字段） | 【途锐实测】 |
| `updateBapLaneGuidance` | 车道指引（FctID 24） | 【途锐实测】 |
| `updateBapDistanceToDestination(米, 255, 类型)` | 剩余距离，单位 255 = 由 HMI 自动选 km/m | 【途锐实测】 |
| `updateBapTimeToDestination2(时, 分, 格式)` | 同时更新剩余时间和到达时间（到达时间 = 车机当前时间 + 剩余时间） | 【途锐实测】 |

- 仪表上的地图缩放（方向盘上下键）走反方向：仪表通过 NavSD **MapScale** 发给车机，车机调用 `NavigationService.incrementZoomIndex / decrementZoomIndex`（长按每 500 ms 一步），作用在原车仪表地图上。这个服务对象放在 `APIFactory.navigationService` 静态字段里，可以用反射换成一个包装类：想自己处理缩放时拦下这两个方法，其余方法原样转发；同时让"已到最大/最小"返回否，否则按键会被原车吞掉。【途锐实测】
- FctID 38（InfoStates）在 CarPlay 路线指引激活时发 6，仪表就显示"导航已在移动设备上激活"。整个程序里只有它调用 `isCarPlayRouteGuidanceActive()`。【途锐实测】

## 8. 显示管理接口

- 切屏服务入口：OSGi 服务 **`de.vw.mib.display.DisplayManagementService`**（通过 `bctx.getServiceReference("de.vw.mib.display.DisplayManagementService")` 获取；而 `de.vw.mib.widgets.internal.ServiceManager.displayManager` 仅用于只读查询 `getActiveDisplayables()`）。【途锐实测】
- `showDisplayables(屏编号, int[] 编号列表)`：设置某块屏显示哪些窗口，**列表第一个在最上层**。
- `setUpdateRate(屏编号, 刷新率, 回调)`：原车地图可见时设 10，不可见时设 1。
- `disableDisplayable` / `enableDisplayable` / `updateDisplay`：单独开关某个窗口。
- 原车按方向盘 VIEW 键切到地图的流程：先 `showDisplayables(4, [33])`，再把地图设为可见，再 `setUpdateRate(4, 10)`。在 HMI 里搜 `SwitchToPermanentMapSequence` 能找到这段流程。【途锐实测】
- 屏编号和窗口编号见 [R4](R4-显示系统.md)。
