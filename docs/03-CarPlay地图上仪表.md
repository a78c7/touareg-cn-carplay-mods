# 03 CarPlay 地图上仪表

[← 目录](README.md)

目标：手机导航 App 的地图画面显示在仪表中间（原车导航地图的位置），断开 CarPlay 自动回到原车地图。对应 U 盘包里的 `VW_HUD_3_CarPlayCluster`。

整体链路：

```
iPhone ──第二路视频 stream 111（加密 H.264）──▶ cl_probe.so（钩在 dio_manager 里：声明仪表屏、收流、解密）
        ──共享内存 /cluster_h264_shm──▶ cl_render（NvMedia 解码，画到 QNX screen 窗口 58）
        ──displaymanager──▶ 仪表（display 4，列表 [58, 33]）
clusterd：有流就拉起 cl_render、通知 Java 插件切仪表列表；断流就停掉并恢复 [33]
```

## 1. 原车仪表地图是怎么来的

- `displaymanager.json`：display 0 = 中控（LVDS1/HDMI1，1920x1020），display 4 = 仪表（LVDS2/INTERNAL2，1920x722），display 5 = HUD（MOST）。
- 原车导航进程有一个 QNX screen 窗口，id string = **"33"**（1920x639，GLES2），displaymanager 把它合成到仪表。和保时捷 PCM5 一样。**【MH2p 通用，编号在途锐上验证】**
- 实测：断开 CarPlay、仪表在原车地图页时，往 33 号窗口画东西会出现在仪表中间。
- 车机有 `libnvmedia.so`（H.264 硬解）、libscreen、EGL/GLESv2，够用。

## 2. 让 iPhone 发第二路视频

原厂 `dio_manager.json` 只声明了一块 CarPlay 屏，没有第二路视频，要靠钩子加。本车 libairplay（320.17.1）的相关函数都是导出符号、库内调用走 PLT，所以 LD_PRELOAD 能拦截。钩子 `cl_probe.so`（[`src/cluster-probe`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/src/cluster-probe)）和 dio_cluster.so 一起注入：

- `AirPlayCopyServerInfo`：在 `/info` 的 `displays` 数组里加第二块屏（uuid 不同的仪表屏），`extendedFeatures` 加 `altScreen`、`viewAreas`。
- `AirPlayReceiverSessionSetup`：control SETUP 回复里加 `enabledFeatures: [altScreen, viewAreas]`；收到 type 111 的媒体 SETUP（uuid 是我们的仪表屏）时自己开端口接收。
- 本车没有 luka-dev 方案里用的 `AirPlayReceiverSessionScreen_CopyDisplaysInfo`，要绕开；`SendCommand` 是 4 个参数 `(session, cmd, completion, ctx)`。
- 收到第一帧关键帧后发 `showUI`、`setNightMode`、`updateViewArea`。
- 坑：**车机 pf 防火墙**在 carplay0/bcm0 上只放行固定 TCP 端口（5000-5001、5010、6000-6001、6030、6100、6200、7000-7001、7100），随便开的端口收不到连接。用 **6200**。**【途锐验证，MH2p 大概率相同】**
- 结果：iPhone 连上后发 op=1（明文 avcC）、op=0（加密视频帧，约 20 fps，空闲地图约 100 KB/s）、op=5（每秒一次）。高德、苹果地图、百度地图都支持，不是只有苹果地图。

## 3. 解密：不是 AES，是 ChaCha20-Poly1305

本车 libairplay（HomeKit 配对）屏幕流的加密：**【MH2p 通用（同版本 libairplay）】**

