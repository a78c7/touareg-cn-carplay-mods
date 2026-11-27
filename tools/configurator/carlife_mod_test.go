package main

import (
	"os"
	"path/filepath"
	"testing"
)

func allSkipFeatures() map[string]string {
	m := make(map[string]string, len(features))
	for _, f := range features {
		m[f.ID] = "skip"
	}
	return m
}

func TestCarLifeFeatureMetadataAndPosition(t *testing.T) {
	aaIdx, clIdx, hudIdx := -1, -1, -1
	var cl feature
	for i, f := range features {
		switch f.ID {
		case "aa":
			aaIdx = i
		case "carlife":
			clIdx = i
			cl = f
		case "hud":
			hudIdx = i
		}
	}
	if clIdx < 0 {
		t.Fatalf("carlife feature not registered in features")
	}
	if clIdx != aaIdx+1 || hudIdx != clIdx+1 {
		t.Fatalf("expected carlife positioned right after aa and before hud, got aa=%d carlife=%d hud=%d", aaIdx, clIdx, hudIdx)
	}
	if len(cl.Dirs) != 1 || cl.Dirs[0] != "VW_FullScreenCarLife" {
		t.Fatalf("expected carlife Dirs=[VW_FullScreenCarLife], got %v", cl.Dirs)
	}
}

func TestCarLifeModLifecycleAndCarPlayCoexistence(t *testing.T) {
	root := makeFakeUSB(t)
	p := pkg{Root: root}

	// 1. 默认关闭（位于 未选模块/，状态为 skip）
	st := p.state()
	if st.States["carlife"] != "skip" {
		t.Fatalf("expected default carlife state skip, got %q", st.States["carlife"])
	}
	if isDir(filepath.Join(root, "Mods", "VW_FullScreenCarLife")) {
		t.Fatalf("VW_FullScreenCarLife must not be in Mods/ by default")
	}
	if !isDir(filepath.Join(root, parkDir, "VW_FullScreenCarLife")) {
		t.Fatalf("expected VW_FullScreenCarLife parked in %s/ by default", parkDir)
	}

	// 2. 面向纯苹果车主的快捷预设（base / full）绝不顺带开启 carlife
	for _, preset := range []string{"base", "full"} {
		if code := runCLI([]string{"--root", root, "--preset", preset, "--verify"}); code != 0 {
			t.Fatalf("--preset %s failed with exit code %d", preset, code)
		}
		if loc, err := p.location("VW_FullScreenCarLife"); err != nil || loc != "park" {
			t.Fatalf("--preset %s must keep VW_FullScreenCarLife parked, got loc=%q err=%v", preset, loc, err)
		}
	}

	// 3. 单独安装 carlife（无强制前置依赖）-> 正确落盘 Mods/VW_FullScreenCarLife，自检通过
	req := applyReq{Features: allSkipFeatures(), Dash: defaultDash()}
	req.Features["carlife"] = "install"
	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("apply carlife=install alone failed: %v", err)
	}
	if !isDir(filepath.Join(root, "Mods", "VW_FullScreenCarLife")) {
		t.Fatalf("expected Mods/VW_FullScreenCarLife after install")
	}
	if isDir(filepath.Join(root, parkDir, "VW_FullScreenCarLife")) {
		t.Fatalf("VW_FullScreenCarLife should be moved out of %s/", parkDir)
	}
	if isFile(filepath.Join(root, "Mods", "VW_FullScreenCarLife", "uninstall.txt")) {
		t.Fatalf("unexpected uninstall.txt on install")
	}
	if rep := p.verify(); !rep.OK {
		t.Fatalf("expected verify OK for standalone carlife install, got errors=%v", rep.Errors)
	}

	// 4. 与全屏 CarPlay (fullscreen) 同时安装 -> 零冲突共存
	req.Features["fullscreen"] = "install"
	req.Features["carlife"] = "install"
	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("apply fullscreen+carlife coexistence failed: %v", err)
	}
	st = p.state()
	if st.States["fullscreen"] != "install" || st.States["carlife"] != "install" {
		t.Fatalf("expected both fullscreen and carlife installed, got fullscreen=%q carlife=%q", st.States["fullscreen"], st.States["carlife"])
	}
	for _, d := range []string{"VW_FullScreenCarPlay", "VW_FullScreenCarLife"} {
		if !isDir(filepath.Join(root, "Mods", d)) {
			t.Fatalf("expected Mods/%s present during coexistence", d)
		}
	}
	if rep := p.verify(); !rep.OK {
		t.Fatalf("expected verify OK for fullscreen+carlife coexistence, got errors=%v", rep.Errors)
	}

	// 5. 卸载 carlife 时不影响 fullscreen
	req.Features["carlife"] = "uninstall"
	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("apply carlife=uninstall failed: %v", err)
	}
	if !isFile(filepath.Join(root, "Mods", "VW_FullScreenCarLife", "uninstall.txt")) {
		t.Fatalf("expected uninstall.txt in Mods/VW_FullScreenCarLife")
	}
	if isFile(filepath.Join(root, "Mods", "VW_FullScreenCarPlay", "uninstall.txt")) {
		t.Fatalf("uninstalling carlife must not touch VW_FullScreenCarPlay")
	}

	// 6. 取消勾选（改为 skip）-> 从 Mods/ 彻底移除并清理 uninstall.txt，fullscreen 保持完好
	req.Features["carlife"] = "skip"
	if _, _, err := p.apply(req); err != nil {
		t.Fatalf("apply carlife=skip failed: %v", err)
	}
	if isDir(filepath.Join(root, "Mods", "VW_FullScreenCarLife")) {
		t.Fatalf("Mods/VW_FullScreenCarLife must be completely removed when unchecked (skip)")
	}
	if !isDir(filepath.Join(root, parkDir, "VW_FullScreenCarLife")) {
		t.Fatalf("expected VW_FullScreenCarLife moved back to %s/", parkDir)
	}
	if isFile(filepath.Join(root, parkDir, "VW_FullScreenCarLife", "uninstall.txt")) {
		t.Fatalf("uninstall.txt must be removed when switching carlife back to skip")
	}
	if !isDir(filepath.Join(root, "Mods", "VW_FullScreenCarPlay")) {
		t.Fatalf("VW_FullScreenCarPlay must remain intact in Mods/")
	}

	// 7. CLI --install fullscreen,carlife 与 --skip carlife 端到端验证
	if code := runCLI([]string{"--root", root, "--install", "fullscreen,carlife", "--verify"}); code != 0 {
		t.Fatalf("CLI --install fullscreen,carlife failed with exit code %d", code)
	}
	if !isDir(filepath.Join(root, "Mods", "VW_FullScreenCarLife")) || !isDir(filepath.Join(root, "Mods", "VW_FullScreenCarPlay")) {
		t.Fatalf("expected both modules in Mods/ after CLI --install")
	}
	if code := runCLI([]string{"--root", root, "--skip", "carlife", "--verify"}); code != 0 {
		t.Fatalf("CLI --skip carlife failed with exit code %d", code)
	}
	if _, err := os.Stat(filepath.Join(root, "Mods", "VW_FullScreenCarLife")); !os.IsNotExist(err) {
		t.Fatalf("expected Mods/VW_FullScreenCarLife removed after CLI --skip carlife, err=%v", err)
	}
}
