# 版本说明（发布时自动取对应版本的一节作为 Release 说明）

## v23.0-beta.3
> **Pre-release 实车崩溃关键修复版（强烈建议所有 `v23` 尝鲜车主升级）**：基于首台途锐实车运行日志（`/tmp/v23_fallback_v22` 触发第 2 层保险丝 0 黑屏退回 `v22.1`），定位并一次性修复了 `v23.0-beta.1/.2` 在途锐 Tegra K1 真机上启动即退出的 **2 个必崩 Bug + 2 处并发/兼容隐患**。已装 `v22.1+` 与 `8080` 无线控制台的车主直接传 `touareg-ota-v23.0-beta.3.tar` 10 秒无线升级即可（升级后需在 8080 点一次「刷新仪表渲染」或重启车机生效）。

- **修复实车必崩 Bug #1：`v23` 模式下传入空指针给 NVIDIA `SiblingCreate`（`cluster.c`）**：
  - `g_v23_mode` 下 `post_win` 仅分配 2 个 EGL 缓冲（`post_bufs[2..15]` 为 `NULL`），但旧版 `setup_screen()` 仍循环 `MAX_SURFACES` 次调用 `nv.SiblingCreate(device, post_bufs[i], ...)`，导致 Tegra K1 上启动瞬间空指针段错误；现已在 `v23` 模式下跳过无用的 `scr_siblings` / `canim_pixmap` / `boot_pixmap`，并同步放行 `cb_display_picture()` 解码帧投递。
- **修复实车必崩 Bug #2：QNX 子线程默认小栈上分配 ~150 KB 局部变量（`v23_gles.c`）**：
  - 将 `v23_run_gl_loop()` 中的 `v23_gl_ctx_t` 与双 QuickJS 沙盒（`fb_sb` + `user_sb`，合计 ~150 KB）全部改为堆分配（`calloc`），同时 `pthread_create` 显式设置 **1 MB** 栈大小，彻底根治 QNX ARMv7 子线程栈溢出。
- **消除 `post_win` 多线程启动竞态 + 默认稳走 `glTexSubImage2D` 紧致上传（`cluster.c` + `v23_gles.c`）**：
  - 将 `v23_map_setup()` 延后至 `setup_screen()` 全部主线程配置完成后再拉起 EGL 线程，并改为**首帧 `eglSwapBuffers()` 成功后才写 `/tmp/cl_render_ready`**（杜绝抢先切屏）；退出时正常 `pthread_join` 回收 GL 线程；`load_gl()` 增加 `dlsym` / `dlopen("libGLESv2.so.2")` 符号兜底；
  - 默认走 100% 稳妥的 `glTexSubImage2D` 路径（支持非紧致 `stride` 行拷贝，规避未实测过的 `EGL_NATIVE_PIXMAP_KHR` 驱动差异风险；仅在手动创建 `v23_zerocopy` 标志时尝试零拷贝）。
- **新增 `/tmp/v23_diag.log` 逐步落盘诊断（`v23_gles.c` + `touareg_netd` + `Persist/install.sh`）**：
  - `v23` 初始化的每一步（EGL 初始化、Shader 编译、字形图集、地图纹理、QuickJS 加载、首帧送显）均实时落盘到 `/tmp/v23_diag.log`，并自动纳入 `8080` 脱敏诊断包与 U 盘 `cluster_logs/` 采集。
- **车机 QNX 6.6 ARMv7 二进制校验和（`src/build.sh`，已通过真实车机 `carlibs` 符号审计：0 缺失非弱符号）**：
  - `cl_render`：`93b4fbfbc032497ef285413e710ea94d79b18f84c2c2adb821d3cc3d0baf1734`
  - `touareg_netd`：`0c32cb18f532b85164ab512799c72cfe626b14ad72248ba63388792a4ed8a8bd`
  - `clusterd`（与 `v23.0-beta.1` 一致）：`096a71c1b37bdcf9ebba3b1d3e66428af38488177c585ed0e3d7f4db695d4f64`
  - `cl_probe.so`（与 `v22.1` 一致）：`e260013df86291611225856c6f1c3c05a1d12857bfeaaa0514d1494192cbfa24`
- 包含 `v23.0-beta.2` 与 `v23.0-beta.1` 的全部内容。

## v23.0-beta.2
> **Pre-release 尝鲜防呆升级版**：在 `v23.0-beta.1` 基础上专项强化了 **U 盘安全弹出、刷机脚本自动开 `v23_mode` 兜底，以及本地 AI 助手（Claude Code / Cursor / Codex / Antigravity 等）零踩坑命令行部署能力**。车机 QNX 二进制与 `v23.0-beta.1` 完全一致，**已装 `v23.0-beta.1` 的车机无需重刷 OTA**；新制作刷机 U 盘或用 AI 写自定义皮肤的车主推荐直接下载本版 `.zip`。

- **刷机脚本自动兜底防漏开关（`VW_HUD_3_CarPlayCluster/Update/install.sh`）**：
  - 默认空包仍 100% 稳跑 `v22.1`；只要 U 盘存在非空自定义皮肤 `themes_user/my_theme.js` 且没有显式关闭标记（`v23_off`），刷机脚本会自动启用 `v23_mode`，彻底杜绝 AI 或手工放了皮肤却忘建 `v23_mode` 导致上车不出皮肤的问题（0 字节写残文件不会误开；在「改装设置」手动取消勾选 v23 会写入 `v23_off` 明确退回 `v22.1`）。
