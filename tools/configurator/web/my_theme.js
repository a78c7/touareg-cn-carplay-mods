/*
 * ============================================================================
 * 途锐 MH2p v23.0 · 60fps 仪表自定义皮肤模板（给 AI 直接编写用）
 * ============================================================================
 *
 * 【车主怎么用（3 步零编程）】
 *   1. 直接把这个 my_theme.js 文件发给任意 AI（DeepSeek / ChatGPT / Claude / 手机 DSH Go），
 *      用大白话告诉它你想要什么风格（例如：“帮我在这个文件里写 2 套皮肤：一套红黑运动双表盘中间嵌高德地图，
 *      一套越野姿态+四轮胎压风格”）。
 *   2. AI 改好保存后，双击打开同目录的单文件 preview.html（或 index.html），把本文件直接拖进网页里 1:1 试驾预览
 *      （键盘 W 油门 / S 刹车 / A D 转向 / 空格弹射 / 双击 O 键切风格；模拟已装切原车地图模块时双击 O 切地图、三连击 O 切风格）。
 *   3. 浏览器里试驾顺滑且右上角绿灯「运行正常」后：
 *      - 【推荐，无需连 8080】运行 U 盘根目录的「改装设置」程序导入本 my_theme.js 并点击「保存到 U 盘」后安全弹出，上车插 U 盘一次写入即生效；
 *      - 【进阶】如车机已装「手机无线控制台」，也可直接连 http://touareg.local:8080 热上传生效。
 *
 * ----------------------------------------------------------------------------
 * 【给 AI 的编写规范（必须严格遵守）】
 * 1. 本文件支持写「1 套或多套皮肤」：直接往下面的 THEMES 数组里追加对象即可（建议 1~5 套，最多 8 套）。
 *    每套皮肤包含：{ id, name, author, desc, shader(可选), render(ctx, bus, state) }。
 *    （也兼容单皮肤写法：顶层 const THEME = {...}; function render(ctx, bus, state) {...}）
 *
 * 2. 途锐实车画布分辨率固定为 1920 × 639（官方物理屏 12 英寸 1920×720，车机 Display 4 逻辑上报 1920×722），必须避开实车正面标定的原车遮挡区：
 *    - 顶部全黑原车状态栏：y = 0 .. 82（时钟中心 x = 965），画在此处完全不可见
 *    - 顶部半透明原车导航栏 + 车道指引扩展带：常态 y = 82 .. 154，百度/苹果下发车道指引时整条顶栏与白线会撑高至 y = 190（190..200 为 10px 呼吸缝），所有卡片/组件顶边请统一对齐 y >= 200
 *    - 左下原车挡位斜梯形：(0,518) → (314,518) → (468,639) → (0,639)
 *    - 右下原车车速斜梯形：(1612,518) → (1920,518) → (1920,639) → (1458,639)
 *    - 【重要】底部正中原车小路图标（即使挂 P 挡 / ACC 关闭也常驻！）：
 *      (890,580) → (1035,580) → (1130,639) → (795,639)，y = 580..639 居中区全天候禁放文字或进度条！
 *    - 【重要】底部正中 ACC 扩展遮挡区：当 bus.sys.acc === true 时向上扩大至 x = 760..1160, y = 495..639
 *      （实车按 dash.conf 的 car_up=35 + acc_up=55 = 90px 上限，开 ACC 时相对常态再上移 55px），底部居中文字/数字/进度条请在 bus.sys.acc 为 true 时自动上移/拆分避让！
 *    - 【切勿截断地图底图】上述 y = 495..639 / 580..639 仅限制文字、数字和进度条；CarPlay 实时地图底图 ctx.drawMap(...) 完全可以直接往下铺到 y = 575 甚至满铺到 639（底部正中被原车小车叠压边角不影响看路，切勿把地图在 y = 495 截断留出大片黑底空挡）！
 *    - 黄金标准对齐框（与原车边框严丝合缝且永久避开车道顶栏，强烈推荐）：
 *      * 左翼黄金栏：x = 5 .. 300（宽 295px），y = 200 .. 508（高 308px，上下均留 10px 呼吸缝）
 *      * 右翼黄金栏：x = 1620 .. 1915（宽 295px），y = 200 .. 508（高 308px，严格镜像）
 *      * 加宽侧翼/圆盘建议宽 <= 340px 且底边停在 y <= 508
 *      * 中央全天候无遮挡主舞台：x = 312 .. 1608, y = 200 .. 512（宽 1296px）
 *      * 底部斜边夹缝安全槽：x = 476 .. 1448, y = 518 .. 576（ACC 开时避开 x = 760..1160）
 *
 * 3. 可用的 60fps 显卡高速画笔 ctx（车机 OpenGL ES 2.0 与浏览器 1:1 同源）：
 *    - ctx.clear(r, g, b, a)
 *    - ctx.drawMap(x, y, w, h, { radius, alpha, vignette, shape:"rect"|"circle", borderColor, borderWidth })
 *      * 提示：用 bus.sys.mapLive 判断当前是否连着 CarPlay 地图；断开时请自行绘制全屏自绘仪表内容。
 *    - ctx.drawShader({ alpha, blend:"normal"|"add" })
 *      * 若皮肤填写了 shader（标准 GLSL ES 1.00 片段着色器，<= 8KB），调用此函数叠画显卡光效；
 *      * 统一 uniforms: u_res(vec2), u_time(float), u_rpm(0..1), u_speed(0..1), u_power(0..1),
 *        u_pedal(0..1), u_brake(0..1), u_g(vec2), u_map_live(0/1), u_night(0/1)
 *    - ctx.rect(x, y, w, h, { fill, fill2, gradDir:"v"|"h", stroke, lineWidth, radius, glow, glowColor })
 *    - ctx.circle(cx, cy, r, { fill, stroke, lineWidth, glow, glowColor })
 *    - ctx.line(x1, y1, x2, y2, { color, lineWidth, cap, glow, glowColor })
 *    - ctx.poly([[x,y], ...], { fill, stroke, lineWidth, glow, glowColor })   // 可矢量绘制车标/姿态剪影
 *    - ctx.arc(cx, cy, r, startDeg, endDeg, { color, lineWidth, cap:"round"|"butt", glow, glowColor })
 *    - ctx.ticks(cx, cy, rIn, rOut, startDeg, endDeg, count, { color, majorEvery, majorLen, lineWidth, majorWidth, redFromFrac, redColor })
 *    - ctx.needle(cx, cy, len, tail, angleDeg, { color, tipColor, width, glow, hubRadius, hubColor })
 *    - ctx.segBar(x, y, w, h, frac, { segs, redSegs, color, warnColor, redColor, pulse })
 *    - ctx.biBar(x, y, w, h, signedFrac, { posColor, negColor })
 *    - ctx.rangeBar(x, y, w, h, frac, { color, tickLo, tickHi })
 *    - ctx.text(str, x, y, { size, bold, italic, color, align:"left"|"center"|"right", glow, glowColor, maxWidth })
 *    - ctx.drawDashBox(x, y, w, "left"|"right")        // 复用用户 8080 勾选的 dash.conf 信息框
 *    - ctx.drawNavCard(x, y, w, h, { alpha })          // 复用大字高德/百度/苹果导航卡
 *    - ctx.drawTurnArrow(x, y, size, bus.nav.turnType, { color })
 *
 * 4. 可用的 60fps 车辆与导航数据总线 bus（连续量已平滑，加 Raw 后缀可取原始值，如 bus.rpmRaw）：
 *    - 动力速度：rpm, rpmRaw, rpmPeak, speed, power(kW), torque(Nm), pedal(%), brake(bar), gear("D4"/"S1"), steer(°)
 *    - 车身动态：g(合G), glong(纵G), glat(横G), pitch(俯仰°), roll(侧倾°), alt(海拔m)
 *    - 温度油量：oil(机油°C), gearbox(波箱°C), coolant(水温°C), outside(车外°C), cons(L/100km), fuel(L), range(km)
 *    - 胎压极值：tpfl, tpfr, tprl, tprr(bar), tires, t100(0-100秒), t100Running, pmax, tqmax, gmax
 *    - 导航状态：nav.{ live, distM, remDistKm, remMin, eta, road, nextRoad, action, turnType, lanes }
 *    - 系统配置：sys.{ mapLive, night, acc, time, dt, fps, mapFps, width, height }, conf(dash.conf 配置)
 *    - 冷车就绪标志：ready.{ oil, gearbox, tires, t100, alt }（车机保证所有数值字段永远为有限数字、不会是 null/NaN，
 *      因此可直接安全调用 .toFixed()；若想在冷车未就绪时显示 "--"，可用 bus.ready.oil ? ... : "--"）
 *
 * 5. 安全红线（车机端强制校验，违者自动熔断回退内置默认皮肤并报错行号）：
 *    - 禁止使用 window / document / fetch / XMLHttpRequest / WebSocket / localStorage / sessionStorage / eval / Function / importScripts / setTimeout / setInterval
 *    - 禁止死循环或超大数组分配（建议单帧脚本预算 <= 10ms，正常渲染建议 <= 8ms；车机单帧硬中断 35ms×3 帧、JS 内存上限 4MB）
 *    - 禁止引用任何外部图片 URL，车标与图形全部用矢量 ctx 函数绘制
 *
 * 6. 【给 AI 的必执行收尾提示】
 *    写完或修改完本 my_theme.js 后，请务必明确提示用户：
 *    「皮肤已写入 my_theme.js！请直接双击打开 preview.html（单文件断网即用，或 index.html），
 *    把本 my_theme.js 拖进浏览器页面（或点击“导入本地 .js 皮肤”加载本文件），即可按键盘 W/S 踩油门刹车、
 *    勾选 ACC 巡航/冷车测试、双击 O 键切换风格（实车默认双击方向盘 OK，装切原车地图模块时三连击 OK），
 *    在浏览器中 1:1 实时验证 60fps 效果与安全体检！验证通过后用 U 盘「改装设置」导入保存并安全弹出即可上车刷入。」
 * ============================================================================
 */

