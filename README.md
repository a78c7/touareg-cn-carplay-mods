# 途锐中国版 CarPlay 改装合集：全屏 + 无线 + Android Auto + 导航上 HUD + CarPlay 地图上仪表 + 仪表信息框

让中国版大众途锐（MH2p 车机）用一个 U 盘一次刷入下面几项改装。最新版本见 [Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest)。

- **全屏 CarPlay**：去掉黑边，铺满中控屏
- **无线 CarPlay**、**Android Auto**：开启原厂未激活的功能（LawPaul 作品）
- **CarPlay 导航上 HUD**：苹果地图、高德、百度的转向箭头、距离、路名、车道显示在抬头显示上
- **CarPlay 地图上仪表**：手机导航地图显示在仪表中间，车标对准仪表时钟下方；仪表上方显示剩余距离和到达时间；断开 CarPlay 自动回到原车地图；方向盘上下键缩放（高德）
- **仪表信息框**：地图左右两边各一个半透明小框，从 30 多项行车数据里自选、排序（功率、扭矩、G 值、转速、油温、油耗、里程、0–100 计时等）
- **行车记录**：U 盘一直插在车上，每次开车记录约 10 次/秒的行车数据，可以直接交给豆包、千问等 AI 分析

![仪表示意](docs/img/cluster.png)

*仪表效果示意（不是实拍）：中间是手机导航地图，左右是信息框；深灰色的导航条、挡位、车速是仪表原车显示。*

