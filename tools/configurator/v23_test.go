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

	// Reject forbidden API (eval / setTimeout / setInterval) even outside comments
	for _, bad := range []string{
		"const THEME={id:'x'}; function render(){ eval('1'); }",
		"const THEME={id:'x'}; function render(){ setTimeout(function(){}, 10); }",
		"const THEME={id:'x'}; function render(){ setInterval(function(){}, 10); }",
	} {
		if err := validateV23(v23ApplyReq{
			Enabled: true, ThemeAction: "set", ThemeCode: bad,
		}); err == nil {
			t.Fatalf("expected forbidden API error for %q", bad)
		}
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

	// Turn off v23_mode while keeping custom theme -> writes v23_off so install.sh respects opt-out
	if _, err := p.writeV23(v23ApplyReq{Enabled: false, ThemeAction: "keep"}); err != nil {
		t.Fatal(err)
	}
	if !isFile(filepath.Join(upd, "v23_off")) || isFile(filepath.Join(upd, "v23_mode")) {
		t.Fatalf("expected v23_off present and v23_mode removed")
	}
	stOff := p.readV23()
	if stOff.Enabled || !stOff.HasTheme {
		t.Fatalf("expected disabled with kept theme, got %+v", stOff)
	}

	// Simulate AI manually dropping my_theme.js without v23_mode or v23_off -> treated as enabled (matches install.sh auto-enable)
	_ = os.Remove(filepath.Join(upd, "v23_off"))
	stAuto := p.readV23()
	if !stAuto.Enabled || !stAuto.HasTheme {
		t.Fatalf("expected auto-enabled when theme exists without v23_off, got %+v", stAuto)
	}

	// Turn off v23_mode + clear theme -> removes v23_off too
	if _, err := p.writeV23(v23ApplyReq{Enabled: false, ThemeAction: "clear"}); err != nil {
		t.Fatal(err)
	}
	if isFile(filepath.Join(upd, "v23_off")) || isFile(filepath.Join(upd, "v23_mode")) {
		t.Fatalf("expected both v23_mode and v23_off absent after clear")
	}
	st2 := p.readV23()
	if st2.Enabled || st2.HasTheme {
		t.Fatalf("expected cleared state, got %+v", st2)
	}
}
