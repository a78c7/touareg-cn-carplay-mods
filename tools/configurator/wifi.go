package main

// 手机无线控制台（VW_WiFi_Dev）配置读写、热点十六进制编码（含 iOS 弯撇号兼容）及本地 Ed25519 密钥对生成。

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type wifiNet struct {
	SSID string `json:"ssid"`
	PSK  string `json:"psk"`
}

type wifiState struct {
	Networks  []wifiNet `json:"networks"`
	Pin       string    `json:"pin"`
	EnableSSH bool      `json:"enableSSH"`
	HasKeys   bool      `json:"hasKeys"`
	PubKeyFP  string    `json:"pubKeyFP,omitempty"`
}

type wifiReq struct {
	Networks  []wifiNet `json:"networks"`
	Pin       string    `json:"pin"`
	EnableSSH bool      `json:"enableSSH"`
	RegenKey  bool      `json:"regenKey"`
}

type wifiApplyOut struct {
	KeySavedDir string
	PrivKeyText string
	GuideText   string
}

func randomPin6() string {
	out := make([]byte, 6)
	var b [1]byte
	for i := range out {
		for {
			_, _ = rand.Read(b[:])
			if b[0] < 250 { // 无偏拒绝采样（250 = 25 * 10）
				out[i] = '0' + (b[0] % 10)
				break
			}
		}
	}
	if out[0] == '0' {
		out[0] = '8'
	}
	return string(out)
}

func (p pkg) wifiUpdateDir() string {
	return filepath.Join(p.modPath("VW_WiFi_Dev"), "Update")
}

// 读取 U 盘现有的无线配置回显到网页表单
func (p pkg) readWifi() wifiState {
	dir := p.wifiUpdateDir()
	st := wifiState{Networks: []wifiNet{}}
	if b, err := os.ReadFile(filepath.Join(dir, "web.pin")); err == nil {
		st.Pin = strings.TrimSpace(string(b))
	}
	if st.Pin == "" {
		st.Pin = randomPin6()
	}

	// 解析已有 wpa.conf，并自动合并紧邻的 iOS 弯撇号副本行
	if b, err := os.ReadFile(filepath.Join(dir, "wpa.conf")); err == nil {
		parsed := parseWpaNetworks(string(b))
		for _, n := range parsed {
			if len(st.Networks) > 0 {
				prev := st.Networks[len(st.Networks)-1]
				if prev.PSK == n.PSK && strings.ReplaceAll(prev.SSID, "'", "\xe2\x80\x99") == n.SSID {
					continue
				}
			}
			st.Networks = append(st.Networks, n)
		}
	}

	akPath := filepath.Join(dir, "authorized_keys")
	hkPath := filepath.Join(dir, "ssh_host_ed25519_key")
	noSSH := isFile(filepath.Join(dir, "no_ssh"))
	if akBytes, err := os.ReadFile(akPath); err == nil && isFile(hkPath) {
		ak := strings.TrimSpace(string(akBytes))
		if ak != "" {
			st.HasKeys = true
			st.PubKeyFP = pubKeyFingerprint(ak)
			if !noSSH {
				st.EnableSSH = true
			}
		}
	}
	return st
}

func parseWpaNetworks(text string) []wifiNet {
	var out []wifiNet
	inBlock := false
	var curSSID, curPSK string
	for _, raw := range strings.Split(strings.ReplaceAll(text, "\r\n", "\n"), "\n") {
		line := strings.TrimSpace(raw)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "network=") && strings.Contains(line, "{") {
			inBlock = true
			curSSID, curPSK = "", ""
			continue
		}
		if inBlock && line == "}" {
			if curSSID != "" && curPSK != "" {
				out = append(out, wifiNet{SSID: curSSID, PSK: curPSK})
			}
			inBlock = false
			continue
		}
		if !inBlock {
			continue
		}
		k, v, ok := confKV(line)
		if !ok {
			continue
		}
		switch k {
		case "ssid":
			if strings.HasPrefix(v, "\"") && strings.HasSuffix(v, "\"") && len(v) >= 2 {
				curSSID = v[1 : len(v)-1]
			} else if rawBytes, err := hex.DecodeString(v); err == nil {
				curSSID = string(rawBytes)
			}
		case "psk":
			if strings.HasPrefix(v, "\"") && strings.HasSuffix(v, "\"") && len(v) >= 2 {
				curPSK = v[1 : len(v)-1]
			}
		}
	}
	return out
}

func cleanWifiNets(in []wifiNet) []wifiNet {
	var out []wifiNet
	for _, n := range in {
		s := strings.TrimSpace(n.SSID)
		k := n.PSK
		if s == "" && k == "" {
			continue
		}
		out = append(out, wifiNet{SSID: s, PSK: k})
	}
	return out
}

