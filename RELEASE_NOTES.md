# 版本说明（发布时自动取对应版本的一节作为 Release 说明）

## v23.6
> **根治 `v23` / 经典模式高码率残余花屏（消除英伟达 Tegra K1 显存属性冲突 + 硬解 8 槽参数同步隔离）+ 修复冷启动 `v23` 超时误退回 `v22`（`15 s` 容错与自动重试，#14）**：综合实车开机首秒显卡驱动日志与高德白天 `2688×960` 高码率（`3–8 Mbps`）实测，定位并拔除两处深层残余花屏根因——默认关闭触发 `/sbin/screen` NVIDIA `Sev 2` 报错的混合属性零拷贝、改为 2D/3D 显存物理隔离的标准纹理上传（仍满帧 60fps），并将硬解参数单按 8 槽独立隔离加落盘同步；同时合并 `clusterd` 冷启动 `15 s` 容错 + `6 s` 补发 `idr` + 1 次自动重试，消除冷车满载时误触发第 2 层保险丝回退旧版的问题。已装 `v22.1+` 与 `8080` 无线控制台的车主上传 `touareg-ota-v23.6.tar` 并点「软重启车机」即可完成升级。

- **彻底消除 Tegra K1 显存属性冲突引起的偶发 ~0.1s 红块/白横条（`src/cl_render/v23_gles.c`）**：
  - 实车开机首秒 `sloginfo` 证实：离屏地图画板同时挂载 `SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ`（Pitch-Linear）与 `SCREEN_USAGE_OPENGL_ES2`（Block-Linear）时，`/sbin/screen` NVIDIA 驱动会连报 12 条 `Sev 2` 错误（`NV: USAGE_OPENGL_* is not supported with the USAGE_READ, USAGE_WRITE and USAGE_NATIVE.`），在高德白天高码率流下会导致 VIC 2D 写入、CPU 昼夜亮度采样与 GK20A 3D 纹理缓存偶发不一致；
  - `v23.6` 将 4 槽离屏地图桥（`free / ready / front / retired`）改为纯 Pitch-Linear 属性，默认通过 `glTexSubImage2D` 物理隔离上传至 GPU 独立 Block-Linear 纹理（仅在新地图帧到达时上传，带宽占比 `< 0.6%`，60fps 仪表合成丝毫不掉帧，驱动 `Sev 2` 报错归零；仍保留 `v23_force_zerocopy` 开关供实验对比）。
- **硬解 8 槽独立参数描述符 + 落盘同步（`src/cl_render/cluster.c`，经典模式与 `v23` 模式共用）**：
  - 将 `cb_decode_picture` 的 `bs_desc` 与 `nv_pi[1024]` 从单份静态变量改为按 `[MAX_SURFACES]`（8 槽）独立隔离，并在 `NvMediaVideoDecoderRender` 提交后立即对目标表面执行 `SurfaceWait` 同步落盘，彻底杜绝单次 `ParserParse` 多帧突发时后帧覆写前帧参数单或 `free(chunk)` 抢跑导致的偶发脏帧；
  - `cb_alloc_picture` 优先避让当前与上一显示表面（`g_pinned_didx` / `g_prev_pinned_didx`），避免刚完成 2D Blit 的 YV12 表面被立刻覆写。
