#!/bin/ksh
# Restore original skin3/viewhandler.zip and skin3/info.txt from the USB backup.
T=/mnt/app/eso/hmi/lsd/Resources/skin3
ORIG_SIZE=14662791
size_of() { ls -l "$1" 2>/dev/null | awk '{print $5}'; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
if [ "$(size_of $MOD_PATH/backup/viewhandler.zip)" = "$ORIG_SIZE" ]; then
    cp -v $MOD_PATH/backup/viewhandler.zip $T/viewhandler.zip
    echo "viewhandler.zip restored, size: $(size_of $T/viewhandler.zip)"
else
    echo "no valid viewhandler.zip backup, skipped"
fi
if grep -q "Carplay.Canvas_Dimension.width=1152" $MOD_PATH/backup/info.txt 2>/dev/null; then
    cp -v $MOD_PATH/backup/info.txt $T/info.txt
    echo "info.txt restored:"; grep Carplay $T/info.txt
else
    echo "no info.txt backup, skipped"
fi
sync
