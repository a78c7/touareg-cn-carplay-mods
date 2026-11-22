package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestV23ConfigRoundTrip(t *testing.T) {
	root := t.TempDir()
	upd := filepath.Join(root, "Mods", "VW_HUD_3_CarPlayCluster", "Update")
	if err := os.MkdirAll(upd, 0o755); err != nil {
		t.Fatal(err)
	}
	p := pkg{Root: root}

	// Default state: v23_mode off, no custom theme
	st0 := p.readV23()
	if st0.Enabled || st0.HasTheme {
		t.Fatalf("expected default off, got %+v", st0)
	}

	// Reject forbidden API (eval) even outside comments
	if err := validateV23(v23ApplyReq{
		Enabled: true, ThemeAction: "set",
		ThemeCode: "const THEME={id:'x'}; function render(){ eval('1'); }",
	}); err == nil {
		t.Fatal("expected forbidden API error")
	}

	// Allow comments mentioning forbidden APIs (just like my_theme.js header instructions)
	goodTheme := "/* 禁止使用 window / eval */\nconst THEMES = [{ id: 's1', name: '红黑运动', render(ctx, bus) { ctx.clear(0,0,0,1); } }];\n"
	if err := validateV23(v23ApplyReq{Enabled: true, ThemeAction: "set", ThemeCode: goodTheme}); err != nil {
		t.Fatalf("unexpected validation err: %v", err)
	}
	if _, err := p.writeV23(v23ApplyReq{Enabled: true, ThemeAction: "set", ThemeCode: goodTheme}); err != nil {
		t.Fatal(err)
	}
	st1 := p.readV23()
	if !st1.Enabled || !st1.HasTheme || st1.ThemeName != "红黑运动" || st1.ThemeSize != len(goodTheme) {
		t.Fatalf("unexpected state after write: %+v", st1)
	}

	// Oversize > 128KB rejected
	if err := validateV23(v23ApplyReq{Enabled: true, ThemeAction: "set", ThemeCode: strings.Repeat("a", v23MaxThemeBytes+1)}); err == nil {
		t.Fatal("expected oversize rejection")
	}

	// Turn off v23_mode + clear theme
	if _, err := p.writeV23(v23ApplyReq{Enabled: false, ThemeAction: "clear"}); err != nil {
		t.Fatal(err)
	}
	st2 := p.readV23()
	if st2.Enabled || st2.HasTheme {
		t.Fatalf("expected cleared state, got %+v", st2)
	}
}
