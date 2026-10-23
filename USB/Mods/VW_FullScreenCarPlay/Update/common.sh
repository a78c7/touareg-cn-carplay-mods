# shared by install.sh / uninstall.sh (sourced). Sets VP to a runnable copy of vhpatch.
P=$MOD_PATH/fullscreen.patch
T=/mnt/app/eso/hmi/lsd/Resources/skin3
VH=$T/viewhandler.zip
INFO=$T/info.txt
VP=
vh_setup() {
    # the USB drive may be mounted noexec: run a copy from /tmp, else from /mnt/app
    for d in /tmp /mnt/app/eso/bin/apps; do
        cp $MOD_PATH/vhpatch $d/vhpatch 2>/dev/null && chmod 755 $d/vhpatch 2>/dev/null
        if [ -n "$($d/vhpatch check $VH $P 2>/dev/null)" ]; then VP=$d/vhpatch; return 0; fi
        rm -f $d/vhpatch 2>/dev/null
    done
    return 1
}
vh_state() { $VP check "$1" $P 2>/dev/null; }
vh_cleanup() { [ -n "$VP" ] && rm -f $VP; }
# same line count, non-empty (awk: the ModKit USB install environment has no wc)
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
