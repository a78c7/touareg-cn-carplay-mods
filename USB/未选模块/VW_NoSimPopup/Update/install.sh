#!/bin/ksh
# VW MH2p: Suppress the recurring "SIM 卡仅用于数据连接。" (PHONE_SIM_DATAONLY) popup when CarPlay / Android Auto connects.
# - installs shared VWPopupFilter.jar (Java 1.4, version 48) as HMI bundle 98 (orthogonal to bundle 97 VWClusterNav)
# - enables flag /mnt/app/eso/hmi/lsd/config/popup_filter/no_sim_dataonly
# - dynamically resolves ASLPhoneSystemEventConstants.PHONE_SIM_DATAONLY from lsd.jxe at runtime

B=/mnt/app/eso/hmi/lsd/config/bundles.properties
FDIR=/mnt/app/eso/hmi/lsd/config/popup_filter
FLAG_NAME=no_sim_dataonly
JD=/mnt/app/eso/hmi/lsd/jars
JAR=VWPopupFilter.jar
BID=98
BNAME=VWPopupFilter
BACT=vwpopup.Activator
BCMT="# VWPopupFilter: HMI popup filter (USB mods VW_NoSimPopup / VW_NoBtAuthPopup)"

echo "=== VW_NoSimPopup install ($RELEASE_VERSION) ==="
if [ -n "$OEM" ] && [ "$OEM" != "VW" ]; then
    echo "abort: built for VW MH2p units, this unit is $RELEASE_VERSION"
    exit 0
fi

touch "$MOD_PATH/test.txt"
if [[ ! -e "$MOD_PATH/test.txt" ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f "$MOD_PATH/test.txt"
[[ ! -f "$MOD_PATH/$JAR" ]] && { echo "abort: $JAR missing on USB"; exit 0; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -f "$B" ]] && { echo "abort: $B not found"; exit 0; }

if [ "$(grep -c '^Bundle\.Startup\.Lateload[ 	]*=' "$B")" != "1" ]; then
    echo "abort: expected 1 Bundle.Startup.Lateload line in $B"
    exit 0
fi

CONFLICT=$(awk -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
{
    line = $0; sub(/\r$/, "", line)
    if (line ~ ("^Bundle\\.Name\\." bid "[ \t]*=") && line != ("Bundle.Name." bid " = " bname) && line != ("Bundle.Name." bid " = VWNoSimPopup")) c++
    if (line ~ ("^Bundle\\.Activator\\." bid "[ \t]*=") && line != ("Bundle.Activator." bid " = " bact) && line != ("Bundle.Activator." bid " = vwnosim.Activator")) c++
}
END { print c+0 }
' "$B")
if [ "$CONFLICT" != "0" ]; then
    echo "abort: bundle id $BID is already used by another bundle in $B"
    exit 0
fi

mkdir -p "$MOD_PATH/backup"
[[ ! -e "$MOD_PATH/backup/bundles.properties" ]] && cp -v "$B" "$MOD_PATH/backup/bundles.properties"

OTHER_CNT=$(awk -v bid="$BID" '
/^Bundle\./ {
    if ($0 ~ ("^Bundle\\.(Name|Activator)\\." bid "[ \t]*=")) next
    c++
}
END { print c+0 }
' "$B")

awk -v mode="add" -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" -v bcmt="$BCMT" '
BEGIN { blanks = 0; skipped = 0 }
/^Bundle\.Startup\.Lateload[ \t]*=/ {
    for (k = 0; k < blanks; k++) print ""
    blanks = 0; skipped = 0
    line = $0; sub(/\r$/, "", line)
    p = index(line, "=")
    lhs = substr(line, 1, p)
    rhs = substr(line, p + 1)
    gsub(/[ \t]+/, "", rhs)
    n = split(rhs, a, ",")
    out = ""
    for (i = 1; i <= n; i++) {
        if (a[i] != "" && a[i] != bid) {
            out = (out == "" ? a[i] : out "," a[i])
        }
    }
    out = (out == "" ? bid : out "," bid)
    print lhs " " out
    next
}
{
    line = $0; sub(/\r$/, "", line)
    if (index(line, "# " bname ":") == 1 || index(line, "# VWNoSimPopup:") == 1 ||
        line == ("Bundle.Name." bid " = " bname) || line == ("Bundle.Name." bid " = VWNoSimPopup") ||
        line == ("Bundle.Activator." bid " = " bact) || line == ("Bundle.Activator." bid " = vwnosim.Activator")) {
        if (skipped == 0 && blanks > 0) blanks--
        skipped = 1
        next
    }
    if (line ~ /^[ \t]*$/) {
        blanks++
        next
    }
    for (k = 0; k < blanks; k++) print ""
    blanks = 0; skipped = 0
    print line
}
END {
    if (skipped == 0) {
        for (k = 0; k < blanks; k++) print ""
    }
    print ""
    print bcmt
    print "Bundle.Name." bid " = " bname
    print "Bundle.Activator." bid " = " bact
}
' "$B" > /tmp/bp_pf_new

LATE_OK=$(awk -v bid="$BID" '
/^Bundle\.Startup\.Lateload[ \t]*=/ {
    lc++
    p = index($0, "=")
    rhs = substr($0, p + 1)
    gsub(/[ \t\r]+/, "", rhs)
    n = split(rhs, a, ",")
    hit = 0; total = 0
    for (i = 1; i <= n; i++) {
        if (a[i] != "") total++
        if (a[i] == bid) hit++
    }
    if (hit == 1 && total >= 2) ok = 1
}
END { print (lc == 1 && ok == 1) ? 1 : 0 }
' /tmp/bp_pf_new)
NAME_CNT=$(awk -v bid="$BID" -v bname="$BNAME" '$0 == ("Bundle.Name." bid " = " bname) {c++} END {print c+0}' /tmp/bp_pf_new)
ACT_CNT=$(awk -v bid="$BID" -v bact="$BACT" '$0 == ("Bundle.Activator." bid " = " bact) {c++} END {print c+0}' /tmp/bp_pf_new)
NEW_CNT=$(awk '/^Bundle\./ {c++} END {print c+0}' /tmp/bp_pf_new)

if [ "$LATE_OK" != "1" ] || [ "$NAME_CNT" != "1" ] || [ "$ACT_CNT" != "1" ] || [ "$NEW_CNT" != "$(( OTHER_CNT + 2 ))" ]; then
    echo "abort: edit verification failed (late=$LATE_OK name=$NAME_CNT act=$ACT_CNT cnt=$NEW_CNT want=$(( OTHER_CNT + 2 )))"
    rm -f /tmp/bp_pf_new
    exit 0
fi

mount -uw /mnt/app

mkdir -p "$JD" "$FDIR"
chmod 755 "$FDIR"
rm -fv "$JD/VWNoSimPopup.jar" "$JD/VWNoSimPopup_"*.jar
cp -v "$MOD_PATH/$JAR" "$JD/$JAR.new" && chmod 644 "$JD/$JAR.new" && mv -f "$JD/$JAR.new" "$JD/$JAR"
: > "$FDIR/$FLAG_NAME"
chmod 644 "$FDIR/$FLAG_NAME"
cp -v /tmp/bp_pf_new "$B.new" && chmod 644 "$B.new" && mv -f "$B.new" "$B"
rm -f /tmp/bp_pf_new
sync

echo "now:"
grep -n 'Startup.Lateload' "$B"
grep -n "Name\.$BID" "$B"
grep -n "Activator\.$BID" "$B"
ls -la "$FDIR" "$JD/$JAR"
mount -ur /mnt/app 2>/dev/null || true
echo "=== VW_NoSimPopup install done ==="
