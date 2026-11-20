package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestOptionalPopupModsLifecycle(t *testing.T) {
	tmp := t.TempDir()
	// 创建基础 USB 目录结构（默认模块在 Mods/，可选弹窗模块在 未选模块/）
	for _, d := range []string{
		"Data", "Meta",
		"Mods/VW_FullScreenCarPlay",
		"Mods/AppleCarPlay",
		"Mods/AndroidAuto",
		"Mods/VW_HUD_1_RouteGuidance",
		"Mods/VW_HUD_2_ClusterNav",
		"Mods/VW_HUD_3_CarPlayCluster/Update",
		"未选模块/VW_Wireless5GHzFix",
		"未选模块/VW_NoCarNet/Post",
		"未选模块/VW_NoSimPopup/Update",
		"未选模块/VW_WiFi_Dev/Update",
	} {
		if err := os.MkdirAll(filepath.Join(tmp, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}

	p := pkg{Root: tmp}
	st := p.state()
	if st.States["nocarnet"] != "skip" || st.States["nosimpopup"] != "skip" {
		t.Fatalf("expected default skip state, got nocarnet=%q nosimpopup=%q", st.States["nocarnet"], st.States["nosimpopup"])
	}

	baseReq := func(nc, ns string) applyReq {
		return applyReq{
			Features: map[string]string{
				"fullscreen": "skip",
				"carplay":    "skip",
				"aa":         "skip",
				"hud":        "skip",
				"cluster":    "skip",
				"wifi5gfix":  "skip",
				"nocarnet":   nc,
				"nosimpopup": ns,
				"wifi":       "skip",
			},
			Dash: defaultDash(),
		}
	}

	// 1) 安装两个弹窗模块 -> 移动到 Mods/ 且无 uninstall.txt
	if _, _, err := p.apply(baseReq("install", "install")); err != nil {
		t.Fatalf("apply install failed: %v", err)
	}
	st = p.state()
	if st.States["nocarnet"] != "install" || st.States["nosimpopup"] != "install" {
		t.Fatalf("expected install state, got nocarnet=%q nosimpopup=%q", st.States["nocarnet"], st.States["nosimpopup"])
	}
	if !isDir(filepath.Join(tmp, "Mods", "VW_NoCarNet")) || !isDir(filepath.Join(tmp, "Mods", "VW_NoSimPopup")) {
		t.Fatalf("expected folders in Mods/")
	}

	// 2) 卸载 nocarnet、保留 nosimpopup 为不处理 -> 验证互不干扰
	if _, _, err := p.apply(baseReq("uninstall", "skip")); err != nil {
		t.Fatalf("apply uninstall/skip failed: %v", err)
	}
	st = p.state()
	if st.States["nocarnet"] != "uninstall" || st.States["nosimpopup"] != "skip" {
		t.Fatalf("expected uninstall/skip, got nocarnet=%q nosimpopup=%q", st.States["nocarnet"], st.States["nosimpopup"])
	}
	if !isFile(filepath.Join(tmp, "Mods", "VW_NoCarNet", "uninstall.txt")) {
		t.Fatalf("expected uninstall.txt in Mods/VW_NoCarNet")
	}
	if !isDir(filepath.Join(tmp, "未选模块", "VW_NoSimPopup")) {
		t.Fatalf("expected VW_NoSimPopup parked in 未选模块/")
	}

	// 3) 恢复全部 skip -> 卸载标志清理并回到 未选模块/
	if _, _, err := p.apply(baseReq("skip", "skip")); err != nil {
		t.Fatalf("apply skip failed: %v", err)
	}
	st = p.state()
	if st.States["nocarnet"] != "skip" || st.States["nosimpopup"] != "skip" {
		t.Fatalf("expected skip/skip, got nocarnet=%q nosimpopup=%q", st.States["nocarnet"], st.States["nosimpopup"])
	}
	if isFile(filepath.Join(tmp, "未选模块", "VW_NoCarNet", "uninstall.txt")) {
		t.Fatalf("uninstall.txt should be removed when switching back to skip")
	}
}