- 配对会话 = `*(session + 0x3E4)`（由 `AirPlayReceiverSessionSetHomeKitSecurityContext` 写入）。`session + 0x1F8` 是旧的 AES 密钥，没用。
- 密钥 = `PairingSessionDeriveKey(pairing, "DataStream-Salt<connectionID 十进制>", "DataStream-Output-Encryption-Key", 32)`，用的是车机自己的配对会话，不是破解。
- 每个 op=0 包：nonce = 8 字节小端计数器（从 0 开始每帧加 1），AAD = 整个 128 字节包头，正文 = 密文 + 16 字节 tag。
- op=1 是明文 avcC（SPS/PPS，NAL 长度 4 字节）。解出来是 H.264 High@3.2，1920x640，约 20 fps，关键帧很少（38 秒只有 2 个）。
- luka-dev（MHI2Q）代码用的是旧版 AES-CTR，在这台车上不适用。
- 实时解密用自己写的 C 实现，先和 pycryptodome 逐字节比对，录的样本 771/771 帧校验通过。

## 4. 解码显示

用 fifthBro 公开的 `cluster.c`（CC BY-NC-SA 4.0）做 H.264 解码 + 显示（[`src/cl_render`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/src/cl_render)），改了几处：

- 花屏：`cb_alloc_picture` 总是选第一个空闲 surface，dpb=1 时新帧覆盖参考帧 → 改为轮转分配。
- 闪烁/不出图：默认的 NvMedia Mixer 在本车 `MixerRender` 直接返回错误 → 编译 `-DUSE_SCREEN_BLIT`（`screen_blit` 做 YUV→RGB）。
- 窗口编号可配置（环境变量 `CL_DISPID`），见第 6 节。
- 窗口状态显示 INVISIBLE，但内容照样被合成到仪表。

## 5. "手机一直不发画面"：权限坑

- 现象：只收到大量 op=1（每秒几百条），没有视频帧。
- 原因：调试用的触发文件是 root 建的，钩子在 dio_manager 里已降权为 uid 1037 删不掉，于是每帧都发一次 `forceKeyFrame`，手机不停重置编码器。`forceKeyFrame` 本身是有效的：发一次就来一个关键帧。
- 解决：命令文件改为**按修改时间触发、从不删除**。

## 6. 断开 CarPlay 后仪表卡在最后一帧（最难的一步）

- 一开始我们的窗口也叫 "33"。displaymanager 把 33 绑定到**最后创建**的窗口，原车导航的 33 被挤掉；我们退出后原车窗口不会重新绑定 → 仪表停在最后一帧，只有按 VIEW 才恢复。fifthBro 代码注释也提到这个问题，他的做法是断开后放 logo 动画一直占着 33。
- 只换成别的编号单独显示：仪表报"加载 CarPlay 失败"。用 `dmdt ts 4` 截图证明车机输出给仪表的画面是正常的，是原车/仪表判断"33 不在显示列表里"。
- **最终方案**：我们用编号 **58**（`DISPLAYABLE_GOOGLE_EARTH_KOMBI_MAP_VIEW`，国内用不到），仪表显示列表设为 `[58, 33]`——**列表第一个在最上层**，原车 33 一直垫在下面"在场"，仪表就不报错；断开时设回 `[33]`，原车地图直接露出来。**【编号途锐验证】**
- 原车按 VIEW 时的流程（反编译 lvdskombi `SwitchToPermanentMapSequenceLVDS` + `ChangeDataRateSequence`）：`showDisplayables(4, [33])` → 地图设可见 → `setUpdateRate(4, 10)`（不可见时为 1）。断开时我们照做一遍。
- 实现：`clusterd`（[`src/clusterd`](../src/clusterd)）看共享内存里有没有流：有流就启动 cl_render，并通过 Java 插件把仪表设为 `[58,33]`，每 3 秒重申一次（防止原车切换把它盖掉）；断流 3 秒后停掉并恢复 `[33]`。Java 插件通过 OSGi 服务 **`de.vw.mib.display.DisplayManagementService`** 执行 `showDisplayables`/`setUpdateRate` 切屏（`ServiceManager.displayManager` 仅用于只读查询当前活动列表）。

## 7. 冷启动后要断开重连几次才投上仪表

- 现象：过一晚第一次开车，要断开重连 2～3 次仪表才出地图；接着用就正常。
- 原因在 Java 插件切仪表列表的线程：
  1. 线程启动时记下请求文件的修改时间当作"已处理"，然后先睡 40 秒。CarPlay 无线连接往往在开机 30～40 秒就完成，`clusterd` 在插件启动前写好的请求被当成旧请求跳过。
  2. QNX 文件修改时间精度是 1 秒，断开/重连在同一秒内写两次请求，后一次被漏掉。
