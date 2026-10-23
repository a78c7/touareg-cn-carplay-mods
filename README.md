# 途锐中国版 CarPlay 改装合集：全屏 + 无线 + Android Auto + 导航上 HUD + CarPlay 地图上仪表

让中国版大众途锐（MH2p 车机）用一个 U 盘一次刷入五项改装：

- **全屏 CarPlay**：去掉黑边，铺满整个中控屏（`Mods/VW_FullScreenCarPlay`）
- **无线 CarPlay**：开启原厂未激活的无线 CarPlay（`Mods/AppleCarPlay`，LawPaul 作品）
- **Android Auto**：开启原厂未激活的 Android Auto，安卓手机可投屏（`Mods/AndroidAuto`，LawPaul 作品）
- **CarPlay 导航上 HUD**：苹果地图 / 高德等 CarPlay 导航的转向箭头、到路口距离（带进度条）、路名、车道指引显示在抬头显示 HUD 上（`Mods/VW_HUD_1_RouteGuidance` + `Mods/VW_HUD_2_ClusterNav`）
- **CarPlay 地图上仪表**：手机导航 App 的地图画面显示在仪表中间（原车导航地图的位置），车标对准仪表时钟正下方、靠近底部（高德、苹果地图、百度地图分别校准，自动切换）；仪表上方显示剩余距离和到达时间（原车样式）；断开 CarPlay 自动回到原车地图；地图左侧半透明小框实时显示行车数据，**显示哪几项、什么顺序用「改装设置」程序勾选**（默认功率、扭矩、G 值、转速、机油温度、变速箱油温；共 30 项可选，名称和大众行车电脑一致，最多显示 10 项）（`Mods/VW_HUD_3_CarPlayCluster`，需要和 HUD 两个文件夹一起装）

下载即用，已包含刷机所需的 [MH2p SD ModKit](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)，不用另外准备。

**只适用于车机版本 `MH2p_CN_VWG36_P2878`**，其它版本脚本会自动退出，不改动任何东西。

✅ 已在作者的 2023 款途锐（P2878）上验证：全屏、无线、HUD（箭头、距离、路名）、仪表 CarPlay 地图（无线/有线 CarPlay，高德、苹果地图、百度地图：出图、按 VIEW 切换、断开自动恢复、车机设置切夜间后仪表同步变深色）、仪表信息框（v16）。
⚠️ **仪表地图模块（VW_HUD_3）的 U 盘安装流程尚未在未改装的车上完整实测**（作者车上是调试时手动装的，程序与本包相同）。另外只在 iOS 27 上测试过。**v17（可自定义信息框、行驶数据存储器各项、「改装设置」程序）刚发布，尚未在车上和 Windows/Mac 实机验证**，遇到问题可以先用 v16（GitHub 上提交 7e17fbd 的 Download ZIP）。遇到问题请按下面"排查日志"收集日志并提 Issue。

**装过作者旧版本（touareg-cn-carplay-fullscreen，已下线）的车主**：可以直接用本仓库的新 U 盘覆盖刷入，脚本会识别已装状态。卸载也可以用新 U 盘（包里自带原厂文件，见下面"原厂备份"）。

## 安装（5 步）