- **「改装设置」强制刷盘 + 一键安全弹出 U 盘 + 无界面 CLI 模式（`tools/configurator`）**：
  - 全部 U 盘文件写入升级为 `fsync` 立即落盘 + 系统 `sync`，保存成功后网页直接提供 **「⏏ 一键安全弹出 U 盘并退出」** 按钮（仅对可移动 U 盘启用，自动校验盘符/挂载点绝不误弹系统盘），避免 FAT32 写缓存未落盘时直接热拔导致皮肤截断或脏分区。
  - 新增无界面命令行模式（双击不带参数仍正常启动网页界面）：支持 `--deploy-theme <my_theme.js>`（自动校验 128 KB / UTF-8 / 禁用接口、补齐 `hud`+`cluster` 依赖、保留原有 `dash.conf`、写 `v23_mode`、清 Mac `._*` 隐藏文件）、`--preset base|full|none`、`--install/--uninstall/--skip`、`--clean`、`--verify/--status`（含 `--json`）与 `--eject`。
- **U 盘根目录内置 AI 部署规矩（`AGENTS.md` / `CLAUDE.md`）**：
  - 车主直接把 U 盘目录交给本地 AI 文字部署时，AI 会自动读取规范并调用「改装设置」命令行完成皮肤校验、依赖补齐、隐藏文件清理与安全弹出，杜绝漏移 `Mods/`、漏开关或未弹出直接拔盘。
- 包含 `v23.0-beta.1` 的全部内容与车机二进制（已补全 `v23_off` 与 CLI 自检全套 Go `-race` 单测）。

## v23.0-beta.1
> **Pre-release 尝鲜测试版说明**：本版本为下一代 `v23` 60fps AI 自定义皮肤架构的首个公开尝鲜包，**正式稳定版仍保持 `v22.1`**。**默认安装不启用 `v23_mode`，开机 100% 稳跑与 `v22.1` 完全一致的经典稳定通道**；仅当你在「改装设置」里主动勾选「启用 v23 60fps 皮肤引擎（Beta 尝鲜）」后才会切换进入新引擎，且全程受车机三层自动保险丝保护。

- **普通车主一站式零门槛体验（「改装设置」 + U 盘一次写入，无需连 8080 热点）**：
  - **「改装设置」（Mac / Windows）整合全流程（`tools/configurator`）**：
    - 新增 **「v23 60fps AI 自定义皮肤（Beta 尝鲜 · 可选）」** 面板，默认不勾选（稳跑 `v22.1`）；
    - 内置 **一键下载空白模板 `my_theme.js`**（文件头写满给 AI 看的坐标禁区、43 项车况变量与画笔规范，支持同一个文件用 `const THEMES = [...]` 写 1～8 套皮肤，并提醒 AI 写完引导你在浏览器预览）；
    - 内置 **1:1 浏览器试驾模拟器**（直接从「改装设置」点按钮打开，零额外文件；支持键盘 `W/S` 油门刹车、`A/D` 转向、空格一键弹射、`ACC 巡航底部居中遮挡区`模拟、冷车传感器未就绪模拟、拖拽加载 `my_theme.js` 与三层保险丝破坏测试）；
    - 导入 AI 写好的 `my_theme.js` 后，点一次「保存到 U 盘」即可把 `v23_mode` 与 `themes_user/my_theme.js` 写入 U 盘；上车插 U 盘一次刷入生效（不装 8080 无线控制台模块也能 100% 使用；取消勾选重刷一次 U 盘即可退回 `v22.1`，升级未提供新皮肤时自动保留车机原有皮肤）。
  - **独立离线预览包随压缩包附赠**：发布包 `v23皮肤模板/` 目录与仓库 `tools/preview/` 同步提供单文件零依赖 `preview.html`（108 KB，断网双击即用）+ `my_theme.js` + `THEME_API_SPEC.md`，方便直接分享给车友或 GitHub Discussions。