func validateWifi(w wifiReq) error {
	nets := cleanWifiNets(w.Networks)
	if len(nets) == 0 {
		return errors.New("请至少填写 1 个手机热点或常用 Wi-Fi")
	}
	if len(nets) > 5 {
		return errors.New("最多填写 5 个热点")
	}
	for i, n := range nets {
		if n.SSID == "" {
			return fmt.Errorf("第 %d 个热点的名称不能为空", i+1)
		}
		if len(n.SSID) > 32 {
			return fmt.Errorf("热点「%s」名称过长（Wi-Fi 标准上限 32 字节，约 10 个汉字）", n.SSID)
		}
		if len(n.PSK) < 8 || len(n.PSK) > 63 {
			return fmt.Errorf("热点「%s」密码长度需为 8～63 位", n.SSID)
		}
		for j := 0; j < len(n.PSK); j++ {
			c := n.PSK[j]
			if c < 0x20 || c > 0x7e || c == '"' || c == '\\' {
				return fmt.Errorf("热点「%s」密码只能包含常规英文、数字或符号，且不能含双引号或反斜杠", n.SSID)
			}
		}
	}
	pin := strings.TrimSpace(w.Pin)
	if len(pin) < 6 || len(pin) > 16 {
		return errors.New("手机网页管理口令请设为 6～16 位数字或英文字母")
	}
	for i := 0; i < len(pin); i++ {
		c := pin[i]
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			return errors.New("手机网页管理口令只能包含数字或英文字母")
		}
	}
	return nil
}

func buildWpaConf(nets []wifiNet) string {
	var b strings.Builder
	b.WriteString("ctrl_interface=/var/run/wpa_supplicant\n")
	for _, n := range nets {
		writeBlock := func(ssid string) {
			b.WriteString("\nnetwork={\n")
			b.WriteString("    ssid=" + hex.EncodeToString([]byte(ssid)) + "\n")
			b.WriteString("    scan_ssid=1\n")
			b.WriteString("    psk=\"" + n.PSK + "\"\n")
			b.WriteString("    key_mgmt=WPA-PSK\n")
			b.WriteString("}\n")
		}
		writeBlock(n.SSID)
		// 兼容 iOS 个人热点默认使用的弯撇号 ’ (\xe2\x80\x99)
		if strings.Contains(n.SSID, "'") {
			writeBlock(strings.ReplaceAll(n.SSID, "'", "\xe2\x80\x99"))
		}
	}
	return b.String()
}

// ---- 纯标准库生成 OpenSSH Ed25519 密钥对（零外部依赖） ----

func sshPutUint32(b *bytes.Buffer, v uint32) {
	var tmp [4]byte
	binary.BigEndian.PutUint32(tmp[:], v)
	b.Write(tmp[:])
}

func sshPutBytes(b *bytes.Buffer, s []byte) {
	sshPutUint32(b, uint32(len(s)))
	b.Write(s)
}

func sshPutString(b *bytes.Buffer, s string) {
	sshPutBytes(b, []byte(s))
}

func genEd25519OpenSSH(comment string) (privPEM string, pubAuthorized string, err error) {
	pub, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return "", "", err
	}
	return encodeEd25519OpenSSH(pub, priv, comment)
}

func encodeEd25519OpenSSH(pub ed25519.PublicKey, priv ed25519.PrivateKey, comment string) (string, string, error) {
	var pubWire bytes.Buffer
	sshPutString(&pubWire, "ssh-ed25519")
	sshPutBytes(&pubWire, pub)

	pubLine := "ssh-ed25519 " + base64.StdEncoding.EncodeToString(pubWire.Bytes())
	if comment != "" {
		pubLine += " " + comment
	}
	pubLine += "\n"

	var rnd [4]byte
	if _, err := rand.Read(rnd[:]); err != nil {
		return "", "", err
	}
	check := binary.BigEndian.Uint32(rnd[:])

	var privBlock bytes.Buffer
	sshPutUint32(&privBlock, check)
	sshPutUint32(&privBlock, check)
	sshPutString(&privBlock, "ssh-ed25519")
	sshPutBytes(&privBlock, pub)
	sshPutBytes(&privBlock, priv) // 64 bytes: seed(32) || pub(32)
	sshPutString(&privBlock, comment)
	for pad := byte(1); privBlock.Len()%8 != 0; pad++ {
		privBlock.WriteByte(pad)
	}

	var outer bytes.Buffer
	outer.WriteString("openssh-key-v1\x00")
	sshPutString(&outer, "none")
	sshPutString(&outer, "none")
	sshPutString(&outer, "")
	sshPutUint32(&outer, 1)
	sshPutBytes(&outer, pubWire.Bytes())
	sshPutBytes(&outer, privBlock.Bytes())

	pemBytes := pem.EncodeToMemory(&pem.Block{
		Type:  "OPENSSH PRIVATE KEY",
		Bytes: outer.Bytes(),
	})
	return string(pemBytes), pubLine, nil
}

