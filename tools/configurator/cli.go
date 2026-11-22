package main

// 无界面命令行模式：供本地 AI 助手（Claude Code / Cursor / Codex / Antigravity 等）或脚本一键部署皮肤、
// 选装模块、自检校验、清理 Mac 隐藏文件、刷盘与安全弹出 U 盘。
// 双击启动（无命令行参数）或仅传入目录路径时，仍保持原有网页图形界面行为。

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

type verifyReport struct {
	OK       bool              `json:"ok"`
	Root     string            `json:"root"`
	UsbMode  string            `json:"usbMode"`
	Vol      volInfo           `json:"vol"`
	States   map[string]string `json:"states"`
	V23      v23State          `json:"v23"`
	MacJunk  int               `json:"macJunk"`
	Logs     []string          `json:"logs,omitempty"`
	Errors   []string          `json:"errors,omitempty"`
	Warnings []string          `json:"warnings,omitempty"`
	Ejected  string            `json:"ejected,omitempty"`
}

func hasCLIFlags(args []string) bool {
	for _, a := range args {
		if strings.HasPrefix(a, "-") {
			return true
		}
	}
	return false
}

func featureByID(id string) (feature, bool) {
	for _, f := range features {
		if f.ID == id {
			return f, true
		}
	}
	return feature{}, false
}

func splitIDs(s string) []string {
	var out []string
	for _, part := range strings.Split(s, ",") {
		k := strings.TrimSpace(strings.ToLower(part))
		if k != "" {
			out = append(out, k)
		}
	}
	return out
}

func (p pkg) cleanMacJunk() int {
	n := 0
	for _, j := range p.macJunk() {
		if os.Remove(j) == nil {
			n++
		}
	}
	return n
}

