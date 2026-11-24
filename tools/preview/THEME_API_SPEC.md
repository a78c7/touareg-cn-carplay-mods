# 途锐 MH2p v23.0 纯文本 `.js` 60fps 仪表皮肤规范（v23.0 正式版）

本规范是 **车机端（QuickJS + OpenGL ES 2.0）** 与 **浏览器端（1:1 模拟器）** 的唯一共同契约。
一套皮肤就是一个独立的纯文本 `.js` 文件（建议 ≤ 64 KB，不含任何外部图片），复制粘贴即可在车友群或 GitHub Discussions 分享。

---

## 1. 皮肤文件基本骨架

```js
const THEME = {
  id: "my_custom_skin",
  name: "我的运动仪表",
  author: "你的昵称",
  version: "1.0",
  desc: "一句话介绍风格特点",
  // 可选：GLSL ES 1.00 片段着色器（车机 GLES2 与浏览器 WebGL1 完全同源）
  // 留空或不写则不启用自定义光效
  shader: ""
};

// 每帧 60fps 调用一次（建议单帧脚本预算 <= 10ms，车机硬中断 35ms×3 帧；超时/报错自动回退内置默认皮肤）
// ctx: 显卡高速画笔（逻辑坐标系固定 1920x639）
// bus: 统一 60fps 数据总线（已做好 10Hz -> 60fps 平滑插值）
// state: 皮肤私有的持久化状态对象（可用于拖尾、粒子、过渡动画）
function render(ctx, bus, state) {
  ctx.clear(8, 12, 18, 1);
  if (bus.sys.mapLive) {
    ctx.drawMap(0, 0, 1920, 639);
  }
  ctx.text(Math.round(bus.speed) + " km/h", 960, 340, {
    size: 72, bold: true, align: "center", color: "#ffffff"
  });
}
```

---

## 2. 途锐实车 1920×639 精确安全可视区（实车正面像素标定，详见本目录 `CLUSTER_LAYOUT_SPEC.md` / 仓库路径 [`docs/reference/CLUSTER_LAYOUT_SPEC.md`](../../docs/reference/CLUSTER_LAYOUT_SPEC.md)；官方物理屏 12 英寸 `1920×720`，车机 Display 4 逻辑上报 `1920×722`）

| 区域 | 精确坐标范围 | 说明 |
| :--- | :--- | :--- |
| 顶部全黑原车状态栏 | `y = 0 .. 82`（时钟中心 `x = 965`） | 原车不透明黑底（方向/时钟/车外温），画在此处完全不可见 |
| 顶部半透明原车导航栏 + 车道扩展带 | 常态 `y = 82 .. 154`；发车道指引时撑至 **`y = 190`**（`190..200` 为 10px 呼吸缝） | 原车顶栏与底边白线盖在最上层，所有组件顶边请统一对齐 **`y >= 200`** |
| 左下原车挡位斜梯形 | `(0,518) → (314,518) → (468,639) → (0,639)` | 顶边 `y = 518`，斜边扩至底边 `x = 468` |
| 右下原车车速斜梯形 | `(1612,518) → (1920,518) → (1920,639) → (1458,639)` | 顶边 `y = 518`，斜边扩至底边 `x = 1458` |
| **底部正中原车小路（常驻！）** | `(890,580) → (1035,580) → (1130,639) → (795,639)` | **挂 P 挡/关 ACC 也常驻**，`y = 580..639` 居中区全天候禁放文字或进度条 |
| **底部正中 ACC 扩展区** | `x = 760 .. 1160, y = 495 .. 639` | `bus.sys.acc === true` 时向上扩大（实车按 `dash.conf` 的 `car_up=35 + acc_up=55 = 90px` 上限，开 ACC 相对常态再上移 55px），底部居中文字/数字/进度条需避让；**`ctx.drawMap` 地图底图可直接铺至 `y = 575` 甚至 `639`，切勿在 `495` 截断留黑空挡** |
| **左 / 右翼黄金标准框** | 左 `x = 5..300` / 右 `x = 1620..1915`，`y = 200..508`（`295×308`） | 永久避开原车车道指引扩展顶栏（`y=190`）和下方斜梯形（`y=518`），上下均留 10px 呼吸缝；加宽圆盘建议宽 ≤ `340` 且底边 `y <= 508` |
| **中央全天候主舞台 / 底部夹缝** | 主区 `x = 312..1608, y = 200..512`；底槽 `x = 476..1448, y = 518..576` | 主区宽 `1296px`；底部细长槽同时避开左右斜梯形与正中常驻小路（ACC 开时避开 `x=760..1160`） |