1. **准备 U 盘**：找一个 32GB 以内的 U 盘，格式化为 **FAT32**（Windows：右键 U 盘 → 格式化；Mac：磁盘工具 → 抹掉 → MS-DOS (FAT)）。
2. **复制文件**：点本页绿色 **Code → Download ZIP**，解压，把 `USB` 文件夹**里面的全部内容**（`Data`、`Meta`、`Logs`、`Mods`、`Stock` 和两个「改装设置」程序）复制到 U 盘根目录。
3. **选择功能**：双击 U 盘里的 **「改装设置（Windows）.exe」** 或 **「改装设置（Mac）.command」**，浏览器里会打开设置页面：每个功能选「安装 / 卸载 / 不处理」，仪表信息框勾选要显示的项目、拖动排序，点 **「保存到 U 盘」**。程序会自动处理依赖关系、检查 U 盘格式，并清掉 Mac 复制时产生的隐藏文件（不联网，只改这个 U 盘里的文件）。全部默认安装的话这一步可以跳过（Mac 用户仍建议运行一次，用来清理隐藏文件）。
   - Windows 如果提示"Windows 已保护你的电脑"：点"更多信息" → "仍要运行"。
   - Mac 第一次打开会提示"无法验证开发者"：打开 系统设置 → 隐私与安全性，在下面点"仍要打开"，再双击一次（macOS 14 及以前也可以右键 → 打开）。
4. **车上刷入**：先在电脑上弹出 U 盘，**发动车辆**，插入 U 盘。约 10 秒后车机自动重启并开始安装，过程几分钟，会重启几次、可能出现红色工程界面，**期间不要操作车机**。
5. **拔掉 U 盘**：屏幕提示移除介质时拔出 U 盘，车机重启后连接 CarPlay 即可。无线连接：iPhone 设置 → 通用 → CarPlay → 选择车辆，或在车机里添加手机。如果仍有黑边，在 iPhone 上"忘记此车辆"后重新连接。

不想用程序也可以手动：把不要的模块文件夹从 U 盘 `Mods` 里删掉（或移到别处）再刷；依赖关系：`VW_HUD_1` + `VW_HUD_2` 要一起；`VW_HUD_3`（仪表地图）需要 HUD_1、HUD_2 也在。

### CarPlay 导航上 HUD 说明

- 装好后连 CarPlay，在苹果地图或高德里开始导航，**等约 1 分钟**（车机开机后组件要等界面完全启动），HUD 出现箭头、距离和路名。
- 路名会自动去掉"进入"等前缀；没有名字的路显示"无名道路"（HUD 和仪表顶部同一行）。
- 车道指引只在地图有车道数据的路口出现（大路口、高架、高速出入口）。
- 只装 HUD 不装仪表地图时，仪表中间会显示"导航已在移动设备上激活"，这是原车行为；装了仪表地图后这条提示会被去掉。
- 临时关闭：U 盘根目录放一个名为 `vwcn_off` 的空文件插在车上即可，拔掉恢复。

### CarPlay 地图上仪表说明

- 仪表切到**导航地图页**（方向盘 VIEW 键切换），连上 CarPlay 后约 1～3 秒，仪表中间显示手机 App 的地图（开机后 CarPlay 一连上就会出图）。
- 断开 CarPlay 后约 3 秒，仪表自动回到原车地图，HUD 和仪表上的导航信息同时清除。
- 仪表两侧的表盘可以在仪表设置里隐藏，地图会铺满整个仪表。
- 导航时仪表上方显示**剩余距离**和**到达时间**（仪表原车样式，数据来自手机导航）。
- **车标位置**：默认对准仪表时钟正下方、靠近底部。三个 App 摆车标的规则不同，已分别校准并按 App 名自动切换：苹果地图按设定居中；百度导航时车标比设定点偏右约 165 像素，已补偿；高德导航时车标固定在画面约 85% 高度，并会往**下一个路口的转弯方向**挪约 210 像素，已按下一个转向自动补偿。百度不导航时车标位置固定、不受控制。车机重启后高德要重新开始一次导航才会被识别（它只在开始导航时发 App 名字）。
- 位置可自己改：车机上 `/mnt/app/eso/bin/apps/cluster/cl.conf`（说明写在文件里；SSH 调试时可用同目录的 `cl.sh` 实时切换，如 `cl.sh va 3`）。不同车（仪表尺寸、时钟位置）需要重新校准。
- 仪表地图的样式、缩放、全览由手机 App 决定，车机控制不了。高德的"全览"按钮会把中控和仪表的全览状态对调（高德自身的问题）。
- 夜间：在车机设置里切到夜间，仪表地图同步变深色。
- 只关闭仪表地图（保留 HUD）：最简单是用"卸载"方法只卸载 `VW_HUD_3_CarPlayCluster`。