func pubKeyFingerprint(pubLine string) string {
	fields := strings.Fields(pubLine)
	if len(fields) < 2 {
		return ""
	}
	raw, err := base64.StdEncoding.DecodeString(fields[1])
	if err != nil {
		return ""
	}
	sum := sha256.Sum256(raw)
	return "SHA256:" + strings.TrimRight(base64.StdEncoding.EncodeToString(sum[:]), "=")
}

func saveKeyToDesktop(privPEM, pubLine, pin string) string {
	home, err := os.UserHomeDir()
	if err != nil || home == "" {
		return ""
	}
	base := home
	for _, cand := range []string{
		filepath.Join(home, "Desktop"),
		filepath.Join(home, "OneDrive", "Desktop"),
		filepath.Join(home, "OneDrive", "桌面"),
	} {
		if isDir(cand) {
			base = cand
			break
		}
	}
	outDir := filepath.Join(base, "途锐无线SSH密钥")
	if err := os.MkdirAll(outDir, 0o700); err != nil {
		return ""
	}
	privPath := filepath.Join(outDir, "touareg_ed25519")
	pubPath := filepath.Join(outDir, "touareg_ed25519.pub")
	guidePath := filepath.Join(outDir, "无线连接说明.txt")

	_ = os.WriteFile(privPath, []byte(privPEM), 0o600)
	_ = os.WriteFile(pubPath, []byte(pubLine), 0o644)
	_ = os.WriteFile(guidePath, []byte(buildWifiGuide(outDir, pin)), 0o644)
	return outDir
}

func buildWifiGuide(keyDir, pin string) string {
	keyFile := "touareg_ed25519"
	if keyDir != "" {
		keyFile = filepath.Join(keyDir, "touareg_ed25519")
	}
	return fmt.Sprintf(`【途锐 MH2p 手机无线控制台 & 傻瓜式无线 SSH / AI 自由开发说明】

本文件夹内的 touareg_ed25519（不带 .pub 后缀）是你这台车专属的唯一登录私钥，请妥善保管，不要发给他人。

==================================================
一、上车第一步：打开手机个人热点（让车机自动连上你）
==================================================
1. 用 U 盘在车上刷完模块后，每次发动车辆，打开手机「个人热点」，等车机开机约 1 分钟会自动连入（手机顶栏热点连接数 +1）。
2. iPhone 注意：打开「设置 → 个人热点 → 最大兼容性」，首次连接时请停留在个人热点页面亮屏等 3～5 秒，连上后即可锁屏。
3. 安卓注意：热点加密选 WPA2；若手机开了 VPN/代理软件（全局模式），请先关闭或开启「绕过局域网」，否则访问不到热点里的车机。

==================================================
二、普通日常使用：手机浏览器秒改信息框 / 下载行车记录 CSV
==================================================
- 手机浏览器直接访问：http://touareg.local:8080
- 首次打开输入你的网页管理口令：%s（输入一次后手机自动记住；仅停车时允许修改设置）

==================================================
三、无线 SSH + AI 自由开发（免查 IP、免填参数，把钥匙和这段话直接发给 AI）
==================================================
支持客户端：
- 电脑端：DeepSeek Harness / Claude Code / Cursor（电脑 Wi-Fi 连同一个手机个人热点，私钥在 %s）
- 手机端：DSH Go（DeepSeek Harness 安卓手机客户端 https://github.com/ccch1mneyyy/dsh-go ，iPhone 暂无公开包；把本文件夹里的 touareg_ed25519 发到安卓手机上即可）

不需要你自己查车机 IP 或手填端口，打开客户端附上 touareg_ed25519 私钥文件，直接把下面整段话复制发给 AI：

--------------------------------------------------
这是我车机的 SSH 私钥文件 touareg_ed25519（电脑默认在桌面「途锐无线SSH密钥」文件夹，安卓手机端请直接读取我上传或放在 Download 目录下的 touareg_ed25519）。
请你自己读取这把私钥并配置保存好车机 SSH 连接（端口 2012，用户名 root）：
- 先尝试连接 touareg.local:2012；如果当前系统（如安卓端）不解析 .local，请你自动读取本机热点路由/ARP 邻居表或扫描当前热点子网开放 2012 端口的设备找到车机 IP 并连上，无需让我手动查 IP 或填配置。

车机环境与安全护栏（请严格遵守）：
1. 系统是 QNX 6.6.0 (ARMv7 Tegra K1)，shell 是 ksh（无 bash / sed -i / wc）。
2. 严禁改动原厂只读分区（/ifs、/mnt/system），不要频繁重载 pfctl 避免冲击 io-pkt。
3. 改装目录在 /mnt/app/eso/bin/apps/cluster/（仪表渲染 cl_render、配置 dash.conf / cl.conf）和 /mnt/app/eso/bin/apps/wifi_dev/；10Hz 实时车速、功率、扭矩、油温等数据在 /tmp/dash.state 与 /tmp/cluster_cp.state。
4. /mnt/app 默认只读：改文件前先 mount -uw /mnt/app 并备份原文件，用 .new 写完原子 mv 替换后立刻 sync && mount -ur /mnt/app 恢复只读；改完仪表配置执行 slay -f cl_render 即可秒级刷新仪表画面（无需重启车机）。
5. 完整接口表与自定义编译说明见公开仓库：https://github.com/a78c7/touareg-cn-carplay-mods/tree/main/docs

现在请自动连上车机，只读查看一下当前车速/油温状态和已安装的模块，向我汇报连接成功，然后听我用大白话吩咐修改。
--------------------------------------------------
`, pin, keyFile)
}

