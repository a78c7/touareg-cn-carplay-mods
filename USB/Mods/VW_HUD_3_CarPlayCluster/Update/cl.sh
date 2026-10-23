#!/bin/ksh
# CarPlay cluster helper (v15). Live: va N | ui N | idr | zoomin N | zoomout N
# Next connect: tall H [croptop]   (writes /tmp/cl.conf, then reconnect CarPlay)
# Other: st (status) | save (make /tmp/cl.conf permanent) | reset (forget /tmp/cl.conf) | log
D=/mnt/app/eso/bin/apps/cluster
T=/tmp/cl.conf
conf_set() { # key value
    [ -f $T ] || cp $D/cl.conf $T 2>/dev/null || : > $T
    grep -v "^$1=" $T > $T.n; echo "$1=$2" >> $T.n; mv -f $T.n $T; chmod 644 $T
}
case "$1" in
    va|ui|idr|zoomin|zoomout|replay) echo "$*" > /tmp/cl_alt_cmd; sleep 1; cat /tmp/cl_status ;;
    tall) conf_set tall "${2:-960}"; [ -n "$3" ] && conf_set croptop "$3"; echo "next CarPlay connect uses:"; cat $T ;;
    set) conf_set "$2" "$3"; cat $T ;;
    save) mount -uw /mnt/app; cp -v $T $D/cl.conf; sync ;;
    reset) rm -f $T; echo "using $D/cl.conf"; cat $D/cl.conf ;;
    log) tail -n 40 /tmp/cl_probe.log; tail -n 10 /tmp/clusterd.log; tail -n 20 /tmp/vwcn.log ;;
    *) cat /tmp/cl_status 2>/dev/null; echo; cat ${T} 2>/dev/null || cat $D/cl.conf ;;
esac
