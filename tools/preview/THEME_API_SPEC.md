# 途锐 MH2p v23 纯文本 `.js` 60fps 仪表皮肤规范（V1 草案）

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

// 每帧 60fps 调用一次（单帧脚本预算 <= 10ms，超时/报错自动回退官方默认皮肤）
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

## 2. 途锐实车 1920×639 安全可视区（避免被原车仪表遮挡）

| 区域 | 坐标范围 | 说明 |
| :--- | :--- | :--- |
| 顶部原车信息栏 / 时钟 | `y = 0 .. 156`（时钟中心 `x = 965`） | 原车仪表自绘覆盖，重要读数请放在 `y >= 160` |
| 左下原车挡位梯形 | `x = 0 .. 355, y = 509 .. 639` | 左卡底边建议停在 `y <= 508` |
| 右下原车车速梯形 | `x = 1565 .. 1920, y = 509 .. 639` | 右卡底边建议停在 `y <= 508` |
| **底部正中 ACC 巡航遮挡区** | `x = 760 .. 1160, y = 495 .. 639` | 仅当 `bus.sys.acc === true` 时原车绘制 ACC 跟车图标（实车地图车标会自动上移 80px），底部居中组件请避让或随 `bus.sys.acc` 上移/隐藏 |
| 黄金安全主舞台 | `x = 370 .. 1550, y = 160 .. 490` | 全天候无遮挡（ACC 关闭时可用至 `y = 628`） |

---

## 3. 统一数据总线 `bus`（60fps 平滑值 + 原始值）

### 3.1 动态车辆数据（连续量均已做 60fps 阻尼平滑；加 `Raw` 后缀可取 10Hz 原始准确值）
- 动力：`bus.rpm` (0..7000), `bus.rpmPeak` (峰值滞留), `bus.power` (kW), `bus.torque` (Nm), `bus.pedal` (0..100%), `bus.brake` (bar)
- 速度与挡位：`bus.speed` (km/h), `bus.gear` (如 `"D4"`, `"M6"`, `"P"`), `bus.gearNum` (0..8), `bus.steer` (° 左负右正)
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

> 提示：单个 `.js` 文件既支持单皮肤（`const THEME = {...}; function render(ctx, bus, state) {...}`），也支持多皮肤包（`const THEMES = [ { id, name, shader, render(ctx, bus, state) {...} }, ... ]`，最多 8 套，见 [`my_theme.js`](./my_theme.js)）。

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
1. 禁止访问外部网络、文件系统或 DOM；
2. 禁止死循环或超大数组分配（单帧超 **10ms** 或内存超限即刻强杀，自动切回默认皮肤并在仪表顶部弹报错行号）；
3. 自定义 `shader` 长度 ≤ 8 KB；编译失败仅跳过光效层，不影响主画面。
