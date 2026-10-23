#!/bin/ksh
# Copyright (c) 2025 LawPaul (https://github.com/LawPaul)
# This file is part of MH2p_SD_ModKit, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

export MODKIT_VERSION=2
# TODO: provide basic info to scripts: BRAND, REGION, TYPE, FW

modkitPersistPath=/mnt/ota/modkit

failsafe() {
    local mediaPath=$1
    if [[ -e "$mediaPath" ]]; then
        mount -uw $mediaPath
        if [[ -e "$mediaPath/failsafe.sh" ]]; then
            echo "running fail-safe at $mediaPath" 
            ksh "$mediaPath/failsafe.sh"
        fi
    fi
}

# wait for removable media to be mounted
waitfor /dev/mcd/AUTORUN 20
sleep 2

echo "----- Start ModKit persist -----"

echo "checking fail-safe"
failsafe "/fs/sda0"
failsafe "/fs/sdb0"
failsafe "/fs/usb0_0"

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

echo "iterating mods"
needReboot=false
for dir in $modkitPersistPath/Mods/*
do
    if [ -d "$dir" ]; then
        MOD="${dir%/}"
        export MOD="${MOD##*/}"
        echo "found $MOD"
        deleteModDir=true
        if [ -d "$dir/Post" ]; then
            needReboot=true
            export MOD_PATH="$dir/Post"
            if [[ -e "$dir/uninstall.txt" ]]; then
                if [ -f "$dir/Post/uninstall.sh" ]; then
                    echo "post update uninstall $MOD"
                    echo "----- Start post uninstalling $MOD -----" > $modkitPersistPath/Logs/$MOD-Post.log
                    ksh "$dir/Post/uninstall.sh" >> "$modkitPersistPath/Logs/$MOD-Post.log" 2>&1
                    echo "----- Done post uninstalling $MOD -----" >> $modkitPersistPath/Logs/$MOD-Post.log
                else
                    echo "post update $MOD uninstall.sh not found"
                fi
            else
                if [ -f "$dir/Post/install.sh" ]; then
                    echo "post update install $MOD"
                    echo "----- Start post installing $MOD -----" > $modkitPersistPath/Logs/$MOD-Post.log
                    ksh "$dir/Post/install.sh" >> "$modkitPersistPath/Logs/$MOD-Post.log" 2>&1
                    echo "----- Done post installing $MOD -----" >> $modkitPersistPath/Logs/$MOD-Post.log
                else
                    echo "error: post update $MOD install.sh not found"
                fi
            fi
            echo "deleting $dir/Post"
            rm -rf "$dir/Post"
            if [[ -e "$dir/Post" ]]; then
                echo "error: could not delete $dir/Post"
                exit 1
            fi
        fi
        if [ -d "$dir/Persist" ]; then
            export MOD_PATH="$dir/Persist"
            if [[ ! -e "$dir/uninstall.txt" ]]; then
                if [ -f "$dir/Persist/install.sh" ]; then
                    deleteModDir=false
                    echo "persist install $MOD"
                    echo "----- Start persist installing $MOD -----" > $modkitPersistPath/Logs/$MOD-Persist.log
                    ksh "$dir/Persist/install.sh" >> "$modkitPersistPath/Logs/$MOD-Persist.log" 2>&1
                    echo "----- Done persist installing $MOD -----" >> $modkitPersistPath/Logs/$MOD-Persist.log
                else
                echo "error: persist $MOD install.sh not found"
                fi
            fi
        fi
        if $deleteModDir; then
            echo "deleting $dir"
            rm -rf "$dir"
        fi
    fi
done

if $needReboot; then
    echo "----- Done ModKit Post persistent reboot -----"
    cp -f "$modkitPersistPath/Logs/_ModKit_Persist.log" "$modkitPersistPath/Logs/_ModKit_Post.log"
    echo gem-reset > /dev/ooc/system
fi

echo "----- Done ModKit persist -----"