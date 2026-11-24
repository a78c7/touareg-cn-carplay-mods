package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestV21SportBarsAndChassisDashRoundTrip(t *testing.T) {
	tmp := t.TempDir()
	// 模拟无预置空 Mods/ 目录时也能从 未选模块/ 正常移入 Mods/
	for _, d := range []string{
		"Data", "Meta",
		"未选模块/VW_FullScreenCarPlay",
		"未选模块/AppleCarPlay",
		"未选模块/AndroidAuto",
		"未选模块/VW_HUD_1_RouteGuidance",
		"未选模块/VW_HUD_2_ClusterNav",
		"未选模块/VW_HUD_3_CarPlayCluster/Update",
		"未选模块/VW_HUD_4_OkMapSwitch/Update",
		"未选模块/VW_HUD_5_SiriShortPress/Update",
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

	// 验证前置依赖校验：单独装 mapswitch / siri 而未勾 hud 应被后端拒绝
	badReq := applyReq{
		Features: map[string]string{
			"fullscreen": "skip", "carplay": "skip", "aa": "skip",
			"hud": "skip", "cluster": "skip", "mapswitch": "install", "siri": "skip",
			"wifi5gfix": "skip", "nobtauth": "skip", "nosimpopup": "skip", "wifi": "skip",
		},
		Dash: defaultDash(),
	}
	if _, _, err := p.apply(badReq); err == nil {
		t.Fatalf("expected dependency validation error when mapswitch=install without hud+cluster")
	}
	badSiri := badReq
	badSiri.Features = map[string]string{
		"fullscreen": "skip", "carplay": "skip", "aa": "skip",
		"hud": "skip", "cluster": "skip", "mapswitch": "skip", "siri": "install",
		"wifi5gfix": "skip", "nobtauth": "skip", "nosimpopup": "skip", "wifi": "skip",
	}
	if _, _, err := p.apply(badSiri); err == nil {
		t.Fatalf("expected dependency validation error when siri=install without hud")
	}

	req := applyReq{
		Features: map[string]string{
			"fullscreen": "install",
			"carplay":    "install",
			"aa":         "skip",
			"hud":        "install",
			"cluster":    "install",
			"mapswitch":  "install",
			"siri":       "install",
			"wifi5gfix":  "skip",
			"nobtauth":   "skip",
			"nosimpopup": "skip",
			"wifi":       "skip",
		},
		Dash: dashConf{
			Show:     true,
			Bars:     true,
			Smooth:   true,
			Accent:   "#d926ef",
			Items:    []string{"realspeed", "rpm", "yaw", "cabin", "cond"},
			RShow:    true,
			RItems:   []string{"tires", "wfl", "wfr", "wrl", "wrr", "pitch", "roll", "alt"},
			X:        5,
			Y:        200,
			Alpha:    200,
			TankL:    90,
			LatFlip:  0,
			CarUp:    35,
			AccUp:    55,
			AmapTurn: 3,
			NavBox:   2,
			NavSide:  "right",
			NavTz:    8,
		},
	}

	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("apply v21 dash failed: %v", err)
	}
	if !isDir(filepath.Join(tmp, "Mods", "VW_HUD_4_OkMapSwitch")) {
		t.Fatalf("expected VW_HUD_4_OkMapSwitch in Mods/ when mapswitch=install")
	}
	if !isDir(filepath.Join(tmp, "Mods", "VW_HUD_5_SiriShortPress")) {
		t.Fatalf("expected VW_HUD_5_SiriShortPress in Mods/ when siri=install")
	}

	got, found := p.readDash()
	if !found {
		t.Fatalf("expected dash.conf to be found")
	}
	if !got.Bars || !got.Smooth || !got.OkSwitch || got.NavBox != 2 || got.NavSide != "right" || got.Accent != "#d926ef" {
		t.Fatalf("unexpected v23.5 dash fields: %+v", got)
	}
	if strings.Join(got.Items, ",") != "realspeed,rpm,yaw,cabin,cond" {
		t.Fatalf("unexpected left items: %v", got.Items)
	}
	if strings.Join(got.RItems, ",") != "tires,wfl,wfr,wrl,wrr,pitch,roll,alt" {
		t.Fatalf("unexpected right items: %v", got.RItems)
	}

	// 关闭运动条、平滑动画并卸载 mapswitch
	req.Features["mapswitch"] = "uninstall"
	req.Dash.Bars = false
	req.Dash.Smooth = false
	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("re-apply failed: %v", err)
	}
	if !isFile(filepath.Join(tmp, "Mods", "VW_HUD_4_OkMapSwitch", "uninstall.txt")) {
		t.Fatalf("expected uninstall.txt in Mods/VW_HUD_4_OkMapSwitch")
	}
	got2, _ := p.readDash()
	if got2.Bars || got2.Smooth || got2.OkSwitch {
		t.Fatalf("expected Bars=false Smooth=false OkSwitch=false, got %+v", got2)
	}

	// Switch to record mode and verify AI guide contains 34 real sensor columns
	if _, err := p.setUsbMode("record"); err != nil {
		t.Fatalf("setUsbMode(record) failed: %v", err)
	}
	guideBytes, err := os.ReadFile(filepath.Join(tmp, "drive_logs", aiGuideName))
	if err != nil {
		t.Fatalf("read aiGuide failed: %v", err)
	}
	for _, col := range []string{"pitch_deg", "roll_deg", "alt_m", "tp_fl", "ZF 8HP65A"} {
		if !strings.Contains(string(guideBytes), col) {
			t.Fatalf("aiGuide missing %q", col)
		}
	}
}

func TestRealUSBPackageAllDefaultOffAndUninstallable(t *testing.T) {
	realUSB := filepath.Clean("../../USB")
	if !isDir(filepath.Join(realUSB, "Data")) {
		t.Skip("real ../../USB directory not present in this test environment")
	}
	p := pkg{Root: realUSB}
	st := p.state()
	if len(st.Errors) > 0 {
		t.Fatalf("unexpected USB package errors: %v", st.Errors)
	}
	if len(features) != 11 {
		t.Fatalf("expected 11 streamlined features, got %d", len(features))
	}
	for _, f := range features {
		if st.States[f.ID] != "skip" {
			t.Fatalf("real USB feature %s (%s) must default to skip (off), got %q", f.ID, f.Name, st.States[f.ID])
		}
		for _, d := range f.Dirs {
			upDir := filepath.Join(realUSB, parkDir, d, "Update")
			if !isFile(filepath.Join(upDir, "install.sh")) {
				t.Fatalf("missing install.sh in %s", upDir)
			}
			if !isFile(filepath.Join(upDir, "uninstall.sh")) {
				t.Fatalf("missing uninstall.sh in %s", upDir)
			}
		}
	}
	if st.Dash.OkSwitch || !st.Dash.Bars || !st.Dash.Smooth {
		t.Fatalf("expected OkSwitch=false, Bars=true, Smooth=true by default, got OkSwitch=%v Bars=%v Smooth=%v", st.Dash.OkSwitch, st.Dash.Bars, st.Dash.Smooth)
	}
	if st.Dash.CarUp != 35 || st.Dash.AccUp != 55 || st.Dash.CarUp+st.Dash.AccUp > 90 {
		t.Fatalf("expected CarUp=35 AccUp=55 (sum<=90) by default, got CarUp=%d AccUp=%d", st.Dash.CarUp, st.Dash.AccUp)
	}
}