- 修复：`clusterd` 每次请求末尾加 `;#<时间>-<计数>`（插件忽略 `#` 开头的段），插件改为比较文件**内容**，5 秒后就开始工作，服务没就绪时下一秒重试。实测：开机第 30 秒 CarPlay 连上，第 31 秒仪表出图。**【MH2p 通用】**

## 8. 夜间模式

- 车机切换日夜时调用 `AirPlayReceiverSessionSetNightMode`，它只对主屏生效。钩子同时给仪表屏发一条带 uuid 的 `setNightMode`；连接时按 `/info` 里的 nightMode 初始化。

## 9. 车标位置：safeArea / viewAreas

- 仪表地图是手机 App 自己画的，车机能影响的只有"安全区"：`/info` 里仪表屏的 `viewAreas` 数组（每项 = 整个可见区域 + 一个 `safeArea` 子矩形），运行中用 `updateViewArea {uuid, viewAreaIndex}` 切换（iPhone 直接执行，毫秒级生效）。**可见区域必须保持整屏**，只改 safeArea。
- 三个 App 对安全区的理解完全不同（截图测量）：
  - **苹果地图**：车标严格在安全区中心（导航和巡航都是），最好控制。
  - **高德巡航**：车标在安全区中心。**高德导航**：横向在安全区中心，纵向固定在整个画面约 85% 高度、不受安全区影响；并且会随**目的地方向**向左或向右偏约 213 像素（见 [R5 的实测记录](reference/R5-CarPlay与iAP2.md)）。
  - **百度导航**：车标在安全区中心右边约 165 像素；**百度巡航**：完全不理安全区。
- 坑：safeArea 设得太低太窄，苹果地图会把车标放到仪表底部的原车信息栏后面看不见。

## 10. 教训：安全区越界会让 iPhone 拒绝整个 CarPlay

- 有一组测试安全区 `y+h=966 > 960`，结果 CarPlay 会话在 `/info` 之后几十毫秒就被拆掉，中控 CarPlay 也连不上，手机连续失败几次后会停一阵不再自动重连。
- 修复：钩子生成 `/info` 时把每个安全区钳在画面内。**【MH2p 通用】**

## 11. 高屏裁切：让车标更低

- 高德导航时车标纵向只认画面高度，安全区压不下去。办法：`/info` 里告诉手机仪表是 **1920×960**（物理高度按比例放大到 146 mm，保持像素密度不变，地图比例尺不变），`cl_render` 只显示其中 640 行（`croptop=230` → 第 230～869 行）。手机照常把车放在 960 高画面的约 85% 处，落到仪表上就靠近底部。
- `cl_render` 改动：解码尺寸大于输出高度时，窗口按输出尺寸建，`screen_blit` 源矩形取 `(0, croptop, 1920, 639)` 一比一拷贝。
- `croptop` 改了只要重启 `cl_render`；`tall` 和安全区列表改了要重连 CarPlay（只在 `/info` 里发一次）。

## 12. 按 App 自动切换安全区

- App 名来自状态文件的 `source_name`（见 [02 篇](02-CarPlay导航上HUD.md) 第 9 节），只在开始导航时发一次，重连后是空的 → `clusterd` 记住最后一次见到的名字。
- `cl.conf` 规则：`rule=<App 名的一部分>,<1 导航中|0 未导航|*>,<安全区编号>`，从上往下第一条匹配生效；`turn=<App>,<左转用的编号>,<右转用的编号>` 在导航中按下一个动作的方向优先匹配。`clusterd` 每秒读一次状态，变化时下发切换命令。
- 早期尝试按下一个路口的转向补偿高德车标偏移。后来实测发现偏移随目的地方向变化，现改为从仪表画面识别车标并平移。

## 13. 对准仪表时钟：用照片定位

