#!/bin/ksh
# VW MH2p: Disable Car-Net / 众行家 online services HMI availability (5F adaptation 0x5F22:0x0986 = 0).
# Eliminates the recurring "输入用户名" login popup after Car-Net expires or after firmware updates,
# and hides the top-bar Car-Net icon. Does not affect WLAN, Bluetooth, CarPlay, Android Auto, HUD, or Cluster Map.
# Runs in ModKit Post stage (modkit_persist.sh triggers gem-reset afterward so HMI reloads adaptation).

PC=/mnt/app/armle/usr/bin/pc
KEY=0x5F22:0x0986

echo "=== VW_NoCarNet install ($RELEASE_VERSION) ==="
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

"$PC" "b:$KEY:0.0" 0
RC=$?
echo "write exit=$RC"

echo "after ($KEY):"
"$PC" "b:$KEY"
echo "=== VW_NoCarNet install done ==="