---

## 3. 统一数据总线 `bus`（60fps 平滑值 + 原始值）

### 3.1 动态车辆数据（连续量均已做 60fps 阻尼平滑；加 `Raw` 后缀可取 10Hz 原始准确值）
- 动力：`bus.rpm` (0..7000，按车型动力而异：2.0T/3.0T 最大功率上限 6400–6500 rpm，eHybrid 2.0T 为 6000 rpm), `bus.rpmPeak` (峰值滞留), `bus.power` (kW), `bus.torque` (Nm), `bus.pedal` (0..100%), `bus.brake` (bar)
- 速度与挡位：`bus.speed` (km/h), `bus.gear` (如 `"D4"`, `"M6"`, `"E4"`, `"P"`), `bus.gearNum` (0..8), `bus.steer` (° 左负右正)
- 车身动态：`bus.g` (带符号合 G), `bus.glong` (纵向 g), `bus.glat` (横向 g), `bus.pitch` (俯仰 °), `bus.roll` (侧倾 °), `bus.alt` (海拔 m)
- 温度与油量：`bus.oil` (°C), `bus.gearbox` (°C), `bus.coolant` (°C), `bus.outside` (°C), `bus.cons` (L/100km), `bus.fuel` (L 或 %), `bus.range` (km)
- 四轮胎压：`bus.tpfl`, `bus.tpfr`, `bus.tprl`, `bus.tprr` (bar), `bus.tires` (前后均值串)
- 性能极值：`bus.t100` (最近/实时 0-100 秒), `bus.t100Running` (是否正在冲刺计时), `bus.pmax`, `bus.tqmax`, `bus.gmax`
- 行车电脑：`bus.trip`, `bus.tripdist`, `bus.tripspeed`, `bus.triptime`, `bus.refuel`, `bus.total`, `bus.tripkm`

### 3.2 导航数据 `bus.nav`
- `bus.nav.live` (boolean 是否导航中), `bus.nav.distM` (下一路口距离 m), `bus.nav.remDistKm`, `bus.nav.remMin`, `bus.nav.eta` (如 `"14:25"`)
- `bus.nav.road` (下一路名), `bus.nav.nextRoad` (下下路名), `bus.nav.action` (如 `"前方右转"`), `bus.nav.turnType` (`"straight"|"left"|"right"|"slight_left"|"slight_right"|"uturn"|"roundabout"`)
- `bus.nav.lanes` (车道数组 `[{dir, rec}]`)

### 3.3 系统、冷车就绪与用户配置
- `bus.sys`: `{ mapLive, night, acc, time, dt, fps, mapFps, width: 1920, height: 639 }`
- `bus.ready`: `{ oil, gearbox, tires, t100, alt }`（车机端强制保证所有数值字段永远为有限数字、绝不传 `null`/`NaN`，冷车未就绪时给 `0` 并置对应 `ready` 为 `false`，防止 `.toFixed()` 抛错崩溃）
- `bus.conf`: 读取车机 `dash.conf`（`items`, `r_items`, `show`, `r_show`, `bars`, `alpha`, `nav_box`, `nav_side`, `tank_l`）

> 提示：单个 `.js` 文件既支持单皮肤（`const THEME = {...}; function render(ctx, bus, state) {...}`），也支持多皮肤包（`const THEMES = [ { id, name, shader, render(ctx, bus, state) {...} }, ... ]`，最多 8 套，见 [`my_theme.js`](./my_theme.js)）。实车切换手势：默认双击方向盘 `OK` 键零延迟切换下一套皮肤；若同时选装了「切原车地图」模块（`VW_HUD_4_OkMapSwitch`），则双击 `OK` 切原车地图、三连击 `OK` 切下一套皮肤（亦可在手机 `8080` 控制台一键点选）。

