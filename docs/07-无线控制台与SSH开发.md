# 07 无线控制台与 SSH 开发

[← 目录](README.md)

前面 01 篇用 D-Link DUB-E100（AX88772C）USB 网卡连车机调试，稳定但有三个痛点：每次都要带网卡和转接头上车；插有线 CarPlay 时车机会切掉 USB 口的网卡供电；普通车主想改一次仪表信息框或把行车记录 CSV 拷到手机上，只能拔 U 盘回电脑改完再刷。这一篇记录怎么用原车闲置的无线客户端接口 `bcm2`，做成「手机开个人热点 → 车机后台自动连上 → 手机网页改配置 / 可选无线终端开发」的过程。

## 1. 为什么不用车机自带热点，也不用中控界面的「连接 Wi-Fi」

- **车机自带热点（`bcm0` / `carplay0`）**：手机连上车机热点后，大部分手机会把默认数据路由切向车机 Wi-Fi；车机自带 eSIM 没流量或网速很慢时，手机自身就断了 5G 外网，没法一边查资料或跑依赖云端接口的客户端（如 [DSH Go](https://github.com/ccch1mneyyy/dsh-go) —— DeepSeek Harness 安卓手机客户端，iPhone 暂无公开包）一边连车机；而且无线 CarPlay 本身就占着 AP 接口。
- **中控菜单里的 WLAN 客户端**：国行车机插着内置 4G 模块（`ecm0`）时，在设置界面点连接外部 Wi-Fi 会直接弹窗拦截：「WLAN 不可用。已使用了其他数据连接。」界面层不让连。**【途锐实测】**

## 2. 空闲的 `bcm2` 客户端接口

查看车机网络接口（`ifconfig`）和博通驱动配置，无线芯片分出了多个虚接口：`bcm0` 跑原厂热点和无线 CarPlay AP，而 **`bcm2` 是闲置的 STA（客户端）接口**。**【途锐实测，MH2p 大概率通用】**

车机自带 `/mnt/app/armle/usr/sbin/wpa_supplicant-2.5-wapi`，支持的驱动名不是 Linux 常见的 `nl80211`，而是 QNX 专用的 **`qwdi`**（`wpa_supplicant -D qwdi -i bcm2 -c wpa.conf`）。只要绕开 HMI 界面的限制直接在后台拉起 `wpa_supplicant`，`bcm2` 就能正常扫描并关联手机个人热点，同时完全不影响 `bcm0` 上的无线 CarPlay 和 `ecm0` 上的原车 4G 车联网。

## 3. 路由隔离：不碰原车 4G（`ecm0`）

关联上手机热点后需要拿 IP。如果直接跑默认参数的 `dhcp.client -i bcm2`，它会把手机热点设成整机默认网关并覆盖 `/etc/resolv.conf`，影响原车车联网通道。

解决办法是加上 `-n -R` 参数：

- `-n`：不写入默认路由（只生成 `bcm2` 所在热点子网的直连路由，比如 `172.20.10.0/28` 或 `192.168.43.0/24`，同热点的手机和电脑直连车机毫无障碍，车机外网仍走原厂 `ecm0`）；
- `-R`：不覆盖系统 DNS；
- `-h touareg -H`：向手机热点上报主机名 `touareg`，方便在安卓热点的「已连接设备」列表里一眼认出车机 IP。**【MH2p 通用】**

另外，当车主关掉热点或切换到另一个热点时，`bcm2` 断开关联不会自动清掉上一次拿到的旧 IPv4 地址；守护脚本在检测到 `status: active` 消失时需要主动停掉 `bcm2` 的 `dhcp.client` 并清掉旧地址，下次关联新热点才会立即重新请求 IP。

## 4. 踩坑：开机过早初始化撞崩 `io-pkt` 导致重启

第一版开机自启脚本一上电就立刻加载规则并拉起 `wpa_supplicant`，结果车机开机几十秒后自动重启。

查 `/mnt/crashlogs/core/` 定位到原因：开机前几十秒原厂正在初始化博通无线固件、`hostapd`、`ecm0` 拨号以及包过滤表（含 `altq` 队列）；这时候抢着操作 `bcm2` 驱动或频繁重载规则，会导致 QNX 网络协议栈进程 `io-pkt-v6-hc` 崩溃，车机看门狗检测到网络栈挂掉便触发整机复位。**【途锐实测，MH2p 通用教训】**

做了三层防护解决：

1. **延后启动**：先等原厂开机完成标志 `/tmp/startupFinishedFlag` 出现，再额外等待 **25 秒**，让无线固件、CarPlay AP 和 4G 拨号彻底稳定后再启动 `bcm2`；
2. **单实例与限次检查**：严格保证 `wpa_supplicant` 只启动一个实例，网络规则只在尚未生效时加载且单次开机最多尝试 3 次，运行期绝不反复刷新；
3. **零闪存写入熔断**：安装时记录当时 `/mnt/crashlogs/core/io-pkt-v6-hc*.core.gz` 的文件数量；以后每次开机先比对数量，一旦发现安装后新增过网络栈崩溃记录，脚本自动停止自启并写日志退出，绝不让车机陷入无限重启循环。

## 5. iPhone 个人热点的两个细节

实车用 iPhone 开热点测试时遇到两个很容易卡住新手的现象：

1. **弯撇号问题**：iOS 默认把热点名叫成 `XX’s iPhone`，里面的撇号不是键盘敲出来的英文直单引号 `'`（`0x27`），而是 UTF-8 弯撇号 `’`（`0xe2 0x80 0x99`）；而且 QNX 上的 `wpa_supplicant` 对中文和非 ASCII 名称最稳的写法是不加引号的小写十六进制字节串。因此「改装设置」和网页端在生成 `wpa.conf` 时统一把名称转成十六进制、加上 `scan_ssid=1`，遇到英文单引号自动双写一条弯撇号配置。
2. **省电休眠策略**：iPhone 退出「设置 → 个人热点」页面或锁屏一段时间后，如果当前没有设备连着，会停止广播热点信标。首次让车机连 iPhone 热点时，手机需要打开「最大兼容性」并停留在「个人热点」页面亮屏 3～5 秒，连上之后就可以正常锁屏切走了；安卓热点（选 WPA2）锁屏状态下也能随时自动连上。

## 6. OpenSSH 7.4 主机密钥与客户端兼容

车机自带的 `sshd` 是 OpenSSH 7.4（2016 年底版本），原厂配置默认只挂了旧式 `ssh-rsa` 主机密钥。现代 macOS、Windows 11 以及移动端新版本 SSH 客户端（如 [DSH Go](https://github.com/ccch1mneyyy/dsh-go) —— DeepSeek Harness 安卓手机客户端，iPhone 暂无公开包）默认已禁用 `ssh-rsa` 签名算法，连上去会报 `no matching host key type found`，而在手机客户端里往往不便手填旧版兼容参数。

好在车机这版 OpenSSH 7.4 本身已经支持 **`ssh-ed25519`**。在「改装设置」勾选开启无线开发终端时，程序会在电脑本地用标准库随机生成这台车专属的 Ed25519 主机密钥和登录密钥对（私钥存电脑桌面，绝不共用默认密钥），车机启动 `sshd` 时通过 `-h` 追加加载这把 Ed25519 主机密钥，手机和电脑客户端就能免任何额外参数直连 `2012` 端口。

## 7. `touareg_netd`：免查 IP（`touareg.local`）与 `8080` 手机网页控制台

为了让不想碰命令行的车主也能用手机改设置，写了一个约 54 KB 的纯 C 守护进程 `touareg_netd`（源码在 `src/touareg_netd`，只依赖 `libc` 和 `libsocket`）：

- **内置轻量 mDNS 响应器（UDP 5353）**：监听 `224.0.0.251:5353`，收到查询 `touareg.local`（A 记录）时直接把当前 `bcm2` 的 IPv4 地址回给局域网设备。iPhone Safari 和电脑无需去热点列表翻 IP，直接访问 `http://touareg.local:8080` 或连 `root@touareg.local`。（注：部分安卓端原生编译程序内置 DNS 解析器不走系统 mDNS，连不上 `.local` 时填热点列表里 `touareg` 的实际 IP 即可。）
- **仅绑定 `bcm2` 地址**：车机本地 `127.0.0.1:8080` 原本就有内部服务占用。`touareg_netd` 轮询拿到 `bcm2` 的局域网 IPv4 后，只绑定 `<bcm2_ip>:8080`（IP 变化时自动重绑），既避开本地端口冲突，又保证绝不在 `ecm0` 车联网接口上监听。
- **口令与行车安全锁**：
  - 除首页 HTML 外壳外，所有 `/api/` 接口默认 **fail-closed** 强制核对管理口令 `web.pin`（无有效口令时一律返回 `503` 拒绝访问）；
  - **首次启动自动生成与旧版平滑升级**：若早期旧版升级或手动部署未配置 `web.pin`，`touareg_netd` 启动时会自动从 `/dev/urandom` 生成 6 位随机数字口令写入 `web.pin`（`0600`）与 `web.pin.unconfirmed` 标记，并同步写入内存盘 `/tmp/touareg_setup_pin`（`0600`），由 `cl_render`（`navbox.c`）在仪表屏弹出大字初始口令提示卡；若当次未连 CarPlay 即熄火，下次重启会自动恢复上屏提示，直至手机首次成功登录 `8080` 后永久清除两个提示/标记文件（已有 `web.pin` 时开机零闪存写入、不弹提示）；
  - 口令仅通过 HTTP 请求头 `X-Pin` 传递（禁止 URL query 传参，文件与诊断包下载走 `fetch` + `Blob`），并按客户端来源 IP 独立计数阶梯退避限速（16 槽静态表，满槽全锁定时直接拒绝新 IP 防多 IP 轮换挤占；连续输错 5 次依次锁定 30s → 120s → 300s → 900s）；桌面「改装设置」要求新设口令为 **6～16 位**字母/数字（车机端兼容旧版 4～8 位）；
  - 所有修改类接口先读 `/tmp/dash.state` 里的实时车速，车速 > 3 km/h 时拒绝写入，只允许停车状态改配置；
  - 写 `dash.conf`、`cl.conf`、`wpa.conf` 一律走同分区 `.new` 写入 + `fsync` + `rename` 原子替换，写完立刻把 `/mnt/app` 恢复只读挂载（`mount -ur /mnt/app`）。
- **按需分离权限**：普通车主在「改装设置」里不勾选开发终端时，模块写入 `no_ssh` 标记，车机只开放 `8080` 网页控制台和 `5353` mDNS，不启动 `2012` 端口、也不写登录公钥；如果安装在未刷途锐仪表模块的同平台其他品牌 MH2p 车机上，网页检测到没有 `dash.conf` 会自动隐藏途锐专属仪表卡片，保留 Wi-Fi 管理、系统日志下载和车机软重启功能。

## 8. 离线无线升级（OTA）与一键回滚（v22.0 / v22.1 签名加固）

已安装 `v22.1+` 无线控制台后，后续升级核心程序不再需要反复拔插 U 盘（`v22.01` 及更早版本首次升级到 `v22.1` 须先走 U 盘覆盖刷入一次）：

- **流式落盘（零 `/tmp` 内存压力）**：在手机浏览器打开 `http://touareg.local:8080` → **开关运维 → 离线无线升级**，上传从官方 Release 下载的 `touareg-ota-<版本>.tar`（上限 `6 MB`）。`touareg_netd` 以 16 KB 分块流式直写至 `/mnt/app/eso/bin/apps/.ota_stage/pkg.tar`，绝不把升级包缓存在 `/tmp` 内存盘中。
- **官方 Ed25519 签名与两阶段白名单校验**：解析 POSIX `ustar` 归档时强制校验八进制长度防 32 位溢出，拒绝 `..` 路径穿越、子目录、符号链接、设备节点与重复条目，仅放行 6 个白名单核心组件（`cl_render`、`clusterd`、`cl_probe.so`、`touareg_netd`、`VWClusterNav_v6.jar`、`VWPopupFilter.jar`）及版本/清单元数据；采用**先验签后解包**两阶段设计——第一阶段只读头部与元数据并用车机内置公钥（`src/touareg_netd/ota_pubkey.h`）完成纯 C RFC 8032 **Ed25519** 验签与 `version.txt` 校验，验签未通过不向 `.ota_stage` 写入任何解包组件；验签通过后再解包逐文件完成 FIPS 180-4 SHA-256 校验，任意字节不匹配即整体中止，不触碰车机任何现役文件。（注：`v22.01` 及更早版本因旧版白名单不含 `manifest.sha256.sig`，升级到 `v22.1` 须先用 U 盘刷入一次；当前内置单把官方公钥，若未来需轮换公钥亦须通过 U 盘升级；开发者若需测试本地未签名开发包，可通过 SSH 手动在车机创建 `/mnt/app/eso/bin/apps/wifi_dev/allow_unsigned_ota` 显式放行，该开关绝不通过网页暴露。）
- **自动备份与一键回滚**：校验通过后先将车机现役文件复制到 `/mnt/app/eso/bin/apps/.ota_backup/`，再用同分区 `.new` → `fsync` → `rename` 原子替换并立刻把 `/mnt/app` 恢复只读。若新版异常，可在网页点 **「回滚上一版备份」** 一键还原。

## 9. 一键脱敏排障诊断包（v22.0）

在 `8080` 控制台 **日志导出** 页点击 **「下载脱敏诊断包」**（`GET /api/diag`），车机会在内存中即时组装 `touareg_diag_sanitized.txt`，包含：当前版本与安全模式熔断状态、全部开关位、`dash.conf`/`cl.conf`、`/mnt/crashlogs/{java,core}` 文件名列表、以及白名单内 `/tmp/*.log` 末尾 12 KB 摘要。

输出前逐行经 `diag_sanitize_line()` 过滤器脱敏，自动把 `psk`、`pin`、`password`、`key`、`token`、SSH 私钥块、17 位车架号（VIN）与 11 位手机号替换为 `[REDACTED]`，方便直接发往 GitHub Issue 或车友群定位问题。

## 10. 3 次崩溃零闪存损耗安全模式熔断（v22.0）

为避免自定义改件或 AI 误改代码导致车机反复崩溃死循环，`v22.0` 将原本仅覆盖 `io-pkt` 的零损耗熔断机制扩展至**全链路四个核心节点**：

| 保护目标 | 基线记录文件（仅安装/重置时写 1 次） | 触发条件（开机只读比对，零 eMMC 写入） | 熔断行为 |
| :--- | :--- | :--- | :--- |
| J9 HMI 插件（Bundle 97 / 98） | `cluster/j9_base_crashes` | `/mnt/crashlogs/java/*` 新增 ≥ 3 次 | 创建 `/tmp/vw_hmi_safe_mode`，跳过 `VWClusterNav` 与 `VWPopupFilter` |
| CarPlay 探针（`cl_probe.so`） | `cluster/dio_base_cores` | `dio_manager*.core.gz` 新增 ≥ 3 次 | 创建 `/tmp/vw_dio_safe_mode`，探针直通原厂不做拦截 |
| 仪表渲染守护（`clusterd` / `cl_render`） | `cluster/render_base_cores` + 运行期 30 秒计数 | 新增 ≥ 3 次 core，或 `cl_render` 30 秒内连崩 3 次 | 写 `/tmp/cl_render_fused` 并切回原车地图 `[33]` |
| 无线网络栈（`VW_WiFi_Dev`） | `wifi_dev/iopkt_base_cores` | `io-pkt-v6-hc*.core.gz` 新增 ≥ 1 次 | 停止拉起 `bcm2`，不碰原车网络栈 |

随时也可在 `8080` 网页勾选 **「手动安全模式（`safe_mode`）」** 临时退回纯原厂；排除故障后点 **「清除安全模式并刷新崩溃基线」** 即可恢复。

## 11. AI 无线开发边界与车机硬件护栏（开发者与 AI 必读）

无论你用 DeepSeek Harness、Claude Code、Cursor 还是安卓端 DSH Go 无线修改车机，都请把以下 5 条硬件硬边界写进工作流程，避免“AI 写得没错、但把车机拖卡或写伤闪存”：

1. **内存总量仅 ~2 GB，且 `/tmp` 是内存盘（tmpfs）**：禁止往 `/tmp` 拷大文件、未截断的无限增长日志、或把多帧未压缩 `1920×639` RGBA 位图常驻堆内存；新增常驻进程内存占用应压在 20 MB 以内。
2. **eMMC 寿命红线（禁止高频写闪存）**：`/mnt/app`、`/mnt/ota`、`/mnt/system` 均为车规 eMMC 闪存。**绝对不要**把每秒数次的传感器数据、心跳计数或状态缓存写进 `/mnt/app`（必须放 `/tmp` 内存盘）；`/mnt/app` 平时始终保持只读挂载，仅在保存配置或 OTA 时短暂 `mount -uw`，用 `.new` + `fsync` + `mv` 原子替换后立即 `sync && mount -ur /mnt/app`。
3. **锁车休眠与电瓶寄生耗电**：熄火锁车后 MH2p 主机随整车 CAN 总线休眠彻底断电，本仓库所有后台服务均随主机一同停止，**不会产生锁车静态寄生耗电**；开发新功能时严禁尝试阻止车机电源管理（`ooc`）休眠。
4. **渲染架构与帧率边界（CPU 软渲 vs OpenGL ES 2.0 硬件加速）**：
   - `cl_render` 默认的 **`capture=h264`** 模式采用 **Tegra K1 NVDEC 硬解 + QNX `screen_blit` 硬件合成**，`dash.c` 仅在 CPU 上绘制两侧 `295×344` 小框。这套架构显示数字与轻量运动条极稳，配合 `v22.0` 的 `10Hz` 采样 + `smooth=1` 指数阻尼插值已足够跟手；**但千万不要让 AI 在 `dash.c` 里用 CPU 循环做全屏 `1920×639` 逐像素动画或高频大面积重绘**，否则必然掉帧发卡！
   - 若要实现 **全屏 60fps 仪表指针、弧形转速表、发光渐变或自定义动效主题**，请使用 `v22.0` 新增的独立 OpenGL ES 2.0 硬件加速引擎 **`src/cl_render/gles60.c`**（运行参数 `capture=gles60`，同一窗口 `58`，以 `eglSwapInterval(dpy, 1)` 锁死硬件 VSync 60fps，并用 `eglGetProcAddress` 动态解析 `gl*` 符号、绝不直接链接 `-lGLESv2`）。
5. **先本地回放预览，再上车验证**：修改信息框布局或动画参数前，先在电脑双击打开 [`tools/preview/index.html`](../tools/preview/index.html)，拖入真实 `drive_logs/*.csv` 或切换内置场景实时预览，确认观感与帧率后再上车部署，并欢迎把满意的 `dash.conf` 与截图发到仓库 **GitHub Discussions** 共享给车友。

## 12. 自动化测试、静态分析与 CI 门禁（v22.1）

所有提交与发布前均由 [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) 及本地一键脚本 [`tests/pre_release_check.sh`](../tests/pre_release_check.sh) 执行五类自动校验：

1. **Go 配置器检查**：`go vet ./...` 与 `go test -race -v ./...`（覆盖模块依赖联动、PIN 6～16 位策略、Ed25519 SSH 密钥生成及 U 盘默认全关验证）。
2. **C 主机单测（ASan + UBSan）**：[`tests/run_c_tests.sh`](../tests/run_c_tests.sh) 以 `-Wall -Wextra -Werror -fsanitize=address,undefined` 编译运行 `test_wired_rg`、`tests/test_ota.c`（SHA-256、RFC 8032 Ed25519 向量、OTA 验签/篡改拦截/回滚、诊断脱敏）与 `tests/test_netd.c`（fail-closed PIN、首次随机生成与自动清除、按 IP 阶梯限速、禁止 URL `?pin=`、CSRF 与路径穿越拦截）。
3. **静态分析（`cppcheck`）**：对 `touareg_netd.c`、`clusterd.c`、`cl_wired_rg.c`、`vhpatch.c` 执行 `--enable=warning,performance,portability --error-exitcode=1` 零告警门禁。
4. **43 项遥测三端对齐与仓库卫生门禁**：自动比对 `src/cl_render/dash.c`、`tools/configurator/package.go` 与 `src/touareg_netd/web_ui.html` 的 43 项遥测键名一致；检查前端 HTML 裸 DOM ID、禁止 ModKit `Update/*.sh` 提前锁回 `/mnt/app` 只读、检查 `web_ui_data.h` 与 `web_ui.html` 字节长度同步，并禁止大体积桌面配置器二进制或本机私有绝对路径误入 git。
5. **Release 签名与产物校验**：[`.github/workflows/release.yml`](../.github/workflows/release.yml) 现场从源码编译双端配置器，使用仓库 Secret `OTA_SIGNING_KEY`（核对与 `src/touareg_netd/ota_pubkey.h` 严格匹配）为 `touareg-ota-*.tar` 生成 Ed25519 签名，并同步发布 `SHA256SUMS.txt`。
