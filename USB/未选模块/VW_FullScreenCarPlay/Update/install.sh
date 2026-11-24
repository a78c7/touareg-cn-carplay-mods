#!/bin/ksh
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
# VW Touareg CN P-series (MH2p_CN_VWG36_P*, pre-facelift 15" Discover Premium skin3 only;
# excludes 9.2" Discover Pro & MIB3/Asterix facelift) full-screen CarPlay - patched on the car.
# No VW file is shipped: vhpatch patches CarPlay/AndroidAuto geometry bytes in the unit's own
# skin3/viewhandler.zip (exact CRC table for P2878; strict J9 bytecode signature auto-locator for other
# CN 15" P-series such as P2838/P2853/P2869/P2876/P2497/P9832), and info.txt changes CarPlay canvas
# (1152x592 -> 1673x748). If a firmware doesn't match cleanly, vhpatch safely aborts without modifying any file.
# Originals are backed up per RELEASE_VERSION on USB (backup/) first; uninstall.sh also reverts in place.
case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
echo "firmware: $RELEASE_VERSION"
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/vhpatch || ! -f $MOD_PATH/fullscreen.patch ]] && { echo "abort: vhpatch / fullscreen.patch missing on USB"; exit 0; }
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
. $MOD_PATH/common.sh
vh_setup || { echo "abort: vhpatch cannot run on this unit, nothing touched"; exit 0; }
mkdir -p $MOD_PATH/backup
BK_VH=$MOD_PATH/backup/viewhandler_${RELEASE_VERSION}.zip
BK_INFO=$MOD_PATH/backup/info_${RELEASE_VERSION}.txt

# ---- viewhandler.zip ----
ST=$(vh_state $VH)
echo "skin3 viewhandler.zip: $ST"
$VP probe $VH $P 2>&1 || true
case "$ST" in
patched)
    echo "viewhandler.zip already patched" ;;
stock)
    [ "$(vh_state $BK_VH)" = "stock" ] || cp -v $VH $BK_VH
    if [ "$(vh_state $BK_VH)" != "stock" ]; then echo "abort: backup to USB failed, nothing touched"; vh_cleanup; exit 0; fi
    rm -f $MOD_PATH/backup/viewhandler.new.zip
    $VP apply $VH $P $MOD_PATH/backup/viewhandler.new.zip
    if [ "$(vh_state $MOD_PATH/backup/viewhandler.new.zip)" != "patched" ]; then echo "abort: patching failed, nothing touched"; vh_cleanup; exit 0; fi
    cp $MOD_PATH/backup/viewhandler.new.zip $VH
    sync
    if [ "$(vh_state $VH)" != "patched" ]; then
        echo "copy failed, restoring original"; cp -v $BK_VH $VH; sync; vh_cleanup; exit 0
    fi
    rm -f $MOD_PATH/backup/viewhandler.new.zip
    echo "viewhandler.zip patched" ;;
*)
    echo "abort: viewhandler.zip is neither stock nor patched by this mod. Nothing touched."; vh_cleanup; exit 0 ;;
esac
vh_cleanup

# ---- info.txt (CRLF line ends: no $ anchors) ----
echo "skin3 info.txt CarPlay canvas:"; grep Carplay $INFO
if grep -q "Carplay.Canvas_Dimension.width=1673" $INFO; then
    echo "info.txt already patched"
elif grep -q "Carplay.Canvas_Dimension.width=1152" $INFO; then
    [[ ! -e $BK_INFO ]] && cp -v $INFO $BK_INFO
    sed -e 's/^Layout.Carplay.Canvas_Dimension.width=1152/Layout.Carplay.Canvas_Dimension.width=1673/' \
        -e 's/^Layout.Carplay.Canvas_Dimension.height=592/Layout.Carplay.Canvas_Dimension.height=748/' $INFO > /tmp/info_new.txt
    if grep -q "width=1673" /tmp/info_new.txt && grep -q "height=748" /tmp/info_new.txt && same_lines /tmp/info_new.txt $INFO; then
        cp -v /tmp/info_new.txt $INFO; sync
        echo "new info.txt CarPlay canvas:"; grep Carplay $INFO
    else
        echo "info.txt edit check failed, not touched"
    fi
else
    echo "abort: unexpected info.txt, not touching it"
fi
echo "done"