- **修复冷启动 `v23` 60fps 引擎初始化超时误退回 `v22`（`src/clusterd/clusterd.c`，[#14](https://github.com/a78c7/touareg-cn-carplay-mods/issues/14)）**：
  - 将 `v23` 窗口就绪超时从 `8 s` 放宽至 **`15 s`**（兼容冷启动 CPU 满载时首帧 IDR + EGL + 字库 + 自定义皮肤编译耗时），并在第 `6 s` 未就绪时自动补发一次 `idr` 催帧；
  - 允许自动重试 `1` 次（连续 `2` 次 `15 s` 超时才触发第 2 层保险丝回退经典 `v22` 通道，就绪后自动清零失败计数）。
- **U 盘安装脚本版本号同步**：
  - `VW_WiFi_Dev/Update/install.sh` 写入的 `ota_version` 同步更新至 `v23.6`，`tools/make_release.sh` 内置说明自动跟随发版版本号。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（全部通过 `DT_HASH+GNU_HASH`、ABI 与 Java `48.0` 校验）**：
  - `cl_render`（`1,464,904 B`）：`a02554c322b3bb95d25659b3326e7eec5237f42969af22f39c443860602e8950`
  - `clusterd`（`28,396 B`）：`1f4fc4f299bba0dc65c55a1a86ee5609ee43f79c6633c9c3207b847e2c89082d`
  - `VWClusterNav_v6.jar`（`76,097 B`，与 `v23.5` 一致）：`7d650a2db10d36fde4a8e631eadc4ab355d598e10fe13b68f9e0fb411761a359`
  - `touareg_netd`（`141,168 B`，与 `v23.5` 一致）：`5e881269083c59f0fb8a1abaf40e8af1a99fd933bd28fee389bb9d4353094f05`
  - `cl_probe.so`（`56,608 B`，与 `v23.5` 一致）：`aa58b9e37d32dd09cfeff1765e9e0f2c5ef2a07777a4992174ff9f5519fe81ff`
  - `VWPopupFilter.jar`（`4,536 B`，与 `v23.5` 一致）：`fe192806e4465c1d83318ca314a19d8ab3526673b3987417e295f5254c5c5b81`

## v23.5
> **51 项实车真数据扩展（真实车速 / 车内温度 / 横摆角速 / 四轮独立轮速 / 底盘工况与热告警）+ 无功率/扭矩车型自动藏死 0 替补 + 氛围灯同款主题主色 + 油门非线性观感条 + 手机 `8080` 新增 Apple Bento「实时大屏」副屏模式**：结合社区实车只读审计反馈，在不新增车机负担、零侵入原车中控的前提下，将仪表信息框与 `v23` 皮肤数据总线从 `43` 项扩充至 **`51` 项**；自动探测部分出厂网关不下发功率/扭矩的车型，仪表原地替补实用真数据告别「死 0 格子」；桌面「改装设置」与手机 `8080` 无线控制台同步支持一键选择途锐常用氛围灯主色（`accent`），仪表信息框与默认 `60fps` 皮肤联动变色；油门进度条采用城市轻油门非线性观感曲线；手机 `8080` 新增「实时大屏」Tab，无需刷中控即可把手机/平板变成真 `10Hz` 实时副屏仪表。已装 `v22.1+` 与 `8080` 无线控制台的车主上传 `touareg-ota-v23.5.tar` 并点「软重启车机」即可完成升级。

- **新增 8 项实车真数据，总计 51 项自由定制（`Dash.java` + `dash.c` + `dash_font.h` + `v23` + 桌面「改装设置」+ `8080` + 模拟器）**：
  - **行驶与操控**：`realspeed`（真实无虚高车速，可与表显欢乐表对照）、`yaw`（横摆角速度 `°/s`，居中双向滑块条）、`wfl / wfr / wrl / wrr`（四轮独立轮速 `km/h`，雨雪/越野打滑监测）；
  - **座舱与工况**：`cabin`（车内空调实测温度 `°C`，冷/适温/热区间状态条）、`cond`（底盘工况与 3 路热告警综合指示：热告警 `HOT!` / `ESP` / `EDS` 差速锁 / `HDC` 陡坡缓降 / 地板油 `KD` / 空挡滑行 `CST` / 制动 `BRK` / `OK`）；
  - 上述 8 项全部同步加入桌面「改装设置」、手机 `8080` 控制台（含新增「真车速·底盘 6 项」快捷预设）以及 `v23` 皮肤总线（`bus.realSpeed`、`bus.yaw`、`bus.cabin`、`bus.wfl..wrr`、`bus.cond.*`）。
- **无功率/扭矩车型自动识别与原地替补，彻底消除「死 0 格子」（`ExtraDSI.java` + `Dash.java` + `dash.c` + `v23`）**：
  - `ExtraDSI.java` 新增高频 `ViewOptions`（属性 `12`）只读探测，`Dash.java` 配合行驶期连续帧动态双保险输出 `pok=0/1` 与 `tqok=0/1`；
  - 当检测到本车网关不下发功率/扭矩时（如部分 2.0T/特定网关配置），经典信息框自动原地替补同框尚未显示的真实车速/油门/车内温等真数据，`v23` 皮肤通过 `bus.ready.power` / `bus.ready.torque` 自动适配，3.0T 等有数车型 100% 保持原样。
- **氛围灯同款主题主色（`accent`）与油门非线性观感进度条（`dash.c` + `v23` + 配置器 + `8080`）**：
  - `dash.conf` 新增 `accent=#RRGGBB`（默认冰川青 `#22d3ee`），桌面「改装设置」与手机 `8080` 提供 6 个大众途锐常用氛围灯色预设（冰川青 / 香槟金 / 电光紫 / 大众 R 蓝 / 赛道红 / 极光绿）及自定义色板，同步作用于信息框顶边亮线、进度条基色以及 `v23` 皮肤 `bus.conf.accent`；
  - 油门进度条改为城市轻油门非线性观感曲线（数字仍显示真实油门百分比），日常 `10%–30%` 轻踩油门也能清晰看到动感呼吸。
- **手机 `8080` 无线控制台新增「实时大屏」副屏页面（`touareg_netd.c` + `web_ui.html`）**：
  - 新增受 `web.pin` 保护的轻量只读接口 `GET /api/live`（行车中只读可用），切入「实时大屏」Tab 即可查看 Apple 深空钛金 Bento 风格实时遥测，支持一键「全屏副屏」与「模拟演示」，不切入该页时零轮询零耗电，且自动隐藏本车无上报的功率/扭矩卡片。
- **平台参考文档沉淀（`docs/reference/R3` + `R4`）**：
  - 补录社区实车只读审计确认的中控 `Display 0 (LVDS1)` 五场景挂载矩阵、三硬件管线与 `/dev/rstp/keypanel → J9` 触控链路，以及部分车型无功率/扭矩与 MF 车内温度/工况字段实测说明。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（全部通过 `DT_HASH+GNU_HASH`、ABI 与 Java `48.0` 校验）**：
  - `VWClusterNav_v6.jar`（`76,097 B`）：`7d650a2db10d36fde4a8e631eadc4ab355d598e10fe13b68f9e0fb411761a359`
  - `cl_render`（`1,464,900 B`）：`4812cc74989614f54a975afd6e8c0eb07a2b8eeeb046f931b9ff4204360a60eb`
  - `touareg_netd`（`141,168 B`）：`5e881269083c59f0fb8a1abaf40e8af1a99fd933bd28fee389bb9d4353094f05`
  - `clusterd`（`24,344 B`，与 `v23.4` 一致）：`f46edfd3aa8259ac0e4f4a2b0793d7f15487565ce380550b7d211503a652982a8`
  - `cl_probe.so`（`56,608 B`，与 `v23.4` 一致）：`aa58b9e37d32dd09cfeff1765e9e0f2c5ef2a07777a4992174ff9f5519fe81ff`
  - `VWPopupFilter.jar`（`4,536 B`，与 `v23.4` 一致）：`fe192806e4465c1d83318ca314a19d8ab3526673b3987417e295f5254c5c5b81`

## v23.4
> **`v23` 60fps 零拷贝偶发花屏/闪条根治 + 百度/苹果地图车道指引顶栏永久避让 + 国行 15 寸 `P` 版全屏 CarPlay 跨版本适配 + `P2869` 仪表地图半秒规律闪屏修复 + 关键帧缓存水位调度**：彻底消除 `v23_mode` 下偶发 ~0.1s 大面积红块与白横条纹（双线程 `screen_context_t` 物理隔离 + 4 缓冲窗口 + 4 槽零拷贝带退役冷却）；左右信息框与右侧导航卡统一调整为 `y = 200..508`（高 `308 px`），永久预留百度/苹果下发车道指引时原车顶栏撑高至 `y ≈ 190` 的空位并支持老配置自动迁移；`vhpatch` 新增跨版本 J9 字节码指纹引擎，除 `P2878` 精确表外自动适配 `P2838`/`P2853`/`P2869`/`P2876`/`P2497`/`P9832` 等其余国行 15 寸 `P` 版并按固件版本分存备份防串车；修复 `P2869` 等固件 `Rebind.java` 的 `2 Hz` 周期重刷闪屏（#14）；将 `cl_probe.so` 稳态关键帧改为按 `6 MB` GOP 水位触发；补充 AI 皮肤底部地图边界与高德第二屏机型/第三方转换盒限制说明（#7 / #11 / #14）。已装 `v22.1+` 与 `8080` 无线控制台的车主上传 `touareg-ota-v23.4.tar` 并点「软重启车机」即可完成仪表与 HUD 核心组件升级；需要新全屏跨版本适配的老固件车主请用本版 U 盘包刷入 `VW_FullScreenCarPlay`。

- **根治 `v23` 60fps 零拷贝模式偶发 ~0.1s 白横条纹与大红块（`src/cl_render/cluster.c` + `src/cl_render/v23_gles.c`）**：
  - **跨线程 `screen_context_t` 100% 物理隔离**：`v23_mode` 下不再在解码主线程的 `g_dec.scr_ctx` 上创建 `post_win`，改由 GL 渲染线程自建独立 `own_ctx` + `own_win` 并屏蔽子线程 `SIGUSR1/SIGUSR2`，彻底消除 `eglSwapBuffers` 与 `screen_blit` 并发重入非线程安全命令环（实车黑匣子 7 分钟捕获 `5,911` 次碰撞降至 `0`）；
  - **4 缓冲显示窗口 + 4 槽零拷贝退役同步**：GL 窗口缓冲从 `2` 扩至 `4` 并配合 `16,667 us` 单调时钟网格稳锁 60fps（防双缓冲扫描线追尾及无 VSync 空转）；离屏零拷贝桥从 3 槽扩至 `4` 槽（`free / ready / front / retired`），退下槽位须完成至少 `1` 次 `eglSwapBuffers` 且满 `15.5 ms` 后才归还重写，切槽时重绑 `glEGLImageTargetTexture2DOES` 刷新 Tegra L2 纹理缓存；
  - **YV12 `2×2` 偶数裁剪对齐与异常帧拦截**：强制源裁剪矩形偶数边界对齐，按表面槽位校验 `DecoderRender` 与 `SurfaceWait` 状态拦截脏帧，并在首帧就绪后关闭常规流水日志、保留 `16 KB` 独立异常诊断配额。
- **永久避让百度/苹果地图原车车道指引扩展顶栏（`dash.c` + `navbox.c` + `cluster.c` + `v23_gles.c` + `v23_sandbox.h` + 配置器 & 预览器）**：
  - **全天候对称安全框 `y = 200..508`（高 `308 px`）**：实拍标定百度地图下发 BAP 车道指引时原车顶栏底边白线会向下撑高至 `y ≈ 189 ~ 190`；左右信息框、右侧导航卡（含实时导航/待机巡航/初始口令卡）、`v23` 内置默认皮肤与顶部提示横幅统一移至 `y = 200`（上下与顶栏底线及底部 `y = 518` 挡位/车速梯形均保留 `10 px` 呼吸缝），无论是否触发车道指引都绝不压线；
  - **保留大字号与运动条 + 老车免手改配置**：5 行（行高 `59 px`）保留 `46 px` 特大粗体数字 + `8 px` 运动条，6 行（行高 `49 px`）保留 `41 px` 大粗体 + `7 px` 运动条，1～10 行均不裁切；`dash.c`、`navbox.c` 与配置器自动将旧版默认 `y / r_y / nav_y = 164 / 172` 迁移至 `200`，升级即生效；浏览器模拟器同步标出 `y = 154..190` 车道指引扩展带。
- **全屏 CarPlay 跨版本自适应补丁与防串车备份（`src/vhpatch/vhpatch.c` + `VW_FullScreenCarPlay`）**：
  - **双模式就地打补丁**：`P2878` 继续优先走原精确 CRC 校验表（零改动）；`P2838`、`P2853`、`P2869`、`P2876`、`P2497`、`P9832` 等其余国行 15 寸 `P` 版自动启用严格 J9 字节码指纹引擎，全包扫描 `.jxe` 自动定位 CarPlay/Android Auto `1152×592` / `1224×656` 矩形与边框透明度（不怕偏移漂移、常量池重排、文件改名与图库 ID 变动），写前双重自检、遇到歧义或非 15 寸屏安全退出不改文件，支持无备份原地还原；
  - **刷机诊断与分固件备份**：`install.sh` / `uninstall.sh` 刷机日志新增 `vhpatch probe` 详细诊断输出，U 盘原厂备份改为按 `RELEASE_VERSION` 分存（`backup/viewhandler_${RELEASE_VERSION}.zip` 与 `info_${RELEASE_VERSION}.txt`，兼容旧版 `P2878` 备份），杜绝同一根 U 盘刷多台不同固件车辆时串车还原。
- **修复 `P2869` 等固件仪表地图每 ~0.5 秒周期闪屏（`src/hud-java/vwcn/Rebind.java` + `VWClusterNav_v6.jar`，[#14](https://github.com/a78c7/touareg-cn-carplay-mods/issues/14)）**：
  - 当底层原车地图窗口 `33` 处于 `vis=false` 时，部分固件 `showDisplayablesResult(4, ...)` 仅回报可见前缀 `[58]`；新增 `isActivePrefix()` 前缀在位守卫，只要顶层 `58` 稳居首位即不再误触发 `500 ms`（`2 Hz`）无限重刷，顶层被切走时仍 `<=100 ms` 自动拉回。
- **优化 `cl_probe.so` 稳态关键帧调度消除瞬时微掉帧（`src/cluster-probe/cl_probe.c`）**：
  - 无丢包稳态下不再固定每 `30 s` 强发 `forceKeyFrame`，改为当 `g_gop` 热重启秒开缓存达 `6 MB` 水位（上限 `8 MB`）时刷新、静止低码率放宽至 `180 s` 兜底，消除高码率叠加周期 IDR 引起的单帧掉帧。
- **补充 AI 皮肤底部地图边界规范及高德机型/无线转换盒说明（`CLUSTER_LAYOUT_SPEC.md` + `THEME_API_SPEC.md` + `my_theme.js` + 三语 `README` + `docs/03`）**：
  - 明确区分 `y = 495..639` 前景文字/进度条避让区与 `ctx.drawMap` 地图底图可绘范围（地图可直接延伸至 `y = 575` 乃至满铺 `639`，避免 AI 在 `495` 截断地图留出底部黑空挡）；补充高德第二屏需 **iPhone 12+ & iOS 17.4+** 及外插 U2W 无线转换盒不支持转发第二屏的排障说明。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（全部通过 `DT_HASH+GNU_HASH`、ABI 与 Java `48.0` 校验）**：
  - `VWClusterNav_v6.jar`（`72,481 B`）：`403006a3fb5f21352f8361821e65df42e43ae98d17bfb2e15e2f8596d4d8f301`
  - `cl_render`（`1,346,092 B`）：`6ea0ae33b32517ddf27d28442386ff880bc1631d3f40bff8b589576c369c58c2`
  - `touareg_netd`（`120,688 B`，与 `v23.1` 一致）：`47d6c6db23645751d7ad36232f8a7557923cdfe10d1b652d6861f7ff71b57d19`
  - `clusterd`（`24,344 B`，与 `v23.1` 一致）：`f46edfd3aa8259ac0e4f4a2b0793d7f15487565ce380550b7d211503a652982a8`
  - `cl_probe.so`（`56,608 B`）：`aa58b9e37d32dd09cfeff1765e9e0f2c5ef2a07777a4992174ff9f5519fe81ff`
  - `VWPopupFilter.jar`（`4,536 B`，与 `v23.1` 一致）：`fe192806e4465c1d83318ca314a19d8ab3526673b3987417e295f5254c5c5b81`
  - `vhpatch`（`73,116 B`，全屏 U 盘模块）：`111c01c29cd9b5e3201f516df7dcae38bfbfda5a15a17c6e0bd1ec96804057c8`

## v23.3
> **无线 SSH 安装脚本 QNX `tr` 缺失修复 + `sshd` 全网卡监听与 `pf` 防火墙第 3 级兜底 + 浏览器 1:1 模拟器转向修复 + iPhone 热点共存提示**：修复 `VW_WiFi_Dev` 安装脚本在无 `tr` 命令的 QNX 6.6 固件上写入 SSH 公钥报错的问题，增强异构固件 `sshd` 监听与 `pf` 在线规则兜底，修正桌面「改装设置」与独立预览模拟器中 CarPlay 地图弯道与方向盘反向的问题，并补充同一部 iPhone 开个人热点与无线 CarPlay 的单 Wi-Fi 限制说明。本次车机核心二进制与 `v23.2` 完全一致（未勾选无线开发终端或已正常连接 `8080` 的车主无需重刷；勾选了 SSH 开发终端或异构固件连不上 `8080/2012` 的车主用新版 U 盘覆盖刷入 `VW_WiFi_Dev` 即可）。

- **修复 `VW_WiFi_Dev` SSH 公钥写入依赖缺失 `tr` 命令及门禁补强（`VW_WiFi_Dev/Update/install.sh` + `tests/pre_release_check.sh`）**：
  - 途锐 MH2p 的 QNX 6.6 固件未内置 `tr` 工具；现将 `authorized_keys` 导入从 `tr -d '\r'` 改为原生 `awk` 去 `\r`、跳过空行/注释并幂等打标输出行数，同时将 `tr` 纳入 `pre_release_check.sh` 静态门禁（与 `wc`、`sed -i` 同级拦截）。
- **增强无线 SSH 监听与 `pf` 防火墙第 3 级在线规则兜底（`VW_WiFi_Dev/Persist/install.sh` + `wifi_test.go`）**：
  - `sshd` 启动显式附加 `-o ListenAddress=0.0.0.0`，确保跨子网/热点接口稳定监听 `2012` 端口；
  - `ensure_pf_bcm2()` 在静态底表（`/etc/pf.ecm0.conf`、`/etc/pf.mlan0.conf`、`/etc/pf.conf`）均未命中或预检不过时，新增基于 `pfctl -sr / -sn / -sq` 当前在线生效规则动态叠加 `bcm2` 放行的第 3 级兜底，并补充对应 Go 单元测试。
- **修复浏览器 1:1 模拟器 CarPlay 地图转向反相（`tools/configurator/web/v23_preview.html` + `tools/preview/index.html`）**：
  - 修正模拟器按左/右转时前方主干道、绿地与背景纵网偏转符号写反（导致车标往右转、道路往左撇）的问题；同时修正绿色导航主线在弯道处偏出白色主干道边缘及「前方左转」路口分支固定右折的视觉瑕疵。
- **补充单台 iPhone 个人热点与无线 CarPlay 共存说明（三语 `README` + `docs/07` + `tools/configurator`）**：
  - 在配置器界面、连接说明与文档中明确提示：同一部 iPhone 受单 Wi-Fi 通道限制，开启个人热点连 `8080/SSH` 时无法同时连无线 CarPlay（日常关掉热点即自动恢复，需边看导航边调试可插线用有线 CarPlay + 开热点，或用备用机开热点）。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（与 `v23.2` 完全一致）**：
  - `VWClusterNav_v6.jar`（`72,460 B`）：`48da5b662a29c55617c06b8579420bcc0ae9edcf49c991f8438ab78b5921cf10`
  - `cl_render`（`1,341,972 B`）：`48e5e0f595c23d4b5f98e1c109cf69576b3625d02d17f3b7d1ea5536b16f3468`
  - `touareg_netd`（`120,688 B`）：`47d6c6db23645751d7ad36232f8a7557923cdfe10d1b652d6861f7ff71b57d19`
  - `clusterd`（`24,344 B`）：`f46edfd3aa8259ac0e4f4a2b0793d7f15487565ce380550b7d211503a652982a8`
  - `cl_probe.so`（`56,608 B`）：`848da1b7e94d7fe39f60fa93fecea944b453b9ab9e1304c8d6d32917c4c7dc2f`
  - `VWPopupFilter.jar`（`4,536 B`）：`fe192806e4465c1d83318ca314a19d8ab3526673b3987417e295f5254c5c5b81`

## v23.2
> **胎压单位换算修复 + 已改装车暗桩只读体检 + 无线防火墙跨车型兼容 + 首页白话精简与社区分享支持**：修复车机胎压单位设为 `psi` / `kPa` 时仪表信息框胎压偏低的 Bug（#14），新增改装店遗留定时还原脚本与陌生 SSH 公钥只读体检告警（#10），改进无线控制台 `pf` 防火墙规则对无 `ecm0` 车型的兼容，同时将三语首页彻底精简为通俗大白话并开放 GitHub Discussions 社区自制包分享模板。已装 `v22.1+` 与 `8080` 无线控制台的车主直接上传 `touareg-ota-v23.2.tar` 并点「软重启车机」即可完成胎压修复升级；如需对已改装车执行刷机只读体检或更新无线持久化脚本，请使用 U 盘覆盖刷入一次。

- **修复胎压单位设为 `psi` / `kPa` 时数值偏低（`src/hud-java/vwcn/ExtraDSI.java` + `VWClusterNav_v6.jar`，[#14](https://github.com/a78c7/touareg-cn-carplay-mods/issues/14)）**：
  - 反汇编原厂 `HsmTargetCarService.dsiCarComfortUpdateRDKTireDisplay` 确认大众 DSI `CarConstants` 的 `pressureUnit` 枚举顺序（`0 = BAR` `0.1 bar/LSB`、`1 = PSI` `0.5 psi/LSB`、`2 = KPA` `10 kPa/LSB`）与 ASL 层不同；修正此前 `1`/`2` 分支对调及 `PSI` 漏乘 `0.5` 的问题，三种单位下仪表胎压均与原车完全一致。
- **新增已改装车 / 二手车刷机前只读体检（`VW_HUD_1_RouteGuidance` & `VW_WiFi_Dev` `Update/install.sh`，[#10](https://github.com/a78c7/touareg-cn-carplay-mods/issues/10)）**：
  - 在写入前只读检查 `/mnt/app/eso/bin/servicemgrmibhigh`（识别非 ELF 且不含 `modkit_persist` 的商业锁死脚本并自动脱敏打印前 15 行）、`servicemgrmibhigh0` ELF 完整性、`del`/`shadow` 定时还原伴生文件、第三方 Persist 模块 `sshd`/`challenge` 引用及 `/mnt/app/root/.ssh/authorized_keys` 非托管残留公钥，输出至 U 盘 `Logs/` 供零门槛排障（不写闪存、不阻断正常安装）。
- **增强 `VW_WiFi_Dev` 防火墙规则跨车型兼容性（`VW_WiFi_Dev/Persist/install.sh`）**：
  - 将 `ensure_pf_bcm2()` 从宽泛匹配 `on bcm2` 升级为精确校验 `pass in quick on bcm2 ... port = 8080`（及开启 SSH 时的 `2012`），并按实车 `ifconfig ecm0` 状态自适应轮询 `/etc/pf.ecm0.conf`、`/etc/pf.mlan0.conf` 与 `/etc/pf.conf`，兼容无 4G 模块的同平台 MH2p 车型。
- **首页大白话瘦身、免升系统防野包提示与社区自制包分享**：
  - 将底层逆向数据、固件演进差异（`P9832`～`P2878`）与排障技术细节统一归档至 `docs/05`、`docs/07` 与 `docs/reference/R3`；三语 `README` 改用通俗直白表述，明确原车 `P` 版免升系统直刷范围并警示切勿自行刷入网传非官方 `2878` 系统包；
  - 新增 `.github/DISCUSSION_TEMPLATE/自制包与皮肤分享.yml` 社区皮肤与模块包 `.zip` 分享表单模板及顶部导航直达入口。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（全部通过 `DT_HASH+GNU_HASH`、ABI 与 Java `48.0` 校验）**：
  - `VWClusterNav_v6.jar`（`72,460 B`）：`48da5b662a29c55617c06b8579420bcc0ae9edcf49c991f8438ab78b5921cf10`
  - `cl_render`（`1,341,972 B`，与 `v23.1` 一致）：`48e5e0f595c23d4b5f98e1c109cf69576b3625d02d17f3b7d1ea5536b16f3468`
  - `touareg_netd`（`120,688 B`，与 `v23.1` 一致）：`47d6c6db23645751d7ad36232f8a7557923cdfe10d1b652d6861f7ff71b57d19`
  - `clusterd`（`24,344 B`，与 `v23.1` 一致）：`f46edfd3aa8259ac0e4f4a2b0793d7f15487565ce380550b7d211503a652982a8`
  - `cl_probe.so`（`56,608 B`，与 `v23.1` 一致）：`848da1b7e94d7fe39f60fa93fecea944b453b9ab9e1304c8d6d32917c4c7dc2f`
  - `VWPopupFilter.jar`（`4,536 B`，与 `v23.1` 一致）：`fe192806e4465c1d83318ca314a19d8ab3526673b3987417e295f5254c5c5b81`

## v23.1
> **大众官方资料全仓库核实修复 + `v23` 皮肤引擎核心缺陷修复 + 首页清爽白底统一改版**：对照 2018.07 中规途锐原厂使用说明书（件号 `760012774AC`）与 2019–2026 大众官方公开资料完成全仓库交叉核实，集中修复了 `v23` 引擎挡位/转向/遥测 3 个真实功能缺陷、U 盘刷机 `ota_version` 滞后、配置器 Wi-Fi 热点截断与模拟器手势不一致，并统一许可证边界与全新清爽白底首页视觉。已装 `v22.1+` 与 `8080` 无线控制台的车主直接上传 `touareg-ota-v23.1.tar` 并点「软重启车机」即可完成升级。

- **修复 `v23` 60fps 皮肤引擎 3 处实车数据与图标缺陷（`src/cl_render/v23_gles.c` + `v23_sandbox.h`）**：
  - **补回经济模式 `E` 挡与完整换挡模式码**：对齐经典通道完整支持 `E`（经济模式）、`B`、全部 `S`/`M`/`D` 挡位及未就绪 `"-"` 兜底，不再将经济模式误显为 `D` / `D1`；
  - **修正 iAP2 转向枚举错位并补齐掉头/环岛绘制**：修正原枚举把掉头误映为左转、把环岛/下匝道误映为掉头的问题，并补齐 `uturn` 与 `roundabout` 转向箭头矢量绘制；
  - **修正 `fuel` 升数折算与 13 项行车电脑字段**：`bus->fuel` 按 `tank_l` 正确折算为升数（L），补齐全部 13 个行车电脑字段赋值，统一 ETA `+30s` 舍入 / 1970 时钟兜底与逐轮有效胎压判定。
- **修复多处实用细节与配置边界**：
  - **胎压高红刻度放宽至 `3.1 bar`**（`dash.c`）：避免满载冷胎压 `2.8–3.0 bar` 及高速热胎误报过压红区；
  - **修复 `VW_WiFi_Dev` 刷机版本号写入**：`install.sh` 的 `ota_version` 由滞后的 `v22.1` 修正并同步至 `v23.1`；
  - **默认偏移量与 Wi-Fi 热点一致**：全仓统一默认 `car_up = 35`、`acc_up = 55`（合计 `90 ≤ 90`）；桌面配置器前端热点扩展至 5 个，不再静默截断第 4–5 个热点；
  - **模拟器与实车手势一致**：两套离线预览模拟器与 `my_theme.js` 同步为双击/三连击 `OK` 切皮肤与最新遮挡区坐标，同步清理多处滞后 Beta 标签；补齐 `VW_HUD_3` / `VW_HUD_5` 安装前置校验。
- **全仓大众官方资料校准、许可证合规统一与首页清爽改版**：
  - 修正「国产途锐」与「12.3 寸虚拟座舱 / 物理屏 `1920×722`」等表述（官方物理屏为 12 英寸 `1920×720`，车机 Display 4 逻辑上报 `1920×722`），明确区分 2019–2023 改款前 15 寸 MH2p 与 9.2 寸领航版、欧版 MY2024+ MIB3 及中规 2025–2026 Asterix 新途锐，并补全油箱（`90 / 76 / 75 L`）与空悬/3.0T 标定边界；
  - 统一许可证归属：`src/cl_render/` 整体及编译产物 `cl_render` 遵循 **CC BY-NC-SA 4.0**（不叠加附加条款），全部独立原创组件保留 **PolyForm Noncommercial 1.0.0 + `LICENSE.ADDITIONAL-TERMS.md`**，在 `THIRD_PARTY_NOTICES.md` 补列 QuickJS；
  - 重制清爽白底统一视觉资产（双主题细线头标/系统原理图、日间仪表图、`v23` 新版配置器与浅色手机三联屏），精简三语首页大白话并补齐中英双语 GitHub About 与 20 个搜索标签。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（全部通过 `DT_HASH+GNU_HASH`、ABI 与 Java `48.0` 校验）**：
  - `cl_render`（`1,341,972 B`）：`48e5e0f595c23d4b5f98e1c109cf69576b3625d02d17f3b7d1ea5536b16f3468`
  - `touareg_netd`（`120,688 B`）：`47d6c6db23645751d7ad36232f8a7557923cdfe10d1b652d6861f7ff71b57d19`
  - `VWClusterNav_v6.jar`（`72,458 B`）：`c0b75574110f52cc922936e79cc698452df9609d33dfda8089a4d3b44dd21bb1`
  - `clusterd`（`24,344 B`）：`f46edfd3aa8259ac0e4f4a2b0793d7f15487565ce380550b7d211503a652982a8`
  - `cl_probe.so`（`56,608 B`）：`848da1b7e94d7fe39f60fa93fecea944b453b9ab9e1304c8d6d32917c4c7dc2f`

## v23.0
> **正式稳定版（实车全流程验通 · 55～59 fps 硬件零拷贝 AI 自定义皮肤 + 方向盘 OK 盲切皮肤 + 四色动态运动条 + `1920×639` 实车像素标定规范）**：在 2023 款国行途锐实车上完成全链路真机验收（`EGL_KHR_image_pixmap` 硬件零拷贝 55～59 fps、4 套复杂矢量与真 GLSL 光效皮肤热切、方向盘 NavBAP 按键实测、35 ms×3 帧保险丝与 `y=162` 避让横幅、正视像素标定）。默认安装仍不开启 `v23_mode`（100% 稳跑经典通道），勾选即享 60fps AI 皮肤且全程受车机三层自动保险丝保护。已装 `v22.1+` 与 `8080` 无线控制台的车主直接上传 `touareg-ota-v23.0.tar`（约 1.6 MB）并点「软重启车机」即可完成升级（因同时更新了 `VWClusterNav_v6.jar`）。

- **默认开启实车验通的 `EGL_KHR_image_pixmap` 硬件零拷贝（`v23_gles.c`，帧率直接翻倍至 `55～59 fps`）**：
  - 途锐 Tegra K1 真机实测带 `17 fps` 实时高德地图全屏监控 HUD 跑满 **`59 fps`**、GT 双炮筒 **`56 fps`**、赛博真 GLSL 显卡光效 **`55 fps`**，全程 0 反色 / 0 花屏 / 0 超时毛刺；
  - 现改为默认开启零拷贝，遇到不兼容驱动自动无缝退回 `glTexSubImage2D` 紧致上传（仍保留 `/mnt/app/eso/bin/apps/cluster/v23_no_zerocopy` 手动强制降级开关）。
- **方向盘 `OK` 键盲切多皮肤 + `8080` 控制台一键切换皮肤（`NavZoom.java` + `v23_gles.c` + `touareg_netd`）**：
  - **方向盘零延迟智能手势（方案 A）**：
    - 未选装「切原车地图」模块（默认态）且 `v23` 启用时：**双击 `OK` 键**零延迟立刻切换下一套皮肤（0.8 秒短冷却；单皮肤包在自定义皮肤 ↔ 内置默认经典间往返）；
    - 同时选装了「切原车地图」模块（`VW_HUD_4_OkMapSwitch`）且 `v23` 启用时：**双击 `OK` 切 CarPlay ↔ 原车地图**（480 ms 判定窗）、**三连击 `OK` 切下一套 v23 皮肤**，零冲突共存；
  - **`8080` 无线控制台多皮肤切换 UI**：`v23` 面板自动从 `/api/theme/status` 拉取实时 `FPS / 零拷贝状态 / 昼夜 / 当前皮肤` 并渲染多皮肤一键切换按钮及「内置默认经典」按钮；底层切换指令升级为内容 + 递增序号比对并自动 `chmod 0666`，彻底消除 QNX 1 秒 `st_mtime` 粒度与 `root/java` 跨用户写权限隐患。
- **信息框运动条升级为「整条平滑四色热力渐变」（`dash.c` + 浏览器 1:1 模拟器同步）**：
  - 转速（满 `6500`）、扭矩（满 `450`）、功率（满 `250`）、油门（`100%`）与制动（满 `60 bar`）斜切分段运动条不再前 70% 单调纯绿，改为已点亮格子随当前负载整条线性平滑变色：**`≤22%` 冰青绿 → `34%` 明黄（约 2200 rpm / 153 Nm）→ `56%` 亮橙（约 3640 rpm / 252 Nm）→ `≥80%` 烈红（约 5200 rpm / 360 Nm）**（注：满量程按 3.0TSI 官标 `250 kW / 450 N·m` 标定；2.0TSI 为 `180–195 kW / 370 N·m` 满油略低于顶格，中规 eHybrid 综合 `270 kW / 700 N·m` 及欧版 TDI 扭矩更高，属视觉参考区间），未亮槽继续保留暗红红线提示；经典 `v22.1` 通道与 `v23` 内置默认经典皮肤均直接受益。
- **移除 2 秒开机启动图直出地图（`cluster.c`，瘦身 287 KB 源码）**：
  - 删除 `src/cl_render/boot_canim_data.h` 及 `h264` 模式下 2 秒 `fifthBro` 开机启动图门控，连上 CarPlay 直接出地图免等待（CC BY-NC-SA 4.0 署名继续在 `README` 与 `LICENSES/THIRD_PARTY_NOTICES.md` 完整保留）。
- **途锐 `1920×639` 实车像素级精确几何规范入仓 + 模拟器参考线全面校准**：
  - 新增 [`docs/reference/CLUSTER_LAYOUT_SPEC.md`](docs/reference/CLUSTER_LAYOUT_SPEC.md)（并随发布包打入 `v23皮肤模板/`），同步更新 `THEME_API_SPEC.md`、`my_theme.js`、`USB/AGENTS.md` 及浏览器 1:1 模拟器 `drawOemGuides`：修正顶部双层遮挡（`y=0..82` 全黑栏 + `y=82..154` 导航栏 + `y>=164` 安全顶线）、精确左右斜梯形底边（`468 / 1458`）、新增**全天候常驻底部正中小路禁区（`y=580..639`）** 以及左右 `295×344` 黄金对称对齐参考框。
- **修复车机 QNX 默认 `PATH` 缺 `/mnt/app/armle/bin/awk`**：
  - 修复 `VW_HUD_2` / `VW_HUD_3` Persist 启动脚本、`touareg_netd` 重置基线命令与 `tools/deploy_eth.sh` 在 QNX 运行态下因默认 `PATH` 缺 `/mnt/app/armle/bin` 导致裸 `awk` 计数报错的隐患。
- **车机 QNX 6.6 ARMv7 二进制 & Java 1.4 校验和（`src/build.sh` + `src/hud-java/build.sh`，已通过 `carlibs` 符号审计 0 新增缺失符号、`DT_HASH+GNU_HASH` 完整、29 个 Java class 全为 version `48`）**：
  - `cl_render`（`1,341,980 B`）：`ca73ffcbfdf8984b975b9719cf3821b8f2626b8ff8fb036a9098720f22a55c0e`
  - `touareg_netd`：`b50f4b6fb7f0cc22b849f1110b394cbd66b91b9909597e5ed87114d13a639ce3`
  - `VWClusterNav_v6.jar`：`db5807bb5c4613a61898ad0a8906c5d19932f4fa3e969881f12a26974be47540`
  - `clusterd`（与 `v23.0-beta.1` 一致）：`096a71c1b37bdcf9ebba3b1d3e66428af38488177c585ed0e3d7f4db695d4f64`
  - `cl_probe.so`（与 `v22.1` 一致）：`e260013df86291611225856c6f1c3c05a1d12857bfeaaa0514d1494192cbfa24`
- 包含 `v23.0-beta.4` ～ `v23.0-beta.1` 的全部内容。

## v23.0-beta.4
> **Pre-release 实车性能优化与超时容错升级版**：在 `v23.0-beta.3` 实车跑通 60fps 全屏 AI 皮肤与 CarPlay 地图切换的基础上，针对实车反馈的**复杂多组件皮肤运行数十秒偶发触发 10ms 硬中断回退**与**红条提示重叠原车路名**问题，完成底层 GLES2 渲染管线全面提速与容错升级。已装 `v22.1+` 与 `8080` 无线控制台的车主直接传 `touareg-ota-v23.0-beta.4.tar`（1.6 MB）10 秒无线升级即可。

- **GLES2 渲染管线大幅提速（`v23_gles.c`）**：
  - **预缓存全部 Shader 句柄 + 状态去重**：6 个内置 Shader 与用户 Shader 编译后一次性缓存全部 Attrib/Uniform Location，渲染主循环实现 **0 次** `glGetUniformLocation` / `glGetAttribLocation` 字符串查找，并跳过重复 `glUseProgram` / `glBindTexture`；
  - **文字与多段矢量合批绘制**：`ctx.text()` 整串合批（最多 64 字 1 次 `glDrawArrays`），`ctx.segBar()`、`ctx.ticks()`、`ctx.biBar()`、`ctx.rangeBar()` 与 `ctx.poly()` 描边全部合批绘制，大幅降低复杂皮肤在 Tegra K1 上的 Draw Call 数量；
  - **字形图集扩容 + O(1) 哈希查找**：动态字形图集由 256 槽扩至 **512 槽**并加入 `1024` 项开放寻址哈希表，彻底消除线性扫描与运行几十秒后图集写满重置造成的突发毛刺。
- **第 1 层保险丝超时容错升级（`v23_sandbox.h` + `v23_gles.c`）**：
  - 单帧硬中断由 `10 ms` 放宽至 **`35 ms`**（真正 `while(1)` 死循环仍 0.035 秒秒杀），偶发 1～2 帧抖动当帧补画内置默认版保 0 黑屏、不永久踢掉用户皮肤；仅当**连续 3 帧硬超时**、**JS 语法/运行报错**或**连续 60 帧 > 25 ms** 才永久回退默认皮肤。
- **顶部安全提示横幅避让原车路名区（`v23_gles.c`）**：
  - 报错/状态横幅从 `y = 66` 下移至黄金安全区 **`y = 162`**，不再与途锐原车顶部路名文字重叠。
- **车机 QNX 6.6 ARMv7 二进制校验和（`src/build.sh`，动态导入符号与实车跑通的 `v23.0-beta.3` 完全一致，0 缺失非弱符号）**：
  - `cl_render`：`93cf66d188bd295a9b0b54aa68a134ff2ca757a8462f16ca004c1d2e5107390b`
  - `touareg_netd`（与 `v23.0-beta.3` 一致）：`0c32cb18f532b85164ab512799c72cfe626b14ad72248ba63388792a4ed8a8bd`
  - `clusterd`（与 `v23.0-beta.1` 一致）：`096a71c1b37bdcf9ebba3b1d3e66428af38488177c585ed0e3d7f4db695d4f64`
  - `cl_probe.so`（与 `v22.1` 一致）：`e260013df86291611225856c6f1c3c05a1d12857bfeaaa0514d1494192cbfa24`
- 包含 `v23.0-beta.3`、`v23.0-beta.2` 与 `v23.0-beta.1` 的全部内容。

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
  - **默认只内置 1 套内置默认经典皮肤（`default_classic`）**：100% 沿用老用户 `dash.conf` 43 项勾选与大字导航卡；支持全部 17 个 `ctx.*` 高速画笔 + 可选 ≤ 8 KB 自定义 GLSL ES 1.00 光效着色器；冷车未就绪信号（`NaN/-999`）统一清洗为有限 `0` 并提供 `bus.ready.*`，杜绝 `.toFixed()` 抛错。
  - **三层自动保险丝保障（无需 8080 救砖）**：
    - **第 1 层（QuickJS 安全沙盒）**：纯核心 QuickJS（零 `quickjs-libc`/`pthread`/`assert`、物理移除 `eval`/`Function`），强制 **128 KB 单文件上限 + 4 MB 16B 对齐精确内存封顶 + 256 KB 栈上限 + 单帧 10 ms 硬中断强杀 + 掉帧看门狗 + 冷启动崩溃哨兵**；任一触发当帧 **0 黑屏** 补画内置默认经典皮肤并在仪表顶栏弹出报错原因与行号，坏 GLSL 光效仅降级关闭光效层；
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
    - 通过实车验证只读接入 `de.vw.mib.dsi.carcomfort.DSICarComfort`（RDK 直接胎压监测）、`DSICarDrivingCharacteristics`（车身倾角，需配备空气悬架底盘；无空悬车型如中规 2021–2022 eHybrid 无上报）与 `DSINavigation`（GNSS 海拔），信息框新增 **`tires`（单行四轮胎压）、`tpfl/tpfr/tprl/tprr`（四轮独立胎压 bar，低压预警附 `!`）、`pitch`（俯仰角 `°`）、`roll`（侧倾角 `°`）、`alt`（海拔 `m`）**，所有车友均可在电脑端「改装设置」或手机 `8080` 无线控制台自由组合
    - 针对途锐 `DSICarVehicleStates` 原厂 `currentGear` 常报 `0` 的缺陷，基于 2023 款 3.0TSI 实车采埃孚 **ZF 8HP65A + 3.076 主减速比** 锁止齿比实时标定兜底输出 **`D1～D8` / `S1～S8`** 挡位（注：大众官方仅标称 8 速手自一体 Tiptronic / AG8 采埃孚 8AT，未公开全系统一主减速比；2.0TSI 或 eHybrid 纯电/混动模式下可能存在齿比或解耦偏差）；`10Hz` 行车记录 CSV 同步扩充至 **34 列**真实物理传感器数据
  - **交互减法与独立选装模块规范化（10 个模块默认全关、均可独立安装与卸载）**：
    - 方向盘右侧 `OK` 键 **2 秒内连按 2 下（双击）** 切换 CarPlay 地图与原车地图拆分为独立选装模块 `VW_HUD_4_OkMapSwitch`（默认放在 `USB/未选模块/` 不开启，按需选装）
    - 电脑端「改装设置」新增**前置依赖联动弹窗**：单独点选需要前置模块配合的功能时（如勾选「仪表地图」自动关联「导航上 HUD」，勾选「OK 双击切地图」自动关联前两项），自动弹窗提醒并同步勾选（卸载时反向联动）
  - **工程健壮性与安全加固**：
    - 修复旧版 `VW_ClusterNav` 升级至 `VW_HUD_2_ClusterNav` 时的双 Persist 循环遗留问题、`Rebind.java` 周期日志异常、`VW_WiFi_Dev` 卸载 `/mnt/ota` 挂载保护，并为 `touareg_netd` 补齐 CSRF 请求头校验与 PIN 口令防爆破限速
- 包含 v20.3 的全部内容

全套 `v21.0` 组件已在 2023 款途锐（`MH2p_CN_VWG36_P2878`）完成实车全流程验证（大字运动信息框、左右 `295px` 对称对齐、实时与常驻待机导航卡切换、四轮胎压/倾角实车直读），全部二进制与配置器均通过 Java 1.4（class version `48.0`）、ELF `DT_HASH` 与 `go test` 自动化回归检验。

## 历史版本归档

更早的历史版本记录（`v18` ～ `v20.3`）已归档至 [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md)。
