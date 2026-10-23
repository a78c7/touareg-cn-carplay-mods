#!/bin/ksh
# Copyright (c) 2025 LawPaul (https://github.com/LawPaul)
# This file is part of MH2p_AndroidAuto, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

[[ ! -e $MOD_PATH/Backup ]] && mkdir $MOD_PATH/Backup

cp -vrf /mnt/persist_new/fec $MOD_PATH/Backup
rm -vf /mnt/persist_new/fec/illegal.fecs
if [[ ! -e "/mnt/persist_new/fec/granted.fecs.bak" ]]; then
    echo "saving existing granted.fecs to granted.fecs.bak..."
    cp -vf /mnt/persist_new/fec/granted.fecs /mnt/persist_new/fec/granted.fecs.bak
else
    echo "granted.fecs.bak already exists; not overwriting"
fi
# remove (if not part of original FECs) AMI/USB enabled, Bluetooth, Android Auto
fecswap -r 00030000 00050000 00060900 -f /mnt/persist_new/fec/granted.fecs

# Uninstall Android Auto multi device fix
if [ "$OEM" = "PO" ]; then
    echo "Porsche detected, checking if Android Auto fix was applied"
    if [[ "$SOFTWARE_VERSION" == 26?? || "$SOFTWARE_VERSION" == 28?? ]]; then
        echo "Firmware $RELEASE_VERSION is within the affected range (26xx-28xx) -> Android Auto fix was required"
        if [[ -e "/mnt/app/eso/hmi/lsd/jars" ]]; then
            if [[ -f "/mnt/app/eso/hmi/lsd/jars/aafix.jar" ]]; then
                echo "Backing up: /mnt/app/eso/hmi/lsd/jars/aafix.jar"
                cp -vf /mnt/app/eso/hmi/lsd/jars/aafix.jar $MOD_PATH/Backup/
                echo "Removing: /mnt/app/eso/hmi/lsd/jars/aafix.jar"
                rm -vf /mnt/app/eso/hmi/lsd/jars/aafix.jar
            fi
        else
            echo "error: /mnt/app/eso/hmi/lsd/jars does not exist"
        fi
    else
        echo "Firmware $RELEASE_VERSION is outside the affected range (26xx-28xx) -> Android Auto fix was not required"
    fi
fi