- **车机端统一单画布 60fps OpenGL ES 2.0 + QuickJS 零编译皮肤引擎（`cl_render` + `clusterd`）**：
  - **CarPlay 实时地图与 60fps 仪表同屏（窗口 `58`，`[58, 33]`）**：NVDEC 硬解 CarPlay 地图流（~20fps）经 `screen_blit` 写入三缓冲离屏 RGBX8888 表面，优先通过实车驱动原生 `EGL_KHR_image_pixmap` 零拷贝绑定为 GLES2 纹理（不兼容时自动无缝退回 20fps `glTexSubImage2D`），上层指针、圆弧、运动条、导航卡与文字全部由 Tegra K1 GPU 以 60fps VSync 合成；复用 `navbox.c` 已加载的原厂 `ZYHei_GB18030_c.ttf` + `VWTextCarUI-Bold.ttf` 升级为 `1024×1024` 共享 GLES2 动态字形图集；支持稀疏采样地图亮度 + 滞回防闪烁自动昼夜判定。
  - **默认只内置 1 套官方经典皮肤（`default_classic`）**：100% 沿用老用户 `dash.conf` 43 项勾选与大字导航卡；支持全部 17 个 `ctx.*` 高速画笔 + 可选 ≤ 8 KB 自定义 GLSL ES 1.00 光效着色器；冷车未就绪信号（`NaN/-999`）统一清洗为有限 `0` 并提供 `bus.ready.*`，杜绝 `.toFixed()` 抛错。
  - **三层自动保险丝保障（无需 8080 救砖）**：
    - **第 1 层（QuickJS 安全沙盒）**：纯核心 QuickJS（零 `quickjs-libc`/`pthread`/`assert`、物理移除 `eval`/`Function`），强制 **128 KB 单文件上限 + 4 MB 16B 对齐精确内存封顶 + 256 KB 栈上限 + 单帧 10 ms 硬中断强杀 + 掉帧看门狗 + 冷启动崩溃哨兵**；任一触发当帧 **0 黑屏** 补画官方默认经典皮肤并在仪表顶栏弹出报错原因与行号，坏 GLSL 光效仅降级关闭光效层；
    - **第 2 层（`clusterd` 秒退 `v22.1`）**：`v23` 渲染进程异常退出或 8 秒内未就绪，立即写 `/tmp/v23_fallback_v22` 并自动无缝拉起久经验证的 `v22.1` 经典 `screen_blit` 通道，本次行车照常看导航；
    - **第 3 层（原车地图硬兜底）**：30 秒内连续崩溃 3 次自动熔断回大众原车地图 `[33]`，行车中方向盘双击 `OK` 随时可手动切回原车地图。
- **8080 无线控制台（进阶可选）同步支持 `v23` 极客热调（`touareg_netd`）**：
  - 新增 `v23_mode` 开关、`/api/theme/status` 状态查询、流式 **128 KB** `/api/theme/live`（直写 `/tmp/theme_live.js` 内存盘，**0 次 eMMC 闪存写**，1 秒热生效）、`/api/theme/save` 停车固化保存与 `/api/theme/select` 风格切换（全部受 `X-Pin`、`X-Requested-With` 与车速停车锁保护）。
- **车机 QNX 6.6 ARMv7 二进制构建信息与校验和**（Ubuntu `clang 14.0.0` + `arm-linux-gnueabi-ld --hash-style=both` + `qnx65-armv7-toolchain`，`src/build.sh`）：
  - `cl_render`（含 QuickJS + GLES2 引擎，1.4 MB，0 非弱缺失符号 / 0 私有 `libm` 符号）：`768e8875f0b6047d2f74b273d1a5e2eec342da83e154fcb855c035c27fade116`
  - `clusterd`：`096a71c1b37bdcf9ebba3b1d3e66428af38488177c585ed0e3d7f4db695d4f64`
  - `touareg_netd`：`1154362bc7b52c4434e839485baf1c0a757aeab162b017ea7641b5413f2f1259`
  - `cl_probe.so`（与 `v22.1` 完全一致）：`e260013df86291611225856c6f1c3c05a1d12857bfeaaa0514d1494192cbfa24`
- **诚实透明的验证边界说明**：
  - ✅ **已全量实测通过**：独立离线 `preview.html`（真 Chrome 隔离零外链 0 报错）、`tools/configurator` Go `vet`/`-race` 单测、主机 4 套 C 单测（`test_wired_rg`/`test_ota`/`test_netd`/`test_v23_theme`，含 17 项 `ctx` 接口、7 项安全熔断、冷车兜底、72KB/140KB 限额、32KB 8080 流式上传、亮度自动昼夜，ASan + UBSan 全绿）、`cppcheck` 静态分析、QNX 6.6 ARMv7 真实交叉编译与 `carlibs_v1.tgz` 全量符号审计（0 缺失非弱符号、0 私有数学符号）。
  - ⚠️ **待实车最终复核（因此发为 Pre-release 且默认不启用 `v23_mode`）**：Tegra K1 实车 GLES2 60fps 帧率与 CPU 占用、`EGL_KHR_image_pixmap` 零拷贝命中率（未命中会自动降级 `glTexSubImage2D`）、CarPlay 重连切屏与实车中文字体图集观感、以及车机端三层保险丝真机触发表现。欢迎有兴趣尝鲜并愿意反馈脱敏诊断包的车友测试！

