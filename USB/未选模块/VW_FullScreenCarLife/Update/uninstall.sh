#!/bin/ksh
# Undo full-screen Baidu CarLife enhancement in place:
# - Removes LD_PRELOAD=carlife_probe.so from the "carlife" block of smartphone_integrator.json.
# - Reverts ONLY CarLife Cm_429C.jxe (19B) inside skin3/viewhandler.zip via vhpatch_carlife revert
#   (never overwrites CarPlay / AndroidAuto patch state).
# - Reverts ONLY Layout.CarLife.Canvas_Dimension (1673x748 -> 1152x592) inside skin3/info.txt.
# - Removes /mnt/app/eso/bin/apps/carlife_mod and runtime /tmp/carlife_* files.
# Never touches VW_FullScreenCarPlay, VW_HUD_*, or any other module.
# Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.

echo "firmware: $RELEASE_VERSION"
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
mount -uw /mnt/app

. $MOD_PATH/common.sh

# ---- 1. smartphone_integrator.json（精确移除 carlife_probe.so，兼容空/非空 envs 与多库冒号拼接）----
if grep -q "$PR" $SI; then
    sed -e "s#:$PR##g" \
        -e "s#$PR:##g" \
        -e "s#\"LD_PRELOAD=$PR\", ##" \
        -e "s#, \"LD_PRELOAD=$PR\"##" \
        -e "s#\"LD_PRELOAD=$PR\"##" $SI > /tmp/si_cl_un.json
    if [ "$(grep -c "$PR" /tmp/si_cl_un.json)" = "0" ] && same_lines /tmp/si_cl_un.json $SI; then
        mount -uw /mnt/system
        cp -v /tmp/si_cl_un.json $SI
        sync
        echo "smartphone_integrator.json restored"
    else
        echo "warn: smartphone_integrator.json restore check failed, not touched"
    fi
    rm -f /tmp/si_cl_un.json
else
    echo "smartphone_integrator.json does not reference carlife_probe.so, skipped"
fi

# ---- 2. skin3/viewhandler.zip（原地逆向还原 Cm_429C.jxe，保留 CarPlay/AA 状态）----
if [[ -f $MOD_PATH/vhpatch_carlife && -f $P ]]; then
    vh_setup || { echo "warn: vhpatch_carlife cannot run on this unit, viewhandler.zip not touched"; VP=; }
fi
if [ -n "$VP" ]; then
    ST=$(vh_state $VH)
    echo "skin3 viewhandler.zip CarLife: $ST"
    $VP probe $VH $P 2>&1 || true
    if [ "$ST" = "patched" ]; then
        REV_OUT=/tmp/viewhandler_cl_rev.zip
        mkdir -p $MOD_PATH/backup 2>/dev/null && touch $MOD_PATH/backup/.wtest 2>/dev/null && { rm -f $MOD_PATH/backup/.wtest; REV_OUT=$MOD_PATH/backup/viewhandler_cl_rev.zip; }
        rm -f $REV_OUT
        $VP revert $VH $P $REV_OUT
        if [ "$(vh_state $REV_OUT)" = "stock" ]; then
            cp -v $REV_OUT $VH
            sync
            echo "viewhandler.zip CarLife now: $(vh_state $VH)"
        else
            echo "revert failed, viewhandler.zip not touched"
        fi
        rm -f $REV_OUT
    elif [ "$ST" = "stock" ]; then
        echo "viewhandler.zip CarLife already stock"
    else
        echo "viewhandler.zip CarLife is not patched by this mod, not touched"
    fi
    vh_cleanup
fi

# ---- 3. skin3/info.txt（原地还原 CarLife 画布 1673x748 -> 1152x592，保留 CRLF 与 CarPlay 配置）----
if grep -q "Layout.CarLife.Canvas_Dimension.width=1673" $INFO; then
    sed -e 's/^Layout.CarLife.Canvas_Dimension.width=1673/Layout.CarLife.Canvas_Dimension.width=1152/' \
        -e 's/^Layout.CarLife.Canvas_Dimension.height=748/Layout.CarLife.Canvas_Dimension.height=592/' $INFO > /tmp/info_cl_stock.txt
    if grep -q "Layout.CarLife.Canvas_Dimension.width=1152" /tmp/info_cl_stock.txt \
       && grep -q "Layout.CarLife.Canvas_Dimension.height=592" /tmp/info_cl_stock.txt \
       && same_lines /tmp/info_cl_stock.txt $INFO; then
        cp -v /tmp/info_cl_stock.txt $INFO
        sync
        echo "info.txt CarLife restored in place:"; grep CarLife $INFO
    else
        echo "info.txt in-place restore failed, not touched"
    fi
    rm -f /tmp/info_cl_stock.txt
else
    echo "info.txt CarLife not patched, skipped"
fi

# ---- 4. 清理 carlife_mod 目录与运行态临时文件 ----
rm -rf $D
rm -f /tmp/carlife_probe.log /tmp/carlife_media.state /tmp/carlife_res.conf \
      /tmp/carlife_probe_off /tmp/carlife_hud_off /tmp/carlife_force_touch_scale \
      /tmp/carlife_allow_navi_popup /tmp/carlife_no_res_passthrough \
      /tmp/carlife_no_reinit /tmp/carlife_fps_req /tmp/carlife_no_touch_scale \
      /tmp/carlife_bt_audio_lock /tmp/carlife_active /dev/shmem/carlife_h264_shm

sync
echo "done"
