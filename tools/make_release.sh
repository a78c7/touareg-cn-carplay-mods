#!/bin/sh
# Pack the USB folder + license notices into a release zip (UTF-8 names, executable bits kept).
# usage: sh tools/make_release.sh v21.0.1  -> touareg-cn-carplay-mods-v21.0.1.zip
set -e
cd "$(dirname "$0")/.."
V=${1:?usage: make_release.sh <version>}
python3 - "$V" <<'PY'
import os, sys, zipfile

v = sys.argv[1]
out = f'touareg-cn-carplay-mods-{v}.zip'

quick_guide = f"""途锐 MH2p CarPlay 改装包 {v} — 必看说明与免费开源声明
================================================================

【严禁商用与转售】
本项目为免费开源的个人技术研究项目，仅供个人非商业学习与使用。
如果你是花钱买到这个压缩包或付费代刷的，请立即申请退款并举报倒卖者！
官方免费仓库与完整源码：https://github.com/a78c7/touareg-cn-carplay-mods

【4 步安装（注意：第 3 步必做！）】
1. 准备 32GB 以内 U 盘，格式化为 FAT32（Mac：磁盘工具 -> 抹掉 -> MS-DOS (FAT)）。
2. 把本压缩包内的全部文件和文件夹直接解压到 U 盘根目录（确保 Data、Meta、Mods 在第一层）。
3. 【必做】双击 U 盘根目录的「改装设置（Windows）.exe」或「改装设置（Mac）.command」：
   - 所有模块默认在「未选模块」中未开启！请先在网页里勾选要安装/卸载的功能（可直接点快捷选装），再点「保存到 U 盘」。
   - 不运行「改装设置」直接插车会空刷，不会安装任何模块。
4. 安全弹出 U 盘 -> 发动车辆 -> 插入扶手箱 USB 口，等待自动刷机重启几次（期间切勿操作车机），提示移除介质后拔出即可。

【许可证】
本包内附 LICENSE 与 LICENSES/ 目录；详细适用边界与免责声明见仓库 README.md。
"""

def add_bytes(z, arc, data, mode=0o644):
    zi = zipfile.ZipInfo(arc)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.flag_bits |= 0x800  # UTF-8 file names
    zi.external_attr = (0o100000 | mode) << 16
    z.writestr(zi, data)

def add_file(z, path, arc, mode=None):
    zi = zipfile.ZipInfo.from_file(path, arc)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.flag_bits |= 0x800
    if mode is None:
        base = os.path.basename(path)
        is_exec = base.endswith(('.command', '.exe', '.sh')) or os.access(path, os.X_OK)
        mode = 0o755 if is_exec else 0o644
    zi.external_attr = (0o100000 | mode) << 16
    with open(path, 'rb') as fh:
        z.writestr(zi, fh.read())

with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk('USB'):
        for f in sorted(fs):
            if f.startswith('._') or f == '.DS_Store':
                continue
            p = os.path.join(d, f)
            arc = os.path.relpath(p, 'USB')
            add_file(z, p, arc)
    # 随发布包附带完整许可文本与中文免费声明（GPLv3 / CC BY-NC-SA 合规）
    if os.path.isfile('LICENSE'):
        add_file(z, 'LICENSE', 'LICENSE', 0o644)
    if os.path.isdir('LICENSES'):
        for f in sorted(os.listdir('LICENSES')):
            p = os.path.join('LICENSES', f)
            if os.path.isfile(p) and not f.startswith('.'):
                add_file(z, p, os.path.join('LICENSES', f), 0o644)
    add_bytes(z, '必看-使用说明与免费声明.txt', quick_guide.encode('utf-8'), 0o644)

print(out, os.path.getsize(out))
PY
