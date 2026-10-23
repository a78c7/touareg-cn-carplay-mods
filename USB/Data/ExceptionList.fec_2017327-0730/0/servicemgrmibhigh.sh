#!/bin/sh

/eso/bin/servicemgrmibhigh0 &

if [[ -e /mnt/ota/modkit/modkit_persist.sh ]]; then
    /bin/ksh /mnt/ota/modkit/modkit_persist.sh > /mnt/ota/modkit/Logs/_ModKit_Persist.log 2>&1 &
fi
