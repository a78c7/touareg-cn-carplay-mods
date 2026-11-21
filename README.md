<div align="center">

<img src="docs/img/hero_banner.svg" alt="途锐 CarPlay 改装全家桶与无线自由定制底座" width="100%">

# 途锐 MH2p CarPlay 改装全家桶 & 无线开发底座

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><img src="docs/img/badges/release.svg" alt="Release v21.0.1"></a>
  <a href="docs/README.md"><img src="docs/img/badges/platform.svg" alt="MH2p QNX 6.6"></a>
  <a href="#手机无线控制台与-ai-自由开发"><img src="docs/img/badges/console.svg" alt="touareg.local:8080"></a>
  <a href="docs/07-无线控制台与SSH开发.md"><img src="docs/img/badges/ssh.svg" alt="Wireless SSH Ed25519"></a>
  <a href="#许可证"><img src="docs/img/badges/license.svg" alt="License GPL-3.0 / CC BY-NC-SA 4.0"></a>
</p>

**不拆一粒螺丝、无需专用网卡 —— 单 U 盘解锁途锐全屏 / 无线 / HUD / 仪表地图与大字运动信息框，并开放手机无线控制台与 AI 自由开发底座。**

<p>
  <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest"><b>📦 下载最新版 U 盘包</b></a> ·
  <a href="#4-步免拆刷入安装"><b>⚡ 4 步安装</b></a> ·
  <a href="#10-大可选功能全景"><b>🧩 功能清单</b></a> ·
  <a href="#手机无线控制台与-ai-自由开发"><b>📱 无线控制台 & SSH</b></a> ·
  <a href="docs/README.md"><b>📚 MH2p 开发手册</b></a> ·
  <a href="#免责声明"><b>⚠️ 免责声明</b></a> ·
  <a href="#vw-touareg-cn--mh2p-open-cockpit"><b>🇺🇸 EN</b></a> ·
  <a href="#vw-touareg--mh2p-open-cockpit-deutsch"><b>🇩🇪 DE</b></a>
</p>

</div>

---

## 演示与仪表视觉效果

https://github.com/user-attachments/assets/618e8fe2-e579-4aed-b9de-ab50b8576374

<div align="center">
  <sub><i>上方视频可点击有声播放（中英双语字幕） · 收藏最高画质可下载 <a href="https://github.com/a78c7/touareg-cn-carplay-mods/releases/download/v20.0/touareg-cockpit-demo-1080p60.mp4"><b>🎞️ 1080p 60fps 演示母带 (67 MB)</b></a></i></sub>
  <br>
  <sub><i>注：视频前段多套定制仪表为基于本仓库 <code>1920×639</code> 独立图层与 10Hz 实时通道的扩展演示；当前 U 盘包内置「全屏 CarPlay 地图 + 295px 对称大字运动信息框与导航卡」，并开放无线控制台与无线 SSH 自由定制。</i></sub>
</div>

<br>

<div align="center">
  <img src="docs/img/cluster.png" alt="v21 仪表地图、左侧大字运动信息框与右侧对称导航卡示意图" width="98%">
  <br>
  <sub><i><b>v21 仪表视觉示意图</b>：中央为手机 CarPlay 实时导航地图（车标自动居中），左右为严格等宽对称（<code>295×344px</code>）的大字运动信息框与大字导航信息卡，严丝合缝对齐左下挡位与右下车速斜边。</i></sub>
</div>

---

