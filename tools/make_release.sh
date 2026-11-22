#!/bin/sh
# Pack the USB folder + license notices into a release zip, an Ed25519-signed offline OTA .tar,
# and SHA256SUMS.txt.
#
# usage:
#   OTA_SIGNING_KEY_FILE=~/.config/touareg-ota-signing/ota_signing_key.pem sh tools/make_release.sh v22.02
#   OTA_SIGNING_KEY_PEM="..." sh tools/make_release.sh v22.02
#   sh tools/make_release.sh v22.02 --unsigned-dev   (local dev testing only; rejected on head unit unless allow_unsigned_ota exists)
#
# outputs:
#   -> touareg-cn-carplay-mods-<version>.zip
#   -> touareg-ota-<version>.tar
#   -> SHA256SUMS.txt
set -e
cd "$(dirname "$0")/.."
V=${1:?usage: make_release.sh <version> [--unsigned-dev]}
MODE=${2:-}
python3 - "$V" "$MODE" <<'PY'
import hashlib, io, os, re, shutil, subprocess, sys, tarfile, tempfile, zipfile

v = sys.argv[1]
mode = sys.argv[2]
allow_unsigned = (mode == '--unsigned-dev')
if mode and not allow_unsigned:
    sys.exit(f'unknown flag: {mode} (only --unsigned-dev is supported)')

out = f'touareg-cn-carplay-mods-{v}.zip'
ota_out = f'touareg-ota-{v}.tar'
sums_out = 'SHA256SUMS.txt'

# Ensure configurator binaries built from source (excluded from git) are present before zipping
req_bins = [
    'USB/改装设置（Mac）.command',
    'USB/改装设置（Windows）.exe',
]
missing_bins = [p for p in req_bins if not os.path.isfile(p)]
if missing_bins:
    sys.exit(
        'ERROR: missing configurator binaries: ' + ', '.join(missing_bins) +
        f'\nRun `sh tools/configurator/build.sh {v}` first.'
    )

def find_openssl3():
    cands = [
        '/opt/homebrew/bin/openssl',
        '/usr/local/bin/openssl',
        shutil.which('openssl') or '',
    ]
    for c in cands:
        if not c or not os.path.isfile(c):
            continue
        try:
            ver = subprocess.check_output([c, 'version'], text=True, stderr=subprocess.STDOUT)
            if ver.startswith('OpenSSL 3.') or ver.startswith('OpenSSL 1.1.1'):
                return c
        except Exception:
            pass
    return None

def load_official_pubkey_hex():
    txt = open('src/touareg_netd/ota_pubkey.h', encoding='utf-8').read()
    m = re.search(r'OTA_OFFICIAL_PUBKEY\[32\]\s*=\s*\{(.*?)\};', txt, re.S)
    if not m:
        sys.exit('ERROR: failed to parse OTA_OFFICIAL_PUBKEY from src/touareg_netd/ota_pubkey.h')
    bs = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1))]
    if len(bs) != 32:
        sys.exit(f'ERROR: expected 32 bytes in OTA_OFFICIAL_PUBKEY, got {len(bs)}')
    return bytes(bs)

