#!/bin/ksh
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
# VW Touareg CN P-series (MH2p_CN_VWG36_P*, pre-facelift 15" Discover Premium skin3 only;
# excludes 9.2" Discover Pro & MIB3/Asterix facelift) full-screen Baidu CarLife enhancement.
#
# 功能清单（100% 独立于全屏 CarPlay / AndroidAuto / VW_HUD_*，原地可逆卸载）：
#   1. 15 寸满屏点对点无黑边：vhpatch_carlife 仅原地修补车机自有 skin3/viewhandler.zip 中的 CarLife
#      编译视图 Cm_429C.jxe（19 字节，精确补丁表 + 跨 P 版字节码指纹兜底），skin3/info.txt CarLife 画布
#      1152x592 -> 1673x748，默认 res.conf 锁定 1664 736 20（16px 宏块对齐 @20fps）。
#   2. 2016 原厂 SDK 防崩护盾 + 屏蔽原厂仪表「导航已在移动设备上激活」弹窗。
#   3. 百度导航转向信息上挡风玻璃 HUD 与仪表导航卡（需车机已装 VWClusterNav_v6.jar，拔线约 1 秒抗 SIGKILL 自清）。
#   4. 可选防蓝牙听歌抢焦点开关（bt_audio_lock，默认关闭）：
#      如需保留车机蓝牙听歌同时仅走 USB 播报导航语音，可在车机创建
#      /mnt/app/eso/bin/apps/carlife_mod/bt_audio_lock（永久）或 /tmp/carlife_bt_audio_lock（本次上电临时生效）。
#
# Originals are backed up per RELEASE_VERSION on USB (backup/) first; uninstall.sh reverts in place.
# Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
echo "firmware: $RELEASE_VERSION"

touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt

for f in vhpatch_carlife carlife_fullscreen.patch carlife_probe.so common.sh; do
    [[ ! -f $MOD_PATH/$f ]] && { echo "abort: $f missing on USB"; exit 0; }
done

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
mount -uw /mnt/app

. $MOD_PATH/common.sh
vh_setup || { echo "abort: vhpatch_carlife cannot run on this unit, nothing touched"; exit 0; }

mkdir -p $MOD_PATH/backup $D
BK_VH=$MOD_PATH/backup/viewhandler_${RELEASE_VERSION}.zip
BK_INFO=$MOD_PATH/backup/info_${RELEASE_VERSION}.txt
BK_SI=$MOD_PATH/backup/smartphone_integrator_${RELEASE_VERSION}.json

# ---- 1. viewhandler.zip（仅改 CarLife Cm_429C.jxe 19B，绝不碰 CarPlay/AA/MirrorLink）----
ST=$(vh_state $VH)
echo "skin3 viewhandler.zip CarLife: $ST"
$VP probe $VH $P 2>&1 || true
case "$ST" in
patched)
    echo "viewhandler.zip CarLife already patched" ;;
stock)
    [ "$(vh_state $BK_VH)" = "stock" ] || cp -v $VH $BK_VH
    if [ "$(vh_state $BK_VH)" != "stock" ]; then echo "abort: backup to USB failed, nothing touched"; vh_cleanup; exit 0; fi
    rm -f $MOD_PATH/backup/viewhandler_cl.new.zip
    $VP apply $VH $P $MOD_PATH/backup/viewhandler_cl.new.zip
    if [ "$(vh_state $MOD_PATH/backup/viewhandler_cl.new.zip)" != "patched" ]; then
        echo "abort: patching failed, nothing touched"; rm -f $MOD_PATH/backup/viewhandler_cl.new.zip; vh_cleanup; exit 0
    fi
    cp $MOD_PATH/backup/viewhandler_cl.new.zip $VH
    sync
    if [ "$(vh_state $VH)" != "patched" ]; then
        echo "copy failed, restoring original"; cp -v $BK_VH $VH; sync; rm -f $MOD_PATH/backup/viewhandler_cl.new.zip; vh_cleanup; exit 0
    fi
    rm -f $MOD_PATH/backup/viewhandler_cl.new.zip
    echo "viewhandler.zip CarLife patched" ;;
*)
    echo "abort: viewhandler.zip CarLife is neither stock nor patched by this mod. Nothing touched."; vh_cleanup; exit 0 ;;