> ⚠️ **非官方改装，风险自负。** 使用前请先读[免责声明](#免责声明)。

**只适用于车机版本 `MH2p_CN_VWG36_P2878`**，其它版本脚本会自动退出，不改任何东西。已在一台 2023 款途锐上验证全部功能，也有车主在未改装的车上用 U 盘完整刷入过；只在 iOS 27 上测试过。

## 安装

1. **准备 U 盘**：32GB 以内，格式化为 **FAT32**（Windows：右键 → 格式化；Mac：磁盘工具 → 抹掉 → MS-DOS (FAT)）。
2. **复制文件**：从 [Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) 下载 zip，解压后把**里面的全部内容**复制到 U 盘根目录。
3. **选择功能**（可选）：双击 U 盘里的 **「改装设置（Windows）.exe」** 或 **「改装设置（Mac）.command」**，在打开的网页里选每个功能"安装 / 卸载 / 不处理"、选信息框项目，点「保存到 U 盘」。全部默认安装可以跳过（Mac 用户建议运行一次，清理隐藏文件）。

   ![改装设置程序](docs/img/configurator.png)

   程序没有购买开发者签名，所以会有提示：Windows 点"更多信息"→"仍要运行"；Mac 在 系统设置 → 隐私与安全性 里点"仍要打开"。程序**不联网**，只改这个 U 盘里的文件，源码在 `tools/configurator`。
4. **车上刷入**：电脑上弹出 U 盘，**发动车辆**，插入 U 盘。约 10 秒后车机自动重启并安装，过程几分钟、会重启几次，**期间不要操作车机**。
5. **拔掉 U 盘**：提示移除介质时拔出，车机重启后连接 CarPlay。仍有黑边的话，在 iPhone 上"忘记此车辆"后重连。

刷过旧版的车直接用新 U 盘覆盖刷入即可，车上的车标位置校准（`cl.conf`）会保留。

## 使用说明

**HUD**：连 CarPlay 开始导航，开机后约 1 分钟 HUD 出现箭头、距离和路名。车道只在有车道数据的路口出现。

**仪表地图**：仪表切到导航地图页（方向盘 VIEW 键），连上 CarPlay 后地图出现在仪表中间。仪表两侧表盘可以在仪表设置里隐藏。地图的样式、比例尺由手机 App 决定。车机设置切夜间时仪表地图同步变深色。
- 车标位置：开 ACC 时地图自动上移，不挡 ACC 图案；常驻上移量在「改装设置」里调。

**信息框**：在「改装设置」里点选项目、拖动排序、选左边框或右边框，改完重新刷一次 U 盘。最多 10 项，项目多时字号自动变小。数据来自车机自己的车辆数据，不连 OBD、不改仪表编码；车速、警告等以原车仪表为准。

**行车记录**：
1. 刷完改装后，在「改装设置」的「U 盘用途」点 **改成行车记录 U 盘**，这个 U 盘就可以一直插在车上，不会再触发刷机（要刷机时点"改回刷机 U 盘"）。
2. 每次车机启动在 `drive_logs` 里生成一个 CSV 文件（约 10 行/秒，每小时约 5 MB），Excel / Numbers 能直接打开。
3. **用 AI 分析**：把 CSV 文件和 `drive_logs` 里的 **「给AI分析行车记录.txt」** 一起上传到豆包、通义千问、DeepSeek 等，再把说明文件里的"提问模板"复制过去发送，就能得到这趟的里程、车速、急加速急刹车、动力、油耗、温度等分析。
4. 车机时钟每次开机从 1970 年开始，文件名里的日期不可信；熄火前最后不到 10 秒的数据可能没存上。

### 高德地图的已知问题

下面这些是高德自身的行为，车机这边改不了或只能绕过：
- **车标左右偏**：高德导航时会把车标往目的地方向挪约 3 厘米（不是按下一个路口）。可在「改装设置」的「高德转向补偿」里选「看画面自动居中（试验）」自动挪回；高德切到车道级 3D 视图、夜间地图时也能识别，偶尔认不出时会保持原样。
- **比例尺自己变**：导航中高德会按车速和路口自动调比例尺，方向盘上下键手动缩放过一会儿会被改回去。
- **车道指引**：HUD 上的车道完全取决于导航 App 有没有发：只有地图里有车道数据的路口（一般是大路口、高架、高速出入口）才会发。目前实测的几段路上，高德和百度都没有发过车道信息，所以 HUD 没有车道（转向箭头、距离、路名正常）；其它路段还没有测到。
- **不导航（巡航）时**：仪表只显示一张带位置点的普通地图，没有电子眼、限速等巡航信息；HUD 也没有内容（iPhone 只在导航时发路线数据）。
- **重新规划路线**：高德会先结束导航、几秒后再开始，HUD 和仪表上方的信息会闪一下。
- **全览按钮**：高德的"全览"会把中控和仪表的全览状态对调。

偶尔开机后仪表一直是原车地图（iPhone 没响应仪表画面的请求），断开 CarPlay 重连一次即可。

## 卸载

运行「改装设置」，把要卸载的功能选为「卸载」，保存后按安装第 4、5 步再刷一次，原厂文件会还原。手动方法：在要卸载的模块文件夹里新建空文件 `uninstall.txt`。三个 HUD / 仪表模块要一起卸（程序会自动处理）。

## 出问题怎么办

- 插 U 盘没反应：U 盘不是 FAT32，或 `Data`、`Meta` 不在根目录。
- 刷完没变化：看 U 盘 `Logs/` 里对应模块的 `.log`（版本不匹配会写 `abort`）。`_ModKit_Update.log` 里的 `Filename too long` 是 ModKit 自带的提示，可以忽略。
- 无线 CarPlay 连不上：需要车上有 5GHz WiFi 天线；Android Auto 连不上：换一根能传数据的线。
- 仪表一直是原车地图：确认仪表在地图页；断开 CarPlay 重连一次。
- **提 Issue 请附日志**：U 盘根目录新建 `cluster_logs` 文件夹，插在车上导航 2 分钟以上，日志会自动拷进去。只传 `Logs` 和 `cluster_logs` 两个文件夹（U 盘里的备份文件和本车绑定，不要公开）；日志里可能有手机名称和路名，公开前看一遍。

## 开发过程与源码

- **[docs/开发过程.md](docs/开发过程.md)**：完整的开发和调试过程、踩过的坑、版本记录，给其他 MH2p 车型（大众/奥迪/保时捷）参考。
- **[a78c7/vw-mh2p-dev-notes](https://github.com/a78c7/vw-mh2p-dev-notes)**：按主题整理的平台笔记。
- 源码：`src/hud-java`（车机 Java 插件）、`src/cluster-probe`（CarPlay 进程钩子，接收仪表视频）、`src/clusterd`（仪表守护进程）、`src/cl_render`（解码显示、信息框、车标识别）、`src/vhpatch`（全屏补丁）、`tools/configurator`（改装设置程序）；编译见 `src/build.sh`。

## 致谢

- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)**（[网站](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)）：让 MH2p 能用 U 盘改装的基础工具。本仓库 `USB/Data`、`USB/Meta`、`USB/Logs`、`USB/Mods/README.md` 原样来自该项目，遵循 **CC BY-NC-SA 4.0**（禁止商用），许可证见 [LICENSES/MH2p_SD_ModKit_LICENSE.md](LICENSES/MH2p_SD_ModKit_LICENSE.md)。
- **LawPaul / MH2p CarPlay**：`USB/Mods/AppleCarPlay` 原样来自 ModKit 官网提供的模块包（CC BY-NC-SA 4.0），作用是写入 CarPlay 功能授权并开启无线。
- **LawPaul / MH2p Android Auto**：`USB/Mods/AndroidAuto` 原样来自 ModKit 模块包（CC BY-NC-SA 4.0）；其中仅对保时捷生效的多设备修复引用了 [fifthBro/pcm5-androidauto-connect-fix](https://github.com/fifthBro/pcm5-androidauto-connect-fix)，途锐不会执行。
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**：英国右舵途锐全屏方案，本补丁的几何数值参考于此。
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)**（[网站](https://fifthbro.github.io)）：`dio_cluster.so` 原样来自其 v0034 版本（让 iPhone 发出路线指引数据）；仪表显示程序 `cl_render` 基于其公开的 `cluster.c` 修改。均遵循 **CC BY-NC-SA 4.0**（见 [LICENSES/fifthBro_mh2p-cluster_LICENSE.md](LICENSES/fifthBro_mh2p-cluster_LICENSE.md)）。他是 MH2p 仪表投屏的开拓者，本项目的"断开后恢复原车地图"等问题都参考了他代码里的注释。
- **[fifthBro / mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)**：开发调试用的 SSH 模块（未包含在 U 盘包里）。
- **[luka-dev / mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)**：CarPlay 路线指引和第二屏（altScreen）协议研究；**[luka-dev / jxe2jar](https://github.com/luka-dev/jxe2jar)**：车机 Java 程序转换/反编译；**[luka-dev / qnx65-armv7-toolchain](https://github.com/luka-dev/qnx65-armv7-toolchain)**：QNX 交叉编译环境。

## 免责声明

1. **非官方**：本项目是个人业余研究，与大众汽车（Volkswagen）、上汽大众 / 一汽-大众、苹果（Apple）、谷歌（Google）、高德等公司没有任何关系，也未获得其授权或认可。文中提到的品牌和商标归各自所有者，只用来说明适用车型和功能。
2. **只公开方法，不提供保证**：仓库里的代码、U 盘包和文档按"现状"公开，供学习、研究和个人使用。作者不保证它们能用、适合你的车、没有错误，也不保证以后会维护或更新。
3. **风险自负**：刷机和改装可能导致车机或仪表功能异常、无法启动、需要到 4S 店恢复，可能失去原厂保修，也可能不符合你所在地的法规或车辆检验要求。是否使用由你自己判断，**由此造成的车辆损坏、数据丢失、维修费用、保修或法律纠纷等任何直接或间接后果，由使用者自行承担，作者不承担责任**。
4. **行车安全**：不要在驾驶时操作手机或车机设置。HUD、仪表地图和信息框里的数据只供参考，车速、警告灯等一律以原车仪表为准；数据显示有误或延迟时不要据此驾驶。
5. **不提供服务、禁止商用**：作者不提供付费刷机或远程协助。任何人修改、打包、转售或代刷本项目的，与作者无关，也违反下面的非商业许可。
6. **权利联系**：如果你是相关权利人，认为本仓库的内容侵犯了你的权利，请通过 [Issue](https://github.com/a78c7/touareg-cn-carplay-mods/issues) 联系，核实后会及时修改或删除。

下载、复制或使用本项目，即表示你已阅读并同意以上内容。

## 许可证

- 本仓库新写的部分（全屏补丁脚本、`src/hud-java`、`src/cluster-probe`、`src/clusterd`、`tools/`、各模块安装脚本、文档）：**GPL-3.0**（见 [LICENSE](LICENSE)）。
- `src/cl_render` 和 `dio_cluster.so`：fifthBro 作品及其修改版，**CC BY-NC-SA 4.0**。信息框字形 `src/cl_render/dash_font.h` 由 Noto Sans CJK SC 预渲染，**SIL OFL 1.1**（见 [LICENSES/NotoSansCJK_OFL.md](LICENSES/NotoSansCJK_OFL.md)）。
- ModKit 及 LawPaul 的模块：**CC BY-NC-SA 4.0**。
- 因包含禁止商用的部分，**整个 U 盘包仅供个人非商业使用**。
- 仓库和 U 盘包里不含大众或苹果的文件：全屏 CarPlay 只带补丁表（改哪几个字节），在车上就地修改本车已有的文件。

---

# VW Touareg (China) CarPlay mods

One USB flash for the China-market VW Touareg (MH2p, head unit `MH2p_CN_VWG36_P2878` only — other versions exit without changes): full-screen CarPlay, wireless CarPlay and Android Auto (LawPaul), CarPlay turn-by-turn on the HUD, the phone's CarPlay map on the instrument cluster (back to the stock map on disconnect), two configurable info boxes on the cluster, and a drive log on a USB drive left in the car. Verified on a 2023 Touareg; iOS 27 only.

**Install**: FAT32 drive ≤ 32 GB → copy everything from the [release zip](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) to the root → optionally run `改装设置（Windows）.exe` / `改装设置（Mac）.command` on the drive (offline, local web page) to choose install / uninstall / skip per mod and the info-box rows → start the engine, insert the drive, wait for the reboots → remove it when prompted. No VW or Apple files are included: full-screen CarPlay patches 38 bytes of the car's own file in place.

**How it was built**: [docs/开发过程.md](docs/开发过程.md) (Chinese) and [a78c7/vw-mh2p-dev-notes](https://github.com/a78c7/vw-mh2p-dev-notes). Sources in `src/`, build script `src/build.sh`.

**Credits**: [LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit) and its CarPlay / Android Auto mods (included unmodified), [fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster) (`dio_cluster.so`, `cluster.c`), [davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay), [luka-dev](https://github.com/luka-dev).

**License**: new code GPL-3.0; `src/cl_render`, `dio_cluster.so`, ModKit and LawPaul mods CC BY-NC-SA 4.0; info-box glyphs SIL OFL 1.1. The USB bundle is for personal, non-commercial use only.

**Disclaimer**: unofficial personal research, not affiliated with or endorsed by Volkswagen, Apple, Google or any other company. Provided "as is" without warranty; you use it entirely at your own risk and the author is not liable for any damage or loss. Do not operate the phone or head unit while driving. Rights holders: please open an Issue.