func (p pkg) writeWifi(w wifiReq) ([]string, wifiApplyOut, error) {
	var logs []string
	var out wifiApplyOut
	dir := p.wifiUpdateDir()
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, out, err
	}
	nets := cleanWifiNets(w.Networks)
	pin := strings.TrimSpace(w.Pin)

	if err := writeFileSync(filepath.Join(dir, "wpa.conf"), []byte(buildWpaConf(nets)), 0o600); err != nil {
		return nil, out, err
	}
	if err := writeFileSync(filepath.Join(dir, "web.pin"), []byte(pin+"\n"), 0o600); err != nil {
		return nil, out, err
	}

	var names []string
	for _, n := range nets {
		names = append(names, n.SSID)
	}
	logs = append(logs, fmt.Sprintf("手机无线控制台：已写入 %d 个热点（%s），网页管理口令 %s", len(nets), strings.Join(names, "、"), pin))

	akPath := filepath.Join(dir, "authorized_keys")
	hkPath := filepath.Join(dir, "ssh_host_ed25519_key")
	hkPubPath := filepath.Join(dir, "ssh_host_ed25519_key.pub")
	noSSHPath := filepath.Join(dir, "no_ssh")

	if !w.EnableSSH {
		if err := writeFileSync(noSSHPath, nil, 0o644); err != nil {
			return logs, out, err
		}
		_ = os.Remove(akPath)
		_ = os.Remove(hkPath)
		_ = os.Remove(hkPubPath)
		logs = append(logs, "无线开发终端（端口 2012）：关闭（仅开启 8080 网页控制台）")
		return logs, out, nil
	}

	_ = os.Remove(noSSHPath)
	if !isFile(hkPath) {
		hPriv, hPub, err := genEd25519OpenSSH("mh2p-host")
		if err != nil {
			return logs, out, fmt.Errorf("生成车机主机密钥失败：%v", err)
		}
		if err := writeFileSync(hkPath, []byte(hPriv), 0o600); err != nil {
			return logs, out, err
		}
		_ = writeFileSync(hkPubPath, []byte(hPub), 0o644)
	}

	if !isFile(akPath) || w.RegenKey {
		uPriv, uPub, err := genEd25519OpenSSH("touareg-owner")
		if err != nil {
			return logs, out, fmt.Errorf("生成登录密钥对失败：%v", err)
		}
		if err := writeFileSync(akPath, []byte(uPub), 0o600); err != nil {
			return logs, out, err
		}
		savedDir := saveKeyToDesktop(uPriv, uPub, pin)
		out.KeySavedDir = savedDir
		out.PrivKeyText = uPriv
		out.GuideText = buildWifiGuide(savedDir, pin)
		if savedDir != "" {
			logs = append(logs, "无线开发终端（端口 2012）：开启，专属私钥已存到电脑「"+savedDir+"」（U 盘内不留私钥）")
		} else {
			logs = append(logs, "无线开发终端（端口 2012）：开启，请点击下方按钮下载专属私钥文件")
		}
	} else {
		logs = append(logs, "无线开发终端（端口 2012）：开启（保留 U 盘已配置的专属公钥）")
	}
	return logs, out, nil
}
