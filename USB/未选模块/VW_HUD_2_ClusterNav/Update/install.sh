#!/bin/ksh
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): CarPlay route guidance -> instrument cluster + HUD, step 2.
# The jar keeps the file name VWClusterNav_v6.jar; content is v21.1 (also used by VW_HUD_3_CarPlayCluster: event-driven display switching, info box & tire/chassis telemetry).
# Needs step 1 (VW_HUD_1_RouteGuidance: dio_cluster.so preloaded into dio_manager) already installed.
# - copies VWClusterNav_v6.jar (Java 1.4 class files, version 48) to /mnt/app/eso/hmi/lsd/jars/, removes older versions
# - registers it orthogonally as HMI bundle 97 in Bundle.Startup.Lateload without disturbing other bundles (e.g. 98 VWPopupFilter)
B=/mnt/app/eso/hmi/lsd/config/bundles.properties
JD=/mnt/app/eso/hmi/lsd/jars
JAR=VWClusterNav_v6.jar
SO=/mnt/app/eso/bin/apps/cluster/dio_cluster.so
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
BID=97
BNAME=VWClusterNav
BACT=vwcn.Activator
BCMT="# VWClusterNav: CarPlay route guidance -> cluster/HUD (USB mod VW_ClusterNav)"

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/$JAR ]] && { echo "abort: $JAR missing on USB"; exit 0; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system

[[ ! -f $SO ]] && { echo "abort: $SO not found - install VW_HUD_1_RouteGuidance first"; exit 0; }
if ! grep -q "LD_PRELOAD=$SO" $SI; then echo "abort: dio_cluster.so not preloaded in $SI"; exit 0; fi
echo "step 1 present: OK"

[[ ! -f "$B" ]] && { echo "abort: $B not found"; exit 0; }
if [ "$(grep -c '^Bundle\.Startup\.Lateload[ 	]*=' "$B")" != "1" ]; then
    echo "abort: expected 1 Bundle.Startup.Lateload line in $B"; exit 0
fi

CONFLICT=$(awk -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
{
    line = $0; sub(/\r$/, "", line)
    if (line ~ ("^Bundle\\.Name\\." bid "[ \t]*=") && line != ("Bundle.Name." bid " = " bname)) c++
    if (line ~ ("^Bundle\\.Activator\\." bid "[ \t]*=") && line != ("Bundle.Activator." bid " = " bact)) c++
}
END { print c+0 }
' "$B")
if [ "$CONFLICT" != "0" ]; then
    echo "abort: bundle id $BID is already used by another bundle in $B"; exit 0
fi

mkdir -p $MOD_PATH/backup
if [[ ! -e $MOD_PATH/backup/bundles.properties ]]; then
    cp -v $B $MOD_PATH/backup/bundles.properties
fi

# 2) build updated bundles.properties orthogonally (preserves any other custom bundle like 98 VWNoSimPopup)
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
    if (index(line, "# " bname ":") == 1 || line == ("Bundle.Name." bid " = " bname) || line == ("Bundle.Activator." bid " = " bact)) {
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
' "$B" > /tmp/bp_new

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
' /tmp/bp_new)
NAME_CNT=$(awk -v bid="$BID" -v bname="$BNAME" '$0 == ("Bundle.Name." bid " = " bname) {c++} END {print c+0}' /tmp/bp_new)
ACT_CNT=$(awk -v bid="$BID" -v bact="$BACT" '$0 == ("Bundle.Activator." bid " = " bact) {c++} END {print c+0}' /tmp/bp_new)
NEW_CNT=$(awk '/^Bundle\./ {c++} END {print c+0}' /tmp/bp_new)

if [ "$LATE_OK" != "1" ]; then echo "abort: edit check 1 failed"; rm -f /tmp/bp_new; exit 0; fi
if [ "$ACT_CNT" != "1" ]; then echo "abort: edit check 2 failed"; rm -f /tmp/bp_new; exit 0; fi
if [ "$NAME_CNT" != "1" ]; then echo "abort: edit check 3 failed"; rm -f /tmp/bp_new; exit 0; fi
if [ "$NEW_CNT" != "$(( OTHER_CNT + 2 ))" ]; then echo "abort: edit check 4 failed"; rm -f /tmp/bp_new; exit 0; fi

mount -uw /mnt/app

# 1) jar
mkdir -p $JD
for old in $JD/VWClusterNav_*.jar; do [[ -f $old && "${old##*/}" != "$JAR" ]] && rm -fv $old; done
cp -v $MOD_PATH/$JAR $JD/$JAR.new && chmod 644 $JD/$JAR.new && mv -f $JD/$JAR.new $JD/$JAR
ls -l $JD

# 2) install updated bundles.properties atomically
cp -v /tmp/bp_new $B.new && chmod 644 $B.new && mv -f $B.new $B
rm -f /tmp/bp_new
# 3) clean legacy pre-v19 Persist directory so two Persist loops never run side-by-side
[[ ! -e "/mnt/ota" ]] && mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota 2>/dev/null
mount -uw /mnt/ota 2>/dev/null || true
rm -rf /mnt/ota/modkit/Mods/VW_ClusterNav 2>/dev/null
sync
echo "now:"
grep -n 'Startup.Lateload' $B
grep -n 'Name.97' $B
grep -n 'Activator.97' $B
sync
echo "done"
