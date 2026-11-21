package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestOptionalPopupModsLifecycle(t *testing.T) {
	tmp := t.TempDir()
	// 创建基础 USB 目录结构（所有模块初始均在 未选模块/，默认不开启）
	for _, d := range []string{
		"Data", "Meta", "Mods",
		"未选模块/VW_FullScreenCarPlay",
		"未选模块/AppleCarPlay",
		"未选模块/AndroidAuto",
		"未选模块/VW_HUD_1_RouteGuidance",
		"未选模块/VW_HUD_2_ClusterNav",
		"未选模块/VW_HUD_3_CarPlayCluster/Update",
		"未选模块/VW_HUD_4_OkMapSwitch/Update",
		"未选模块/VW_Wireless5GHzFix",
		"未选模块/VW_NoBtAuthPopup/Update",
		"未选模块/VW_NoSimPopup/Update",
		"未选模块/VW_WiFi_Dev/Update",
	} {
		if err := os.MkdirAll(filepath.Join(tmp, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}

	p := pkg{Root: tmp}
	st := p.state()
	for _, f := range features {
		if st.States[f.ID] != "skip" {
			t.Fatalf("expected default skip state for %s, got %q", f.ID, st.States[f.ID])
		}
	}

	baseReq := func(bt, ns string) applyReq {
		return applyReq{
			Features: map[string]string{
				"fullscreen": "skip",
				"carplay":    "skip",
				"aa":         "skip",
				"hud":        "skip",
				"cluster":    "skip",
				"mapswitch":  "skip",
				"wifi5gfix":  "skip",
				"nobtauth":   bt,
				"nosimpopup": ns,
				"wifi":       "skip",
			},
			Dash: defaultDash(),
		}
	}

	// 1) 安装两个弹窗屏蔽模块 -> 移动到 Mods/ 且无 uninstall.txt
	if _, _, err := p.apply(baseReq("install", "install")); err != nil {
		t.Fatalf("apply install failed: %v", err)
	}
	st = p.state()
	if st.States["nobtauth"] != "install" || st.States["nosimpopup"] != "install" {
		t.Fatalf("expected install state, got nobtauth=%q nosimpopup=%q", st.States["nobtauth"], st.States["nosimpopup"])
	}
	for _, d := range []string{"VW_NoBtAuthPopup", "VW_NoSimPopup"} {
		if !isDir(filepath.Join(tmp, "Mods", d)) {
			t.Fatalf("expected %s in Mods/", d)
		}
	}

	// 2) 卸载 nobtauth、nosimpopup 改回不处理 -> 互不干扰
	if _, _, err := p.apply(baseReq("uninstall", "skip")); err != nil {
		t.Fatalf("apply mixed failed: %v", err)
	}
	st = p.state()
	if st.States["nobtauth"] != "uninstall" || st.States["nosimpopup"] != "skip" {
		t.Fatalf("unexpected mixed state: nobtauth=%q nosimpopup=%q", st.States["nobtauth"], st.States["nosimpopup"])
	}
	if !isFile(filepath.Join(tmp, "Mods", "VW_NoBtAuthPopup", "uninstall.txt")) {
		t.Fatalf("expected uninstall.txt in Mods/VW_NoBtAuthPopup")
	}
	if !isDir(filepath.Join(tmp, "未选模块", "VW_NoSimPopup")) {
		t.Fatalf("expected VW_NoSimPopup parked in 未选模块/")
	}

	// 3) 恢复全部 skip -> 卸载标志清理并回到 未选模块/
	if _, _, err := p.apply(baseReq("skip", "skip")); err != nil {
		t.Fatalf("apply skip failed: %v", err)
	}
	st = p.state()
	if st.States["nobtauth"] != "skip" || st.States["nosimpopup"] != "skip" {
		t.Fatalf("expected skip/skip, got %+v", st.States)
	}
	if isFile(filepath.Join(tmp, "未选模块", "VW_NoBtAuthPopup", "uninstall.txt")) {
		t.Fatalf("uninstall.txt should be removed when switching back to skip")
	}
}
