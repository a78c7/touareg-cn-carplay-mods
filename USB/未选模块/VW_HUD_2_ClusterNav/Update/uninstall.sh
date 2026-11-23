#!/bin/ksh
# Orthogonally remove VWClusterNav (bundle 97) from bundles.properties and delete VWClusterNav jar,
# preserving any other installed bundles (such as bundle 98 VWPopupFilter).
B=/mnt/app/eso/hmi/lsd/config/bundles.properties
BID=97
BNAME=VWClusterNav
BACT=vwcn.Activator

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -f "$B" ]] && { echo "abort: $B not found"; exit 0; }

if [ "$(grep -c '^Bundle\.Startup\.Lateload[ 	]*=' "$B")" != "1" ]; then
    echo "abort: expected 1 Bundle.Startup.Lateload line in $B"; exit 0
fi

OTHER_CNT=$(awk -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
/^Bundle\./ {
    line = $0; sub(/\r$/, "", line)
    if (line == ("Bundle.Name." bid " = " bname) || line == ("Bundle.Activator." bid " = " bact)) next
    c++
}
END { print c+0 }
' "$B")

awk -v mode="del" -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
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
    if (hit == 0 && total >= 1) ok = 1
}
END { print (lc == 1 && ok == 1) ? 1 : 0 }
' /tmp/bp_new)
NEW_CNT=$(awk '/^Bundle\./ {c++} END {print c+0}' /tmp/bp_new)

if [ "$LATE_OK" != "1" ] || [ "$NEW_CNT" != "$OTHER_CNT" ]; then
    echo "abort: restore verification failed (late=$LATE_OK cnt=$NEW_CNT want=$OTHER_CNT)"
    rm -f /tmp/bp_new
    exit 0
fi

mount -uw /mnt/app
cp -v /tmp/bp_new $B.new && chmod 644 $B.new && mv -f $B.new $B
rm -f /tmp/bp_new
echo "bundles.properties updated:"; grep -n 'Startup.Lateload' $B
rm -fv /mnt/app/eso/hmi/lsd/jars/VWClusterNav_*.jar
[[ ! -e "/mnt/ota" ]] && mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota 2>/dev/null
mount -uw /mnt/ota 2>/dev/null || true
rm -rf /mnt/ota/modkit/Mods/VW_ClusterNav 2>/dev/null
sync
echo "done"
