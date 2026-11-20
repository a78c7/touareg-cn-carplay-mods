#!/bin/ksh
# VW MH2p: Restore Car-Net / 众行家 online services HMI availability (5F adaptation 0x5F22:0x0986 = 1).

PC=/mnt/app/armle/usr/bin/pc
KEY=0x5F22:0x0986

echo "=== VW_NoCarNet uninstall ($RELEASE_VERSION) ==="
if [ -n "$OEM" ] && [ "$OEM" != "VW" ]; then
    echo "abort: built for VW MH2p units, this unit is $RELEASE_VERSION"
    exit 0
fi

[[ ! -e "/mnt/app/armle" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
if [ ! -x "$PC" ]; then
    echo "abort: $PC not found"
    exit 0
fi

BEFORE=$("$PC" "b:$KEY" 2>&1)
echo "before ($KEY):"
echo "$BEFORE"
if ! echo "$BEFORE" | grep -q '^00000000:'; then
    echo "abort: failed to read $KEY (persistence not ready or key missing)"
    exit 0
fi

"$PC" "b:$KEY:0.0" 1
RC=$?
echo "write exit=$RC"

echo "after ($KEY):"
"$PC" "b:$KEY"
echo "=== VW_NoCarNet uninstall done ==="
