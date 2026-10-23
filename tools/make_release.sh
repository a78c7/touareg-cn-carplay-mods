#!/bin/sh
# Pack the USB folder into a release zip (UTF-8 names, executable bit kept for the Mac program).
# usage: sh tools/make_release.sh v17  -> touareg-cn-carplay-mods-v17.zip (upload it as a GitHub release asset)
set -e
cd "$(dirname "$0")/.."
V=${1:?usage: make_release.sh <version>}
python3 - "$V" <<'PY'
import os, sys, zipfile
v = sys.argv[1]; out = f'touareg-cn-carplay-mods-{v}.zip'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk('USB'):
        for f in sorted(fs):
            if f.startswith('._') or f == '.DS_Store':
                continue
            p = os.path.join(d, f); arc = os.path.relpath(p, 'USB')
            zi = zipfile.ZipInfo.from_file(p, arc)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.flag_bits |= 0x800                      # UTF-8 file names
            mode = 0o755 if (f.endswith('.command') or f.endswith('.exe')) else 0o644
            zi.external_attr = (0o100000 | mode) << 16
            with open(p, 'rb') as fh:
                z.writestr(zi, fh.read())
print(out, os.path.getsize(out))
PY
