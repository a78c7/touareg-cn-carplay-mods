#!/bin/sh
# Build 「改装设置」 for Windows (x64) and macOS (universal: Intel + Apple Silicon) into the USB package root.
# Needs Go 1.22+ and lipo / llvm-lipo. usage: sh build.sh v22.01
set -e
cd "$(dirname "$0")"
V=${1:?usage: build.sh <version>}

# 1) 校验 43 项指标清单与 dash.c / web_ui.html 一致（含 t100），并检查前端 DOM ID 完整性
python3 - <<'PY'
import re, sys

def read(p):
    with open(p, encoding='utf-8') as f:
        return f.read()

c_txt = read('../../src/cl_render/dash.c')
c_blk = re.search(r'g_items\[IT_N\] = \{(.*?)\n\};', c_txt, re.S).group(1)
c_items = dict(re.findall(r'\{\s*"([a-z0-9]+)"\s*,\s*"([^"]+)"', c_blk))

go_txt = read('package.go')
go_blk = re.search(r'var catalogue = \[\]item\{(.*?)\n\}', go_txt, re.S).group(1)
go_items = dict(re.findall(r'\{"([a-z0-9]+)"\s*,\s*"([^"]+)"', go_blk))

w_txt = read('../../src/touareg_netd/web_ui.html')
w_blk = re.search(r'const ITEMS=\[(.*?)\];', w_txt, re.S).group(1)
w_keys = set(re.findall(r'\["([a-z0-9]+)"', w_blk))

if c_items != go_items:
    print('ERROR: package.go catalogue differs from dash.c:', set(c_items.items()) ^ set(go_items.items()))
    sys.exit(1)
if set(c_items.keys()) != w_keys:
    print('ERROR: web_ui.html ITEMS keys differ from dash.c:', set(c_items.keys()) ^ w_keys)
    sys.exit(1)

# 检查 configurator 前端所有 $('id') 是否都在 HTML 中声明
html = read('web/index.html')
ids_def = set(re.findall(r'\bid="([^"]+)"', html))
ids_used = set(re.findall(r"\$\('([^']+)'\)", html))
# 允许 if ($('...')) 防御性判断，拒绝裸 $('...').xxx 访问缺失 ID
bare_used = set(re.findall(r"(?<!if \()\$\('([^']+)'\)\.", html))
missing = bare_used - ids_def
if missing:
    print('ERROR: web/index.html references missing DOM IDs:', missing)
    sys.exit(1)
print(f'catalogue verified: {len(go_items)} items match dash.c & web_ui.html; web/index.html DOM IDs OK')
PY

# 2) 运行 Go 单元测试
go test ./...

# 3) 交叉编译 Windows x64 + macOS Universal
mkdir -p out
LD="-s -w -buildid= -X main.version=$V"
CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -trimpath -ldflags "$LD" -o out/win_amd64.exe .
CGO_ENABLED=0 GOOS=darwin GOARCH=amd64 go build -trimpath -ldflags "$LD" -o out/mac_amd64 .
CGO_ENABLED=0 GOOS=darwin GOARCH=arm64 go build -trimpath -ldflags "$LD" -o out/mac_arm64 .
LIPO=$(command -v lipo || command -v llvm-lipo-18 || command -v llvm-lipo-14 || command -v llvm-lipo || true)
: "${LIPO:?need lipo or llvm-lipo}"
"$LIPO" -create -output out/mac_universal out/mac_amd64 out/mac_arm64
cp out/win_amd64.exe "../../USB/改装设置（Windows）.exe"
cp out/mac_universal "../../USB/改装设置（Mac）.command"
chmod 755 "../../USB/改装设置（Mac）.command"
ls -l ../../USB/改装设置*
