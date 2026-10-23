#!/bin/ksh
# Copyright (c) 2025 LawPaul (https://github.com/LawPaul)
# This file is part of MH2p_SD_ModKit, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

check_modkit() {
    if [[ -e "$1" ]]; then
        mount -uw $1
        if [[ -e "$1/Data" ]] && [[ -e "$1/Meta" ]] && [[ -e "$1/Mods" ]]; then
            export MEDIA_PATH=$1
        fi
    fi
}

check_modkit "/fs/sda0"
check_modkit "/fs/sdb0"
check_modkit "/fs/usb0_0"
if [[ ! -e "$MEDIA_PATH" ]]; then
    exit 1
fi

touch "$MEDIA_PATH/test.txt"
if [[ ! -e "$MEDIA_PATH/test.txt" ]]; then
    exit 1
fi
rm -f "$MEDIA_PATH/test.txt"

mkdir -p "$MEDIA_PATH/Logs"

[[ ! -e "/mnt/app" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app/

/mnt/app/eso/bin/apps/showimage s 0 /mnt/persist_new/splashscreen.jpg

sleep 10

cp -f /mnt/persist_new/fecswap /bin/fecswap

echo "----- Start ModKit update from $MEDIA_PATH -----" >> $MEDIA_PATH/Logs/_ModKit_Update.log
ksh /mnt/persist_new/modkit_install.sh >> $MEDIA_PATH/Logs/_ModKit_Update.log 2>&1
echo "----- Done ModKit update from $MEDIA_PATH -----" >> $MEDIA_PATH/Logs/_ModKit_Update.log

rm -f /bin/fecswap

rm -f /mnt/persist_new/modkit_install.sh
rm -f /mnt/persist_new/modkit_persist.sh
rm -f /mnt/persist_new/servicemgrmibhigh.sh
rm -f /mnt/persist_new/fecswap
rm -f /mnt/persist_new/fwversion
/mnt/app/eso/bin/apps/showimage h 0
rm -f /mnt/persist_new/splashscreen.jpg