func (p pkg) setFeatureStates(want map[string]string) ([]string, error) {
	var logs []string
	for _, f := range features {
		w, ok := want[f.ID]
		if !ok {
			continue
		}
		for _, d := range f.Dirs {
			loc, err := p.location(d)
			if err != nil {
				return nil, err
			}
			if loc == "" && w != "skip" {
				return nil, fmt.Errorf("U 盘里找不到「%s」文件夹（%s），请重新复制完整的改装包", f.Name, d)
			}
		}
	}
	for _, f := range features {
		w, ok := want[f.ID]
		if !ok {
			continue
		}
		for _, d := range f.Dirs {
			loc, _ := p.location(d)
			if loc == "" {
				continue
			}
			if w == "skip" && loc == "mods" {
				if err := os.MkdirAll(p.path(parkDir), 0o755); err != nil {
					return logs, err
				}
				if err := os.Rename(p.path("Mods", d), p.path(parkDir, d)); err != nil {
					return logs, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				logs = append(logs, fmt.Sprintf("%s：移到「%s」（本次不刷）", d, parkDir))
			} else if w != "skip" && loc == "park" {
				if err := os.MkdirAll(p.path("Mods"), 0o755); err != nil {
					return logs, err
				}
				if err := os.Rename(p.path(parkDir, d), p.path("Mods", d)); err != nil {
					return logs, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				logs = append(logs, fmt.Sprintf("%s：移入 Mods", d))
			}
			u := filepath.Join(p.modPath(d), "uninstall.txt")
			if w == "uninstall" {
				if !isFile(u) {
					if err := writeFileSync(u, nil, 0o644); err != nil {
						return logs, err
					}
				}
				logs = append(logs, fmt.Sprintf("%s：卸载（已放 uninstall.txt）", d))
			} else {
				if isFile(u) {
					if err := os.Remove(u); err != nil {
						return logs, err
					}
					logs = append(logs, fmt.Sprintf("%s：删掉 uninstall.txt", d))
				}
				if w == "install" {
					logs = append(logs, fmt.Sprintf("%s：安装", d))
				}
			}
		}
	}
	_ = os.Remove(p.path(parkDir))
	return logs, nil
}

func (p pkg) verify() verifyReport {
	rep := verifyReport{
		OK:      true,
		Root:    p.Root,
		UsbMode: p.usbMode(),
		Vol:     volumeInfo(p.Root),
		States:  map[string]string{},
		V23:     p.readV23(),
		MacJunk: len(p.macJunk()),
	}

	if rep.UsbMode != "flash" {
		rep.Errors = append(rep.Errors, "U 盘当前处于「行车记录模式」（_Data/_Meta），插车不会刷机；请先切回刷机模式")
	}

	activeCount := 0
	for _, f := range features {
		st, err := p.featureState(f)
		if err != nil {
			rep.Errors = append(rep.Errors, err.Error())
			rep.States[f.ID] = "error"
			continue
		}
		rep.States[f.ID] = st
		if st == "mixed" {
			rep.Errors = append(rep.Errors, fmt.Sprintf("「%s」子模块状态不一致（部分在 Mods、部分在 %s），请重新统一设置", f.Name, parkDir))
		}
		if st == "install" || st == "uninstall" {
			activeCount++
		}
	}

	if activeCount == 0 {
		rep.Errors = append(rep.Errors, "Mods/ 中没有任何待刷模块（全部在「未选模块」中），插车会空刷")
	}

	// 依赖一致性检查（与网页版 validate 规则完全对齐）
	if rep.States["cluster"] == "install" && rep.States["hud"] != "install" {
		rep.Errors = append(rep.Errors, "依赖缺失：安装「仪表地图 + 大字运动信息框」时，「导航上 HUD」（VW_HUD_1_RouteGuidance + VW_HUD_2_ClusterNav）也必须在 Mods/ 中")
	}
	if rep.States["siri"] == "install" && rep.States["hud"] != "install" {
		rep.Errors = append(rep.Errors, "依赖缺失：安装「语音键短按唤 Siri」时，「导航上 HUD」也必须在 Mods/ 中")
	}
	if rep.States["mapswitch"] == "install" && (rep.States["cluster"] != "install" || rep.States["hud"] != "install") {
		rep.Errors = append(rep.Errors, "依赖缺失：安装「OK 双击切地图」时，「导航上 HUD」与「仪表地图」也必须在 Mods/ 中")
	}
	if rep.States["hud"] == "uninstall" && (rep.States["cluster"] == "install" || rep.States["mapswitch"] == "install" || rep.States["siri"] == "install") {
		rep.Errors = append(rep.Errors, "卸载冲突：卸载「导航上 HUD」时，依赖它的模块不能同时为安装状态")
	}
	if rep.States["cluster"] == "uninstall" && rep.States["mapswitch"] == "install" {
		rep.Errors = append(rep.Errors, "卸载冲突：卸载「仪表地图」时，「OK 双击切地图」不能同时为安装状态")
	}

	// v23 皮肤完整性检查
	if dir := p.v23Dir(); dir != "" {
		themePath := filepath.Join(dir, "themes_user", "my_theme.js")
		if isFile(themePath) {
			b, err := os.ReadFile(themePath)
			if err != nil {
				rep.Errors = append(rep.Errors, fmt.Sprintf("读取自定义皮肤失败：%v", err))
			} else if len(b) == 0 {
				rep.Errors = append(rep.Errors, "自定义皮肤 themes_user/my_theme.js 为空文件（0 字节，可能是上次未安全弹出写残），请重新写入")
			} else if err := validateV23(v23ApplyReq{Enabled: true, ThemeAction: "set", ThemeCode: string(b)}); err != nil {
				rep.Errors = append(rep.Errors, "自定义皮肤校验未通过："+err.Error())
			}
			if isFile(filepath.Join(dir, "v23_off")) && !isFile(filepath.Join(dir, "v23_mode")) {
				rep.Warnings = append(rep.Warnings, "U 盘存在自定义皮肤 my_theme.js，但 v23 已被显式关闭（v23_off），上车将运行 v22.1 经典通道")
			}
		}
		if rep.States["cluster"] == "install" && isFile(themePath) && !isFile(filepath.Join(dir, "v23_mode")) && !isFile(filepath.Join(dir, "v23_off")) {
			rep.Warnings = append(rep.Warnings, "缺少显式 v23_mode 标志文件（新版 install.sh 会自动兜底开启，仍建议用 --v23 on 补齐）")
		}
	}

	// Wi-Fi 模块完整性检查
	if rep.States["wifi"] == "install" {
		wdir := p.wifiUpdateDir()
		if !isFile(filepath.Join(wdir, "wpa.conf")) || !isFile(filepath.Join(wdir, "web.pin")) {
			rep.Errors = append(rep.Errors, "已选装「手机无线控制台（VW_WiFi_Dev）」，但缺少 wpa.conf 或 web.pin，请双击「改装设置」填写热点和口令")
		}
	}

	if rep.MacJunk > 0 {
		rep.Warnings = append(rep.Warnings, fmt.Sprintf("发现 %d 个 Mac 隐藏文件（._* / .DS_Store），可能干扰车机升级程序，请加 --clean 清理", rep.MacJunk))
	}
	if rep.Vol.FS != "" && !rep.Vol.FAT {
		rep.Warnings = append(rep.Warnings, fmt.Sprintf("当前分区格式为 %s，车机仅识别 FAT32 U 盘", rep.Vol.FS))
	}
	if rep.Vol.Mount != "" && !rep.Vol.IsRoot {
		rep.Warnings = append(rep.Warnings, fmt.Sprintf("当前目录（%s）不是磁盘根目录（%s）；插车前请确保 Data/Meta/Mods 在 U 盘第一层", p.Root, rep.Vol.Mount))
	}

	rep.OK = len(rep.Errors) == 0
	return rep
}

func printCLIUsage(w io.Writer) {
	fmt.Fprintf(w, `途锐 MH2p 改装设置 %s — 无界面命令行模式（供 AI 助手与脚本自动化部署）

常用命令：
  1) 一键校验并部署自定义 v23 皮肤 + 清理垃圾 + 自检 + 安全弹出：
     --deploy-theme <my_theme.js 路径> --verify --eject

  2) 按预设或指定模块选装并自检：
     --preset full --verify --eject
     --install fullscreen,carplay,hud,cluster,mapswitch --verify

  3) 仅自检 U 盘（含清理 Mac 隐藏文件）：
     --clean --verify

全部参数：
  --root <目录>          指定改装包 / U 盘根目录（默认使用程序所在目录）
  --preset <值>          快捷选装预设：base（基础四件套）| full（驾驶推荐全套）| none（全部不处理）
  --install <ID列表>     逗号分隔功能 ID 设为安装并自动补齐前置依赖
  --uninstall <ID列表>   逗号分隔功能 ID 设为卸载
  --skip <ID列表>        逗号分隔功能 ID 设为不处理
  --deploy-theme <路径>  校验并写入 themes_user/my_theme.js，自动启用 hud+cluster+v23_mode（保留现有 dash.conf）
  --v23 <on|off>         显式开启或关闭 v23 60fps 皮肤引擎
  --clear-theme          清除 U 盘自定义皮肤 themes_user/my_theme.js
  --clean                清理 Mac 隐藏文件（._* / .DS_Store）并强制刷盘
  --verify / --status    自检 U 盘目录、模块依赖、v23 皮肤合法性与垃圾文件（异常返回退出码 1）
  --eject                强制刷盘后向系统发送安全弹出 U 盘指令（仅对可移动 U 盘生效）
  --json                 以 JSON 格式输出结果

可用功能 ID：
  fullscreen, carplay, aa, hud, cluster, mapswitch, siri, wifi5gfix, nobtauth, nosimpopup, wifi
`, version)
}

func runCLI(args []string) int {
	fs := flag.NewFlagSet("touareg-configurator", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var (
		rootFlag      string
		presetFlag    string
		installFlag   string
		uninstallFlag string
		skipFlag      string
		deployTheme   string
		v23Flag       string
		clearTheme    bool
		cleanFlag     bool
		verifyFlag    bool
		statusFlag    bool
		ejectFlag     bool
		jsonFlag      bool
		helpFlag      bool
	)
	fs.StringVar(&rootFlag, "root", "", "改装包 / U 盘根目录")
	fs.StringVar(&presetFlag, "preset", "", "快捷预设：base | full | none")
	fs.StringVar(&installFlag, "install", "", "要安装的功能 ID（逗号分隔）")
	fs.StringVar(&uninstallFlag, "uninstall", "", "要卸载的功能 ID（逗号分隔）")
	fs.StringVar(&skipFlag, "skip", "", "设为不处理的功能 ID（逗号分隔）")
	fs.StringVar(&deployTheme, "deploy-theme", "", "校验并部署自定义 v23 皮肤 .js")
	fs.StringVar(&v23Flag, "v23", "", "开启或关闭 v23：on | off")
	fs.BoolVar(&clearTheme, "clear-theme", false, "清除 U 盘自定义皮肤")
	fs.BoolVar(&cleanFlag, "clean", false, "清理 Mac 隐藏文件并刷盘")
	fs.BoolVar(&verifyFlag, "verify", false, "自检 U 盘是否可安全刷机")
	fs.BoolVar(&statusFlag, "status", false, "同 --verify")
	fs.BoolVar(&ejectFlag, "eject", false, "刷盘并安全弹出 U 盘")
	fs.BoolVar(&jsonFlag, "json", false, "以 JSON 格式输出")
	fs.BoolVar(&helpFlag, "help", false, "显示帮助")
	fs.BoolVar(&helpFlag, "h", false, "显示帮助")

	if err := fs.Parse(args); err != nil {
		return 2
	}
	if helpFlag {
		printCLIUsage(os.Stdout)
		return 0
	}

	var root string
	var err error
	if rootFlag != "" {
		root, err = filepath.Abs(rootFlag)
	} else if fs.NArg() > 0 && isDir(fs.Arg(0)) {
		root, err = filepath.Abs(fs.Arg(0))
	} else {
		root, err = exeRoot()
	}
	if err == nil {
		err = checkRoot(root)
	}
	if err != nil {
		emitCLIError(jsonFlag, err.Error())
		return 1
	}

	p := pkg{Root: root}
	var logs []string
	mutated := false

	// 1. 先预读并校验待部署皮肤（校验不通过时不改 U 盘任何文件）
	var themeCode string
	if deployTheme != "" {
		b, rerr := os.ReadFile(deployTheme)
		if rerr != nil {
			emitCLIError(jsonFlag, fmt.Sprintf("读取皮肤文件失败（%s）：%v", deployTheme, rerr))
			return 1
		}
		themeCode = string(b)
		if verr := validateV23(v23ApplyReq{Enabled: true, ThemeAction: "set", ThemeCode: themeCode}); verr != nil {
			emitCLIError(jsonFlag, "皮肤校验失败："+verr.Error())
			return 1
		}
	}

	// 2. 处理模块选装状态变更
	want := map[string]string{}
	if presetFlag != "" {
		switch strings.ToLower(strings.TrimSpace(presetFlag)) {
		case "base":
			for _, f := range features {
				want[f.ID] = "skip"
			}
			for _, id := range []string{"fullscreen", "carplay", "hud", "cluster"} {
				want[id] = "install"
			}
		case "full":
			for _, f := range features {
				want[f.ID] = "skip"
			}
			for _, id := range []string{"fullscreen", "carplay", "hud", "cluster", "mapswitch"} {
				want[id] = "install"
			}
		case "none":
			for _, f := range features {
				want[f.ID] = "skip"
			}
		default:
			emitCLIError(jsonFlag, "无效的 --preset 值（仅支持 base / full / none）："+presetFlag)
			return 1
		}
	}

	applyIDs := func(raw, state string) error {
		for _, id := range splitIDs(raw) {
			if _, ok := featureByID(id); !ok {
				return fmt.Errorf("未知功能 ID：%s", id)
			}
			want[id] = state
		}
		return nil
	}
	if err := applyIDs(installFlag, "install"); err != nil {
		emitCLIError(jsonFlag, err.Error())
		return 1
	}
	if err := applyIDs(uninstallFlag, "uninstall"); err != nil {
		emitCLIError(jsonFlag, err.Error())
		return 1
	}
	if err := applyIDs(skipFlag, "skip"); err != nil {
		emitCLIError(jsonFlag, err.Error())
		return 1
	}

	// 部署皮肤时，自动确保 hud + cluster 在 Mods/ 中
	if deployTheme != "" {
		want["hud"] = "install"
		want["cluster"] = "install"
	}

	// 自动补齐前置依赖（避免 AI 漏选依赖）
	if want["mapswitch"] == "install" {
		if want["cluster"] != "install" {
			want["cluster"] = "install"
			logs = append(logs, "自动补齐前置依赖：cluster（仪表地图）")
		}
		if want["hud"] != "install" {
			want["hud"] = "install"
			logs = append(logs, "自动补齐前置依赖：hud（导航上 HUD）")
		}
	}
	if (want["cluster"] == "install" || want["siri"] == "install") && want["hud"] != "install" {
		want["hud"] = "install"
		logs = append(logs, "自动补齐前置依赖：hud（导航上 HUD）")
	}

	if len(want) > 0 {
		// 确保处于刷机模式（若在 record 模式且准备部署，则报错提醒，不静默破坏行车记录盘）
		if p.usbMode() != "flash" {
			emitCLIError(jsonFlag, "当前 U 盘处于行车记录模式（_Data/_Meta），请先切回刷机模式再部署模块")
			return 1
		}
		mlogs, merr := p.setFeatureStates(want)
		if merr != nil {
			emitCLIError(jsonFlag, merr.Error())
			return 1
		}
		logs = append(logs, mlogs...)
		// 同步 switch_off（仅当明确调整了 mapswitch 时）
		if mw, ok := want["mapswitch"]; ok {
			swPath := p.switchOffPath()
			if mw == "install" {
				_ = os.Remove(swPath)
			} else if mw == "uninstall" {
				_ = writeFileSync(swPath, []byte{}, 0o644)
			}
		}
		mutated = true
	}

	// 3. 处理 v23 开关与自定义皮肤写入
	if deployTheme != "" || v23Flag != "" || clearTheme {
		curV23 := p.readV23()
		req := v23ApplyReq{
			Enabled:     curV23.Enabled,
			ThemeAction: "keep",
		}
		if deployTheme != "" {
			req.Enabled = true
			req.ThemeAction = "set"
			req.ThemeCode = themeCode
		}
		if clearTheme {
			req.ThemeAction = "clear"
			req.ThemeCode = ""
		}
		if v23Flag != "" {
			switch strings.ToLower(strings.TrimSpace(v23Flag)) {
			case "on", "1", "true":
				req.Enabled = true
			case "off", "0", "false":
				req.Enabled = false
			default:
				emitCLIError(jsonFlag, "无效的 --v23 值（仅支持 on / off）："+v23Flag)
				return 1
			}
		}
		vlogs, verr := p.writeV23(req)
		if verr != nil {
			emitCLIError(jsonFlag, "写入 v23 配置失败："+verr.Error())
			return 1
		}
		logs = append(logs, vlogs...)
		mutated = true
	}

	// 4. 清理 Mac 隐藏文件 + 刷盘
	if mutated || cleanFlag {
		if n := p.cleanMacJunk(); n > 0 {
			logs = append(logs, fmt.Sprintf("清理了 %d 个 Mac 隐藏文件（._* / .DS_Store）", n))
		}
		syncFS()
		logs = append(logs, "已强制同步写入缓存到磁盘（sync）")
	}

	// 5. 自检（写操作后默认也附带自检，或显式 --verify / --status）
	doVerify := verifyFlag || statusFlag || mutated || (!ejectFlag && !cleanFlag)
	var rep verifyReport
	if doVerify {
		rep = p.verify()
	} else {
		rep = verifyReport{OK: true, Root: p.Root, UsbMode: p.usbMode(), Vol: volumeInfo(p.Root), V23: p.readV23()}
	}
	rep.Logs = logs

	// 6. 仅在无阻塞错误时才执行安全弹出
	if ejectFlag {
		if !rep.OK {
			rep.Errors = append(rep.Errors, "存在上述阻塞错误，已取消自动弹出 U 盘，请修复后再弹出")
		} else {
			mnt, eerr := scheduleEject(p.Root)
			if eerr != nil {
				rep.Warnings = append(rep.Warnings, "未能自动弹出 U 盘："+eerr.Error()+"（请手动点安全弹出后再拔）")
			} else {
				rep.Ejected = mnt
				rep.Logs = append(rep.Logs, fmt.Sprintf("已发送安全弹出 U 盘指令（%s），请待系统图标消失后再拔出", mnt))
			}
		}
	}

	if jsonFlag {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		_ = enc.Encode(rep)
	} else {
		printVerifyText(rep)
	}
	if !rep.OK {
		return 1
	}
	return 0
}

func emitCLIError(asJSON bool, msg string) {
	if asJSON {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		_ = enc.Encode(verifyReport{OK: false, Errors: []string{msg}})
		return
	}
	fmt.Fprintln(os.Stderr, "错误：", msg)
}

func printVerifyText(rep verifyReport) {
	if len(rep.Logs) > 0 {
		fmt.Println("【已执行】")
		for _, l := range rep.Logs {
			fmt.Println("  ✓", l)
		}
	}
	fmt.Printf("【U 盘状态】%s（模式：%s", rep.Root, rep.UsbMode)
	if rep.Vol.FS != "" {
		fmt.Printf("，文件系统：%s", rep.Vol.FS)
	}
	fmt.Println("）")
	if len(rep.States) > 0 {
		var inst, uninst []string
		for _, f := range features {
			switch rep.States[f.ID] {
			case "install":
				inst = append(inst, f.ID)
			case "uninstall":
				uninst = append(uninst, f.ID)
			}
		}
		fmt.Printf("  待安装模块（Mods/）：%s\n", strings.Join(inst, ", "))
		if len(uninst) > 0 {
			fmt.Printf("  待卸载模块（Mods/）：%s\n", strings.Join(uninst, ", "))
		}
	}
	if rep.V23.Enabled {
		if rep.V23.HasTheme {
			fmt.Printf("  v23 60fps 皮肤引擎：已启用 · 自定义皮肤「%s」（%.1f KB）\n", rep.V23.ThemeName, float64(rep.V23.ThemeSize)/1024.0)
		} else {
			fmt.Println("  v23 60fps 皮肤引擎：已启用 · 官方默认经典皮肤")
		}
	} else {
		fmt.Println("  v23 60fps 皮肤引擎：未启用（稳跑 v22.1 经典通道）")
	}
	if len(rep.Warnings) > 0 {
		fmt.Println("【提示】")
		for _, w := range rep.Warnings {
			fmt.Println("  !", w)
		}
	}
	if len(rep.Errors) > 0 {
		fmt.Println("【阻塞错误（未通过自检）】")
		for _, e := range rep.Errors {
			fmt.Println("  ✗", e)
		}
	} else {
		fmt.Println("【结果】✅ 全部自检通过，可安全弹出 U 盘后上车刷机。")
	}
}
