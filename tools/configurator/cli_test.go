package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func makeFakeUSB(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	for _, d := range []string{"Data", "Meta", "Mods", parkDir} {
		if err := os.MkdirAll(filepath.Join(root, d), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	for _, f := range features {
		for _, dir := range f.Dirs {
			upd := filepath.Join(root, parkDir, dir, "Update")
			if err := os.MkdirAll(upd, 0o755); err != nil {
				t.Fatal(err)
			}
		}
	}
	// 放一份自定义 dash.conf 以验证 --deploy-theme 不会误覆盖
	dashPath := filepath.Join(root, parkDir, "VW_HUD_3_CarPlayCluster", "Update", "dash.conf")
	if err := os.WriteFile(dashPath, []byte("items=rpm,oil\nshow=1\ncar_up=55\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	return root
}

func TestCLIDeployThemeAndVerify(t *testing.T) {
	root := makeFakeUSB(t)
	p := pkg{Root: root}

	// 1. 初始状态全部在「未选模块」，自检必须报错（防止空刷）
	rep0 := p.verify()
	if rep0.OK {
		t.Fatalf("expected verify failure when Mods is empty")
	}

	// 2. 放一个非法皮肤（含 window），--deploy-theme 应拒绝且不改动目录
	badTheme := filepath.Join(t.TempDir(), "bad.js")
	if err := os.WriteFile(badTheme, []byte("const THEME={name:'x'}; function render(){ return window.innerWidth; }"), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := runCLI([]string{"--root", root, "--deploy-theme", badTheme}); code == 0 {
		t.Fatalf("expected non-zero exit for invalid theme")
	}
	if loc, _ := p.location("VW_HUD_3_CarPlayCluster"); loc != "park" {
		t.Fatalf("expected VW_HUD_3_CarPlayCluster untouched in park, got %q", loc)
	}

	// 3. 放一个 Mac 隐藏文件，跑合法皮肤部署 + 自检
	junk := filepath.Join(root, "._Data")
	_ = os.WriteFile(junk, []byte("junk"), 0o644)
	goodTheme := filepath.Join(t.TempDir(), "my_theme.js")
	goodCode := "const THEMES = [{ id: 'neon', name: '暗夜霓虹', render(ctx, bus) { ctx.clear(0,0,0,1); } }];\n"
	if err := os.WriteFile(goodTheme, []byte(goodCode), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := runCLI([]string{"--root", root, "--deploy-theme", goodTheme, "--verify", "--json"}); code != 0 {
		t.Fatalf("expected 0 exit for valid --deploy-theme, got %d", code)
	}

	// 验证依赖自动补齐、皮肤写入、v23_mode 存在、dash.conf 保留、Mac 垃圾已清
	for _, d := range []string{"VW_HUD_1_RouteGuidance", "VW_HUD_2_ClusterNav", "VW_HUD_3_CarPlayCluster"} {
		if loc, _ := p.location(d); loc != "mods" {
			t.Fatalf("expected %s in mods, got %q", d, loc)
		}
	}
	upd := filepath.Join(root, "Mods", "VW_HUD_3_CarPlayCluster", "Update")
	if !isFile(filepath.Join(upd, "v23_mode")) || isFile(filepath.Join(upd, "v23_off")) {
		t.Fatalf("expected v23_mode present and v23_off absent")
	}
	gotDash, err := os.ReadFile(filepath.Join(upd, "dash.conf"))
	if err != nil || !strings.Contains(string(gotDash), "car_up=55") {
		t.Fatalf("expected existing dash.conf preserved, got %q err=%v", string(gotDash), err)
	}
	if isFile(junk) {
		t.Fatalf("expected macOS junk file %s removed", junk)
	}

	// 4. 模拟未安全弹出导致皮肤截断为 0 字节 -> verify 必须抓出来
	if err := os.WriteFile(filepath.Join(upd, "themes_user", "my_theme.js"), nil, 0o644); err != nil {
		t.Fatal(err)
	}
	repTrunc := p.verify()
	if repTrunc.OK {
		t.Fatalf("expected verify failure on 0-byte truncated my_theme.js")
	}
}

func TestCLIVerifyCatchesMissingDepAndRecordMode(t *testing.T) {
	root := makeFakeUSB(t)
	p := pkg{Root: root}

	// 手工只移 VW_HUD_3_CarPlayCluster 进 Mods（漏掉 HUD_1 / HUD_2）-> verify 应报错
	if err := os.Rename(
		filepath.Join(root, parkDir, "VW_HUD_3_CarPlayCluster"),
		filepath.Join(root, "Mods", "VW_HUD_3_CarPlayCluster"),
	); err != nil {
		t.Fatal(err)
	}
	rep := p.verify()
	if rep.OK {
		t.Fatalf("expected verify failure when hud dependency is missing")
	}

	// 用 --preset full 一键修好 -> verify 应通过
	if code := runCLI([]string{"--root", root, "--preset", "full", "--verify"}); code != 0 {
		t.Fatalf("expected --preset full to succeed, got %d", code)
	}

	// 切到行车记录模式 -> verify 应报阻塞错误
	if _, err := p.setUsbMode("record"); err != nil {
		t.Fatal(err)
	}
	repRec := p.verify()
	if repRec.OK {
		t.Fatalf("expected verify failure in record mode")
	}
}
