#!/bin/ksh
# Restore original skin3/viewhandler.zip and skin3/info.txt.
# Source, in this order: the backup this USB drive made at install time (Update/backup/), then the stock copies
# shipped with the package (Stock/<release>/skin3/ at the USB root), so any copy of the package can uninstall.
T=/mnt/app/eso/hmi/lsd/Resources/skin3
ORIG_SIZE=14662791
size_of() { ls -l "$1" 2>/dev/null | awk '{print $5}'; }
STOCK=$MEDIA_PATH/Stock/$RELEASE_VERSION/skin3

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
VH=
if [ "$(size_of $MOD_PATH/backup/viewhandler.zip)" = "$ORIG_SIZE" ]; then VH=$MOD_PATH/backup/viewhandler.zip
elif [ "$(size_of $STOCK/viewhandler.zip)" = "$ORIG_SIZE" ]; then VH=$STOCK/viewhandler.zip
fi
if [ -n "$VH" ]; then
    echo "restoring viewhandler.zip from $VH"
    cp -v $VH $T/viewhandler.zip
    sync
    echo "viewhandler.zip restored, size: $(size_of $T/viewhandler.zip)"
else
    echo "no valid viewhandler.zip backup (USB backup or Stock/$RELEASE_VERSION), skipped"
fi
INFO=
if grep -q "Carplay.Canvas_Dimension.width=1152" $MOD_PATH/backup/info.txt 2>/dev/null; then INFO=$MOD_PATH/backup/info.txt
elif grep -q "Carplay.Canvas_Dimension.width=1152" $STOCK/info.txt 2>/dev/null; then INFO=$STOCK/info.txt
fi
if [ -n "$INFO" ]; then
    cp -v $INFO $T/info.txt
    echo "info.txt restored from $INFO:"; grep Carplay $T/info.txt
elif grep -q "Carplay.Canvas_Dimension.width=1673" $T/info.txt; then
    # no copy anywhere: undo our two values in place
    # (the file has CRLF line ends: no $ anchor)
    sed -e 's/^Layout.Carplay.Canvas_Dimension.width=1673/Layout.Carplay.Canvas_Dimension.width=1152/' \
        -e 's/^Layout.Carplay.Canvas_Dimension.height=748/Layout.Carplay.Canvas_Dimension.height=592/' $T/info.txt > /tmp/info_stock.txt
    if grep -q "width=1152" /tmp/info_stock.txt && [ "$(wc -l < /tmp/info_stock.txt)" = "$(wc -l < $T/info.txt)" ]; then
        cp -v /tmp/info_stock.txt $T/info.txt; echo "info.txt restored in place:"; grep Carplay $T/info.txt
    else
        echo "info.txt in-place restore failed, not touched"
    fi
else
    echo "info.txt not patched, skipped"
fi
sync