## v22.1
- **无线控制台全面安全加固 + OTA 官方 Ed25519 数字签名 + CI/测试与发布体系升级**：
  - **无线控制台 PIN 鉴权 Fail-Closed + 首次启动自动随机生成并上屏显示（`touareg_netd` + `cl_render`）**：
    - 彻底移除未配置 `web.pin` 时默认放行的历史兼容路径；`check_pin_auth()` 改为严格 **fail-closed**（无有效口令时返回 `503` 拒绝所有 `/api/` 请求）；
    - 为兼顾无痛上手与旧版升级：`touareg_netd` 启动时若检测到车机尚未配置 `web.pin`，自动使用 `/dev/urandom` 无偏生成 **6 位随机数字初始口令** 原子落盘并写入 `web.pin.unconfirmed` 与内存盘 `/tmp/touareg_setup_pin`（`0600`），由 `cl_render`（`navbox.c`）在仪表屏幕弹出高对比度大字口令卡片（即使 `nav_box=0` 也可显示，首次未登录即熄火重启仍会自动重显）；手机首次使用该口令登录 `8080` 成功后立即永久清除提示与标记；
    - 彻底取消 URL query `?pin=` 传参（文件与诊断包下载前端改为 `fetch` 带 `X-Pin` 请求头转 `Blob` 触发下载），消除浏览器历史与日志泄露口令的风险；
    - 限速由全局计数升级为**按来源 IP 独立的 16 槽防挤占静态表 + 阶梯指数退避（30s → 120s → 300s → 900s，1 小时无失败自动衰减降级）**，杜绝同 Wi-Fi 他人误输把车主锁死的拒绝服务问题与多 IP 轮换绕过；桌面「改装设置」新建口令要求提升至 **6～16 位**数字/字母（车机端继续兼容旧版已存在的 4～8 位口令）。
  - **离线无线升级（OTA）新增官方 Ed25519 数字签名与两阶段先验签后解包（`ed25519_verify.h` + `ota_pubkey.h` + `ota_util.h`）**：
    - 车机端内置 32 字节官方 Ed25519 公钥（单公钥设计，未来若需轮换公钥须通过 U 盘升级）；采用**两阶段先验签后解包**设计——第一阶段只读 tar 头与元数据并校验 32 位八进制防溢出、完成纯 C99 RFC 8032 Ed25519 验签（含 `S < L` 规范性检查）与 `version.txt` 校验，未通过不向 `.ota_stage` 写入任何解包组件；验签通过后再解包逐文件核对 SHA-256 并拒绝重复 tar 条目；
    - 官方 Release 打包脚本（`tools/make_release.sh`）与 GitHub Actions 自动使用仓库 Secret 私钥签名并核对公钥一致，私钥严格离线/存于独立密钥库、绝不进入 git；本地 SSH 开发者可在车机手动创建 `/mnt/app/eso/bin/apps/wifi_dev/allow_unsigned_ota` 测试未签名开发包（网页端绝不暴露该开关）。
  - **大体积桌面配置器二进制出库 + 发布校验和**：
    - 从 git 源码树移除 16.6 MB 的 `USB/改装设置（Mac）.command` 与 `USB/改装设置（Windows）.exe` 并加入 `.gitignore` 与 CI 拦截；每次发版由 GitHub Actions 从 `tools/configurator` 源码交叉编译打包，并随 Release 同步发布 **`SHA256SUMS.txt`** 供车主核对完整性。
  - **C 代码安全审计收尾 + 测试/静态分析体系升级**：
    - 全仓清理剩余 `strcpy` / `sprintf` 调用，顺带修复 `cluster.c` 调试日志截断写边界，并将 `touareg_netd.c` 全部 9 处常量 `system()` 统一收口并消除 GCC `-Werror=unused-result`；
    - 将内嵌在 YAML 中的测试抽离至独立 `tests/` 目录（`test_ota.c`、`test_netd.c`、`run_c_tests.sh`、`pre_release_check.sh`），全面覆盖 RFC 8032 Ed25519 向量、八进制溢出、先验签后解包、OTA 篡改/穿越/回滚、fail-closed PIN、首次随机生成与跨重启保显、按 IP 退避/衰减限速及 CSRF 拦截；CI 全面开启 **`-fsanitize=address,undefined`（ASan + UBSan）**、**`cppcheck`** 静态分析与 **`go vet` / `go test -count=1 -race`**。
- **旧用户升级路径与 PIN 兼容性说明（必读）**：
  - **重要：`v22.01` 及更早版本首次升级到 `v22.1` 必须使用 U 盘覆盖刷入**：旧版车机 `ota_util.h` 尚未内置 Ed25519 公钥，白名单不含 `manifest.sha256.sig`，直接上传新版 `touareg-ota-v22.1.tar` 会被旧版安全拒绝（不会损坏车机）；用 U 盘刷入 `v22.1` 之后，后续升级即可继续使用 `8080` 无线 OTA。
  - **已有 `web.pin` 的 `v20+` / `v22.01` 用户**：通过 U 盘覆盖刷入 `v22.1` 时，车机原有 `web.pin` **自动完整保留**（兼容原 4～8 位口令，正常开机零额外闪存写入），不会被锁在控制台外。
  - **极早期未配置过 `web.pin` 的旧版用户**：`VW_WiFi_Dev` 安装脚本不再误删或阻断升级；重启后 `touareg_netd` 会自动生成随机 6 位口令并在途锐仪表屏醒目显示（未登录即熄火重启仍会自动重显），手机照屏输入登录一次即永久隐藏（未装仪表模块的其他 MH2p 车型可通过 SSH 读取 `/mnt/app/eso/bin/apps/wifi_dev/web.pin`，或在电脑「改装设置」里自定义 6～16 位口令重刷）。