- 时钟、到达时间框、挡位等是**仪表自己画的**，`dmdt ts 4` 截不到。
- 办法：拍一张导航中的仪表正面照，量出照片里地图左右边缘和时钟中心，按比例换算到 1920 宽画面：本车时钟在 **x≈965**（不是画面中心 960）。纵向目标约第 556 行（底部信息栏上方一点）。**【途锐特有】**
- 教训：第一次用另一张照片估成 985，调了几轮结果互相矛盾；后来在**已知设置**下再拍一张、用车标反推才准。参照点一定要用同一张照片里的已知量标定。

## 14. 已知限制

- 仪表画面内容由手机 App 决定。高德的"全览"按钮会把中控和仪表的全览状态对调（高德自身的问题）。
- 高德的 `suggestUI` 不给 url，仪表场景由 App 自绘；车机只能 `showUI` + `changeMapZoomLevel`（以屏幕中心缩放），不能"回到车位置"。
- 高德车标识别偶尔会失败，此时保持当前画面位置。百度巡航时车标位置不受控制。
- 车机重启后要等导航 App 重新开始一次导航、发出名字后才会按 App 切换位置。
- 中控 CarPlay 上苹果/百度导航时"我的位置"偏右，和改装无关（App 自身布局）。
- 仪表两侧表盘可以在仪表设置里隐藏，地图就铺满了。

## 15. 为什么没做 CarPlay Ultra

纸面调研结论：完整 Ultra 在这台车上做不到。主题资产由苹果按车企 MFi 证书和资产 ID 下发、本地渲染器和资产格式不公开、uiSync / vehicleStateProtocol 等新协议本车 libairplay 不支持，而且途锐的表盘由仪表控制器自己画，车机只能往 33/58 号窗口送画面。能做的是"子集"：仪表地图 + 自己叠加的数据（见 [04 篇](04-仪表信息框.md)）。

## 16. v19.7 试验设置：扩大仪表地图视野

- 在 `cl.conf` 中取消 `wide=2688` 和 `view=85` 两行的注释。此设置默认关闭。手机按 2688 像素宽绘制仪表地图，显示时缩放到 85%，前方和两侧能多看到一截路，车标仍由显示程序平移到时钟下方。
- 【途锐实测】2023 款途锐（P2878）使用高德导航，有线和无线 CarPlay 均已验证。
- 刷机保留车机上已有的 `cl.conf`。已安装的车辆要启用这项设置，需在现有配置中手动加入这两行。

## 17. `cl_render` 启动命令、字库重生成与 GLES2 60fps 通道（v22）

- **默认模式（CarPlay H.264 地图 + 信息框/导航卡叠加）**：
  ```sh
  CL_DISPID=58 cl_render capture=h264 xres=1920 yres=639 mode=stretch
  ```
- **独立 60fps OpenGL ES 2.0 动画通道（试验，`gles60.c`，由 `8080` 控制台 `gles60_mode` 切换）**：
  ```sh
  CL_DISPID=58 cl_render capture=gles60 xres=1920 yres=639
  ```
  复用窗口 `58`（仍以 `[58, 33]` 覆盖在原车地图上方），通过 `eglGetProcAddress` 动态解析 `gl*` 符号（不直接链接 `-lGLESv2`），以 `eglSwapInterval(dpy, 1)` 锁死硬件 60fps VSync，并叠加 `dash.c` 预渲染的 `g_dash_pxbuf[0..1]` 文本与刻度纹理。
- **预渲染字库与跨编译适配**：`src/cl_render/dash_font.h` 基于 Noto Sans CJK SC（SIL OFL 1.1）预生成，重新生成命令为 `python3 src/cl_render/gen_dash_font.py > src/cl_render/dash_font.h`；`glchar.h` / `shim.c` / `src/common/aeabi_shim.h` 用于适配 Clang + QNX 6.6 libc 交叉编译。版权署名与 CC BY-NC-SA 4.0 许可声明见 [`LICENSES/THIRD_PARTY_NOTICES.md`](../LICENSES/THIRD_PARTY_NOTICES.md)。
