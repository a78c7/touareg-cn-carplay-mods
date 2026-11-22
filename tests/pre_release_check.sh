#!/bin/sh
# One-command pre-release verification script (mirrors .github/workflows/ci.yml locally).
# Runs: Go vet + Go race tests, C unit tests (ASan + UBSan), cppcheck,
#       43-item telemetry alignment / DOM ID / USB script / web_ui sync checks, and shell syntax checks.
set -e
cd "$(dirname "$0")/.."
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

echo "=== [1/5] Go vet & unit tests (-count=1 -race) ==="
(cd tools/configurator && go vet ./... && go test -count=1 -race -v ./...)

echo "=== [2/5] C unit tests (ASan + UBSan) ==="
sh tests/run_c_tests.sh

echo "=== [3/5] Static analysis (cppcheck) ==="
cppcheck --enable=warning,performance,portability \
  --suppress=missingIncludeSystem --inline-suppr --error-exitcode=1 \
  -I src/common -I src/touareg_netd -I src/cluster-probe/wired \
  src/touareg_netd/touareg_netd.c \
  src/clusterd/clusterd.c \
  src/cluster-probe/wired/cl_wired_rg.c \
  src/vhpatch/vhpatch.c

echo "=== [4/5] Telemetry alignment, HTML DOM, USB script safety & repo hygiene ==="
python3 - <<'PY'
import os, re, sys

def read(p):
    with open(p, encoding='utf-8') as f:
        return f.read()

errors = []
c_blk = re.search(r'g_items\[IT_N\] = \{(.*?)\n\};', read('src/cl_render/dash.c'), re.S).group(1)
c_items = dict(re.findall(r'\{\s*"([a-z0-9]+)"\s*,\s*"([^"]+)"', c_blk))
go_blk = re.search(r'var catalogue = \[\]item\{(.*?)\n\}', read('tools/configurator/package.go'), re.S).group(1)
go_items = dict(re.findall(r'\{"([a-z0-9]+)"\s*,\s*"([^"]+)"', go_blk))
w_blk = re.search(r'const ITEMS=\[(.*?)\];', read('src/touareg_netd/web_ui.html'), re.S).group(1)
w_keys = set(re.findall(r'\["([a-z0-9]+)"', w_blk))
if c_items != go_items:
    errors.append(f'dash.c vs package.go mismatch: {set(c_items.items()) ^ set(go_items.items())}')
if set(c_items.keys()) != w_keys:
    errors.append(f'dash.c vs web_ui.html mismatch: {set(c_items.keys()) ^ w_keys}')

for path in ['tools/configurator/web/index.html', 'src/touareg_netd/web_ui.html']:
    html = read(path)
    ids_def = set(re.findall(r'\bid="([^"]+)"', html))
    bare_dollar = set(re.findall(r"(?<!if \()\$\('([^']+)'\)\.", html))
    bare_gebi = set(re.findall(r"document\.getElementById\('([^']+)'\)\.", html))
    miss = (bare_dollar | bare_gebi) - ids_def
    if miss:
        errors.append(f'{path}: references missing DOM IDs {sorted(miss)}')

for root, _, fs in os.walk('USB'):
    for f in fs:
        if not f.endswith('.sh'):
            continue
        p = os.path.join(root, f)
        raw = open(p, 'rb').read()
        if b'\r\n' in raw:
            errors.append(f'{p}: contains CRLF line endings')
        txt = raw.decode('utf-8', errors='replace')
        if '/Update/' in p.replace('\\', '/'):
            for ln, line in enumerate(txt.splitlines(), 1):
                s = line.strip()
                if s.startswith('#'):
                    continue
                if re.search(r'\bmount\s+-ur\s+/mnt/app\b', s):
                    errors.append(f'{p}:{ln}: Update script must NOT remount /mnt/app read-only')

forbidden_paths = ['/' + 'Users/', '.gemini/' + 'antigravity', '/home/admin/' + 'qnx-build']
for folder in ['src', 'tools', 'docs', 'tests']:
    for root, _, fs in os.walk(folder):
        for f in fs:
            if not f.endswith(('.sh', '.py', '.go', '.c', '.h', '.java', '.md', '.html')):
                continue
            p = os.path.join(root, f)
            txt = read(p)
            if any(bad in txt for bad in forbidden_paths):
                errors.append(f'{p}: contains hardcoded personal machine path')

for forbidden in ['USB/改装设置（Mac）.command', 'USB/改装设置（Windows）.exe']:
    if os.path.exists(forbidden):
        errors.append(f'{forbidden}: configurator release binary must NOT remain in working tree / git')

h_txt = read('src/touareg_netd/web_ui_data.h')
m = re.search(r'WEB_UI_HTML_LEN\s*=\s*(\d+)', h_txt)
actual_len = os.path.getsize('src/touareg_netd/web_ui.html')
if not m or int(m.group(1)) != actual_len:
    errors.append(f'web_ui_data.h out of sync (header={m.group(1) if m else None}, html={actual_len})')
if not os.path.isfile('LICENSE.ADDITIONAL-TERMS.md'):
    errors.append('missing LICENSE.ADDITIONAL-TERMS.md')

if errors:
    print('PRE-RELEASE CHECK FAILED:')
    for e in errors:
        print('  -', e)
    sys.exit(1)
print(f'ALL PYTHON CHECKS PASSED ({len(c_items)} telemetry items, DOM IDs, USB scripts, web_ui sync, repo hygiene)')
PY

echo "=== [5/5] POSIX / Bash shell syntax checks ==="
sh -n src/build.sh
sh -n src/hud-java/build.sh
sh -n src/popup-filter-java/build.sh
sh -n tools/configurator/build.sh
sh -n tools/make_release.sh
sh -n tests/run_c_tests.sh
sh -n tests/pre_release_check.sh
bash -n tools/deploy_eth.sh

echo "ALL PRE-RELEASE CHECKS PASSED!"
