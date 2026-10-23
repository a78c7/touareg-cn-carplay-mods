#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878): CarPlay route-guidance probe, step 1.
# - copies dio_cluster.so (fifthBro/mh2p-cluster, CC BY-NC-SA, personal non-commercial use)
#   to /mnt/app/eso/bin/apps/cluster/
# - adds LD_PRELOAD + DIO_CLUSTER_LOG=1 to the "carplay" child envs in smartphone_integrator.json
#   (the dio_manager binary itself is NOT modified)
# Original json is backed up to the USB drive first; uninstall.sh restores it.
# same line count, non-empty (awk: the ModKit USB install environment has no wc)
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
J=/mnt/system/etc/eso/production/smartphone_integrator.json
SO_DIR=/mnt/app/eso/bin/apps/cluster
SO=$SO_DIR/dio_cluster.so
MARK="LD_PRELOAD=$SO"

if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/dio_cluster.so ]] && { echo "abort: dio_cluster.so missing on USB"; exit 0; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system

mkdir -p $MOD_PATH/backup
if [[ ! -e $MOD_PATH/backup/smartphone_integrator.json ]]; then
    if grep -q "$MARK" $J; then
        # already patched by an earlier install from another drive: rebuild the stock file for the backup
        echo "json already patched by an earlier install - rebuilding the stock version as backup"
        sed -e "s#\"LD_PRELOAD=$SO[^\"]*\", \"DIO_CLUSTER_LOG=1\", ##" $J > /tmp/si_stock.json
        if grep -q "dio_cluster" /tmp/si_stock.json || ! same_lines /tmp/si_stock.json $J; then
            echo "abort: could not rebuild the stock json"; exit 0
        fi
        cp -v /tmp/si_stock.json $MOD_PATH/backup/smartphone_integrator.json
    else
        cp -v $J $MOD_PATH/backup/smartphone_integrator.json
    fi
fi
if ! grep -q '"carplay"' $MOD_PATH/backup/smartphone_integrator.json; then
    echo "abort: json backup failed"; exit 0
fi

# 1) .so
mount -uw /mnt/app
mkdir -p $SO_DIR
cp -v $MOD_PATH/dio_cluster.so $SO
chmod 755 $SO
ls -l $SO

# 2) json: edit only the first "envs" line after the "carplay" key
if grep -q "$MARK" $J; then
    echo "json already patched"
else
    CL=$(grep -n '"carplay":{' $J | head -1 | cut -d: -f1)
    [[ -z "$CL" ]] && { echo "abort: carplay block not found"; exit 0; }
    EL=$(awk -v s=$CL 'NR>s && /"envs":\[/ {print NR; exit}' $J)
    [[ -z "$EL" ]] && { echo "abort: carplay envs line not found"; exit 0; }
    echo "carplay block line $CL, envs line $EL"
    awk -v e=$EL -v m="$MARK" 'NR==e { sub(/"envs":\[/, "\"envs\":[\"" m "\", \"DIO_CLUSTER_LOG=1\", ") } {print}' $J > /tmp/si_new.json
    if [ "$(grep -c "$MARK" /tmp/si_new.json)" != "1" ]; then echo "abort: edit check failed"; exit 0; fi
    if ! same_lines /tmp/si_new.json $J; then echo "abort: line count changed"; exit 0; fi
    mount -uw /mnt/system
    cp -v /tmp/si_new.json $J
    sync
    echo "patched line:"; sed -n "${EL}p" $J
fi
sync
echo "done"
