#!/bin/ksh
# Copyright (c) 2025 LawPaul (https://github.com/LawPaul)
# This file is part of MH2p_SD_ModKit, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

export MODKIT_VERSION=2
# TODO: provide basic info to scripts: BRAND, REGION, TYPE, FW

modkitPersistPath=/mnt/ota/modkit

# ex: MH2p_US_PO416_P2870
export RELEASE_VERSION=`/mnt/app/armle/usr/bin/pc b:46924065:401 | cut -c 61- | sed ':a;N;$!ba;s/\n//g' | sed -e 's/\.//g' | sed -e 's/ //g'`
# AS, CN, ER, US, ...
export REGION="$(echo $RELEASE_VERSION | cut -d'_' -f2)"
# VW, AU, PO, LB, ...
export OEM="$(echo $RELEASE_VERSION | cut -d'_' -f3 | cut -b -2)"
# 416, 636, G33, G35, G36, ...
export TYPE="$(echo $RELEASE_VERSION | cut -d'_' -f3 | cut -b 3-)"
# E: engineering, K: customer update, P: production, S: security update
export RELEASE_TYPE="$(echo $RELEASE_VERSION | cut -d'_' -f4 | cut -b -1)"
# 9830, 2870, ...
export SOFTWARE_VERSION="$(echo $RELEASE_VERSION | cut -d'_' -f4 | cut -b 2-)"

# mount ota dir where ModKit is persisted
[[ ! -e "/mnt/ota" ]] &&  mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota
mount -uw /mnt/ota

echo "iterating mods"
for dir in $MEDIA_PATH/Mods/*
do
    if [ -d "$dir" ]; then
        MOD="${dir%/}"
        export MOD="${MOD##*/}"
        echo "found $MOD"
        # Update folder is run immediately during the software update
        if [ -d "$dir/Update" ]; then
            export MOD_PATH="$dir/Update"
            if [[ -e "$dir/uninstall.txt" ]]; then
                if [ -f "$dir/Update/uninstall.sh" ]; then
                    echo "update uninstall $MOD"
                    echo "----- Start uninstalling $MOD -----" >> $MEDIA_PATH/Logs/$MOD.log
                    ksh "$dir/Update/uninstall.sh" >> "$MEDIA_PATH/Logs/$MOD.log" 2>&1
                    echo "----- Done uninstalling $MOD -----" >> $MEDIA_PATH/Logs/$MOD.log
                else
                    echo "update $MOD uninstall.sh not found"
                fi
            else
                if [ -f "$dir/Update/install.sh" ]; then
                    echo "update install $MOD"
                    echo "----- Start installing $MOD -----" >> $MEDIA_PATH/Logs/$MOD.log
                    ksh "$dir/Update/install.sh" >> "$MEDIA_PATH/Logs/$MOD.log" 2>&1
                    echo "----- Done installing $MOD -----" >> $MEDIA_PATH/Logs/$MOD.log
                else
                    echo "error: update $MOD install.sh not found"
                fi
            fi
        fi
        # Post folder is copied to /mnt/ota and run once on first boot into /mnt/app followed by gem-reset
        if [ -d "$dir/Post" ]; then
            echo "post copy $MOD"
            mkdir -p "$modkitPersistPath/Mods/$MOD"
            if [[ -e "$dir/uninstall.txt" ]]; then
                cp -f "$dir/uninstall.txt" "$modkitPersistPath/Mods/$MOD"
            fi
            cp -rf "$dir/Post" "$modkitPersistPath/Mods/$MOD"
        fi
        if [[ ! -e "$dir/uninstall.txt" ]]; then
            # Persist folder is copied to /mnt/ota and run on each boot into /mnt/app
            if [ -d "$dir/Persist" ]; then
                echo "persist copy $MOD"
                mkdir -p "$modkitPersistPath/Mods/$MOD"
                cp -rf "$dir/Persist" "$modkitPersistPath/Mods/$MOD"
            fi
        fi
    fi
done

if [[ -e "$modkitPersistPath/Logs" ]]; then
    echo "copying logs to media from Post & Persist"
    cp -rf "$modkitPersistPath/Logs/." "$MEDIA_PATH/Logs"
    rm -rf "$modkitPersistPath/Logs/*"
fi

is_elf_binary() {
    local file=$1
    if [[ ! -f "$file" ]]; then
        return 1  # The file does not exist
    fi

    # Read bytes 2, 3 and 4 of the file (skipping the first byte)
    elf_check=$(dd if="$file" bs=1 skip=1 count=3 2>/dev/null)
    # If the file is less than 4 bytes, dd will return an empty string
    if [[ -z "$elf_check" ]]; then
        return 1  # The file is too small
    fi

    # Checking if bytes 2, 3, and 4 match ELF
    if [[ "$elf_check" == "ELF" ]]; then
        return 0
    else
        return 1
    fi
}

if [[ ! -e "/mnt/persist_new/servicemgrmibhigh.sh" ]]; then
    echo "error: servicemgrmibhigh.sh not found"
    exit 1
fi
if [[ ! -e "/mnt/persist_new/modkit_persist.sh" ]]; then
    echo "error: modkit_persist.sh not found"
    exit 1
fi

if is_elf_binary "/mnt/app/eso/bin/servicemgrmibhigh"; then
    # Case 1: servicemgrmibhigh - binary file (clean system)
    echo "clean system detected"
    cp -fv "/mnt/app/eso/bin/servicemgrmibhigh" "/mnt/app/eso/bin/servicemgrmibhigh0"
else
    # Case 2: servicemgrmibhigh is not a binary (patch already installed)
    if is_elf_binary "/mnt/app/eso/bin/servicemgrmibhigh0"; then
        # Subcase 2.1: servicemgrmibhigh0 - binary
        echo "existing patch detected"
    else
        # Subcase 2.2: servicemgrmibhigh0 is not binary
        echo "error: unknown patch state"
        exit 1
    fi
fi
if [[ ! -e "/mnt/app/eso/bin/servicemgrmibhigh0" ]]; then
    echo "error: servicemgrmibhigh0 not found after copy"
    exit 1
fi
cp -fv "/mnt/persist_new/servicemgrmibhigh.sh" "/mnt/app/eso/bin/servicemgrmibhigh"
chmod 755 "/mnt/app/eso/bin/servicemgrmibhigh"
# result: script as servicemgrmibhigh, binary as servicemgrmibhigh0

echo "setup ModKit persistence"
mkdir -p "$modkitPersistPath"
mkdir -p "$modkitPersistPath/Logs"
cp -f "/mnt/persist_new/modkit_persist.sh" "$modkitPersistPath/modkit_persist.sh"
chmod 755 "$modkitPersistPath/modkit_persist.sh"