- **车机 QNX 6.6 ARMv7 二进制构建信息与校验和**（Ubuntu 22.04 `clang 14.0.0` + `arm-linux-gnueabi-ld` + `qnx65-armv7-toolchain`，`src/build.sh`）：
  - `touareg_netd`：`2d844e50c167c3ce785450fe12e0dbaa55a92c35d48b06d6bebd38e8ce7d97f2`
  - `cl_render`：`bf20e3fa9e5ce039e2acb03490cd60170e69d89c58a78fe5d89bcac25f002cef`
  - `clusterd`：`e88818a0cb4a7778b1cc9fc35845a67872dcf075f15ee002de2f9530982e1f90`
  - `cl_probe.so`：`e260013df86291611225856c6f1c3c05a1d12857bfeaaa0514d1494192cbfa24`
- **验证边界说明**：全套 QNX 6.6 ARMv7 二进制已完成交叉编译与 `readelf` 符号审计；macOS Clang 与 Linux GCC 下 ASan/UBSan 全量 C 单测、`cppcheck`、`go vet`/`go test -count=1 -race` 及真实 Ed25519 私钥签包 + 车机内置公钥验签回滚闭环均已通过；**`v22.01 → v22.1` U 盘升级保留旧 PIN、首次随机 PIN 上仪表屏显示与登录后清除、实车 OTA 签名包/篡改包拦截、实车回滚与 3 次崩溃安全模式共 5 项待上车实机最终复核**。
- 包含 v22.01 的全部内容

## v22.01
- **全面代码审计修复与细节打磨（含全量 `v22.0` 新特性）**：
  - **修复运动条阻尼平滑动画节奏（`cl_render`）**：调整 `cluster.c` 空闲补画与 `dash_tick()` 判定顺序避免丢弃已算好的收敛帧；修复 `dash.c` 从静止起步首帧 `dt` 偏大导致单步跳幅过大的问题，让 10Hz 遥测数据以 3～4 帧均匀平滑过渡。
  - **修复独立 GLES2 60fps 引擎 3 处车端兼容隐患（`gles60.c` + `Rebind.java`）**：修复发动机启停/熄火 `0 rpm` 时转速条卡在最后非零值不归零的问题；窗口格式与 EGL 配置对齐已验证的 `SCREEN_FORMAT_RGBX8888` 并为 `gl*` 符号解析增加 `dlsym` 兜底；双击 OK 键切回原车地图时自动暂停后台 60fps GPU 循环。
  - **改进语音键短按唤 Siri（试验，`SiriShort.java`）**：修复未装仪表地图模块（仅装 HUD + Siri 模块）时 `carplayOnline()` 恒为假的问题，新增 `/tmp/cluster_cp.state` 与 `DSICarPlayListener` 在线状态反射检测；配置器与控制台统一标明试验提示。
  - **加固启动熔断、无线控制台性能与脱敏过滤器**：调整 `VW_HUD_3` Persist 脚本先做安全模式/3 次崩溃熔断判定再创建启用标记，消除冷启动竞态窗口；`touareg_netd` 将状态轮询的 3 次 `pidin | grep` 合并为单次缓存快照并补齐跨品牌目录存在性检查；升级 `ota_util.h` 脱敏过滤器支持大小写不敏感的 `pin=`/`psk=`/`password=`/SSH 私钥行脱敏及独立 17 位 VIN、11 位手机号掩码；电脑端 `tools/preview/index.html` 增加老版浏览器圆角绘制兼容。
  - **严禁商业用途条款强化**：`LICENSE.ADDITIONAL-TERMS.md` 及桌面配置器/`8080` 控制台内置声明明确禁止任何形式的商用、售卖、付费代刷或商业捆绑；版本号采用单小数点规则（`v22.01` 小补丁 → `v22.1` 新功能 → `v23` 大换代）。
- **验证边界说明**：全套 QNX ARMv7 二进制、Java 1.4 JAR（major version `48`）及双端 Go 配置器已完成交叉编译、符号审计与 C/Go 单元测试及 `touareg-ota-v22.01.tar` 验签回滚回归；语音键短按 Siri、独立 `gles60_mode` 与实车 OTA 等车端新增交互待实车复核。
- 包含 v22.0 的全部内容

