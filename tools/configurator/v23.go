package main

// v23 60fps AI custom theme configuration inside VW_HUD_3_CarPlayCluster/Update/:
//   - v23_mode                 : opt-in flag (default absent -> head unit runs stable v22.1 path)
//   - themes_user/my_theme.js  : optional single-file 1..8 skin JS bundle written once via USB flash

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"unicode/utf8"
)

const v23MaxThemeBytes = 128 * 1024

type v23State struct {
	Enabled   bool   `json:"enabled"`
	HasTheme  bool   `json:"hasTheme"`
	ThemeSize int    `json:"themeSize"`
	ThemeName string `json:"themeName"`
}

type v23ApplyReq struct {
	Enabled     bool   `json:"enabled"`
	ThemeAction string `json:"themeAction"` // "keep" | "set" | "clear"
	ThemeCode   string `json:"themeCode,omitempty"`
}

var (
	reV23BlockCmt = regexp.MustCompile(`(?s)/\*.*?\*/`)
	reV23LineCmt  = regexp.MustCompile(`//[^\n]*`)
	reV23Forbid   = regexp.MustCompile(`\b(window|document|fetch|XMLHttpRequest|WebSocket|localStorage|sessionStorage|eval|Function|importScripts)\b`)
	reV23Name     = regexp.MustCompile(`\bname\s*:\s*["']([^"']{1,60})["']`)
)

func (p pkg) v23Dir() string {
	d := p.modPath("VW_HUD_3_CarPlayCluster")
	if d == "" {
		return ""
	}
	return filepath.Join(d, "Update")
}

func (p pkg) readV23() v23State {
	var st v23State
	dir := p.v23Dir()
	if dir == "" {
		return st
	}
	tp := filepath.Join(dir, "themes_user", "my_theme.js")
	if b, err := os.ReadFile(tp); err == nil && len(b) > 0 {
		st.HasTheme = true
		st.ThemeSize = len(b)
		if m := reV23Name.FindSubmatch(b); len(m) >= 2 {
			st.ThemeName = string(m[1])
		} else {
			st.ThemeName = "my_theme.js"
		}
	}
	// 与车机 install.sh 保持一致：有 v23_mode，或存在非空自定义皮肤且未显式写 v23_off，均视作启用 v23
	st.Enabled = isFile(filepath.Join(dir, "v23_mode")) || (st.HasTheme && !isFile(filepath.Join(dir, "v23_off")))
	return st
}

func validateV23(r v23ApplyReq) error {
	if r.ThemeAction != "" && r.ThemeAction != "keep" && r.ThemeAction != "set" && r.ThemeAction != "clear" {
		return fmt.Errorf("无效的 v23 皮肤操作：%s", r.ThemeAction)
	}
	if r.ThemeAction != "set" {
		return nil
	}
	if len(r.ThemeCode) == 0 || len(r.ThemeCode) > v23MaxThemeBytes {
		return fmt.Errorf("自定义皮肤大小不合法（当前 %d B，需 1 B ～ 128 KB）", len(r.ThemeCode))
	}
	if !utf8.ValidString(r.ThemeCode) {
		return fmt.Errorf("自定义皮肤文件必须是 UTF-8 纯文本")
	}
	stripped := reV23LineCmt.ReplaceAllString(reV23BlockCmt.ReplaceAllString(r.ThemeCode, ""), "")
	if m := reV23Forbid.FindString(stripped); m != "" {
		return fmt.Errorf("自定义皮肤包含车机禁止接口：%s（仅允许使用 ctx / bus 绘制）", m)
	}
	if !strings.Contains(stripped, "render") && !strings.Contains(stripped, "THEMES") {
		return fmt.Errorf("自定义皮肤缺少 THEMES 数组或 render(ctx, bus) 函数")
	}
	return nil
}

func (p pkg) writeV23(r v23ApplyReq) ([]string, error) {
	dir := p.v23Dir()
	if dir == "" {
		return nil, fmt.Errorf("找不到 VW_HUD_3_CarPlayCluster/Update 目录")
	}
	var logs []string
	userDir := filepath.Join(dir, "themes_user")
	themePath := filepath.Join(userDir, "my_theme.js")
	switch r.ThemeAction {
	case "set":
		if err := os.MkdirAll(userDir, 0o755); err != nil {
			return logs, err
		}
		if err := writeFileSync(themePath, []byte(r.ThemeCode), 0o644); err != nil {
			return logs, err
		}
		logs = append(logs, fmt.Sprintf("已写入自定义 AI 皮肤到 U 盘：themes_user/my_theme.js（%.1f KB，上车插 U 盘一次生效）", float64(len(r.ThemeCode))/1024.0))
	case "clear":
		if isFile(themePath) {
			if err := os.Remove(themePath); err != nil {
				return logs, err
			}
		}
		_ = os.Remove(userDir)
		logs = append(logs, "已清除 U 盘自定义皮肤（恢复使用内置官方默认 60fps 经典版）")
	}

	flagPath := filepath.Join(dir, "v23_mode")
	offPath := filepath.Join(dir, "v23_off")
	if r.Enabled {
		if err := writeFileSync(flagPath, []byte("1\n"), 0o644); err != nil {
			return logs, err
		}
		if isFile(offPath) {
			_ = os.Remove(offPath)
		}
		logs = append(logs, "v23 60fps 皮肤引擎（Beta）：已启用（异常自动退回 v22.1 经典通道）")
	} else {
		if isFile(flagPath) {
			if err := os.Remove(flagPath); err != nil {
				return logs, err
			}
		}
		if isFile(themePath) {
			if err := writeFileSync(offPath, []byte("1\n"), 0o644); err != nil {
				return logs, err
			}
		} else if isFile(offPath) {
			_ = os.Remove(offPath)
		}
		logs = append(logs, "v23 60fps 皮肤引擎（Beta）：关闭（默认稳跑 v22.1 经典通道）")
	}
	return logs, nil
}
