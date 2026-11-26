package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestOpenSSHKeyGenAndWpaConf(t *testing.T) {
	priv, pub, err := genEd25519OpenSSH("test-owner")
	if err != nil {
		t.Fatalf("genEd25519OpenSSH: %v", err)
	}
	tmp := t.TempDir()
	privFile := filepath.Join(tmp, "id_ed25519")
	if err := os.WriteFile(privFile, []byte(priv), 0o600); err != nil {
		t.Fatal(err)
	}
	// 使用系统真实的 ssh-keygen 校验私钥完整性并反推公钥比对
	if _, err := exec.LookPath("ssh-keygen"); err == nil {
		derived, err := exec.Command("ssh-keygen", "-y", "-f", privFile).CombinedOutput()
		if err != nil {
			t.Fatalf("ssh-keygen -y failed: %v\n%s", err, string(derived))
		}
		wantFields := strings.Fields(pub)
		gotFields := strings.Fields(string(derived))
		if len(gotFields) < 2 || gotFields[0] != wantFields[0] || gotFields[1] != wantFields[1] {
			t.Fatalf("derived pubkey mismatch:\ngot:  %s\nwant: %s", string(derived), pub)
		}
	}

	// 测试含英文单引号及 5 个热点的双向生成与解析去重
	conf := buildWpaConf([]wifiNet{
		{SSID: "Phone's Hotspot", PSK: "12345678"},
		{SSID: "HomeWiFi_5G", PSK: "abcdefgh99"},
		{SSID: "Net3", PSK: "pass33333"},
		{SSID: "Net4", PSK: "pass44444"},
		{SSID: "Net5", PSK: "pass55555"},
	})
	rawNets := parseWpaNetworks(conf)
	if len(rawNets) != 6 {
		t.Fatalf("expected 6 raw wpa blocks (5 networks + curly apostrophe variant), got %d", len(rawNets))
	}

	// 模拟 readWifi 合并回显
	p := pkg{Root: tmp}
	upDir := filepath.Join(tmp, "未选模块", "VW_WiFi_Dev", "Update")
	if err := os.MkdirAll(upDir, 0o755); err != nil {
		t.Fatal(err)
	}
	_ = os.WriteFile(filepath.Join(upDir, "wpa.conf"), []byte(conf), 0o600)
	_ = os.WriteFile(filepath.Join(upDir, "web.pin"), []byte("654321\n"), 0o600)

	st := p.readWifi()
	if st.Pin != "654321" || len(st.Networks) != 5 || st.Networks[0].SSID != "Phone's Hotspot" || st.Networks[4].SSID != "Net5" {
		t.Fatalf("unexpected readWifi state: %+v", st)
	}
}

func TestValidateWifiPinPolicy(t *testing.T) {
	baseNet := []wifiNet{{SSID: "MyHotspot", PSK: "12345678"}}

	for _, bad := range []string{"", "1234", "12345", "12345678901234567", "1234 56", "abc-123"} {
		if err := validateWifi(wifiReq{Networks: baseNet, Pin: bad}); err == nil {
			t.Fatalf("expected pin %q to be rejected", bad)
		}
	}
	for _, good := range []string{"654321", "Touareg2026", "Ab12Cd34Ef56Gh78"} {
		if err := validateWifi(wifiReq{Networks: baseNet, Pin: good}); err != nil {
			t.Fatalf("expected pin %q to pass, got error: %v", good, err)
		}
	}
	rp := randomPin6()
	if len(rp) != 6 || rp[0] == '0' {
		t.Fatalf("unexpected randomPin6: %q", rp)
	}
}

