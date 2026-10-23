#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878) full-screen CarPlay, skin3 only.
# Needs the patched viewhandler.zip + info.txt next to this script.
# Originals are backed up to the USB drive; uninstall.sh restores them.
T=/mnt/app/eso/hmi/lsd/Resources/skin3
ORIG_SIZE=14662791
size_of() { ls -l "$1" 2>/dev/null | awk '{print $5}'; }

if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt

NEW_SIZE=$(size_of $MOD_PATH/viewhandler.zip)
if [ -z "$NEW_SIZE" ] || [ "$NEW_SIZE" = "$ORIG_SIZE" ]; then
    echo "abort: patched viewhandler.zip missing on USB (or it is the unpatched original)"; exit 0
fi
if ! grep -q "Carplay.Canvas_Dimension.width=1673" $MOD_PATH/info.txt 2>/dev/null; then
    echo "abort: patched info.txt missing on USB"; exit 0
fi

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mkdir -p $MOD_PATH/backup

# ---- viewhandler.zip ----
CUR=$(size_of $T/viewhandler.zip)
echo "current skin3 viewhandler.zip size: $CUR"
if [ "$CUR" = "$NEW_SIZE" ]; then
    echo "viewhandler.zip already patched"
elif [ "$CUR" = "$ORIG_SIZE" ]; then
    [[ ! -e $MOD_PATH/backup/viewhandler.zip ]] && cp -v $T/viewhandler.zip $MOD_PATH/backup/viewhandler.zip
    if [ "$(size_of $MOD_PATH/backup/viewhandler.zip)" != "$ORIG_SIZE" ]; then echo "abort: no valid original backup on USB"; exit 0; fi
    mount -uw /mnt/app
    cp -v $MOD_PATH/viewhandler.zip $T/viewhandler.zip
    sync
    if [ "$(size_of $T/viewhandler.zip)" != "$NEW_SIZE" ]; then
        echo "copy failed, restoring original"; cp -v $MOD_PATH/backup/viewhandler.zip $T/viewhandler.zip; sync; exit 0
    fi
    echo "viewhandler.zip patched, size $(size_of $T/viewhandler.zip)"
else
    echo "abort: unexpected viewhandler.zip size (not stock, not this patch). Run uninstall first. Nothing touched."; exit 0
fi

# ---- info.txt ----
echo "current skin3 info.txt CarPlay canvas:"; grep Carplay $T/info.txt
if grep -q "Carplay.Canvas_Dimension.width=1673" $T/info.txt; then
    echo "info.txt already patched"
elif grep -q "Carplay.Canvas_Dimension.width=1152" $T/info.txt; then
    [[ ! -e $MOD_PATH/backup/info.txt ]] && cp -v $T/info.txt $MOD_PATH/backup/info.txt
    if ! grep -q "Carplay.Canvas_Dimension.width=1152" $MOD_PATH/backup/info.txt; then echo "abort: info.txt backup failed"; exit 0; fi
    mount -uw /mnt/app
    cp -v $MOD_PATH/info.txt $T/info.txt
    sync
    echo "new info.txt CarPlay canvas:"; grep Carplay $T/info.txt
else
    echo "abort: unexpected info.txt, not touching it"
fi
echo "done"
