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