def sign_manifest_ed25519(man_bytes: bytes) -> bytes:
    key_file = os.environ.get('OTA_SIGNING_KEY_FILE', '').strip()
    key_pem = os.environ.get('OTA_SIGNING_KEY_PEM', '').strip()
    if not key_file and not key_pem:
        sys.exit(
            'ERROR: OTA signing key required for release builds.\n'
            'Set OTA_SIGNING_KEY_FILE=/path/to/ota_signing_key.pem or OTA_SIGNING_KEY_PEM (GitHub Secret: OTA_SIGNING_KEY),\n'
            'or pass --unsigned-dev for local unsigned developer packages.'
        )
    ossl = find_openssl3()
    if not ossl:
        sys.exit('ERROR: OpenSSL 3.x (or 1.1.1+) with Ed25519 support is required to sign OTA packages.')

    with tempfile.TemporaryDirectory() as td:
        if key_file:
            if not os.path.isfile(key_file):
                sys.exit(f'ERROR: OTA_SIGNING_KEY_FILE does not exist: {key_file}')
            kp = key_file
        else:
            kp = os.path.join(td, 'key.pem')
            fd = os.open(kp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as f:
                f.write(key_pem if key_pem.endswith('\n') else key_pem + '\n')

        # Verify signing key matches embedded OTA_OFFICIAL_PUBKEY to prevent mismatched release keys
        pub_der = subprocess.check_output([ossl, 'pkey', '-in', kp, '-pubout', '-outform', 'DER'])
        spki_prefix = bytes.fromhex('302a300506032b6570032100')
        if len(pub_der) != 44 or not pub_der.startswith(spki_prefix):
            sys.exit('ERROR: OTA signing key is not a valid Ed25519 private key')
        pub_raw = pub_der[12:]
        expected_pub = load_official_pubkey_hex()
        if pub_raw != expected_pub:
            sys.exit(
                'ERROR: OTA signing key public half does NOT match src/touareg_netd/ota_pubkey.h!\n'
                f'  key pubkey:      {pub_raw.hex()}\n'
                f'  expected pubkey: {expected_pub.hex()}'
            )

        man_path = os.path.join(td, 'manifest.sha256')
        sig_path = os.path.join(td, 'manifest.sha256.sig')
        with open(man_path, 'wb') as f:
            f.write(man_bytes)
        subprocess.check_call([ossl, 'pkeyutl', '-sign', '-inkey', kp, '-rawin', '-in', man_path, '-out', sig_path])
        sig_raw = open(sig_path, 'rb').read()
        if len(sig_raw) != 64:
            sys.exit(f'ERROR: unexpected Ed25519 signature length: {len(sig_raw)}')
        return (sig_raw.hex() + '\n').encode('ascii')

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
4. 点击界面里的「一键安全弹出 U 盘并退出」（或手动安全弹出 U 盘）后再拔出 -> 发动车辆 -> 插入扶手箱 USB 口，等待自动刷机重启几次（期间切勿操作车机），提示移除介质后拔出即可。

【v23 60fps AI 自定义皮肤（Beta 尝鲜 · 可选，无需连 8080）】
- 普通车主最省心流程：在「改装设置」里一站式勾选 v23 60fps 尝鲜开关（默认关闭，不勾稳跑 v22.1）-> 下载空白模板 my_theme.js 发给 AI 写皮肤 -> 打开内置 1:1 浏览器试驾模拟器预览 -> 导入写好的 my_theme.js -> 点「保存到 U 盘」并点「一键安全弹出 U 盘」，上车插 U 盘一次写入即生效。
- 进阶玩家直接把 U 盘交给本地 AI（Claude Code / Cursor / Codex / Antigravity 等）文字部署时：U 盘根目录已内置 AGENTS.md / CLAUDE.md 规范，且「改装设置」内置无界面命令行模式（--deploy-theme / --verify / --eject），AI 可自动校验皮肤、补齐依赖、清理隐藏文件并安全弹出；切勿写完未弹出直接热拔 U 盘。
- 本压缩包「v23皮肤模板/」目录也额外附带了独立离线版 preview.html 与 my_theme.js，方便分享给朋友。
- 车机内置三层自动保险丝（坏皮肤当帧 0 黑屏退回默认经典 / 渲染异常秒退回 v22.1 / 3 次崩溃退回原车地图 [33]），无需依赖 8080 救砖。

【离线无线升级 (OTA)（进阶可选）】
- 若车机为 v22.01 及更早版本：首次升级到 v22.1 请使用本 U 盘包覆盖刷入（旧版尚未内置 Ed25519 公钥，会安全拒绝新版签名 .tar 包）；刷入时原有 web.pin 与 cl.conf 自动保留。
- 刷入 v22.1 及以后版本：后续升级无需再插拔 U 盘，手机下载 Release 中的 touareg-ota-<版本>.tar，连接车机打开 http://touareg.local:8080 -> 开关运维 -> 离线无线升级（先校验官方 Ed25519 签名再解包、逐文件 SHA-256 校验，支持自动备份与一键回滚）。

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
    for src_p, arc_p in (
        ('tools/preview/index.html', 'v23皮肤模板/preview.html'),
        ('tools/preview/my_theme.js', 'v23皮肤模板/my_theme.js'),
        ('tools/preview/THEME_API_SPEC.md', 'v23皮肤模板/THEME_API_SPEC.md'),
    ):
        if os.path.isfile(src_p):
            add_file(z, src_p, arc_p, 0o644)
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

ver_bytes = (v + '\n').encode('utf-8')
manifest_lines = []
with tarfile.open(ota_out, 'w', format=tarfile.USTAR_FORMAT) as tf:
    for arcname, srcpath, fmode in ota_specs:
        with open(srcpath, 'rb') as fh:
            data = fh.read()
        h = hashlib.sha256(data).hexdigest()
        manifest_lines.append(f'{h}  {arcname}\n')
        ti = tarfile.TarInfo(name=arcname)
        ti.size = len(data)
        ti.mode = fmode
        ti.uid = 0; ti.gid = 0; ti.uname = 'root'; ti.gname = 'root'
        ti.mtime = 1700000000
        tf.addfile(ti, io.BytesIO(data))

    ver_hash = hashlib.sha256(ver_bytes).hexdigest()
    manifest_lines.append(f'{ver_hash}  version.txt\n')
    ti_v = tarfile.TarInfo(name='version.txt')
    ti_v.size = len(ver_bytes); ti_v.mode = 0o644
    ti_v.uid = 0; ti_v.gid = 0; ti_v.uname = 'root'; ti_v.gname = 'root'; ti_v.mtime = 1700000000
    tf.addfile(ti_v, io.BytesIO(ver_bytes))

    man_bytes = ''.join(manifest_lines).encode('utf-8')
    ti_m = tarfile.TarInfo(name='manifest.sha256')
    ti_m.size = len(man_bytes); ti_m.mode = 0o644
    ti_m.uid = 0; ti_m.gid = 0; ti_m.uname = 'root'; ti_m.gname = 'root'; ti_m.mtime = 1700000000
    tf.addfile(ti_m, io.BytesIO(man_bytes))

    if not allow_unsigned:
        sig_bytes = sign_manifest_ed25519(man_bytes)
        ti_s = tarfile.TarInfo(name='manifest.sha256.sig')
        ti_s.size = len(sig_bytes); ti_s.mode = 0o644
        ti_s.uid = 0; ti_s.gid = 0; ti_s.uname = 'root'; ti_s.gname = 'root'; ti_s.mtime = 1700000000
        tf.addfile(ti_s, io.BytesIO(sig_bytes))

# Generate SHA256SUMS.txt for published Release artifacts
with open(sums_out, 'w', encoding='utf-8') as sf:
    for artifact in (out, ota_out):
        digest = hashlib.sha256(open(artifact, 'rb').read()).hexdigest()
        sf.write(f'{digest}  {artifact}\n')

print(out, os.path.getsize(out))
print(ota_out, os.path.getsize(ota_out), '(Ed25519 signed)' if not allow_unsigned else '(UNSIGNED DEV)')
print(sums_out, os.path.getsize(sums_out))
PY
