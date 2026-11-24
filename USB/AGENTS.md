# 途锐 MH2p 改装包 — AI 助手部署规范（必读）

> 当车主把本 U 盘目录交给你（Claude Code / Cursor / Codex / Antigravity / OpenClaw 等）用文字代为写皮肤或配置刷机 U 盘时，**必须严格遵守以下规范**，禁止凭猜测手工乱改目录或改完直接让用户热拔 U 盘。

---

## 1. 写 v23 60fps 自定义皮肤前的硬规矩

1. 动笔前必须先完整阅读 `v23皮肤模板/THEME_API_SPEC.md` 与 `v23皮肤模板/my_theme.js`。
2. 皮肤为单个纯文本 UTF-8 `.js` 文件，大小 **1 B ～ 128 KB**（建议 ≤ 64 KB），支持单皮肤（`THEME` + `render(ctx, bus, state)`）或单文件多皮肤包（`const THEMES = [...]`，最多 8 套）。
3. **严禁使用任何浏览器/宿主接口**：`window`、`document`、`fetch`、`XMLHttpRequest`、`WebSocket`、`localStorage`、`sessionStorage`、`eval`、`Function`、`importScripts`、`setTimeout`、`setInterval`（注释里提及没关系，正文代码禁止出现）。
4. 避开实车 `1920×639` 精确遮挡区（详见本 U 盘 `v23皮肤模板/CLUSTER_LAYOUT_SPEC.md`；仓库源码路径 `docs/reference/CLUSTER_LAYOUT_SPEC.md`；官方物理屏 12 英寸 `1920×720`，车机 Display 4 逻辑上报 `1920×722`）：顶部黑栏 `y = 0..82` + 导航栏 `y = 82..154` + 百度/苹果车道指引扩展带 `y = 154..190`（组件顶边请统一对齐 **`y >= 200`**）；左下挡位斜梯形 `(0,518)→(314,518)→(468,639)→(0,639)`；右下车速斜梯形 `(1612,518)→(1920,518)→(1920,639)→(1458,639)`；**底部正中常驻小路 `y = 580..639`（`x = 795..1130`，关 ACC 也常驻，全天候禁放文字/进度条）**；以及 `bus.sys.acc === true` 时的 ACC 扩展区 `x = 760..1160, y = 495..639`。左右翼黄金标准对齐框为 `x = 5..300` 与 `x = 1620..1915`（`y = 200..508`, `295×308`），中央全天候主舞台 `x = 312..1608, y = 200..512`，底部斜边夹缝安全槽 `x = 476..1448, y = 518..576`。
5. 冷车未就绪数值必须结合 `bus.ready.*` 做显示兜底；建议单帧脚本预算 `≤ 10 ms`（正常渲染建议控在 `≤ 8 ms`），车机单帧硬中断上限 `35 ms`（连续 3 帧或持续 > 25 ms 熔断）。

---

## 2. 唯一推荐部署方式：调用本目录「改装设置」命令行模式

本 U 盘根目录内置的「改装设置」程序支持无界面 CLI，会自动完成：**皮肤合法性校验、补齐前置依赖（`VW_HUD_1` + `VW_HUD_2` + `VW_HUD_3`）、写入 `v23_mode`、保留原有 `dash.conf`、清理 Mac 隐藏文件（`._*` / `.DS_Store`）、强制刷盘（`sync`）、全量自检，并自动安全弹出 U 盘**。

> 注意：执行带 `--eject` 的命令前先 `cd /`（Mac）或 `Set-Location $env:TEMP`（Windows），避免当前终端工作目录占用 U 盘导致无法自动弹出。

### A. 写好皮肤后一键部署 + 自检 + 安全弹出（最常用）
- **macOS**：
  ```sh
  cd / && "/Volumes/<U盘名>/改装设置（Mac）.command" --deploy-theme "/tmp/my_theme.js" --verify --eject
  ```
- **Windows (PowerShell)**：
  ```powershell
  Set-Location $env:TEMP; & "E:\改装设置（Windows）.exe" --deploy-theme "C:\path\to\my_theme.js" --verify --eject
  ```

### B. 首次全套选装 + 自定义皮肤一起写入
- **macOS**：
  ```sh
  cd / && "/Volumes/<U盘名>/改装设置（Mac）.command" --preset full --deploy-theme "/tmp/my_theme.js" --verify --eject
  ```
- **Windows (PowerShell)**：
  ```powershell
  Set-Location $env:TEMP; & "E:\改装设置（Windows）.exe" --preset full --deploy-theme "C:\path\to\my_theme.js" --verify --eject
  ```
  - `--preset` 支持：`base`（全屏 + 无线 CarPlay + HUD + 仪表地图）、`full`（基础四件套 + 方向盘 OK 双击切地图）、`none`（全部不处理）。
  - 如需按项选装，改用 `--install fullscreen,carplay,hud,cluster,mapswitch,nobtauth,nosimpopup`（全部功能 ID 可运行 `--help` 查看；`wifi` 无线控制台涉及热点密码与密钥生成，建议由车主双击图形界面配置）。

### C. 仅自检与清理（检查是否漏文件/空刷/残留隐藏文件）
- 加 `--clean --verify`（可追加 `--json` 获取结构化结果）。若返回退出码非 `0`，**必须修复全部阻塞错误后再让车主上车刷机**。

---

## 3. 兜底手工核对清单（仅当环境受限无法运行上方程序时）

1. 车机 ModKit **只刷 `Mods/` 下的模块**，`未选模块/` 里的文件夹插车会被跳过（空刷）；且同一个模块绝不能同时出现在 `Mods/` 和 `未选模块/` 两边。
2. 安装 `VW_HUD_3_CarPlayCluster` 时，`VW_HUD_1_RouteGuidance` 与 `VW_HUD_2_ClusterNav` 也必须在 `Mods/` 中（首次安装缺少前置会中止）。
3. 自定义皮肤必须放在：`Mods/VW_HUD_3_CarPlayCluster/Update/themes_user/my_theme.js`（非空 UTF-8，≤ 128 KB），且同级 `Update/` 目录下应建立 `v23_mode` 标记文件（并确保无 `v23_off`）。
4. 未经车主明确要求，不要篡改或覆盖其已定制好的 `dash.conf`。
5. macOS 必须清理 U 盘中所有 `._*` 与 `.DS_Store` 文件（会干扰车机升级程序）。
6. 写完必须执行 `sync` 并安全弹出 U 盘。

---

## 4. 回复车主的最后一步铁律

- **严禁在写完文件后直接说“现在拔 U 盘就行”**。
- 必须确认 `--verify` 返回通过、且已完成安全弹出（或明确提示车主：先点系统 ⏏ /「安全删除硬件」待图标消失后再拔），防止 FAT32 写缓存未落盘导致皮肤变成 0 字节/半截文件。
