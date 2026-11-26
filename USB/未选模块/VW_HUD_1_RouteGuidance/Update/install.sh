#!/bin/ksh
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): CarPlay route-guidance probe, step 1.
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

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/dio_cluster.so ]] && { echo "abort: dio_cluster.so missing on USB"; exit 0; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system

# 只读体检：在 ModKit 覆写 servicemgrmibhigh 前检查改装店遗留锁死脚本与陌生 SSH 公钥（不写闪存、不阻断安装）
preflight_readonly_check() {
    _app="${PREFLIGHT_APP_ROOT:-/mnt/app}"
    _ota="${PREFLIGHT_OTA_ROOT:-/mnt/ota}"
    _bin="$_app/eso/bin"
    _entry="$_bin/servicemgrmibhigh"
    _entry0="$_bin/servicemgrmibhigh0"
    _ak="$_app/root/.ssh/authorized_keys"
    _warn=0
    echo "--- preflight (read-only) ---"

    _entry_is_elf=0
    if [ -f "$_entry" ]; then
        _m=$(dd if="$_entry" bs=1 skip=1 count=3 2>/dev/null)
        if [ "$_m" = "ELF" ]; then
            _entry_is_elf=1
            echo "entry: stock ELF"
        elif grep -q "modkit_persist" "$_entry" 2>/dev/null; then
            echo "entry: standard ModKit script"
        else
            _warn=1
            _sz=$(ls -l "$_entry" 2>/dev/null | awk 'NR==1 { print $5 + 0 }')
            echo "WARN: suspected shop / third-party servicemgrmibhigh (${_sz:-?} bytes, missing modkit_persist)"
            awk '
            NF > 0 && shown < 15 {
                sub(/\r$/, "")
                low = tolower($0)
                if (low ~ /(private key|password|passwd|psk|secret|token)/) print "  | [REDACTED]"
                else print "  | " $0
                shown++
            }' "$_entry" 2>/dev/null || true
        fi
    else
        echo "entry: missing ($_entry)"
    fi

    if [ -f "$_entry0" ]; then
        _m0=$(dd if="$_entry0" bs=1 skip=1 count=3 2>/dev/null)
        if [ "$_m0" != "ELF" ]; then
            _warn=1
            echo "WARN: servicemgrmibhigh0 is NOT ELF (ModKit WILL abort with 'error: unknown patch state' and Persist will NOT run)"
        fi
    elif [ "$_entry_is_elf" -eq 0 ]; then
        _warn=1
        echo "WARN: servicemgrmibhigh0 is MISSING while servicemgrmibhigh is not ELF (ModKit WILL abort with 'error: unknown patch state' and Persist will NOT run; see Issue #16)"
        for _sf in "$_bin"/servicemgrmibhigh*; do
            [ -e "$_sf" ] || continue
            ls -l "$_sf" 2>/dev/null || true
        done
    fi

    if [ -e "$_bin/del" ] || [ -e "$_bin/shadow" ]; then
        _warn=1
        echo "WARN: suspected shop anti-tamper files (del/shadow) detected -- boot entry may be restored ~2min after boot (see Issue #10)"
        [ -e "$_bin/del" ] && ls -l "$_bin/del" 2>/dev/null || true
        [ -e "$_bin/shadow" ] && ls -l "$_bin/shadow" 2>/dev/null || true
    fi

    if [ -e "$_app/eso/hmi/lsd/.j" ] || [ -e "$_ota/challenge.pub" ] || [ -e "$_bin/autorunner" ]; then
        _warn=1
        echo "WARN: suspected shop payload/hook files (.j / challenge.pub / autorunner) detected (see Issue #16)"
        [ -e "$_app/eso/hmi/lsd/.j" ] && ls -l "$_app/eso/hmi/lsd/.j" 2>/dev/null || true
        [ -e "$_ota/challenge.pub" ] && ls -l "$_ota/challenge.pub" 2>/dev/null || true
        [ -e "$_bin/autorunner" ] && ls -l "$_bin/autorunner" 2>/dev/null || true
    fi

    if [ -f "$_ota/modkit/Logs/_ModKit_Persist.log" ]; then
        echo "persist log: present"
    else
        echo "persist log: absent (normal on first install)"
    fi
    if [ -d "$_ota/modkit/Mods" ]; then
        _mods=""
        for _d in "$_ota/modkit/Mods"/*; do
            [ -d "$_d" ] || continue
            _bn="${_d##*/}"
            _mods="$_mods $_bn"
            case "$_bn" in
                VW_*|AppleCarPlay|AndroidAuto) ;;
                *)
                    if [ -f "$_d/Persist/install.sh" ]; then
                        if grep -qi "sshd" "$_d/Persist/install.sh" 2>/dev/null || grep -qi "challenge" "$_d/Persist/install.sh" 2>/dev/null; then
                            _warn=1
                            echo "WARN: third-party Persist mod '$_bn' references sshd/challenge"
                        fi
                    fi
                    ;;
            esac
        done
        [ -n "$_mods" ] && echo "existing mods:$_mods"
    fi

    if [ -f "$_ak" ]; then
        _ext=$(awk '
        {
            sub(/\r$/, "")
            if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*#/) next
            if ($0 ~ /VW_WiFi_Dev_Managed/) next
            cnt++
            ki = 1
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^(ssh-|ecdsa-|sk-)/) { ki = i; break }
            }
            ktype = $ki
            kbody = (ki + 1 <= NF) ? $(ki + 1) : ""
            tail = (length(kbody) > 8) ? substr(kbody, length(kbody) - 7) : kbody
            cmt = ""
            for (i = ki + 2; i <= NF; i++) cmt = (cmt == "" ? $i : cmt " " $i)
            printf "  key#%d: %s ...%s %s\n", cnt, ktype, tail, cmt
        }
        ' "$_ak" 2>/dev/null)
        if [ -n "$_ext" ]; then
            _warn=1
            echo "WARN: non-managed SSH public key(s) found in authorized_keys (verify before enabling :2012 SSH):"
            echo "$_ext"
        fi
    fi

    if [ "$_warn" -eq 0 ]; then
        echo "preflight: clean"
    else
        echo "preflight: warnings found (read-only check, continuing install; see docs/07 sec.12 & Issues #10/#16)"
    fi
    echo "--- end preflight ---"
}
preflight_readonly_check || true

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
