#!/bin/ksh
# Restore "enable5GHzCountryAdaption":true in place without touching other mods' LD_PRELOAD entries.
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
J=/mnt/system/etc/eso/production/smartphone_integrator.json

[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
[[ ! -f "$J" ]] && { echo "abort: $J not found"; exit 0; }

if grep -q '"enable5GHzCountryAdaption"[ 	]*:[ 	]*true' "$J"; then
    echo "already stock: enable5GHzCountryAdaption is already true"
    exit 0
fi

CNT=$(grep -c '"enable5GHzCountryAdaption"[ 	]*:[ 	]*false' "$J")
if [ "$CNT" != "1" ]; then
    echo "abort: expected 1 enable5GHzCountryAdaption:false line, found $CNT"
    exit 0
fi

sed -e 's#"enable5GHzCountryAdaption"[ 	]*:[ 	]*false#"enable5GHzCountryAdaption":true#' "$J" > /tmp/si_5gfix_un.json
if [ "$(grep -c '"enable5GHzCountryAdaption":true' /tmp/si_5gfix_un.json)" != "1" ] || \
   ! grep -q '"carplay"' /tmp/si_5gfix_un.json || \
   ! same_lines /tmp/si_5gfix_un.json "$J"; then
    echo "abort: restore verification failed (json not changed)"
    rm -f /tmp/si_5gfix_un.json
    exit 0
fi

mount -uw /mnt/system
cp -v /tmp/si_5gfix_un.json "$J"
sync
mount -ur /mnt/system 2>/dev/null || true
rm -f /tmp/si_5gfix_un.json
echo "restored line:"
grep "enable5GHzCountryAdaption" "$J"
echo "done"
