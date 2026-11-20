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

	// 测试含英文单引号的热点双向生成与解析去重
	conf := buildWpaConf([]wifiNet{
		{SSID: "Phone's Hotspot", PSK: "12345678"},
		{SSID: "HomeWiFi_5G", PSK: "abcdefgh99"},
	})
	rawNets := parseWpaNetworks(conf)
	if len(rawNets) != 3 {
		t.Fatalf("expected 3 raw wpa blocks (with curly apostrophe variant), got %d", len(rawNets))
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
	if st.Pin != "654321" || len(st.Networks) != 2 || st.Networks[0].SSID != "Phone's Hotspot" || st.Networks[1].SSID != "HomeWiFi_5G" {
		t.Fatalf("unexpected readWifi state: %+v", st)
	}
}
