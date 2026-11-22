#!/usr/bin/env bash
# tools/deploy_eth.sh — 免 U 盘直连车机一键部署 v22.01（安全熔断 + 8080 离线 OTA/脱敏诊断 + 10Hz 阻尼平滑/GLES2 60fps）
#
# 支持两种连接方式：
#   1) 默认有线网卡（D-Link DUB-E100）：自动拉起 sudo axbridge (172.16.250.248:2012)
#      注意：插 DUB-E100 时请拔掉有线 CarPlay 数据线（有线 CarPlay 会断 USB 网卡供电；无线 CarPlay 不影响）
#   2) 无线热点直连：./tools/deploy_eth.sh touareg.local /path/to/touareg_ed25519
#
# 可选参数：
#   --no-reboot   仅热更新 cl_render / dash.conf 并秒级刷新画面（不重启车机；升级 Java 插件请勿加此参数）
#   --probe-only  不部署文件，仅连接车机实时读取 /tmp/dash.state 与运行日志
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
USB_DIR="$REPO_DIR/USB"
find_mod() {
    if [ -e "$USB_DIR/Mods/$1" ]; then
        echo "$USB_DIR/Mods/$1"
    else
        echo "$USB_DIR/未选模块/$1"
    fi
}
JAR_SRC="$(find_mod "VW_HUD_2_ClusterNav/Update/VWClusterNav_v6.jar")"
PER_SRC="$(find_mod "VW_HUD_2_ClusterNav/Persist/install.sh")"
PER3_SRC="$(find_mod "VW_HUD_3_CarPlayCluster/Persist/install.sh")"
POP_SRC="$(find_mod "VW_NoBtAuthPopup/Update/VWPopupFilter.jar")"
PRB_SRC="$(find_mod "VW_HUD_3_CarPlayCluster/Update/cl_probe.so")"
CLD_SRC="$(find_mod "VW_HUD_3_CarPlayCluster/Update/clusterd")"
CLR_SRC="$(find_mod "VW_HUD_3_CarPlayCluster/Update/cl_render")"
CONF_SRC="$(find_mod "VW_HUD_3_CarPlayCluster/Update/dash.conf")"
NETD_SRC="$(find_mod "VW_WiFi_Dev/Update/touareg_netd")"

NO_REBOOT=0
PROBE_ONLY=0
RESET_CONF=0
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --no-reboot)  NO_REBOOT=1 ;;
        --probe-only) PROBE_ONLY=1 ;;
        --reset-conf) RESET_CONF=1 ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done

HOST="${POSITIONAL[0]:-172.16.250.248}"
KEY="${POSITIONAL[1]:-}"

if [ -z "$KEY" ]; then
    for cand in \
        "$HOME/Desktop/途锐无线SSH密钥/touareg_ed25519" \
        "$HOME/.ssh/touareg_ed25519" \
        "$HOME/.ssh/mh2p.key" \
        "$HOME/touareg/ssh/mh2p.key"; do
        if [ -f "$cand" ]; then
            KEY="$cand"
            break
        fi
    done
fi

if [ -z "$KEY" ] || [ ! -f "$KEY" ]; then
    echo "❌ 未找到车机 SSH 私钥（请传入私钥路径，或放在 ~/Desktop/途锐无线SSH密钥/touareg_ed25519 / ~/.ssh/touareg_ed25519）。"
    exit 1
fi

AX_BIN=""
for cand in "$HOME/touareg/axbridge/axbridge" "$REPO_DIR/tools/axbridge/axbridge"; do
    if [ -x "$cand" ]; then AX_BIN="$cand"; break; fi
done

