#!/bin/ksh
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): Fix Wireless CarPlay handshake blocked by 5GHz country adaption.
# On older units (e.g. P9832) upgraded to P2878, the BCM4359 WLAN country code may remain locked to XT/0
# (2.4GHz channels 1..11 only, no 5GHz channels). P2878 introduced "enable5GHzCountryAdaption":true in
# /mnt/system/etc/eso/production/smartphone_integrator.json, which aborts wireless CarPlay before
# launching dio_manager. Setting it to false restores wireless CarPlay without touching LD_PRELOAD entries.
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
J=/mnt/system/etc/eso/production/smartphone_integrator.json

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
touch "$MOD_PATH/test.txt"
if [[ ! -e "$MOD_PATH/test.txt" ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f "$MOD_PATH/test.txt"

[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
[[ ! -f "$J" ]] && { echo "abort: $J not found"; exit 0; }

mkdir -p "$MOD_PATH/backup"
[[ ! -e "$MOD_PATH/backup/smartphone_integrator.json" ]] && cp -v "$J" "$MOD_PATH/backup/smartphone_integrator.json"

if grep -q '"enable5GHzCountryAdaption"[ 	]*:[ 	]*false' "$J"; then
    echo "already patched: enable5GHzCountryAdaption is already false"
    exit 0
fi

CNT=$(grep -c '"enable5GHzCountryAdaption"[ 	]*:[ 	]*true' "$J")
if [ "$CNT" != "1" ]; then
    echo "abort: expected 1 enable5GHzCountryAdaption:true line, found $CNT"
    exit 0
fi

sed -e 's#"enable5GHzCountryAdaption"[ 	]*:[ 	]*true#"enable5GHzCountryAdaption":false#' "$J" > /tmp/si_5gfix.json
if [ "$(grep -c '"enable5GHzCountryAdaption":false' /tmp/si_5gfix.json)" != "1" ] || \
   ! grep -q '"carplay"' /tmp/si_5gfix.json || \
   ! same_lines /tmp/si_5gfix.json "$J"; then
    echo "abort: edit verification failed (json not changed)"
    rm -f /tmp/si_5gfix.json
    exit 0
fi

mount -uw /mnt/system
cp -v /tmp/si_5gfix.json "$J"
sync
mount -ur /mnt/system 2>/dev/null || true
rm -f /tmp/si_5gfix.json
echo "patched line:"
grep "enable5GHzCountryAdaption" "$J"
echo "done"
