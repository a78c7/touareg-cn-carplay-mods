# 05 全屏 CarPlay 与 U 盘包

[← 目录](README.md)

## 1. 全屏 CarPlay 改了什么

- 原厂 CarPlay 画面是带黑边的小窗。要改两处：
  - `skin3/info.txt` 里的 CarPlay 画面尺寸：1152×592 → 1673×748（注意这个文件是 CRLF 换行，用 sed 时不能用 `$` 锚定）。
  - 国行皮肤 `skin3/viewhandler.zip`（1246 个编译后的界面 jxe）里只有 `Am.jxe`、`Cm_0409.jxe` 两个文件要改，大小不变，各差 19 个字节，共 38 个字节。几何数值参考了 [davecozens/touareg-carplay](https://github.com/davecozens/touareg-carplay)（英国右舵途锐）。
- 偏移和数值都是 P2878 国行皮肤专属，别的版本要自己对比原厂和改装文件重新生成补丁表。**【途锐特有】**

## 2. 不分发大众文件：在车上就地打补丁

- 第一版直接分发一份改好的 `viewhandler.zip`（14.6 MB，里面是大众编译后的界面程序），后来又附带一份原厂文件用于卸载。这等于在网上传播大众的程序，不妥，于是改成在车上就地修改。
- 对比原厂和改装版，`mkpatch.py` 把差异写成补丁表（条目名、大小、改前/改后 CRC-32、"偏移 原值 新值"），**只有这些数字进仓库**。
- `vhpatch`（C，zlib 静态编进去，只依赖 libc，[`src/vhpatch`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/src/vhpatch)）：
  - 读车上的 zip，按中央目录核对两个条目的大小和 CRC，判断"原厂 / 已改 / 未知"；未知版本一律不动。
  - 改的时候解压这两个条目、逐字节核对原值、改完核对新 CRC、重新压缩，其余条目原样拷贝，写完再自检一遍才输出；还原同理。
- 安装流程：先把车上原文件复制到 U 盘，改好的文件先写在 U 盘上自检，再拷回车机并再检查一次，失败就用备份恢复。
- 坑：U 盘可能是 noexec 挂载，程序要先复制到 `/tmp` 再运行。
- 卸载：优先用 U 盘里的备份，没有就按同一张补丁表在车上改回去，所以换了 U 盘也能卸载。

## 3. 其它改动都能原地还原

- `smartphone_integrator.json`：去掉我们加的 LD_PRELOAD。
- `bundles.properties`：去掉 97 号插件。
- 无线 CarPlay / Android Auto 的功能授权由 LawPaul 的模块写入，车上有它自己的 `granted.fecs.bak`。

## 4. ModKit 的机制（写模块前要知道）

- U 盘 `Mods/` 下每个模块，刷机时都会跑它的 `Update` 脚本。
- 已装模块的 `Persist` 部分会存到车机 `/mnt/ota/modkit/Mods/`，**每次开机都跑**，与 U 盘无关——开机自启的东西放这里。
- U 盘一直插在车上会每次进入刷机。想只收日志不刷机，可以把 `Data`、`Meta` 改名（如 `_Data`、`_Meta`）。
- Mac 复制到 U 盘会产生 `._*`、`.DS_Store`，车机刷机会被它们卡住，要删掉。

## 5. 「改装设置」程序

- 改配置文件、删文件夹对多数人太麻烦，于是在 U 盘根目录放一个小程序（Go 写的，Windows / Mac 通用版，[`tools/configurator`](https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/tools/configurator)）：双击后在本机 127.0.0.1 起一个只有本次有效口令的网页，选功能（安装 / 卸载 / 不处理）、信息框项目和顺序；保存时把不刷的模块移出 `Mods`（ModKit 只跑 `Mods/*`）、给卸载的模块放 `uninstall.txt`、改写 `dash.conf`（保留注释），并清掉 Mac 的隐藏文件。
- 依赖在程序里强制：装仪表地图必须同时装 HUD（插件版本要配套）；卸载 HUD 必须同时卸载仪表地图。
- 为什么不用纯网页：网页改不了 U 盘上的文件夹（只有 Chrome/Edge 的文件系统接口能做，而且对 U 盘根目录有限制），Safari 用户更做不到；本地程序 + 浏览器界面两个系统都能用，代价是第一次打开有系统安全提示。