### 仪表信息框说明（v16，v17 起可自定义）

- 仪表显示 CarPlay 地图时，地图左侧有一个半透明小框，每行一个数，每秒刷新 5 次。只在连着 CarPlay、仪表显示地图时出现。
- **自己选显示什么**：刷机前运行 U 盘里的「改装设置」程序，在"仪表信息框"里点选项目、拖动排序，页面上有效果示意；改完要重新刷一次 U 盘。（也可以手改 `Mods/VW_HUD_3_CarPlayCluster/Update/dash.conf` 的 `items=` 行，文件里有说明。）
  可选的 30 项，名称和大众行车电脑一致：功率、扭矩、G 值、纵向 G、横向 G、转速、车速、油门、制动压力、转向角、挡位、变速箱油温、机油温度、冷却液温度、车外温度、当前消耗、燃油存量（升）、续驶里程，以及"行驶数据"三个存储器**自启动起、自上次加油起、累计**各自的平均消耗、行驶里程、平均车速、行驶时间（框里分两行显示，上面小字是存储器名）。
  - **最多 10 项**，第 11 项起不显示。字号按项数自动选：1～6 项大字，7～8 项中字，9～10 项小字（都不会挡到仪表左下角的挡位框；小字在仪表上偏小，建议不超过 8 项）。
  - 不想要信息框、位置、背景透明度、油箱容积（默认 90 升）也在程序里设置。
  - `dash.conf` 每次刷 U 盘都会覆盖车上的同名文件（`cl.conf` 不会被覆盖，那是车标位置的校准）。
- **G 值**：加速度合成值（纵向 + 横向），加速时带 `+`、刹车时带 `-`，只转弯或匀速时不带符号；轻度平滑，比实际慢约 0.2～0.3 秒。要分开看可选"纵向G""横向G"。
- 单位按公制（km/h、°C、L/100km）显示；当前消耗停车时自动变成 L/h。
- 数据由 Java 插件（`VW_HUD_2_ClusterNav`）读车机自己的车辆状态和行车电脑接口，不连 OBD、不改仪表编码。功率/扭矩/加速度/转速/油温已在作者车上实测；车速、油门、刹车、方向盘、外温、续航和行车电脑数据走同一接口，**v17 尚未上车验证**。

### 排查日志

U 盘根目录新建 `cluster_logs` 文件夹，插在车上开导航 2 分钟以上，日志会自动拷进去（`vwcn.log`、`dio_cluster.log`、`cl_probe.log`、`clusterd.log`）。提 Issue 时附上。注意：日志里可能有手机名称、导航路名等信息，公开前先看一遍，不想公开的删掉。

安装时原版文件会备份到 U 盘（各模块文件夹的 `Update/backup/` 或 `Update/Backup/`），建议保留这个 U 盘。平时不要把它插在车上，否则每次插入都会进入刷机。

### 原厂备份

`USB/Stock/` 里是本车机版本（P2878）被改过的原厂文件（全屏 CarPlay 改的界面文件 `viewhandler.zip`、`info.txt`）。卸载全屏 CarPlay 时优先用 U 盘里当初的备份，没有就用这里的原厂文件，所以**换了 U 盘、U 盘格式化过也能卸载**。其它被改的文件（CarPlay 进程配置、HMI 插件列表、功能授权）卸载时在车机上原地还原，不需要备份。

## 卸载（恢复原车）

运行「改装设置」，把要卸载的功能选为「卸载」，保存后按上面第 4、5 步再刷一次，原版恢复（任何一份完整的新版改装包都可以）。手动方法：在要卸载的模块文件夹里新建一个空文件 `uninstall.txt`。卸载 HUD 时 `VW_HUD_1`、`VW_HUD_2`、`VW_HUD_3` 三个一起卸（程序会自动处理）。