func TestWifiUpdateScriptWithoutQnxTr(t *testing.T) {
	scriptPath := filepath.Join("..", "..", "USB", "未选模块", "VW_WiFi_Dev", "Update", "install.sh")
	raw, err := os.ReadFile(scriptPath)
	if err != nil {
		t.Fatalf("read Update/install.sh: %v", err)
	}
	if strings.Contains(string(raw), "tr -d") {
		t.Fatalf("Update/install.sh still invokes missing QNX tr command")
	}

	// 在禁止 tr 的模拟环境下验证 CRLF 密钥安装与既有非托管公钥保留
	tmp := t.TempDir()
	fakeBin := filepath.Join(tmp, "bin")
	if err := os.MkdirAll(fakeBin, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(fakeBin, "tr"), []byte("#!/bin/sh\nexit 127\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	modAcc := filepath.Join(tmp, "usb_ak")
	akDst := filepath.Join(tmp, "authorized_keys")
	_ = os.WriteFile(akDst, []byte("ssh-rsa AAAAB3NzaOldWiredKey wired@laptop\nssh-ed25519 AAAAC3OldManaged VW_WiFi_Dev_Managed\n"), 0o600)
	_ = os.WriteFile(modAcc, []byte("# comment\r\n\r\nssh-ed25519 AAAAC3NewKey1 owner@mac\r\n"), 0o644)

	snippet := `
set -e
export PATH="` + fakeBin + `:$PATH"
AK="` + akDst + `"
MOD_AK="` + modAcc + `"
grep -v "VW_WiFi_Dev_Managed" "$AK" > "$AK.new" 2>/dev/null || : > "$AK.new"
awk '{
    sub(/\r$/, "")
    if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*#/) next
    if ($0 ~ /VW_WiFi_Dev_Managed/) print $0
    else print $0 " VW_WiFi_Dev_Managed"
}' "$MOD_AK" >> "$AK.new"
mv -f "$AK.new" "$AK"
`
	if out, err := exec.Command("sh", "-c", snippet).CombinedOutput(); err != nil {
		t.Fatalf("awk authorized_keys install failed: %v\n%s", err, string(out))
	}
	got, err := os.ReadFile(akDst)
	if err != nil {
		t.Fatal(err)
	}
	s := string(got)
	if strings.Contains(s, "\r") || !strings.Contains(s, "AAAAB3NzaOldWiredKey") || !strings.Contains(s, "AAAAC3NewKey1 owner@mac VW_WiFi_Dev_Managed") || strings.Contains(s, "AAAAC3OldManaged") {
		t.Fatalf("unexpected merged authorized_keys:\n%s", s)
	}
}

func extractPreflightFunc(t *testing.T, scriptPath string) string {
	t.Helper()
	raw, err := os.ReadFile(scriptPath)
	if err != nil {
		t.Fatalf("read %s: %v", scriptPath, err)
	}
	s := string(raw)
	start := strings.Index(s, "preflight_readonly_check() {")
	if start < 0 {
		t.Fatalf("%s: preflight_readonly_check not found", scriptPath)
	}
	end := strings.Index(s[start:], "\n}\n")
	if end < 0 {
		t.Fatalf("%s: preflight_readonly_check closing brace not found", scriptPath)
	}
	return s[start : start+end+2]
}

func TestShopPreflightReadonlyCheck(t *testing.T) {
	if _, err := exec.LookPath("sh"); err != nil {
		t.Skip("sh not available")
	}
	hudFn := extractPreflightFunc(t, filepath.Join("..", "..", "USB", "未选模块", "VW_HUD_1_RouteGuidance", "Update", "install.sh"))
	wifiFn := extractPreflightFunc(t, filepath.Join("..", "..", "USB", "未选模块", "VW_WiFi_Dev", "Update", "install.sh"))
	if hudFn != wifiFn {
		t.Fatalf("preflight_readonly_check diverged between VW_HUD_1_RouteGuidance and VW_WiFi_Dev")
	}

	runCase := func(t *testing.T, setup func(appRoot, otaRoot string)) string {
		t.Helper()
		tmp := t.TempDir()
		appRoot := filepath.Join(tmp, "app")
		otaRoot := filepath.Join(tmp, "ota")
		for _, d := range []string{
			filepath.Join(appRoot, "eso", "bin", "apps"),
			filepath.Join(appRoot, "eso", "hmi", "lsd"),
			filepath.Join(appRoot, "root", ".ssh"),
			filepath.Join(otaRoot, "modkit", "Mods"),
		} {
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
		}
		setup(appRoot, otaRoot)
		cmd := exec.Command("sh", "-c", hudFn+"\npreflight_readonly_check\n")
		cmd.Env = append(os.Environ(),
			"PREFLIGHT_APP_ROOT="+appRoot,
			"PREFLIGHT_OTA_ROOT="+otaRoot,
		)
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("preflight_readonly_check failed: %v\n%s", err, string(out))
		}
		return string(out)
	}

	elfStub := []byte("\x7fELF\x01\x01\x01\x00")

	t.Run("stock_clean", func(t *testing.T) {
		out := runCase(t, func(appRoot, _ string) {
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh"), elfStub, 0o755)
		})
		if !strings.Contains(out, "entry: stock ELF") || !strings.Contains(out, "preflight: clean") || strings.Contains(out, "WARN:") {
			t.Fatalf("unexpected stock output:\n%s", out)
		}
	})

	t.Run("modkit_clean", func(t *testing.T) {
		out := runCase(t, func(appRoot, _ string) {
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh"), []byte("#!/bin/ksh\n/mnt/ota/modkit/modkit_persist.sh &\n"), 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh0"), elfStub, 0o755)
		})
		if !strings.Contains(out, "entry: standard ModKit script") || !strings.Contains(out, "preflight: clean") || strings.Contains(out, "WARN:") {
			t.Fatalf("unexpected modkit output:\n%s", out)
		}
	})

	t.Run("shop_variant_a_issue10", func(t *testing.T) {
		out := runCase(t, func(appRoot, _ string) {
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh"), []byte("#!/bin/sh\n/eso/bin/servicemgrmibhigh0 &\nslay -9 sshd\n"), 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh0"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "del"), []byte("x"), 0o644)
		})
		if !strings.Contains(out, "WARN: suspected shop / third-party servicemgrmibhigh") ||
			!strings.Contains(out, "WARN: suspected shop anti-tamper files (del/shadow)") ||
			strings.Contains(out, "servicemgrmibhigh0 is MISSING") {
			t.Fatalf("unexpected variant A output:\n%s", out)
		}
	})

	t.Run("shop_variant_b_issue16", func(t *testing.T) {
		out := runCase(t, func(appRoot, otaRoot string) {
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh"), []byte("#!/bin/sh\non -p 63 /eso/hmi/lsd/.j &\n/eso/bin/servicemgrmibhigh_orgapp &\n"), 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh_orgapp"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "apps", "dio_manager"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "apps", "dio_manager.org"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "hmi", "lsd", ".j"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(otaRoot, "challenge.pub"), []byte("pub"), 0o644)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "autorunner"), []byte("#!/bin/sh\n"), 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "root", ".ssh", "authorized_keys"), []byte("ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDemImkw== rsa-key-20210318\n"), 0o600)
		})
		for _, want := range []string{
			"WARN: suspected shop / third-party servicemgrmibhigh",
			"WARN: servicemgrmibhigh0 is MISSING while servicemgrmibhigh is not ELF",
			"error: unknown patch state",
			"servicemgrmibhigh_orgapp",
			"WARN: suspected shop payload/hook files (.j / challenge.pub / autorunner)",
			"WARN: suspected shop dio_manager replacement detected",
			"dio_manager.org",
			"rsa-key-20210318",
		} {
			if !strings.Contains(out, want) {
				t.Fatalf("variant B output missing %q:\n%s", want, out)
			}
		}
	})

	t.Run("shop_small_dio_manager_without_org", func(t *testing.T) {
		out := runCase(t, func(appRoot, _ string) {
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "servicemgrmibhigh"), elfStub, 0o755)
			_ = os.WriteFile(filepath.Join(appRoot, "eso", "bin", "apps", "dio_manager"), elfStub, 0o755)
		})
		if !strings.Contains(out, "WARN: dio_manager size is unusually small") {
			t.Fatalf("expected small dio_manager warning, got:\n%s", out)
		}
	})
}
