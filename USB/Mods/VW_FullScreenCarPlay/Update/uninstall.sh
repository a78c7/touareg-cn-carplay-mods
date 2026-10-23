#!/bin/ksh
# Undo full-screen CarPlay: viewhandler.zip from this drive's backup (exact original) if present, otherwise the
# same byte patch is reverted on the car (vhpatch revert) - so any copy of the package can uninstall.
# info.txt: backup if present, otherwise the two values are changed back in place.
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
. $MOD_PATH/common.sh
vh_setup || { echo "vhpatch cannot run on this unit, viewhandler.zip not touched"; VP=; }
mkdir -p $MOD_PATH/backup
if [ -n "$VP" ]; then
    ST=$(vh_state $VH)
    echo "skin3 viewhandler.zip: $ST"
    if [ "$ST" = "patched" ]; then
        if [ "$(vh_state $MOD_PATH/backup/viewhandler.zip)" = "stock" ]; then
            cp -v $MOD_PATH/backup/viewhandler.zip $VH
        else
            rm -f $MOD_PATH/backup/viewhandler.rev.zip
            $VP revert $VH $P $MOD_PATH/backup/viewhandler.rev.zip
            if [ "$(vh_state $MOD_PATH/backup/viewhandler.rev.zip)" = "stock" ]; then
                cp -v $MOD_PATH/backup/viewhandler.rev.zip $VH
            else
                echo "revert failed, viewhandler.zip not touched"
            fi
            rm -f $MOD_PATH/backup/viewhandler.rev.zip
        fi
        sync
        echo "viewhandler.zip now: $(vh_state $VH)"
    elif [ "$ST" = "stock" ]; then
        echo "viewhandler.zip already stock"
    else
        echo "viewhandler.zip is not the version this mod patched, not touched"
    fi
    vh_cleanup
fi
if grep -q "Carplay.Canvas_Dimension.width=1152" $MOD_PATH/backup/info.txt 2>/dev/null && grep -q "width=1673" $INFO; then
    cp -v $MOD_PATH/backup/info.txt $INFO
    echo "info.txt restored from backup:"; grep Carplay $INFO
elif grep -q "Carplay.Canvas_Dimension.width=1673" $INFO; then
    sed -e 's/^Layout.Carplay.Canvas_Dimension.width=1673/Layout.Carplay.Canvas_Dimension.width=1152/' \
        -e 's/^Layout.Carplay.Canvas_Dimension.height=748/Layout.Carplay.Canvas_Dimension.height=592/' $INFO > /tmp/info_stock.txt
    if grep -q "width=1152" /tmp/info_stock.txt && [ "$(wc -l < /tmp/info_stock.txt)" = "$(wc -l < $INFO)" ]; then
        cp -v /tmp/info_stock.txt $INFO; echo "info.txt restored in place:"; grep Carplay $INFO
    else
        echo "info.txt in-place restore failed, not touched"
    fi
else
    echo "info.txt not patched, skipped"
fi
sync