esac
vh_cleanup

# ---- 2. info.txt（仅改 Layout.CarLife.Canvas_Dimension，保留 CRLF 与 CarPlay 配置）----
echo "skin3 info.txt CarLife canvas:"; grep CarLife $INFO
if grep -q "Layout.CarLife.Canvas_Dimension.width=1673" $INFO; then
    echo "info.txt CarLife already patched"
elif grep -q "Layout.CarLife.Canvas_Dimension.width=1152" $INFO; then
    [[ ! -e $BK_INFO ]] && cp -v $INFO $BK_INFO
    sed -e 's/^Layout.CarLife.Canvas_Dimension.width=1152/Layout.CarLife.Canvas_Dimension.width=1673/' \
        -e 's/^Layout.CarLife.Canvas_Dimension.height=592/Layout.CarLife.Canvas_Dimension.height=748/' $INFO > /tmp/info_cl_new.txt
    if grep -q "Layout.CarLife.Canvas_Dimension.width=1673" /tmp/info_cl_new.txt \
       && grep -q "Layout.CarLife.Canvas_Dimension.height=748" /tmp/info_cl_new.txt \
       && same_lines /tmp/info_cl_new.txt $INFO; then
        cp -v /tmp/info_cl_new.txt $INFO
        sync
        echo "new info.txt CarLife canvas:"; grep CarLife $INFO
    else
        echo "info.txt edit check failed, not touched"
    fi
    rm -f /tmp/info_cl_new.txt
else
    echo "abort: unexpected info.txt CarLife entries, not touching it"
fi

# ---- 3. 安装 carlife_probe.so 与默认 res.conf（1664 736 20）----
cp -v $MOD_PATH/carlife_probe.so $D/carlife_probe.so.new && chmod 755 $D/carlife_probe.so.new && mv -f $D/carlife_probe.so.new $PR

if [[ ! -f $D/res.conf ]]; then
    echo "1664 736 20" > $D/res.conf && chmod 644 $D/res.conf
    echo "res.conf created: 1664 736 20"
else
    echo "res.conf preserved: $(cat $D/res.conf 2>/dev/null)"
fi

# 清理残留禁用/临时调频标记（默认不启用 bt_audio_lock，保留用户既有选择）
rm -f $D/disable /tmp/carlife_probe_off /tmp/carlife_fps_req

# ---- 4. smartphone_integrator.json 仅给 "carlife" 块注入 LD_PRELOAD（绝不碰 carplay / androidauto / alibox）----
[[ ! -e $BK_SI ]] && cp -v $SI $BK_SI
if grep -q "$PR" $SI; then
    echo "smartphone_integrator.json already includes carlife_probe.so"
else
    awk -v pr="$PR" '
    /"carlife":\{/ { in_cl=1 }
    in_cl && /"envs":\[\]/ && !done {
        sub(/"envs":\[\]/, "\"envs\":[\"LD_PRELOAD=" pr "\"]")
        done=1
        in_cl=0
    }
    in_cl && /"envs":\[/ && !done {
        sub(/"envs":\[/, "\"envs\":[\"LD_PRELOAD=" pr "\", ")
        done=1
        in_cl=0
    }
    { print }
    ' $SI > /tmp/si_cl_new.json
    if [ "$(grep -c "LD_PRELOAD=$PR" /tmp/si_cl_new.json)" = "1" ] && same_lines /tmp/si_cl_new.json $SI; then
        mount -uw /mnt/system
        cp -v /tmp/si_cl_new.json $SI
        sync
        echo "smartphone_integrator.json patched:"
        grep "carlife_probe.so" $SI
    else
        echo "abort: smartphone_integrator.json patch verification failed, not touched"
    fi
    rm -f /tmp/si_cl_new.json
fi

if [[ -f /mnt/app/eso/hmi/lsd/jars/VWClusterNav_v6.jar ]]; then
    echo "detected VWClusterNav_v6.jar: CarLife HUD & cluster turn card enabled"
else
    echo "note: VWClusterNav_v6.jar not found; full-screen/FPS/crash-shield work standalone (install VW_HUD_2_ClusterNav for HUD)"
fi
echo "note: optional bt_audio_lock is OFF by default (create $D/bt_audio_lock to keep VW BT music while using CarLife nav TTS)"

sync
echo "done"
