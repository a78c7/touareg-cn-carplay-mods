#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878) full-screen CarPlay, skin3 only - patched on the car.
# No VW file is shipped: vhpatch changes 38 bytes (listed in fullscreen.patch) in the unit's own
# skin3/viewhandler.zip after checking that every entry is exactly the stock version, and info.txt gets its
# CarPlay canvas size changed (1152x592 -> 1673x748). The originals are copied to this USB drive (backup/) first;
# uninstall.sh reverts the same patch, so it works from any copy of the package even without that backup.
if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/vhpatch || ! -f $MOD_PATH/fullscreen.patch ]] && { echo "abort: vhpatch / fullscreen.patch missing on USB"; exit 0; }
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
. $MOD_PATH/common.sh
vh_setup || { echo "abort: vhpatch cannot run on this unit, nothing touched"; exit 0; }
mkdir -p $MOD_PATH/backup

# ---- viewhandler.zip ----
ST=$(vh_state $VH)
echo "skin3 viewhandler.zip: $ST"
case "$ST" in
patched)
    echo "viewhandler.zip already patched" ;;
stock)
    [ "$(vh_state $MOD_PATH/backup/viewhandler.zip)" = "stock" ] || cp -v $VH $MOD_PATH/backup/viewhandler.zip
    if [ "$(vh_state $MOD_PATH/backup/viewhandler.zip)" != "stock" ]; then echo "abort: backup to USB failed, nothing touched"; vh_cleanup; exit 0; fi
    rm -f $MOD_PATH/backup/viewhandler.new.zip
    $VP apply $VH $P $MOD_PATH/backup/viewhandler.new.zip
    if [ "$(vh_state $MOD_PATH/backup/viewhandler.new.zip)" != "patched" ]; then echo "abort: patching failed, nothing touched"; vh_cleanup; exit 0; fi
    cp $MOD_PATH/backup/viewhandler.new.zip $VH
    sync
    if [ "$(vh_state $VH)" != "patched" ]; then
        echo "copy failed, restoring original"; cp -v $MOD_PATH/backup/viewhandler.zip $VH; sync; vh_cleanup; exit 0
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
    [[ ! -e $MOD_PATH/backup/info.txt ]] && cp -v $INFO $MOD_PATH/backup/info.txt
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