> [!IMPORTANT]
> **适用车型与安全边界（必读）**
> - **途锐专属界面模块**（全屏 CarPlay、导航上 HUD、仪表地图与信息框、OK 切地图、5GHz 修复）严格校验固件 **`MH2p_CN_VWG36_P2878`**，版本不符自动跳过不改任何文件。已在 2023 款途锐（iOS 27）及未改装实车完整验证；另有车友实测反馈 **2838** 与 **2019 款锐锋版**刷入成功（个案供参考）。
> - **大众 MH2p 通用弹窗屏蔽**：`VW_NoBtAuthPopup` / `VW_NoSimPopup` 校验大众品牌（`VW`），通过共享 Bundle `98` 精准过滤弹窗，与 HUD 插件互不冲突。
> - **跨品牌同平台通用底座**：`VW_WiFi_Dev`（手机无线控制台 / 无线 SSH）与 **[`docs/` 平台技术手册](docs/README.md)** 适用于大众集团同平台 **MH2p（MIB2+ High）**——涵盖 **大众途锐、奥迪（A6/A7/A8/Q3/Q7/Q8/e-tron）、保时捷（PCM 5）、宾利、兰博基尼（Urus LIS III）**。
> - **免费开源，严禁任何形式的转售或付费代刷**。使用前请先阅读文末[免责声明](#免责声明)。

---

## 10 大可选功能全景

> 所有模块默认保存在 `USB/未选模块/`（**默认全部未开启**），均通过电脑端「改装设置」按需勾选**安装 / 卸载 / 不处理**，依赖关系自动弹窗联动。

| 分类 | 模块名称 | 核心效果 | 适用范围 |
| :--- | :--- | :--- | :---: |
| **中控与投屏** | **🖥️ 全屏 CarPlay** | 车机端就地修补 38 字节去掉黑边，铺满整块中控屏 | 途锐 P2878 |
| | **📡 无线 CarPlay** | 开启原厂未激活的无线 CarPlay（集成 LawPaul 模块） | MH2p 通用 |
| | **🤖 Android Auto** | 开启原厂未激活的安卓投屏（集成 LawPaul 模块） | MH2p 通用 |
| **HUD 与仪表** | **🧭 导航上 HUD** | 苹果/高德/百度转向箭头、距离、路名、车道直投 HUD（有线/无线双栈） | 途锐 P2878 |
| | **🗺️ 仪表地图 + 大字运动信息框** | CarPlay 地图投射 `1920×639` 全液晶仪表（高德自动居中、方向盘缩放）；左右 `295px` 对称 **46px 粗体 + 斜切运动刻度条**（43 项实车物理数据）与**大字导航卡**（支持常驻待机） | 途锐 P2878 |
| | **🔄 方向盘 OK 双击切原车地图** | 方向盘右侧 `OK` 键 2 秒内连按 2 下，在 CarPlay 地图与原车地图间无缝秒切（`VW_HUD_4_OkMapSwitch`） | 途锐 P2878 |
| **无线与数据** | **📱 手机无线控制台 & SSH** | 手机热点打开 `http://touareg.local:8080` 秒改信息框、调车标、无线下载 10Hz 34 列行车记录 CSV；可选开 `2012` 无线 SSH 供 AI 自由开发 | MH2p 通用 |
| **修复与降噪** | **📶 升 P2878 无线 5GHz 修复** | 仅旧固件（如 P9832）升级 P2878 后有线正常但无线连不上时勾选（解除 `enable5GHzCountryAdaption` 拦截） | 途锐 P2878 |
| | **🔕 去除蓝牙「输入用户名」弹窗** | 共享 Bundle `98` 仅拦截蓝牙 OBEX 认证弹窗（`Busrlo`），不影响通话、音乐与 CarPlay | 大众 MH2p |
| | **🔕 屏蔽「SIM 仅用于数据」弹窗** | 共享 Bundle `98` 仅拦截连接投屏时的 `PHONE_SIM_DATAONLY` 提示，不影响联网与 SOS | 大众 MH2p |

<details>
<summary><b>💡 新手 2 秒抄作业：不知道怎么选？点这里看推荐组合</b></summary>

| 车主类型 | 推荐勾选模块 | 说明 |
| :--- | :--- | :--- |
| **大多数途锐车主（闭眼入）** | 在配置器点一键 **「驾驶推荐全套」**（全屏 + 无线 CarPlay + HUD + 仪表地图 + OK 双击切地图） | 兼顾日常体验与原车地图随时切回 |
| **爱折腾 / 想手机随时调仪表** | 在上面基础上再勾选 **「手机无线控制台」**（普通车主不用勾 SSH） | 以后换指标、导 CSV 再也不用拔插 U 盘 |
| **被烦人弹窗困扰** | 按实际弹窗按需补勾 **「去除输入用户名弹窗」** 或 **「屏蔽 SIM 提示」** | 两项独立生效，互不冲突 |
| **从 P9832 等老固件升上来连不上无线** | 补勾 **「老固件升 P2878 无线 CarPlay 5GHz 区域修复」** | 原厂 P2878 无线正常无需勾选 |

</details>

---

## 4 步免拆刷入安装

1. **准备 U 盘**：容量 **32GB 以内**，格式化为 **FAT32**（Windows：右键格式化；Mac：磁盘工具 → 抹掉 → `MS-DOS (FAT)`）。
2. **解压到 U 盘根目录**：从 **[Releases](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest)** 下载最新 `zip`，把压缩包里的**全部内容直接放进 U 盘第一层**（切勿多套一层外层文件夹）：
   ```text
   U盘根目录/
   ├── Data/                          ← 必须直接位于 U 盘第一层
   ├── Meta/                          ← 必须直接位于 U 盘第一层
   ├── Mods/                          ← 默认为空，由配置器自动移入已选模块
   ├── 未选模块/                       ← 存放全部 11 个待选模块目录
   └── 改装设置（Windows）.exe / （Mac）.command
   ```
3. **【必做】运行「改装设置」勾选并保存**：双击 U 盘根目录的 **`改装设置（Windows）.exe`** 或 **`改装设置（Mac）.command`**（**不运行直接插车会空刷、不会安装任何模块**）：
   - 在本地离线网页里直接点快捷预设或逐项选 **「安装 / 卸载 / 不处理」**，点选拖拽左右信息框与大字导航卡；
   - 若开启「手机无线控制台」，填好手机热点和 4～8 位管理口令 → 点击 **「保存到 U 盘」**（Mac 版自动清掉 `._*` 隐藏文件）。
4. **上车自动刷入**：安全弹出 U 盘 → **发动车辆** → 插入扶手箱 USB 口。约 10 秒后车机自动重启刷机（全程数分钟、会自动重启几次，**期间请勿操作车机**）→ 屏幕提示移除介质后拔出即生效。

> [!TIP]
> - **未签名提示**：桌面配置器未购商业签名，Windows 点「更多信息 → 仍要运行」；Mac 在「系统设置 → 隐私与安全性」点「仍要打开」。程序仅监听本机 `127.0.0.1` 修改当前 U 盘，**绝不联网**，源码见 [`tools/configurator`](tools/configurator)。
> - **旧版升级与黑边**：已刷过旧版直接覆盖刷入即可，原有的车标校准 `cl.conf` 自动保留；首次刷完若 CarPlay 仍有黑边，在 iPhone 蓝牙/CarPlay 设置里「忘记此车辆」重连一次即可。

<details>
<summary><b>🖥️ 点击展开查看：电脑端「途锐改装设置 v21」离线可视化界面</b></summary>
<br>
<div align="center">
  <img src="docs/img/configurator.png" alt="途锐改装设置本地可视化配置界面" width="84%">
</div>
</details>

---

## 手机无线控制台与 AI 自由开发

`VW_WiFi_Dev` 利用车机闲置的 `bcm2` 客户端接口静默连入手机热点（RSDB 并发不占无线 CarPlay、不改原车 `ecm0` 4G 车联网），彻底告别有线网卡和反复拔插 U 盘：

<div align="center">
  <img src="docs/img/wireless_console.png" alt="MH2p 8080 手机无线控制台三屏界面示意" width="96%">
  <br>
  <sub><i>手机连热点打开 <code>http://touareg.local:8080</code>：停车安全锁保护、43 项分类选配与大字预设、运动条/导航卡/车标调节、行车记录 CSV 无线下载。</i></sub>
</div>

### 1. 普通车主：零命令行，手机浏览器秒改配置
- 在「改装设置」里把 **「手机无线控制台」** 选为安装（无需勾选 SSH），填入 1～3 个常用手机热点（自动处理中文名十六进制编码与 iPhone 弯撇号 `’`）及 4～8 位管理口令。
- 上车打开手机个人热点（**iPhone 首次请开启「最大兼容性」并在「设置 → 个人热点」页亮屏等 3～5 秒**；安卓选 WPA2），车机开机约 1 分钟后自动连入。
- 手机浏览器访问 **`http://touareg.local:8080`**（首次输一次口令自动记住），停车时（车速 ≤ 3 km/h）直接改信息框、调车标、开关宽画布，或直接把 U 盘 `drive_logs` 里的行车记录 CSV 下载到手机喂给 AI 分析。

### 2. 傻瓜式无线 SSH + AI 自由开发（把钥匙和一句话发给 AI，免敲命令）

想用电脑上的 **[DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness)**（或 Claude Code / Cursor），或者只带一部安卓手机用 **[DSH Go](https://github.com/ccch1mneyyy/dsh-go)**（DeepSeek Harness 安卓端，iPhone 暂无公开包）无线直连车机自定义仪表，只需 **3 步**：

1. **拿本车专属私钥**：在「改装设置」里安装无线控制台时勾上 **「同时开启 SSH 无线开发终端（端口 2012）」** 并保存，桌面自动生成 `途锐无线SSH密钥/touareg_ed25519`（私钥不留在 U 盘里）。
2. **开热点连车机**：发动车辆并打开手机个人热点等待约 1 分钟（若手机开了全局 VPN/代理，请先关闭或绕过局域网）。
3. **把私钥和下面整段提示词直接发给 AI**（让 AI 自己读私钥、自己扫热点找到车机 IP 连上）：

<details>
<summary><b>🤖 点击展开并一键复制：发给 AI 助手的无线连车安全指令</b></summary>

```text
这是我车机的 SSH 私钥文件 `touareg_ed25519`（电脑默认在桌面 `途锐无线SSH密钥/` 文件夹，安卓手机端请直接读取我上传或放在 Download 目录下的 `touareg_ed25519`）。
请你自己读取这把私钥并配置保存好车机 SSH 连接（端口 `2012`，用户名 `root`）：
- 先尝试连接 `touareg.local:2012`；如果当前系统（如安卓端）不解析 `.local`，请你自动读取本机热点路由/ARP 邻居表或扫描当前热点子网开放 `2012` 端口的设备找到车机 IP 并连上，无需让我手动查 IP 或填配置。

车机环境与安全护栏（请严格遵守）：
1. 系统是 QNX 6.6.0 (ARMv7 Tegra K1)，shell 是 ksh（无 bash / sed -i / wc）。
2. 严禁改动原厂只读分区（/ifs、/mnt/system），不要频繁重载 pfctl 避免冲击 io-pkt。
3. 改装目录在 `/mnt/app/eso/bin/apps/cluster/`（仪表渲染 `cl_render`、配置 `dash.conf` / `cl.conf`）和 `/mnt/app/eso/bin/apps/wifi_dev/`；10Hz 实时车速、功率、扭矩、油温等数据在 `/tmp/dash.state` 与 `/tmp/cluster_cp.state`。
4. `/mnt/app` 默认只读：改文件前先 `mount -uw /mnt/app` 并备份原文件，用 `.new` 写完原子 `mv` 替换后立刻 `sync && mount -ur /mnt/app` 恢复只读；改完仪表配置执行 `slay -f cl_render` 即可秒级刷新仪表画面（无需重启车机）。
5. 完整接口表与自定义编译说明见公开仓库：https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/docs

现在请自动连上车机，只读查看一下当前车速/油温状态和已安装的模块，向我汇报连接成功，然后听我用大白话吩咐修改。
```

</details>

---

## 日常使用、高德说明与排障

- **仪表与 HUD**：仪表按方向盘 `VIEW` 键切到导航页，连上 CarPlay 后自动投屏（两侧原厂圆表盘可在车机设置里隐藏），夜间模式同步变深色；开 ACC 时地图自动上移避让辅助驾驶图标。
- **方向盘 OK 双击切原车地图**：选装 `VW_HUD_4_OkMapSwitch` 后，地图页且 CarPlay 在线时 **2 秒内连按 2 下右侧 `OK` 键**，即可在 CarPlay 地图与原车地图间零延迟切换（单按保持原厂自动缩放）。
- **10Hz 行车记录喂给 AI**：刷完后在「改装设置」第 5 步点 **「改成行车记录 U 盘」** 即可常插车上（不再触发刷机），将 `drive_logs` 里的 34 列 CSV 连同 `给AI分析行车记录.txt` 发给豆包、千问、DeepSeek 即可一键复盘。

<details>
<summary><b>📍 高德地图在 CarPlay 仪表上的已知特性与适配说明</b></summary>

- **车标左右偏（已默认开启画面自动居中）**：高德导航会把车标往目的地侧偏移约 3 cm；改装默认每秒识别画面车标，偏约 3 秒自动挪回时钟正下方（仅对高德生效，苹果/百度不受影响）。
- **比例尺自动缩放**：导航中高德会随车速与路口自动改回比例尺，方向盘上下键缩放为临时生效。
- **车道指引**：高德不通过 CarPlay 下发车道数据，需要 HUD / 导航卡车道指引可使用百度地图。
- **偶发开机停在原车地图**：iPhone 未响应第二屏请求，断开 CarPlay 重连一次即可恢复。

</details>

<details>
<summary><b>🔧 卸载还原、5GHz 无线修复与提 Issue 附日志</b></summary>

- **一键卸载还原**：运行「改装设置」把要移除的模块选为 **「卸载」** 保存后插车刷一次即精准还原（手动卸载需先将对应目录从 `未选模块/` 移回 `Mods/` 再放空文件 `uninstall.txt`；卸载 HUD 时需连同仪表地图与 OK 双击切地图共 4 个模块一并卸载）。
- **插 U 盘没反应 / 刷完没变化**：确认 U 盘为 **≤ 32GB FAT32**、`Data`/`Meta` 直接在根目录，且**已运行「改装设置」将所选模块移入 `Mods/`**；排查日志看 U 盘 `Logs/` 对应 `.log`（`_ModKit_Update.log` 中的 `Filename too long` 可忽略）。
- **老固件升 P2878 后无线 CarPlay 连不上**：在「改装设置」勾选 **「老固件升 P2878 无线 CarPlay 5GHz 区域修复」** 刷入，再于车机和 iPhone 双端忘记设备重新配对（详见 [#7](https://github.com/a78c7/touareg-cn-carplay-mods/issues/7)）。
- **提 Issue 附日志**：U 盘根目录建 `cluster_logs` 文件夹插车导航 2 分钟（或从 `8080` 无线控制台「日志导出」下载），仅上传 `Logs` 与排障日志，切勿公开与本车绑定的 `backup` 目录，发帖前注意抹去手机名或隐私路名。

</details>

---

## MH2p 跨品牌开发文档与源码架构

原开发笔记仓库 `vw-mh2p-dev-notes` 已完整并入 **[`docs/`](docs/README.md)**，每条结论均标注 **【途锐特有】** 或 **【MH2p 通用】**，供大众、奥迪、保时捷、宾利、兰博基尼同平台开发者查阅：

| 开发实战记录（[`docs/01~07`](docs/README.md)） | MH2p 平台参考手册（[`docs/reference/`](docs/reference/README.md)） | 核心源码目录（[`src/` & `tools/`](src)） |
| :--- | :--- | :--- |
| • [01 调试环境与工具](docs/01-调试环境与工具.md)<br>• [02 CarPlay 导航上 HUD](docs/02-CarPlay导航上HUD.md)<br>• [03 CarPlay 地图上仪表](docs/03-CarPlay地图上仪表.md)<br>• [04 仪表信息框与行车记录](docs/04-仪表信息框.md)<br>• [05 全屏补丁与 U 盘包](docs/05-全屏CarPlay与U盘包.md)<br>• [06 给其他车型的建议](docs/06-给其他车型的建议.md)<br>• [07 无线控制台与 SSH 开发](docs/07-无线控制台与SSH开发.md) | • [R1 QNX 分区、进程、ModKit 与防火墙](docs/reference/R1-系统结构与文件位置.md)<br>• [R2 HMI（J9 Java 插件与 BAP）](docs/reference/R2-HMI-J9-Java.md)<br>• [R3 DSI 车辆数据属性实测表](docs/reference/R3-车辆数据接口.md)<br>• [R4 显示屏编号、图层与 dmdt](docs/reference/R4-显示系统.md)<br>• [R5 CarPlay / iAP2 双栈协议](docs/reference/R5-CarPlay与iAP2.md) | • [`src/hud-java`](src/hud-java)：HMI OSGi 插件与 43 项遥测采集<br>• [`src/popup-filter-java`](src/popup-filter-java)：共享 Bundle 98 弹窗过滤器<br>• [`src/cluster-probe`](src/cluster-probe)：双屏解密与有线 Cinemo 钩子<br>• [`src/clusterd`](src/clusterd)：仪表切屏与安全区守护<br>• [`src/cl_render`](src/cl_render)：H.264 硬解、运动信息框与导航卡<br>• [`src/vhpatch`](src/vhpatch)：全屏 CarPlay 就地补丁器<br>• [`src/touareg_netd`](src/touareg_netd)：mDNS 与 `8080` 无线控制台<br>• [`tools/configurator`](tools/configurator)：跨平台本地「改装设置」 |

---

## 社区引用与致谢

- **[luanqibazao0123-bot](https://github.com/luanqibazao0123-bot)**（[#7](https://github.com/a78c7/touareg-cn-carplay-mods/issues/7)）：启发了 `v21` 大字运动刻度信息框视觉重构，并提供了老固件升 `P2878` 实测线索。
- **海外社区引用**：已被德国途锐论坛 **Touareg-Freunde** 的 [欧版 MH2p P2869 仪表投屏项目帖](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/)引用参考。
- **[LawPaul / MH2p SD ModKit](https://github.com/LawPaul/MH2p_SD_ModKit)**（[官网](https://lawpaul.github.io/MH2p_SD_ModKit_Site/)）：免拆 U 盘刷机框架及可选 `AppleCarPlay` / `AndroidAuto` 模块（[CC BY-NC-SA 4.0](LICENSES/MH2p_SD_ModKit_LICENSE.md)，其中安卓修复引用了 [fifthBro/pcm5-androidauto-connect-fix](https://github.com/fifthBro/pcm5-androidauto-connect-fix)）。
- **[fifthBro / mh2p-cluster](https://github.com/fifthBro/mh2p-cluster)** & **[mh2p-ssh-access](https://github.com/fifthBro/mh2p-ssh-access)**：`dio_cluster.so`（v0034）与 `cl_render` 上游基础（[CC BY-NC-SA 4.0](LICENSES/fifthBro_mh2p-cluster_LICENSE.md)）。
- **[davecozens / touareg-carplay](https://github.com/davecozens/touareg-carplay)**：全屏几何数值参考；**[luka-dev](https://github.com/luka-dev)**：`mib2q-carplay-rgi`（GPL-3.0）、`jxe2jar`、`qnx65-armv7-toolchain`。

## 许可证

- **本仓库原创代码**（`src/hud-java`、`src/popup-filter-java`、`src/cluster-probe/cl_probe.c`、`src/clusterd`、`src/touareg_netd`、`src/vhpatch`、`src/common`、`tools/`、模块脚本）：采用 **GPL-3.0 OR CC BY-NC-SA 4.0 双重许可**（见 [LICENSE](LICENSE)）；`docs/` 文档文字同时允许按 **CC BY-SA 4.0** 署名共享。
- **第三方开源组件**：`src/cl_render`、`dio_cluster.so`、`rg_core.inc`、ModKit 及 LawPaul 模块遵循 **CC BY-NC-SA 4.0**；`src/cluster-probe/wired/{framework,routeguidance}` 引用自 `luka-dev/mib2q-carplay-rgi`（**GPL-3.0**，见 [THIRD_PARTY_NOTICES.md](LICENSES/THIRD_PARTY_NOTICES.md)）；`dash_font.h` 预渲染字形基于 Noto Sans CJK SC（[SIL OFL 1.1](LICENSES/NotoSansCJK_OFL.md)）；`stb_truetype.h` 为公有领域（[Public Domain](LICENSES/stb_PUBLIC_DOMAIN.md)）；`vhpatch` 静态链接 `zlib 1.3.1`（[zlib License](LICENSES/zlib_LICENSE.md)）。
- **非商业与版权边界声明**：因发布包包含非商业授权组件，**整个 U 盘发布包仅供个人非商业学习与使用，严禁商用或售卖**。本仓库全部原创代码与自研 `VW_*` 模块不含任何大众或苹果受版权保护的原厂文件；可选的第三方 `AppleCarPlay` / `AndroidAuto` 模块原样引用自上游 LawPaul `MH2p_SD_ModKit`（默认未开启，按需选装）。

## 免责声明

1. **非官方**：个人业余研究项目，与大众、奥迪、保时捷、宾利、兰博基尼、苹果、谷歌、高德等公司无任何关联或授权关系。
2. **按现状公开**：代码、U 盘包和文档按“现状”提供，不作任何明示或暗示保证。
3. **风险自负**：刷机改装可能导致车机/仪表异常、丧失原厂保修或不符合当地法规，**一切直接或间接后果均由使用者自行承担**。
4. **行车安全**：严禁驾驶时操作手机控制台或车机设置；信息框数据仅供参考，车速与告警灯一律以原车仪表为准。
5. **严禁商用与代刷**：作者不提供付费刷机或远程协助；任何转售、打包收费行为均违反非商业许可并与作者无关。
6. **权利人联系**：如相关权利人认为内容涉及合法权益，请通过 [Issue](https://github.com/a78c7/touareg-cn-carplay-mods/issues) 联系，核实后及时处理。

---

## VW Touareg (CN) & MH2p Open Cockpit

One zero-disassembly USB flash for the China-market VW Touareg (`MH2p_CN_VWG36_P2878` — Touareg-specific patches safely skip other firmwares), plus an open wireless web console & SSH development base for **MH2p (MIB2+ High)** units across **VW, Audi, Porsche PCM 5, Bentley, and Lamborghini**:

- **10 Opt-in Modules (all disabled by default under `USB/未选模块/`, independently installable & uninstallable)**: Full-screen CarPlay (38-byte in-place patch), wireless CarPlay & Android Auto (upstream LawPaul modules), HUD turn-by-turn (wireless `libesoiap2` & wired Cinemo iAP2), `1920×639` cluster map with vision-based AMap centering & `OK` double-tap stock-map toggle (`VW_HUD_4_OkMapSwitch`), symmetrical `295px` **46px bold telemetry boxes with inline sport gauge bars (43 real physical DSI channels)** & **large-font navigation card** (auto-popup or always-on standby `nav_box=2`), 10 Hz 34-column CSV drive logger, `VW_Wireless5GHzFix`, and shared Bundle 98 popup filters (`VW_NoBtAuthPopup` / `VW_NoSimPopup`).
- **Wireless Console & SSH (`VW_WiFi_Dev`)**: Connects via idle `bcm2` STA (`dhcp.client -n -R`, preserving 5 GHz RSDB wireless CarPlay and `ecm0` telematics). Open `http://touareg.local:8080` (PIN-protected, CSRF/rate-limit hardened, parked-only lock) or enable key-only **Wireless SSH (`:2012`)** for live AI-assisted cluster development.
- **Install**: Format a ≤ 32 GB **FAT32** USB drive → extract the [latest release zip](https://github.com/a78c7/touareg-cn-carplay-mods/releases/latest) to the USB root → **run `改装设置（Windows）.exe` or `改装设置（Mac）.command` to select modules & save (required, as all modules default to off)** → start engine, insert USB, wait for reboots, remove when prompted.
- **Docs & License**: Full Chinese engineering docs (`01–07`) & platform reference (`R1–R5`) in [`docs/README.md`](docs/README.md). Original code dual-licensed under **GPL-3.0 OR CC BY-NC-SA 4.0**; third-party components per `LICENSES/`. **Personal, non-commercial use only; use entirely at your own risk.**

---

## VW Touareg & MH2p Open Cockpit (Deutsch)

Ein-USB-Stick-Modifikationspaket ohne Ausbau für den **VW Touareg III (CR)** mit **MH2p-Steuergerät (MIB2+ High)** sowie offene drahtlose Web-Konsole & SSH-Entwicklungsbasis für **VW, Audi, Porsche (PCM 5), Bentley und Lamborghini (LIS III)**:

- **10 optionale Module (standardmäßig alle deaktiviert unter `USB/未选模块/`, einzeln installier- und deinstallierbar)**: Vollbild-CarPlay (38-Byte-In-Place-Patch), Wireless CarPlay & Android Auto (LawPaul), HUD-Abbiegehinweise (Wireless & Wired Cinemo), `1920×639` Kombiinstrument-Karte inkl. **OK-Doppelklick-Umschaltung zur Werkskarte (`VW_HUD_4_OkMapSwitch`)**, symmetrische `295px` **Großschrift-Sport-Infoboxen mit integrierten Balkenanzeigen (43 echte DSI-Kanäle)** & **Großschrift-Navigationskarte** (`nav_box=1/2`), 10-Hz-CSV-Fahrtenlogger, `VW_Wireless5GHzFix` sowie gemeinsame Bundle-98-Popup-Filter (`VW_NoBtAuthPopup` / `VW_NoSimPopup`).
- **Drahtlose Web-Konsole & WLAN-SSH (`VW_WiFi_Dev`)**: Hintergrundverbindung über `bcm2` (`http://touareg.local:8080`, PIN-geschützt, Park-Sperre > 3 km/h) sowie optionales Key-only **WLAN-SSH (Port `2012`)** im RSDB-Parallelbetrieb zu Wireless CarPlay und Werks-eSIM `ecm0`.
- **Hinweis für EU-Fahrzeuge (z. B. `P2869`)**: Touareg-UI-Skripte prüfen auf `MH2p_CN_VWG36_P2878` und brechen auf anderen Ständen sicher ohne Änderungen ab; Architektur, `docs/` (`01–07`, `R1–R5`), Quellcode (`src/`) und `VW_WiFi_Dev` dienen direkt als Basis (siehe auch [Touareg-Freunde-Thread](https://www.touareg-freunde.de/forum/thread/30309-geschafft-apple-carplay-im-digitalen-kombiinstrument-des-touareg-cr-mh2p-p2869/)).
- **Lizenz & Haftungsausschluss**: Eigener Code unter **GPL-3.0 ODER CC BY-NC-SA 4.0**; Drittkomponenten gemäß `LICENSES/`. **Ausschließlich für den privaten, nicht-kommerziellen Gebrauch; Nutzung auf eigene Gefahr.**