## v22.0
- **全链路安全熔断 + `8080` 离线无线升级（OTA）& 脱敏诊断 + 语音键短按 Siri + 10Hz 阻尼平滑与独立 GLES2 60fps 引擎**：
  - **全链路 3 次崩溃零损耗安全模式自动熔断（正常开机零 eMMC 写入）**：
    - 保护范围从原 `io-pkt` 扩展至全栈：J9 HMI 插件（Bundle `97` `VWClusterNav` + Bundle `98` `VWPopupFilter`）、`cl_probe.so`（`dio_manager`）以及 `clusterd` / `cl_render`（开机只读比对 `/mnt/crashlogs/` 崩溃计数基线 + 运行期 30 秒连崩 3 次熔断回退原车地图 `[33]`）；
    - 支持 `8080` 无线控制台顶部告警横幅、手动安全模式开关（`safe_mode`）及一键清除锁并刷新基线。
  - **手机 `8080` 无线控制台重磅升级（`touareg_netd` v22.0）**：
    - **离线无线升级（OTA）与一键回滚**：支持直接上传官方 Release 提供的 `touareg-ota-*.tar`（流式直写 `/mnt/app/eso/bin/apps/.ota_stage/pkg.tar`，绝不占 `/tmp` 内存、上限 6 MB），纯 C FIPS 180-4 SHA-256 逐文件校验 + 6 组件严格白名单 + 路径穿越/符号链接拦截，写入前自动备份至 `.ota_backup/`，支持网页一键回滚；
    - **一键脱敏排障诊断包（`GET /api/diag`）**：自动汇总版本、安全模式状态、配置文件、崩溃列表与关键日志尾段，自动脱敏 Wi-Fi 密码、管理口令、SSH 私钥、手机号与 17 位 VIN。
  - **方向盘语音键短按直接唤 Siri（试验，新增可选模块 `VW_HUD_5_SiriShortPress` + `8080` 实时开关 `siri_short`）**：
    - 连接 CarPlay 时短按方向盘语音键直接唤出手机 Siri（无需长按 2 秒），未连接 CarPlay 时保持原车语音，长按不受影响；支持仅装 HUD（无需仪表地图）即可检测 CarPlay 在线状态；注：部分固件下短按可能同时触发原车语音，属试验功能，默认存放于 `USB/未选模块/` 按需勾选（依赖 `VW_HUD_2_ClusterNav`）。
  - **10Hz 遥测 + 运动条阻尼平滑（`smooth=1`）+ 独立 GLES2 60fps 动画引擎（`gles60.c`）**：
    - 保留 `v21.1` 稳定的 `capture=h264` 地图投屏与窗口 `58`（`[58, 33]`）覆盖架构无需重写：`Dash.java` 采样提频至 `10Hz`，`dash.c` 新增 `smooth=1` 指数阻尼插值（运动条流畅收敛，稳态巡航自动降回 300ms 零额外 CPU 开销）；
    - 新增独立 OpenGL ES 2.0 硬件加速引擎 `src/cl_render/gles60.c`（`capture=gles60`，同一窗口 `58`，通过 `eglGetProcAddress` 动态解析 `gl*`、不硬链 `-lGLESv2`，`eglSwapInterval(dpy, 1)` 锁死 60fps VSync），作为全屏 60fps 动画仪表的试验通道与自定义主题模板（`8080` 网页可开关 `gles60_mode`）。
  - **电脑端离线 CSV 回放与布局预览器（`tools/preview/index.html`）+ AI 无线开发硬件/渲染护栏**：
    - 浏览器本地即可按 `1920×639` 还原左右 `295px` 信息框、导航卡与原厂斜边参考线，直观对比 `smooth=1` 与 5Hz 阶跃，支持拖入真实 `drive_logs/*.csv` 回放、一键导出 `dash.conf` 与截图分享到 GitHub Discussions；
    - `README.md` 与 `docs/07` 补齐车机 2 GB RAM、`/tmp` 内存盘、eMMC 零高频写、锁车断电与 CPU 软渲 vs GLES2 60fps 的明确开发界限；行车记录 U 盘折叠收纳为「进阶工具」并标注品牌正品 U 盘建议。
  - **自研有线 iAP2 导航解析器与免费开源提示完善**：
    - 有线 CarPlay 导航解析模块重构为独立自研实现（`src/cluster-probe/wired/cl_wired_rg.{h,c}`），并同步在桌面「改装设置」（Windows/Mac）、车机 `8080` 无线控制台与刷机脚本中内嵌醒目的免费开源提示。
- **验证边界说明**：全套 QNX ARMv7 二进制（`cl_probe.so` / `clusterd` / `cl_render` / `touareg_netd`）、Java 1.4 JAR（`VWClusterNav_v6.jar` / `VWPopupFilter.jar`，全类 major version `48`）及双端 Go 配置器已完成交叉编译、符号/哈希表审计、`cl_wired_rg` & `ota_util` C 单元测试与真实 `touareg-ota-v22.0.tar` 验签回滚回归；`v22.0` 新增车端交互项（语音键短按 Siri、独立 `gles60_mode` 与实车 OTA）待上车进一步实测复核。
- 包含 v21.1 的全部内容