---

## 4. 显卡高速画笔 `ctx`

所有方法在车机端均映射为 OpenGL ES 2.0 硬件绘制，单帧几十次调用总耗时 < 1.5ms：

- `ctx.clear(r, g, b, a)` — 清空画布
- `ctx.drawMap(x, y, w, h, opts)` — 绘制 CarPlay 实时地图窗口；`opts`: `{ radius, alpha, vignette, shape: "rect"|"circle", borderColor, borderWidth }`
- `ctx.drawShader(opts)` — 绘制 `THEME.shader` 自定义 GLSL 光效；`opts`: `{ alpha, blend: "normal"|"add" }`
  - 统一暴露 Uniforms：`u_res (vec2)`, `u_time (float)`, `u_rpm (0..1)`, `u_speed (0..1)`, `u_power (0..1)`, `u_pedal (0..1)`, `u_brake (0..1)`, `u_g (vec2)`, `u_map_live (float)`, `u_night (float)`
- `ctx.rect(x, y, w, h, opts)` — 圆角矩形/渐变卡片；`opts`: `{ fill, fill2, gradDir: "v"|"h", stroke, lineWidth, radius, glow, glowColor }`
- `ctx.arc(cx, cy, r, startDeg, endDeg, opts)` — 抗锯齿圆弧（0°=3点钟，顺时针递增）；`opts`: `{ color, lineWidth, cap: "round"|"butt", glow, glowColor, segments, gapDeg }`
- `ctx.ticks(cx, cy, rIn, rOut, startDeg, endDeg, count, opts)` — 仪表盘刻度线；`opts`: `{ color, majorEvery, majorLen, lineWidth, majorWidth, redFromFrac, redColor }`
- `ctx.needle(cx, cy, len, tail, angleDeg, opts)` — 60fps 运动指针；`opts`: `{ color, tipColor, width, glow, hubRadius, hubColor }`
- `ctx.segBar(x, y, w, h, frac, opts)` — 斜切分段运动条；`opts`: `{ segs, redSegs, warnSegs, color, warnColor, redColor, pulse }`
- `ctx.biBar(x, y, w, h, signedFrac, opts)` — 中点双极条（-1..+1）；`opts`: `{ posColor, negColor }`
- `ctx.rangeBar(x, y, w, h, frac, opts)` — 温压区间条；`opts`: `{ color, tickLo, tickHi }`
- `ctx.line(x1, y1, x2, y2, opts)` / `ctx.circle(cx, cy, r, opts)` / `ctx.poly(points, opts)` — 矢量线条、圆、多边形（可用于绘制车标、水平仪、G 值轨迹）
- `ctx.text(str, x, y, opts)` — 任意字号中英文文本；`opts`: `{ size, bold, italic, color, align: "left"|"center"|"right", baseline, glow, glowColor, maxWidth }`
- `ctx.drawDashBox(x, y, w, side, opts)` — 渲染兼容 `dash.conf` 的左/右信息框（`side: "left"|"right"`）
- `ctx.drawNavCard(x, y, w, h, opts)` — 渲染大字 CarPlay 导航卡
- `ctx.drawTurnArrow(x, y, size, turnType, opts)` — 单独绘制高德/苹果风格转向矢量图标

---

## 5. 安全红线（车机端强制执行）
1. 禁止访问外部网络、文件系统、DOM 或定时器：`window`、`document`、`fetch`、`XMLHttpRequest`、`WebSocket`、`localStorage`、`sessionStorage`、`eval`、`Function`、`importScripts`、`setTimeout`、`setInterval`；
2. 禁止死循环或超大数组分配（建议单帧脚本预算 ≤ **10ms**，单帧超 **35ms** 硬中断、连续 3 帧或持续 > 25ms 或内存超限即刻熔断，0 黑屏补画内置默认皮肤并在 `y = 162` 弹报错行号）；
3. 自定义 `shader` 长度 ≤ 8 KB；编译失败仅跳过光效层，不影响主画面。