const THEMES = [
  // ======================== 皮肤 1（请让 AI 直接修改或替换为你的设计） ========================
  {
    id: "my_custom_1",
    name: "我的自定义皮肤 1（空白模板）",
    author: "车主 / AI",
    version: "1.0",
    desc: "请把本文件交给 AI，告诉它你想要的仪表风格；也可在 THEMES 数组里一次写多套皮肤",
    shader: "",
    render: function (ctx, bus, state) {
      ctx.clear(8, 12, 20, 1);

      // 连着 CarPlay 时满屏显示实时地图，断开时铺深色座舱底板
      if (bus.sys.mapLive) {
        ctx.drawMap(0, 0, 1920, 639, { alpha: 1.0 });
      } else {
        ctx.rect(0, 0, 1920, 639, { fill: "#070c16", fill2: "#101b2e", gradDir: "v" });
      }

      // 模板占位提示卡（AI 修改时请删除这里，换成你的正式表盘/动效）
      ctx.rect(580, 210, 760, 200, {
        fill: "rgba(10,16,28,0.82)", stroke: "rgba(34,211,238,0.45)", lineWidth: 2, radius: 18
      });
      ctx.text("空白自定义模板已加载 (my_theme.js)", 960, 272, {
        size: 30, bold: true, color: "#67e8f9", align: "center"
      });
      ctx.text("当前车速 " + Math.round(bus.speed) + " km/h · " + Math.round(bus.rpmRaw) + " rpm · " + bus.gear, 960, 326, {
        size: 24, bold: true, color: "#ffffff", align: "center"
      });
      ctx.text("把 my_theme.js 发给 AI 写皮肤，写完拖回本网页即可 60fps 试驾验证", 960, 374, {
        size: 19, color: "#94a3b8", align: "center"
      });
    }
  }

  // AI 如需一次生成第 2 套、第 3 套皮肤，直接在上方大括号后加逗号并继续添加同结构对象即可：
  // , { id: "my_custom_2", name: "我的自定义皮肤 2", ..., render: function(ctx, bus, state) { ... } }
];
