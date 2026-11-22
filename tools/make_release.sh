#!/bin/sh
# Pack the USB folder + license notices into a release zip AND an offline 8080 wireless OTA .tar.
# usage: sh tools/make_release.sh v22.0
#   -> touareg-cn-carplay-mods-v22.0.zip
#   -> touareg-ota-v22.0.tar
set -e
cd "$(dirname "$0")/.."
V=${1:?usage: make_release.sh <version>}
python3 - "$V" <<'PY'
import hashlib, io, os, sys, tarfile, zipfile

v = sys.argv[1]
out = f'touareg-cn-carplay-mods-{v}.zip'
ota_out = f'touareg-ota-{v}.tar'

quick_guide = f"""途锐 MH2p CarPlay 改装包 {v} — 必看说明与免费开源声明
================================================================

【严禁商用与倒卖（PolyForm Noncommercial 1.0.0 + 附加条款）】
本项目为车主社区免费开源技术研究项目，仅供个人非商业学习与使用。
- 严禁出售本压缩包、程序本体、付费代刷，或将其捆绑在任何付费商品/会员群中。
- 如果您是花钱买到本压缩包或付费代刷的，请立即申请退款并举报倒卖者！
官方免费仓库与完整源码：https://github.com/a78c7/touareg-cn-carplay-mods

【4 步首次 U 盘安装（注意：第 3 步必做！）】
1. 准备 32GB 以内 U 盘，格式化为 FAT32（Mac：磁盘工具 -> 抹掉 -> MS-DOS (FAT)）。
2. 把本压缩包内的全部文件和文件夹直接解压到 U 盘根目录（确保 Data、Meta、Mods 在第一层）。
3. 【必做】双击 U 盘根目录的「改装设置（Windows）.exe」或「改装设置（Mac）.command」：
   - 所有模块默认在「未选模块」中未开启！请先在网页里勾选要安装/卸载的功能（可直接点快捷选装），再点「保存到 U 盘」。
   - 不运行「改装设置」直接插车会空刷，不会安装任何模块。
4. 安全弹出 U 盘 -> 发动车辆 -> 插入扶手箱 USB 口，等待自动刷机重启几次（期间切勿操作车机），提示移除介质后拔出即可。

【v22.0+ 离线无线升级 (OTA)】
若车机已安装 v22.0+「手机无线控制台（VW_WiFi_Dev）」，以后升级无需再插拔 U 盘：
手机下载 Release 中的 touareg-ota-<版本>.tar，连接车机打开 http://touareg.local:8080 -> 开关运维 -> 离线无线升级，自动校验 SHA-256 并备份旧版（支持一键回滚）。

【许可证】
本包内附 LICENSE、LICENSE.ADDITIONAL-TERMS.md 与 LICENSES/ 目录；详细适用边界与免责声明见仓库 README.md。
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
    for lic in ('LICENSE', 'LICENSE.ADDITIONAL-TERMS.md'):
        if os.path.isfile(lic):
            add_file(z, lic, lic, 0o644)
    if os.path.isdir('LICENSES'):
        for f in sorted(os.listdir('LICENSES')):
            p = os.path.join('LICENSES', f)
            if os.path.isfile(p) and not f.startswith('.'):
                add_file(z, p, os.path.join('LICENSES', f), 0o644)
    add_bytes(z, '必看-使用说明与免费声明.txt', quick_guide.encode('utf-8'), 0o644)

# Build allowlisted POSIX ustar offline OTA .tar (matches src/touareg_netd/ota_util.h)
def find_mod_file(mod, rel):
    for base in ('USB/未选模块', 'USB/Mods'):
        p = os.path.join(base, mod, rel)
        if os.path.isfile(p):
            return p
    raise FileNotFoundError(f'OTA component missing: {mod}/{rel}')

ota_specs = [
    ('cl_render',           find_mod_file('VW_HUD_3_CarPlayCluster', 'Update/cl_render'), 0o755),
    ('clusterd',            find_mod_file('VW_HUD_3_CarPlayCluster', 'Update/clusterd'), 0o755),
    ('cl_probe.so',         find_mod_file('VW_HUD_3_CarPlayCluster', 'Update/cl_probe.so'), 0o755),
    ('touareg_netd',        find_mod_file('VW_WiFi_Dev', 'Update/touareg_netd'), 0o755),
    ('VWClusterNav_v6.jar', find_mod_file('VW_HUD_2_ClusterNav', 'Update/VWClusterNav_v6.jar'), 0o644),
    ('VWPopupFilter.jar',   find_mod_file('VW_NoBtAuthPopup', 'Update/VWPopupFilter.jar'), 0o644),
]

manifest_lines = []
with tarfile.open(ota_out, 'w', format=tarfile.USTAR_FORMAT) as tf:
    for arcname, srcpath, mode in ota_specs:
        with open(srcpath, 'rb') as fh:
            data = fh.read()
        h = hashlib.sha256(data).hexdigest()
        manifest_lines.append(f'{h}  {arcname}\n')
        ti = tarfile.TarInfo(name=arcname)
        ti.size = len(data)
        ti.mode = mode
        ti.uid = 0; ti.gid = 0; ti.uname = 'root'; ti.gname = 'root'
        ti.mtime = 1700000000
        tf.addfile(ti, io.BytesIO(data))
    ver_bytes = (v + '\n').encode('utf-8')
    ti_v = tarfile.TarInfo(name='version.txt')
    ti_v.size = len(ver_bytes); ti_v.mode = 0o644
    ti_v.uid = 0; ti_v.gid = 0; ti_v.uname = 'root'; ti_v.gname = 'root'; ti_v.mtime = 1700000000
    tf.addfile(ti_v, io.BytesIO(ver_bytes))

    man_bytes = ''.join(manifest_lines).encode('utf-8')
    ti_m = tarfile.TarInfo(name='manifest.sha256')
    ti_m.size = len(man_bytes); ti_m.mode = 0o644
    ti_m.uid = 0; ti_m.gid = 0; ti_m.uname = 'root'; ti_m.gname = 'root'; ti_m.mtime = 1700000000
    tf.addfile(ti_m, io.BytesIO(man_bytes))

print(out, os.path.getsize(out))
print(ota_out, os.path.getsize(ota_out))
PY