## v21.1
- **国行途锐 P 版全系安全通刷（`MH2p_CN_VWG36_P*`）+ 根治 0.1s 仪表瞬闪原车地图 + 消除静止流 6 秒误断连**：
  - **国行途锐 P 版全系自动通刷与安全熔断**：
    - `VW_FullScreenCarPlay`、`VW_HUD_1/2/3/4`、`VW_Wireless5GHzFix` 全部由死锁 `P2878` 放开至国行途锐全部 P 版固件 **`MH2p_CN_VWG36_P*`**（覆盖 `P2878`、`P2876`、`P2838`、`P9832` 等，仍严格拦截海外及非途锐车型）；
    - **`cl_probe.so` 彻底消除 `0x3E4` 结构体硬编码偏移**：改为直接通过 PLT Hook 截获 `AirPlayReceiverSessionSetHomeKitSecurityContext(session, pairing)` 动态取得配对会话指针（并带 Thumb-2 指令自动探针兜底与安全熔断，未知版本不解引用野指针、不搞崩 CarPlay）；
    - **全屏与 Java 插件自动防呆**：`vhpatch` 原生校验 `viewhandler.zip` 大小、CRC32 与 38 字节指纹，皮肤不一致自动跳过不改任何文件；`Bridge.java` 补齐 `ListManager` / `ASLNavBAPAPI` / `aslPropertyManager` 判空保护。
  - **彻底修复仪表盘约 0.1 秒瞬闪回原车地图 / 透底闪烁（`cl_render` + `Rebind.java` 双层根治）**：
    - **`cl_render`（`cluster.c`）**：显式锁定窗口 `58` 为 `SCREEN_TRANSPARENCY_NONE`（杜绝底层原车地图 `33` 透底）；严格遵循 QNX Screen 缓冲所有权约定，每次送显前实时查询 `SCREEN_PROPERTY_RENDER_BUFFERS` 仅向真正的后缓冲绘制，**彻底移除空闲心跳向合成器重复提交当前 Front Buffer 的违规操作**；新增 `g_pinned_didx` 锁定最近一帧已解码 YV12 表面，防止 20ms 节流补帧或 300ms 信息框空闲重绘与下一帧 NVDEC 硬解争抢同一解码缓冲；
    - **`Rebind.java`（`VWClusterNav_v6.jar`）**：改为基于 `DSIProxy` 注册 `DSIDisplayManagementListener` 监听仪表屏 4 的 `showDisplayablesResult` / `activeContext` 事件驱动守护——平时 `[58, 33]` 在位时 **0 次多余 3 秒盲刷 `showDisplayables`**，原车切屏时 ≤100ms 立刻顶回，不再调用在仪表屏 4 必抛 `NullPointerException` 的 `widgets.DisplayManager.getActiveDisplayables(4)`（不可用固件自动降级 5 秒兜底）。
  - **修复静止画面 / 测试版 iOS 下误判断连跳回原车地图（`cl_probe.so` + `clusterd`）**：
    - `cl_probe.so` 在收到第二屏（stream 111）的**任意有效报文**（含每秒一次的 `op=5` 保活包及等待关键帧 `wait_idr` 期间）时，均递增 `/cluster_h264_shm` `flags` 高 24 位心跳计数（100% 兼容现有 SHM 布局），并修复 `op=0` 空载荷加密包（`bs==16`）漏增 `nonce` 导致后续解密永久错位的问题；
    - `clusterd` 将 `dio_manager` 崩溃兜底判据改为同时监控心跳计数与视频字节数且超时放宽至 10 秒，不再把等红灯/静止画面/iOS 停发视频帧超过 6 秒误杀切回原车地图（正常断开仍立即清标志 3 秒恢复原车地图）。
  - **电脑端「改装设置」更新至 `v21.1`**（Windows x64 / macOS Universal 双端同步重编）。
- 包含 v21.0.1 的全部内容

## v21.0.1
- **紧急稳定性、安全与体验热修复（强烈建议所有 `v21.0` 用户直接替换本版）**：
  - **修复桌面「改装设置」启动白板崩溃（Mac / Windows 均受影响）**：移除 `tools/configurator/web/index.html` 中遗留的 `$('maxrows')` 空节点引用，新增预览区「大字导航信息卡」实时可视化预览、修复 Windows OneDrive 桌面路径探测与 Wi-Fi SSID 32 字节标准校验，并去掉行车记录 AI 模板里的 3.0T 写死字样
  - **修复洁净车机首次冷刷持久化失败**：移除 `VW_WiFi_Dev`、`VW_NoSimPopup`、`VW_NoBtAuthPopup` 6 个 `Update/*.sh` 末尾多余的 `mount -ur /mnt/app`（防止把 `/mnt/app` 提前锁回只读导致上游 `modkit_install.sh` 写入 `servicemgrmibhigh` 失败）；同时修复 `VW_HUD_1` 卸载时误覆写 `VW_Wireless5GHzFix`、`VW_NoBtAuthPopup` 卸载误删 `v20.2` 旧版 SIM 屏蔽以及 `authorized_keys` 尾行缺失换行问题，并对 `sshd` 显式启用纯密钥认证加固
  - **修复手机无线控制台 3 处车机安全与体验缺陷（`touareg_netd` + `web_ui.html`）**：修复 QNX 32 位无符号 `time_t` 下溢导致的行车安全锁（>3 km/h）与 PIN 30 秒防爆破锁定双双失效问题、修补认证前 HTTP 请求体栈溢出漏洞、限制下载接口为普通 CSV/TXT 文件、消除前端日志文件名 XSS、修复 HTTP 下“复制 SSH 命令”失败与 429 错页问题，并将手机端 43 项指标升级为分类标签 + 推荐大字预设 + 8 秒定时状态轮询
  - **修复有线 CarPlay 导航/车道解析双重跳过 6 字节头（`cl_probe.so`）**：修正 `rg_core.inc` 重复跳过 iAP2 报文头导致的首 TLV 错位，移除日志中的明文 ChaCha20 密钥并在会话 TearDown 时清空悬空指针
  - **仓库轻瘦身、合规与整页视觉翻新**：合并 6 份重复 `aeabi_shim.h` 至 `src/common/`、清理过时旧产物与死文件、补齐发布包内附 `LICENSE`/`LICENSES/` 与免费开源说明、新增 `.github/workflows/ci.yml` 自动体检与发版现编配置器，并全面焕新 `v21` 仪表示意图、电脑端与三屏手机端界面图及 `README.md` 视觉排版