说明：卸载会还原所有被改的原厂文件、删除新增的程序。ModKit 自身的开机钩子（让 U 盘模块能开机运行的那部分）会留在车机里，没有模块时它什么也不做；如需彻底移除请参考 [ModKit 官网](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)。

## 出问题怎么办

- 插 U 盘没反应或提示"不支持更新"：U 盘不是 FAT32，或 `Data`、`Meta` 不在 U 盘根目录。
- 刷完没变化：用电脑打开 U 盘 `Logs/` 里对应模块的 `.log` 查看原因（例如版本不匹配会写 `abort`）。
- 无线 CarPlay 连不上：需要车上导航数据正常、有 5GHz WiFi 天线（ModKit 官网说明）。
- Android Auto 连不上：换一根支持数据传输的手机线。
- 仪表一直显示原车地图：确认仪表在地图页、手机 App 已开始导航；开机后等 1 分钟；按上面方法收集日志。
- CarPlay 异常：按上面方法卸载（先卸 `VW_HUD_3`，再卸 HUD_1/2）。

⚠️ 改装风险自负，可能影响保修。仪表地图只占用原车导航地图的位置，车速、警告灯等由仪表自己显示，不经过本改装。

## 开发过程与源码

- **[docs/开发过程.md](docs/开发过程.md)**：从零做出"导航上 HUD"和"地图上仪表"的完整过程、踩过的坑、给其他 MH2p 车型（大众/奥迪/保时捷）的参考。
- `src/hud-java`：车机 Java 插件 VWClusterNav（HUD 数据、剩余距离/到达时间、去掉提示、仪表显示切换）
- `src/cluster-probe`：`cl_probe.so`，注入 CarPlay 进程，向 iPhone 声明仪表屏、接收并解密第二路视频
- `src/clusterd`：仪表守护进程，有画面时启动显示、断开时恢复原车
- `src/cl_render`：H.264 解码显示（基于 fifthBro 的 `cluster.c` 修改，CC BY-NC-SA 4.0）；`dash.c` 仪表信息框
- `src/build.sh`：交叉编译脚本；`tools/configurator`：U 盘里的「改装设置」程序（Go，`sh build.sh 版本号` 生成 Windows/Mac 版）；`tools/`：调试工具（macOS 网卡驱动 axbridge、截屏等）

## 致谢

- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)**（[网站](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)）：让 MH2p 能用 U 盘改装的基础工具。本仓库 `USB/Data`、`USB/Meta`、`USB/Logs`、`USB/Mods/README.md` 原样来自该项目，遵循 **CC BY-NC-SA 4.0**（禁止商用），许可证见 [LICENSES/MH2p_SD_ModKit_LICENSE.md](LICENSES/MH2p_SD_ModKit_LICENSE.md)。
- **LawPaul / MH2p CarPlay**：`USB/Mods/AppleCarPlay` 原样来自 ModKit 官网提供的模块包（CC BY-NC-SA 4.0），作用是写入 CarPlay 功能授权并开启无线。
- **LawPaul / MH2p Android Auto**：`USB/Mods/AndroidAuto` 原样来自 ModKit 模块包（CC BY-NC-SA 4.0）；其中仅对保时捷生效的多设备修复引用了 [fifthBro/pcm5-androidauto-connect-fix](https://github.com/fifthBro/pcm5-androidauto-connect-fix)，途锐不会执行。
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**：英国右舵途锐全屏方案，本补丁的几何数值参考于此。
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)**（[网站](https://fifthbro.github.io)）：`dio_cluster.so` 原样来自其 v0034 版本（让 iPhone 发出路线指引数据）；仪表显示程序 `cl_render` 基于其公开的 `cluster.c` 修改。均遵循 **CC BY-NC-SA 4.0**（见 [LICENSES/fifthBro_mh2p-cluster_LICENSE.md](LICENSES/fifthBro_mh2p-cluster_LICENSE.md)）。他是 MH2p 仪表投屏的开拓者，本项目的"断开后恢复原车地图"等问题都参考了他代码里的注释。
- **[fifthBro / mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)**：开发调试用的 SSH 模块（未包含在 U 盘包里）。
- **[luka-dev / mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)**：CarPlay 路线指引和第二屏（altScreen）协议研究；**[luka-dev / jxe2jar](https://github.com/luka-dev/jxe2jar)**：车机 Java 程序转换/反编译；**[luka-dev / qnx65-armv7-toolchain](https://github.com/luka-dev/qnx65-armv7-toolchain)**：QNX 交叉编译环境。

## 许可证

- 本仓库新写的部分（全屏补丁脚本、`src/hud-java`、`src/cluster-probe`、`src/clusterd`、`tools/`、各模块安装脚本、文档）：**GPL-3.0**（见 [LICENSE](LICENSE)）。
- `src/cl_render` 和 `dio_cluster.so`：fifthBro 作品及其修改版，**CC BY-NC-SA 4.0**。信息框字形 `src/cl_render/dash_font.h` 由 Noto Sans CJK SC 预渲染，**SIL OFL 1.1**（见 [LICENSES/NotoSansCJK_OFL.md](LICENSES/NotoSansCJK_OFL.md)）。
- ModKit 及 LawPaul 的模块：**CC BY-NC-SA 4.0**。
- 因包含禁止商用的部分，**整个 U 盘包仅供个人非商业使用**。
- 本仓库不包含任何大众或苹果的原厂程序代码；`VW_FullScreenCarPlay/Update/viewhandler.zip` 为修改后的原厂界面配置文件，`USB/Stock/` 为对应的原厂文件（用于卸载恢复），仅供本车型车主自用。

---

# VW Touareg (China) CarPlay mods: full-screen + wireless + Android Auto + navigation on the HUD + CarPlay map on the instrument cluster

Five mods in one flash for the China-market VW Touareg (MH2p, head unit `MH2p_CN_VWG36_P2878` only — other versions exit without changes):

- **Full-screen CarPlay** (`Mods/VW_FullScreenCarPlay`)
- **Wireless CarPlay** (`Mods/AppleCarPlay`, by LawPaul)
- **Android Auto** (`Mods/AndroidAuto`, by LawPaul)
- **CarPlay turn-by-turn on the HUD**: arrow, distance, street name, lanes (`Mods/VW_HUD_1_RouteGuidance` + `Mods/VW_HUD_2_ClusterNav`)
- **CarPlay map on the instrument cluster**: the phone's cluster map (iPhone second video stream, "altScreen") replaces the stock navigation map in the middle of the cluster; it switches back to the stock map automatically when CarPlay disconnects; a translucent box on the left shows live **power, torque, G, rpm, engine oil and gearbox oil temperature** (`Mods/VW_HUD_3_CarPlayCluster`, needs both HUD folders)

✅ Verified on the author's 2023 Touareg (wireless and wired CarPlay; AMap, Apple Maps and Baidu on the cluster; night mode follows the head-unit setting). ⚠️ The USB install of `VW_HUD_3` has not yet been tested end-to-end on an unmodified car (same binaries were installed by hand on the author's car); only tested on iOS 27. Upgrading from the author's earlier touareg-cn-carplay-fullscreen package: flash this drive over it; keep the old drive to uninstall full-screen CarPlay (its backup lives there). Logs may contain your phone name and street names — review before posting.

**Install**: FAT32 drive ≤ 32 GB → copy everything inside `USB` to the root → double-click `改装设置（Windows）.exe` / `改装设置（Mac）.command` on the drive to choose install / uninstall / skip per mod and the info-box rows in a local web page (it also removes macOS `._*` files) → start the engine, insert the drive, wait (several reboots) → remove the drive when prompted. Stock copies of the files changed by full-screen CarPlay are in `USB/Stock/`, so any copy of the package can uninstall.

**Cluster map**: put the cluster on its navigation map page (VIEW button); the phone's map appears 1–3 s after CarPlay connects, also right after boot. Remaining distance and arrival time are shown by the cluster itself (stock style). The car icon is placed under the cluster clock near the bottom: the phone is told the cluster is 1920x960 and only rows 230–869 are shown; per-app safe areas (AMap / Apple Maps / Baidu behave differently, AMap also shifts the car ~210 px toward the next turn, which is compensated from the route-guidance maneuver type) are switched automatically by app name. Settings: `/mnt/app/eso/bin/apps/cluster/cl.conf`. Side gauges can be hidden in the cluster settings.

**Info box (v16, configurable since v17)**: while the CarPlay map is on the cluster, a translucent box left of the map shows live values, 5 updates/s. Choose the rows and their order in `Mods/VW_HUD_3_CarPlayCluster/Update/dash.conf` before flashing (`items=power,torque,g,rpm,oil,gearbox` is the default; 30 items: power, torque, g, glong, glat, rpm, speed, pedal, brake, steer, gear, gearbox, oil, coolant, outside, cons, fuel, range, plus average consumption / distance / average speed / driving time for each of the three VW board-computer memories — since start `trip…`, since refuelling `refuel…`, long term `total…` — Chinese names work too; up to 10 rows; the font shrinks for 7–8 and 9–10 rows so the box never covers the gear indicator). v17 is not yet verified on the car. `show=0` hides it; `x`, `y`, `alpha`, `tank_l` in the same file. G = total of longitudinal + lateral, `+` accelerating, `-` braking. Metric units assumed. Data comes from the head unit's own vehicle-state / board-computer DSI services (Java bundle `vwcn.Dash`), no OBD or cluster coding.

**Uninstall**: empty `uninstall.txt` in each mod folder to remove, flash again. Remove the three HUD folders together.

**Logs**: create `cluster_logs` on a drive, leave it plugged in while navigating for 2+ minutes.

**How it was built**: see [docs/开发过程.md](docs/开发过程.md) (Chinese) — LD_PRELOAD hook into `dio_manager` advertising a second display to the iPhone, ChaCha20-Poly1305 stream decryption (libairplay 320.17.1), NvMedia H.264 decode, displaymanager layering `[58, 33]` so the stock map stays bound underneath, and the VW HMI (J9 Java) plug-in. Sources in `src/`, build script `src/build.sh`.

## Credits

- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)**: `USB/Data`, `USB/Meta`, `USB/Logs`, `USB/Mods/README.md`, `USB/Mods/AppleCarPlay`, `USB/Mods/AndroidAuto` included unmodified, CC BY-NC-SA 4.0.
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**: full-screen geometry values.
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)**: `dio_cluster.so` (v0034, unmodified) and `cluster.c` (modified, `src/cl_render`), CC BY-NC-SA 4.0. Pioneer of MH2p cluster projection.
- **[luka-dev](https://github.com/luka-dev)**: mib2q-carplay-rgi (CarPlay route guidance and altScreen research), jxe2jar, qnx65-armv7-toolchain.

**License**: new code GPL-3.0 ([LICENSE](LICENSE)); `src/cl_render`, `dio_cluster.so`, ModKit and LawPaul mods CC BY-NC-SA 4.0; info-box glyphs (`src/cl_render/dash_font.h`, pre-rendered from Noto Sans CJK SC) SIL OFL 1.1 ([LICENSES/NotoSansCJK_OFL.md](LICENSES/NotoSansCJK_OFL.md)). The USB bundle as a whole is for **personal, non-commercial use only**. No VW or Apple program code is included. Use at your own risk.
