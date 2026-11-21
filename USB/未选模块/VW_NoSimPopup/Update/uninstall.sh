#!/bin/ksh
# VW MH2p: Remove no_sim_dataonly popup filter flag; remove shared VWPopupFilter (bundle 98) only if no other filter flags remain.

B=/mnt/app/eso/hmi/lsd/config/bundles.properties
FDIR=/mnt/app/eso/hmi/lsd/config/popup_filter
FLAG_NAME=no_sim_dataonly
JD=/mnt/app/eso/hmi/lsd/jars
BID=98
BNAME=VWPopupFilter
BACT=vwpopup.Activator

echo "=== VW_NoSimPopup uninstall ($RELEASE_VERSION) ==="
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -f "$B" ]] && { echo "abort: $B not found"; exit 0; }

mount -uw /mnt/app
rm -fv "$FDIR/$FLAG_NAME"

if [ -f "$FDIR/no_sim_dataonly" ] || [ -f "$FDIR/no_bt_auth" ]; then
    echo "other popup filter flag(s) still active; keeping VWPopupFilter bundle $BID:"
    ls -la "$FDIR"
    sync
    echo "=== VW_NoSimPopup uninstall done ==="
    exit 0
fi

rmdir "$FDIR" 2>/dev/null || true

if [ "$(grep -c '^Bundle\.Startup\.Lateload[ 	]*=' "$B")" != "1" ]; then
    echo "abort: expected 1 Bundle.Startup.Lateload line in $B"
    exit 0
fi

OTHER_CNT=$(awk -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
/^Bundle\./ {
    line = $0; sub(/\r$/, "", line)
    if (line == ("Bundle.Name." bid " = " bname) || line == ("Bundle.Name." bid " = VWNoSimPopup") ||
        line == ("Bundle.Activator." bid " = " bact) || line == ("Bundle.Activator." bid " = vwnosim.Activator")) next
    c++
}
END { print c+0 }
' "$B")

awk -v bid="$BID" -v bname="$BNAME" -v bact="$BACT" '
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
}
' "$B" > /tmp/bp_pf_un

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
' /tmp/bp_pf_un)
NEW_CNT=$(awk '/^Bundle\./ {c++} END {print c+0}' /tmp/bp_pf_un)

if [ "$LATE_OK" != "1" ] || [ "$NEW_CNT" != "$OTHER_CNT" ]; then
    echo "abort: restore verification failed (late=$LATE_OK cnt=$NEW_CNT want=$OTHER_CNT)"
    rm -f /tmp/bp_pf_un
    exit 0
fi

cp -v /tmp/bp_pf_un "$B.new" && chmod 644 "$B.new" && mv -f "$B.new" "$B"
rm -f /tmp/bp_pf_un
rm -fv "$JD/VWPopupFilter.jar" "$JD/VWNoSimPopup.jar" "$JD/VWNoSimPopup_"*.jar
sync
grep -n 'Startup.Lateload' "$B"
echo "=== VW_NoSimPopup uninstall done ==="