- 包含 v21.0 的全部内容

## v21.0
- **仪表大字粗体视觉重构 + 内嵌运动刻度进度条 + 左右等宽对称大字导航卡 + 43 项真实物理传感器遥测**：
  - **行车大字粗体与内嵌运动刻度条（`dash.c` + `gen_dash_font.py`）**：特别致谢社区车友 [@luanqibazao0123-bot](https://github.com/luanqibazao0123-bot)（[#7](https://github.com/a78c7/touareg-cn-carplay-mods/issues/7)）分享的实车视觉效果图与信息框仪表化创意！本次围绕行车余光一瞥即读重构排版 —— 1～5 项锁定 **`46px` Bold 粗体数字 + `24px` Bold 项目名**（6 项 `41px` Bold），左右两侧信息框字号**完全独立计算**，互不拖累缩小；左右卡片统一为**严格镜像等宽（`295px`：`x=5..300` 与 `r_x=1620..1915`）**，严丝合缝对齐左下挡位与右下车速斜边，并在动态数据行内嵌三类高精度运动刻度条（`bars=1`，可开关）：
    - **转速 / 功率 / 扭矩 / 油门 / 制动**：斜切平行四边形分段运动条（冰青色 → 琥珀色 → 暗红底/亮红红线尾段）
    - **G 值 / 纵向 G / 横向 G / 转向角 / 俯仰角 / 侧倾角**：带白色零位刻度的居中双向滑块条
    - **机油温度 / 变速箱油温 / 冷却液温度 / 胎压 / 燃油存量**：低温冰蓝 / 适温青绿 / 高温红连续区间状态条
  - **左右等宽大字导航信息卡与常驻待机模式（`navbox.c`）**：导航卡与左信息框严格等宽等高对齐（`295×344`），全面放大路口剩余距离、当前进入道路名称、下下路口预告以及底部 **剩余路程 / 剩余时间 / 预计到达** 三行大字；新增 **`nav_box=2` 常驻待机导航框模式**（可选「仅导航时弹出」或「常驻显示——未导航时展示巡航待机与当前道路，进退导航零跳动」）
  - **新增 8 项途锐真实物理底盘/越野/胎压数据（总计 43 项，零估算伪数据）+ 8AT 挡位（`D1～D8`）齿比标定**：
    - 通过实车验证只读接入 `de.vw.mib.dsi.carcomfort.DSICarComfort`（RDK 直接胎压监测）、`DSICarDrivingCharacteristics`（车身倾角）与 `DSINavigation`（GNSS 海拔），信息框新增 **`tires`（单行四轮胎压）、`tpfl/tpfr/tprl/tprr`（四轮独立胎压 bar，低压预警附 `!`）、`pitch`（俯仰角 `°`）、`roll`（侧倾角 `°`）、`alt`（海拔 `m`）**，所有车友均可在电脑端「改装设置」或手机 `8080` 无线控制台自由组合
    - 针对途锐 `DSICarVehicleStates` 原厂 `currentGear` 常报 `0` 的缺陷，基于采埃孚 **ZF 8HP65A + 3.076 主减速比** 锁止齿比实时标定输出精准 **`D1～D8` / `S1～S8`** 挡位；`10Hz` 行车记录 CSV 同步扩充至 **34 列**真实物理传感器数据
  - **交互减法与独立选装模块规范化（10 个模块默认全关、均可独立安装与卸载）**：
    - 方向盘右侧 `OK` 键 **2 秒内连按 2 下（双击）** 切换 CarPlay 地图与原车地图拆分为独立选装模块 `VW_HUD_4_OkMapSwitch`（默认放在 `USB/未选模块/` 不开启，按需选装）
    - 电脑端「改装设置」新增**前置依赖联动弹窗**：单独点选需要前置模块配合的功能时（如勾选「仪表地图」自动关联「导航上 HUD」，勾选「OK 双击切地图」自动关联前两项），自动弹窗提醒并同步勾选（卸载时反向联动）
  - **工程健壮性与安全加固**：
    - 修复旧版 `VW_ClusterNav` 升级至 `VW_HUD_2_ClusterNav` 时的双 Persist 循环遗留问题、`Rebind.java` 周期日志异常、`VW_WiFi_Dev` 卸载 `/mnt/ota` 挂载保护，并为 `touareg_netd` 补齐 CSRF 请求头校验与 PIN 口令防爆破限速
- 包含 v20.3 的全部内容

全套 `v21.0` 组件已在 2023 款途锐（`MH2p_CN_VWG36_P2878`）完成实车全流程验证（大字运动信息框、左右 `295px` 对称对齐、实时与常驻待机导航卡切换、四轮胎压/倾角实车直读），全部二进制与配置器均通过 Java 1.4（class version `48.0`）、ELF `DT_HASH` 与 `go test` 自动化回归检验。

## 历史版本归档

更早的历史版本记录（`v18` ～ `v20.3`）已归档至 [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md)。