STARTED_AX=0
AX_PID=""
cleanup() {
    if [ "$STARTED_AX" = "1" ] && [ -n "$AX_PID" ]; then
        sudo kill "$AX_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

ensure_axbridge() {
    if [ "$HOST" != "172.16.250.248" ]; then return 0; fi
    if nc -z -G 2 "$HOST" 2012 >/dev/null 2>&1; then return 0; fi
    if ! pgrep -x axbridge >/dev/null 2>&1; then
        if [ -z "$AX_BIN" ]; then
            echo "❌ 未找到 axbridge 可执行文件，请先编译 tools/axbridge/axbridge.c"
            exit 1
        fi
        echo "🔌 正在启动 macOS DUB-E100 网卡驱动 (axbridge)，如提示请输入 Mac 开机密码..."
        sudo -v
        sudo "$AX_BIN" >/tmp/axbridge_live.log 2>&1 &
        AX_PID=$!
        STARTED_AX=1
        sleep 2
    fi
}

wait_car_ssh() {
    local timeout_s="${1:-35}"
    local elapsed=0
    ensure_axbridge
    printf "⏳ 等待车机 %s:2012 响应" "$HOST"
    while [ "$elapsed" -lt "$timeout_s" ]; do
        if nc -z -G 2 "$HOST" 2012 >/dev/null 2>&1; then
            echo " 已连通！"
            return 0
        fi
        if [ "$HOST" = "172.16.250.248" ] && ! pgrep -x axbridge >/dev/null 2>&1 && [ -n "$AX_BIN" ]; then
            sudo "$AX_BIN" >/tmp/axbridge_live.log 2>&1 &
            AX_PID=$!
            STARTED_AX=1
        fi
        printf "."
        sleep 2
        elapsed=$((elapsed + 2))
    done
    echo ""
    echo "❌ 超时未连上 $HOST:2012。请检查："
    echo "   1) D-Link DUB-E100 网卡两端是否插紧、车机是否已亮屏通电；"
    echo "   2) 是否拔掉了有线 CarPlay 手机线（有线 CarPlay 会切断 USB 网卡供电，请改用无线 CarPlay）；"
    [ -f /tmp/axbridge_live.log ] && { echo "--- axbridge 日志 ---"; tail -n 10 /tmp/axbridge_live.log; }
    exit 1
}

SSH_OPTS=(
    -i "$KEY" -p 2012
    -o ConnectTimeout=6
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR
    -o HostKeyAlgorithms=+ssh-rsa
    -o PubkeyAcceptedAlgorithms=+ssh-rsa
)
run_hu()  { ssh "${SSH_OPTS[@]}" "root@$HOST" "$@"; }
push_hu() { ssh "${SSH_OPTS[@]}" "root@$HOST" "cat > '$2'" < "$1"; }

show_live_probe() {
    echo ""
    echo "📡 正在读取车机实时遥测状态（采样 6 次，按 Ctrl+C 可随时提前结束）..."
    echo "--------------------------------------------------------------------------------"
    run_hu '/bin/ksh -s' <<'EOF'
: > /tmp/dash_on
chmod 666 /tmp/dash_on 2>/dev/null || true
i=0
while [ $i -lt 6 ]; do
    echo "==================== [车机采样 #$((i + 1))] ===================="
    if [ -f /tmp/dash.state ]; then
        echo "[10Hz 仪表遥测 /tmp/dash.state]"
        cat /tmp/dash.state
    else
        echo "[10Hz 仪表遥测] 等待 vwcn-dash 就绪（开机后约 8~12 秒）..."
    fi
    if [ -f /tmp/vwcn.log ]; then
        echo "[VWClusterNav 最新日志]"
        tail -n 6 /tmp/vwcn.log
    fi
    if [ -f /tmp/cl_render_dbg ]; then
        echo "[cl_render 布局调试 /tmp/cl_render_dbg]"
        tail -n 4 /tmp/cl_render_dbg
    fi
    i=$((i + 1))
    [ $i -lt 6 ] && sleep 3
done
EOF
}

wait_car_ssh 35

if [ "$PROBE_ONLY" = "1" ]; then
    show_live_probe
    exit 0
fi

for f in "$JAR_SRC" "$PER_SRC" "$PRB_SRC" "$CLD_SRC" "$CLR_SRC" "$CONF_SRC" "$NETD_SRC"; do
    if [ ! -f "$f" ]; then
        echo "❌ 本地缺少编译产物：$f"
        exit 1
    fi
done

JAR_SZ="$(wc -c < "$JAR_SRC" | tr -d ' ')"
PRB_SZ="$(wc -c < "$PRB_SRC" | tr -d ' ')"
CLD_SZ="$(wc -c < "$CLD_SRC" | tr -d ' ')"
CLR_SZ="$(wc -c < "$CLR_SRC" | tr -d ' ')"
PER_SZ="$(wc -c < "$PER_SRC" | tr -d ' ')"
NETD_SZ="$(wc -c < "$NETD_SRC" | tr -d ' ')"

# 默认以仓库内标准 dash.conf 作为基准（部署时若车机已有 dash.conf，默认保留车上原有配置；加 --reset-conf 可覆盖为模板配置）
TMP_CONF="/tmp/dash_eth_v21.conf"
cp -f "$CONF_SRC" "$TMP_CONF"
CONF_SZ="$(wc -c < "$TMP_CONF" | tr -d ' ')"

echo ">>> [1/4] 连接车机校验环境并备份原有文件..."
PER_DST="$(run_hu 'if [ -d /mnt/ota/modkit/Mods/VW_HUD_2_ClusterNav/Persist ]; then echo /mnt/ota/modkit/Mods/VW_HUD_2_ClusterNav/Persist/install.sh; elif [ -d /mnt/ota/modkit/Mods/VW_ClusterNav/Persist ]; then echo /mnt/ota/modkit/Mods/VW_ClusterNav/Persist/install.sh; else echo /mnt/ota/modkit/Mods/VW_HUD_2_ClusterNav/Persist/install.sh; fi')"
echo "    Persist 目标路径: $PER_DST"
run_hu "PER_DST='$PER_DST' /bin/ksh -s" <<'EOF'
set -e
echo "车机系统: $(uname -a)"
mount -uw /mnt/app
[[ ! -e "/mnt/ota" ]] && mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota 2>/dev/null || true
mount -uw /mnt/ota 2>/dev/null || true
mkdir -p /mnt/app/eso/bin/apps/cluster/bak_pre_v21 /mnt/app/eso/hmi/lsd/jars "${PER_DST%/*}"
B=/mnt/app/eso/bin/apps/cluster/bak_pre_v21
if [ ! -f "$B/VWClusterNav_v6.jar" ] && [ -f /mnt/app/eso/hmi/lsd/jars/VWClusterNav_v6.jar ]; then
    cp -p /mnt/app/eso/hmi/lsd/jars/VWClusterNav_v6.jar "$B/"
fi
if [ ! -f "$B/persist_clusternav_install.sh" ] && [ -f "$PER_DST" ]; then
    cp -p "$PER_DST" "$B/persist_clusternav_install.sh"
fi
for bf in cl_probe.so clusterd cl_render dash.conf; do
    if [ ! -f "$B/$bf" ] && [ -f "/mnt/app/eso/bin/apps/cluster/$bf" ]; then
        cp -p "/mnt/app/eso/bin/apps/cluster/$bf" "$B/"
    fi
done
EOF

echo ">>> [2/4] 上传 v22.01 全量组件（VWClusterNav_v6.jar / cl_probe.so / clusterd / cl_render / dash.conf / Persist）..."
if [ "$NO_REBOOT" = "0" ]; then
    push_hu "$JAR_SRC" "/mnt/app/eso/hmi/lsd/jars/VWClusterNav_v6.jar.new"
    push_hu "$PER_SRC" "${PER_DST}.new"
    push_hu "$PRB_SRC" "/mnt/app/eso/bin/apps/cluster/cl_probe.so.new"
fi
push_hu "$CLD_SRC"  "/mnt/app/eso/bin/apps/cluster/clusterd.new"
push_hu "$CLR_SRC"  "/mnt/app/eso/bin/apps/cluster/cl_render.new"
push_hu "$TMP_CONF" "/mnt/app/eso/bin/apps/cluster/dash.conf.new"

HAS_NETD="$(run_hu '[ -f /mnt/app/eso/bin/apps/wifi_dev/touareg_netd ] && echo 1 || echo 0')"
if [ "$HAS_NETD" = "1" ]; then
    echo "    检测到车机已装 VW_WiFi_Dev，同步升级 touareg_netd v22.01..."
    push_hu "$NETD_SRC" "/mnt/app/eso/bin/apps/wifi_dev/touareg_netd.new"
fi
HAS_POP="$(run_hu '[ -f /mnt/app/eso/hmi/lsd/jars/VWPopupFilter.jar ] && echo 1 || echo 0')"
POP_SZ=0
if [ "$HAS_POP" = "1" ] && [ "$NO_REBOOT" = "0" ]; then
    echo "    检测到车机已装弹窗屏蔽模块，同步升级 VWPopupFilter.jar..."
    POP_SZ="$(wc -c < "$POP_SRC" | tr -d ' ')"
    push_hu "$POP_SRC" "/mnt/app/eso/hmi/lsd/jars/VWPopupFilter.jar.new"
fi
PER3_DST="/mnt/ota/modkit/Mods/VW_HUD_3_CarPlayCluster/Persist/install.sh"
HAS_PER3="$(run_hu "[ -f '$PER3_DST' ] && echo 1 || echo 0")"
PER3_SZ=0
if [ "$HAS_PER3" = "1" ] && [ "$NO_REBOOT" = "0" ]; then
    PER3_SZ="$(wc -c < "$PER3_SRC" | tr -d ' ')"
    push_hu "$PER3_SRC" "${PER3_DST}.new"
fi

echo ">>> [3/4] 严格校验字节长度并执行原子替换..."
run_hu "NO_REBOOT=$NO_REBOOT RESET_CONF=$RESET_CONF HAS_NETD=$HAS_NETD HAS_POP=$HAS_POP POP_SZ=$POP_SZ HAS_PER3=$HAS_PER3 PER3_SZ=$PER3_SZ PER3_DST='$PER3_DST' JAR_SZ=$JAR_SZ PRB_SZ=$PRB_SZ CLD_SZ=$CLD_SZ CLR_SZ=$CLR_SZ PER_SZ=$PER_SZ CONF_SZ=$CONF_SZ NETD_SZ=$NETD_SZ PER_DST='$PER_DST' /bin/ksh -s" <<'EOF'
set -e
chk_sz() {
    f="$1"
    exp="$2"
    set -- $(ls -l "$f")
    if [ "$5" != "$exp" ]; then
        echo "❌ 文件字节校验失败: $f (车机=$5 本地=$exp)"
        exit 1
    fi
}

D=/mnt/app/eso/bin/apps/cluster
J=/mnt/app/eso/hmi/lsd/jars/VWClusterNav_v6.jar
P="$PER_DST"

chk_sz "$D/clusterd.new" "$CLD_SZ"
chk_sz "$D/cl_render.new" "$CLR_SZ"
chk_sz "$D/dash.conf.new" "$CONF_SZ"

# 默认保留车上已有 dash.conf；指定 --reset-conf 或首次安装时写入模板配置
if [ -f "$D/dash.conf" ] && [ "$RESET_CONF" != "1" ]; then
    rm -f "$D/dash.conf.new"
else
    chmod 644 "$D/dash.conf.new" && mv -f "$D/dash.conf.new" "$D/dash.conf"
fi

chmod 755 "$D/clusterd.new" && mv -f "$D/clusterd.new" "$D/clusterd"
chmod 755 "$D/cl_render.new" && mv -f "$D/cl_render.new" "$D/cl_render"
rm -f "$D/triple_off" "$D/tire_ref.state" /tmp/ultra_theme /tmp/media_cp.state /tmp/rdk_probe.log

if [ "$NO_REBOOT" = "0" ]; then
    chk_sz "$J.new" "$JAR_SZ"
    chk_sz "$P.new" "$PER_SZ"
    chk_sz "$D/cl_probe.so.new" "$PRB_SZ"
    chmod 644 "$J.new" && mv -f "$J.new" "$J"
    chmod 755 "$P.new" && mv -f "$P.new" "$P"
    chmod 755 "$D/cl_probe.so.new" && mv -f "$D/cl_probe.so.new" "$D/cl_probe.so"
    # 若新旧两个 Persist 目录同时存在，清理旧目录避免双守护循环并跑
    if [ "$P" = "/mnt/ota/modkit/Mods/VW_HUD_2_ClusterNav/Persist/install.sh" ] && [ -d /mnt/ota/modkit/Mods/VW_ClusterNav ]; then
        rm -rf /mnt/ota/modkit/Mods/VW_ClusterNav
    fi
fi

if [ "$HAS_NETD" = "1" ]; then
    W=/mnt/app/eso/bin/apps/wifi_dev/touareg_netd
    chk_sz "$W.new" "$NETD_SZ"
    chmod 755 "$W.new" && mv -f "$W.new" "$W"
    echo "v22.01" > /mnt/app/eso/bin/apps/wifi_dev/ota_version
fi
if [ "$HAS_POP" = "1" ] && [ "$NO_REBOOT" = "0" ]; then
    PJ=/mnt/app/eso/hmi/lsd/jars/VWPopupFilter.jar
    chk_sz "$PJ.new" "$POP_SZ"
    chmod 644 "$PJ.new" && mv -f "$PJ.new" "$PJ"
fi
if [ "$HAS_PER3" = "1" ] && [ "$NO_REBOOT" = "0" ]; then
    chk_sz "$PER3_DST.new" "$PER3_SZ"
    chmod 755 "$PER3_DST.new" && mv -f "$PER3_DST.new" "$PER3_DST"
fi
# 同步刷新 v22.0 崩溃计数基线
ls /mnt/crashlogs/java/* 2>/dev/null | awk 'END{print NR+0}' > "$D/j9_base_crashes"
ls /mnt/crashlogs/core/dio_manager*.core.gz 2>/dev/null | awk 'END{print NR+0}' > "$D/dio_base_cores"
ls /mnt/crashlogs/core/cl_render*.core.gz /mnt/crashlogs/core/clusterd*.core.gz 2>/dev/null | awk 'END{print NR+0}' > "$D/render_base_cores"

sync
mount -ur /mnt/app 2>/dev/null || true
ls -l "$J" "$D/cl_probe.so" "$D/clusterd" "$D/cl_render" "$D/dash.conf"
EOF

if [ "$NO_REBOOT" = "1" ]; then
    echo ">>> [4/4] 秒级刷新 clusterd / cl_render / touareg_netd 进程（免重启模式）..."
    run_hu 'slay -f clusterd cl_render >/dev/null 2>&1 || true; [ -f /mnt/app/eso/bin/apps/wifi_dev/touareg_netd ] && slay -f touareg_netd >/dev/null 2>&1 || true'
    echo "✅ 仪表渲染进程已热刷新！"
    show_live_probe
    exit 0
fi

echo ">>> [4/4] 触发车机软重启 (gem-reset) 以加载新版 Java 插件（约需 35~45 秒）..."
run_hu 'sync; echo gem-reset > /dev/ooc/system' || true
sleep 12
wait_car_ssh 75
echo "✅ 车机重启完成并已重新连上！"
show_live_probe
