package main

// The USB package model: which mod folders exist, where they are (Mods/ = flashed, 未选模块/ = left out),
// whether they carry uninstall.txt, and the info-box settings in dash.conf.

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const parkDir = "未选模块" // folders moved here are ignored by the ModKit (it only runs Mods/*)

type feature struct {
	ID   string   `json:"id"`
	Name string   `json:"name"`
	Desc string   `json:"desc"`
	Dirs []string `json:"dirs"`
}

var features = []feature{
	{"fullscreen", "全屏 CarPlay", "去掉黑边，CarPlay 铺满整个中控屏", []string{"VW_FullScreenCarPlay"}},
	{"carplay", "无线 CarPlay", "开启原厂未激活的无线 CarPlay（LawPaul 作品）", []string{"AppleCarPlay"}},
	{"aa", "Android Auto", "开启原厂未激活的 Android Auto，安卓手机投屏（LawPaul 作品）", []string{"AndroidAuto"}},
	{"hud", "导航上 HUD", "CarPlay 导航的转向箭头、到路口距离、路名、车道显示在抬头显示 HUD 上", []string{"VW_HUD_1_RouteGuidance", "VW_HUD_2_ClusterNav"}},
	{"cluster", "仪表地图 + 大字运动信息框", "手机导航地图投屏到液晶仪表中央，左右两侧自由定制 43 项真实行车数据（大号粗体 + 运动刻度条）与大字导航卡；需配合「导航上 HUD」", []string{"VW_HUD_3_CarPlayCluster"}},
	{"mapswitch", "方向盘 OK 双击切换 CarPlay / 原车地图", "方向盘右侧 OK 键 2 秒内连按 2 下，在 CarPlay 导航地图与原车自带地图之间来回切换；需配合「导航上 HUD」与「仪表地图 + 大字运动信息框」", []string{"VW_HUD_4_OkMapSwitch"}},
	{"siri", "方向盘语音键短按直接唤 Siri（试验）", "连接 CarPlay 时短按方向盘语音键直接唤出手机 Siri（无需长按 2 秒；原厂长按不受影响，试验功能：部分固件短按可能同时触发原车语音）；需配合「导航上 HUD」", []string{"VW_HUD_5_SiriShortPress"}},
	{"wifi5gfix", "老固件升 P2878 无线 CarPlay 5GHz 区域修复", "仅从旧版本（如 P9832）升级 P2878 后、有线正常但无线 CarPlay 连不上时勾选：解除 5GHz 国家码校验拦截（原厂 P2878 无线正常无需勾选）", []string{"VW_Wireless5GHzFix"}},
	{"nobtauth", "去除「输入用户名」弹窗（屏蔽蓝牙 OBEX 认证）", "屏蔽蓝牙连接时反复弹出的「输入用户名」弹窗（Busrlo / BLUETOOTH_CONNECT_AUTH_REQUIRED），不影响正常蓝牙配对、通话、音乐与 CarPlay（大众 MH2p 通用）", []string{"VW_NoBtAuthPopup"}},
	{"nosimpopup", "屏蔽「SIM 卡仅用于数据连接。」弹窗", "仅屏蔽连接 CarPlay / Android Auto 时反复弹出的 SIM 卡数据模式提示框（PHONE_SIM_DATAONLY），不影响 SIM 卡联网、蓝牙、CarPlay 与 SOS（大众 MH2p 通用）", []string{"VW_NoSimPopup"}},
	{"wifi", "手机无线控制台（可选 SSH 开发）", "连手机热点打开网页即可改信息框、调车标、无线下载行车记录（可选开启 SSH 2012 开发；同平台 MH2p 通用）", []string{"VW_WiFi_Dev"}},
}

// info-box catalogue; keys and labels must match g_items[] in src/cl_render/dash.c (build.sh checks the keys)
type item struct {
	Key    string `json:"key"`
	Label  string `json:"label"`
	Unit   string `json:"unit"`
	Sample string `json:"sample"`
	Group  string `json:"group"`
	Note   string `json:"note,omitempty"`
}

var catalogue = []item{
	{"power", "功率", "kW", "187", "动力", "发动机当前输出功率"},
	{"torque", "扭矩", "Nm", "402", "动力", "发动机当前扭矩"},
	{"rpm", "转速", "rpm", "2350", "动力", "附带冰青→琥珀→红线斜切运动刻度条"},
	{"pedal", "油门", "%", "42", "动力", "油门踏板开度"},
	{"brake", "制动压力", "bar", "0.0", "动力", ""},
	{"g", "G 值", "g", "+0.22", "加速度", "合成加速度，加速带 +、刹车带 -"},
	{"glong", "纵向 G", "g", "+0.21", "加速度", "+ 加速 / - 刹车"},
	{"glat", "横向 G", "g", "+0.08", "加速度", "转弯横向加速度"},
	{"t100", "0–100", "s", "6.8", "性能", "百公里加速：从静止起步自动计时，过 100 km/h 锁定成绩"},
	{"pmax", "最大功率", "kW", "212", "性能", "车机开机以来的最大值"},
	{"tqmax", "最大扭矩", "Nm", "448", "性能", "车机开机以来的最大值"},
	{"gmax", "最大 G 值", "g", "0.62", "性能", "车机开机以来的最大合成加速度"},
	{"speed", "车速", "km/h", "86", "行驶", ""},
	{"steer", "转向角", "°", "-12", "行驶", "方向盘转角（居中双向刻度条）"},
	{"gear", "挡位", "", "D6", "行驶", "P/R/N/D1-8/S1-8/M1-8（ZF 8HP65A 实车齿比标定）"},
	{"oil", "机油温度", "°C", "96", "温度", "低温冰蓝 / 适温青绿 / 高温红刻度条"},
	{"gearbox", "变速箱油温", "°C", "78", "温度", ""},
	{"coolant", "冷却液温度", "°C", "90", "温度", ""},
	{"outside", "车外温度", "°C", "21", "温度", ""},
	{"cons", "当前消耗", "L/100km", "11.4", "油耗", "停车时自动切换为 L/h"},
	{"fuel", "燃油存量", "L", "41", "油耗", "油箱剩余升数"},
	{"range", "续驶里程", "km", "412", "油耗", ""},
	{"trip", "本次油耗", "L/100km", "9.8", "自启动起", "熄火超过 2 小时后重新开始"},
	{"tripdist", "本次里程", "km", "37", "自启动起", ""},
	{"tripspeed", "本次均速", "km/h", "42", "自启动起", ""},
	{"triptime", "本次时长", "h", "0:53", "自启动起", ""},
	{"refuel", "加油后油耗", "L/100km", "10.6", "自上次加油起", "加油后自动清零"},
	{"refueldist", "加油后里程", "km", "286", "自上次加油起", ""},
	{"refuelspeed", "加油后均速", "km/h", "48", "自上次加油起", ""},
	{"refueltime", "加油后时长", "h", "5:58", "自上次加油起", ""},
	{"total", "累计油耗", "L/100km", "11.2", "累计", ""},
	{"totaldist", "累计里程", "km", "1933", "累计", ""},
	{"totalspeed", "累计均速", "km/h", "51", "累计", ""},
	{"totaltime", "累计时长", "h", "37:51", "累计", ""},
	{"tripkm", "Trip", "km", "546.3", "行驶", "仪表 Trip 里程（用总里程推算，需在 dash.conf 设 trip_base＝Trip 清零时的总里程）"},
	{"tires", "四轮胎压", "bar", "2.3·2.2", "胎压与越野", "单行浓缩显示前桥均值·后桥均值胎压（报警附 !）"},
	{"tpfl", "左前胎压", "bar", "2.3", "胎压与越野", "原车 RDK 直接胎压监测传感器数据"},
	{"tpfr", "右前胎压", "bar", "2.3", "胎压与越野", ""},
	{"tprl", "左后胎压", "bar", "2.2", "胎压与越野", ""},
	{"tprr", "右后胎压", "bar", "2.2", "胎压与越野", ""},
	{"pitch", "俯仰角", "°", "+1.2", "胎压与越野", "车身前后俯仰坡度角（上坡 + / 下坡 -）"},
	{"roll", "侧倾角", "°", "-0.5", "胎压与越野", "车身左右侧倾坡度角"},
	{"alt", "海拔", "m", "68", "胎压与越野", "原车 GNSS 海拔高度"},
}

const maxRows = 10

type dashConf struct {
	Show    bool     `json:"show"`
	Bars    bool     `json:"bars"`
	Smooth  bool     `json:"smooth"`
	Items   []string `json:"items"`
	X       int      `json:"x"`
	Y       int      `json:"y"`
	Alpha   int      `json:"alpha"`
	TankL   int      `json:"tank_l"`
	LatFlip int      `json:"lat_flip"`
	Unknown []string `json:"unknown,omitempty"` // names in items= that are not in the catalogue
	// v19: right box (keys r_show / r_items in dash.conf; position r_x / r_y / r_right stays as in the file)
	RShow  bool     `json:"r_show"`
	RItems []string `json:"r_items"`
	// v19.2: car icon on the CarPlay cluster map (read by cl_render / clusterd from dash.conf)
	CarUp    int `json:"car_up"`    // rows the map is always moved up (0-90)
	AccUp    int `json:"acc_up"`    // extra rows while adaptive cruise control is on (0-90)
	AmapTurn int `json:"amap_turn"` // AMap turn compensation: 0 off, 1 on, 2 auto, 3 from the picture (recommended)
	// v20+: large-font navigation card (nav_box: 0 off, 1 auto on nav, 2 always-on standby card; nav_side / nav_tz) and steering-wheel OK double-tap map switch (switch_off flag)
	NavBox   int    `json:"nav_box"`
	NavSide  string `json:"nav_side"` // "right" (default) or "left"
	NavTz    int    `json:"nav_tz"`
	OkSwitch bool   `json:"ok_switch"`
}

func defaultDash() dashConf {
	return dashConf{
		Show:     true,
		Bars:     true,
		Smooth:   true,
		Items:    []string{"power", "torque", "g", "rpm", "oil", "gearbox"},
		X:        5,
		Y:        164,
		Alpha:    200,
		TankL:    90,
		RItems:   []string{},
		CarUp:    35,
		AccUp:    80,
		AmapTurn: 3,
		NavBox:   1,
		NavSide:  "right",
		NavTz:    8,
		OkSwitch: false,
	}
}

type pkg struct{ Root string }

func (p pkg) path(parts ...string) string {
	return filepath.Join(append([]string{p.Root}, parts...)...)
}

func isDir(p string) bool  { st, err := os.Stat(p); return err == nil && st.IsDir() }
func isFile(p string) bool { st, err := os.Stat(p); return err == nil && !st.IsDir() }

// where a mod folder is now: "mods", "park" or "" (missing); error if it is in both places
func (p pkg) location(dir string) (string, error) {
	m, k := isDir(p.path("Mods", dir)), isDir(p.path(parkDir, dir))
	switch {
	case m && k:
		return "", fmt.Errorf("「%s」在 Mods 和 %s 里各有一份，请删掉其中一个", dir, parkDir)
	case m:
		return "mods", nil
	case k:
		return "park", nil
	}
	return "", nil
}

func (p pkg) modPath(dir string) string {
	if loc, _ := p.location(dir); loc == "park" {
		return p.path(parkDir, dir)
	}
	return p.path("Mods", dir)
}

// current state of a feature on the drive: install / uninstall / skip / missing / mixed
func (p pkg) featureState(f feature) (string, error) {
	st := ""
	for _, d := range f.Dirs {
		loc, err := p.location(d)
		if err != nil {
			return "", err
		}
		s := "missing"
		switch loc {
		case "mods":
			s = "install"
			if isFile(p.path("Mods", d, "uninstall.txt")) {
				s = "uninstall"
			}
		case "park":
			s = "skip"
		}
		if st == "" {
			st = s
		} else if st != s {
			st = "mixed"
		}
	}
	return st, nil
}

// ---- dash.conf ----

func normName(s string) string {
	return strings.Map(func(r rune) rune {
		if r == ' ' || r == '\t' || r == '|' || r == '　' {
			return -1
		}
		return r
	}, s)
}

func lookupItem(name string) (string, bool) {
	n := normName(name)
	for _, it := range catalogue {
		if n == it.Key || n == normName(it.Label) {
			return it.Key, true
		}
	}
	return "", false
}

func (p pkg) dashPath() string {
	return filepath.Join(p.modPath("VW_HUD_3_CarPlayCluster"), "Update", "dash.conf")
}

func (p pkg) switchOffPath() string {
	return filepath.Join(p.modPath("VW_HUD_3_CarPlayCluster"), "Update", "switch_off")
}

func confKV(line string) (string, string, bool) {
	t := strings.TrimSpace(line)
	if t == "" || strings.HasPrefix(t, "#") {
		return "", "", false
	}
	i := strings.IndexByte(t, '=')
	if i < 0 {
		return "", "", false
	}
	return strings.TrimSpace(t[:i]), strings.TrimSpace(t[i+1:]), true
}

func (p pkg) readDash() (dashConf, bool) {
	d := defaultDash()
	b, err := os.ReadFile(p.dashPath())
	if err != nil {
		return d, false
	}
	for _, line := range strings.Split(string(b), "\n") {
		k, v, ok := confKV(line)
		if !ok {
			continue
		}
		if i := strings.IndexByte(v, '#'); i >= 0 {
			v = strings.TrimSpace(v[:i])
		}
		n, nerr := strconv.Atoi(v)
		switch k {
		case "items", "r_items":
			var list []string
			for _, s := range strings.Split(v, ",") {
				if strings.TrimSpace(s) == "" {
					continue
				}
				if key, ok := lookupItem(s); ok {
					if len(list) < maxRows {
						list = append(list, key)
					}
				} else {
					d.Unknown = append(d.Unknown, strings.TrimSpace(s))
				}
			}
			if list == nil {
				list = []string{}
			}
			if k == "items" {
				d.Items = list
			} else {
				d.RItems = list
			}
		case "r_show":
			if nerr == nil {
				d.RShow = n != 0
			}
		case "show", "info":
			if nerr == nil {
				d.Show = n != 0
			}
		case "bars":
			if nerr == nil {
				d.Bars = n != 0
			}
		case "smooth":
			if nerr == nil {
				d.Smooth = n != 0
			}
		case "x", "info_x":
			if nerr == nil {
				d.X = n
			}
		case "y", "info_y":
			if nerr == nil {
				d.Y = n
			}
		case "alpha", "info_alpha":
			if nerr == nil {
				d.Alpha = n
			}
		case "tank_l":
			if nerr == nil {
				d.TankL = n
			}
		case "lat_flip":
			if nerr == nil {
				d.LatFlip = n
			}
		case "car_up":
			if nerr == nil {
				d.CarUp = n
			}
		case "acc_up":
			if nerr == nil {
				d.AccUp = n
			}
		case "amap_turn":
			if nerr == nil {
				d.AmapTurn = n
			}
		case "nav_box":
			if nerr == nil {
				d.NavBox = clamp(n, 0, 2)
			}
		case "nav_side":
			lv := strings.ToLower(strings.TrimSpace(v))
			if lv == "left" || lv == "0" {
				d.NavSide = "left"
			} else {
				d.NavSide = "right"
			}
		case "nav_tz":
			if nerr == nil {
				d.NavTz = n
			}
		}
	}
	if loc, _ := p.location("VW_HUD_4_OkMapSwitch"); loc != "" {
		d.OkSwitch = loc == "mods" && !isFile(p.path("Mods", "VW_HUD_4_OkMapSwitch", "uninstall.txt"))
	} else {
		d.OkSwitch = !isFile(p.switchOffPath())
	}
	return d, true
}

// writeDash keeps the comments of the existing file and replaces/adds the value lines
func (p pkg) writeDash(d dashConf) error {
	path := p.dashPath()
	b, err := os.ReadFile(path)
	if err != nil {
		if !errors.Is(err, fs.ErrNotExist) {
			return err
		}
		b = []byte("# 仪表信息框设置（由「改装设置」程序生成）\n")
	}
	show, bars, smooth, rshow := 0, 0, 0, 0
	if d.Show {
		show = 1
	}
	if d.Bars {
		bars = 1
	}
	if d.Smooth {
		smooth = 1
	}
	if d.RShow {
		rshow = 1
	}
	navbox := clamp(d.NavBox, 0, 2)
	navside := "right"
	if strings.EqualFold(strings.TrimSpace(d.NavSide), "left") {
		navside = "left"
	}
	vals := []struct{ k, v string }{
		{"items", strings.Join(d.Items, ",")},
		{"show", strconv.Itoa(show)},
		{"bars", strconv.Itoa(bars)},
		{"smooth", strconv.Itoa(smooth)},
		{"x", strconv.Itoa(d.X)},
		{"y", strconv.Itoa(d.Y)},
		{"alpha", strconv.Itoa(clamp(d.Alpha, 0, 255))},
		{"tank_l", strconv.Itoa(clamp(d.TankL, 0, 200))},
		{"lat_flip", strconv.Itoa(clamp(d.LatFlip, 0, 1))},
		{"r_items", strings.Join(d.RItems, ",")},
		{"r_show", strconv.Itoa(rshow)},
		{"car_up", strconv.Itoa(clamp(d.CarUp, 0, 90))},
		{"acc_up", strconv.Itoa(clamp(d.AccUp, 0, 90))},
		{"amap_turn", strconv.Itoa(clamp(d.AmapTurn, 0, 3))},
		{"nav_box", strconv.Itoa(navbox)},
		{"nav_side", navside},
		{"nav_tz", strconv.Itoa(clamp(d.NavTz, -12, 14))},
	}
	obsolete := map[string]bool{"media_box": true, "ultra_theme": true, "rpm_bar": true, "toast": true}
	aliases := map[string]string{"info": "show", "info_x": "x", "info_y": "y", "info_alpha": "alpha"}
	done := map[string]bool{}
	var out []string
	text := strings.ReplaceAll(string(b), "\r\n", "\n")
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	for _, line := range lines {
		k, _, ok := confKV(line)
		if ok {
			if obsolete[k] {
				continue
			}
			if a, isA := aliases[k]; isA {
				k = a
			}
			found := false
			for _, kv := range vals {
				if kv.k == k {
					found = true
					if !done[k] {
						out = append(out, k+"="+kv.v)
						done[k] = true
					}
				}
			}
			if found {
				continue // replaced, or a duplicate of an already written key
			}
		}
		out = append(out, line)
	}
	for _, kv := range vals {
		if !done[kv.k] {
			out = append(out, kv.k+"="+kv.v)
		}
	}
	if err := os.WriteFile(path, []byte(strings.Join(out, "\n")+"\n"), 0o644); err != nil {
		return err
	}
	swPath := p.switchOffPath()
	if d.OkSwitch {
		if err := os.Remove(swPath); err != nil && !errors.Is(err, fs.ErrNotExist) {
			return err
		}
	} else {
		if err := os.WriteFile(swPath, []byte{}, 0o644); err != nil {
			return err
		}
	}
	return nil
}

func clamp(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// ---- macOS metadata files (._* AppleDouble, .DS_Store): the head unit's updater trips over them ----

var skipMeta = map[string]bool{".Spotlight-V100": true, ".fseventsd": true, ".Trashes": true, "System Volume Information": true, "$RECYCLE.BIN": true}

func (p pkg) macJunk() []string {
	var out []string
	filepath.WalkDir(p.Root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		name := d.Name()
		if d.IsDir() {
			if path != p.Root && skipMeta[name] {
				return filepath.SkipDir
			}
			return nil
		}
		if strings.HasPrefix(name, "._") || name == ".DS_Store" {
			out = append(out, path)
		}
		return nil
	})
	sort.Strings(out)
	return out
}

// ---- apply ----

type applyReq struct {
	Features map[string]string `json:"features"` // id -> install / uninstall / skip
	Dash     dashConf          `json:"dash"`
	V23      v23ApplyReq       `json:"v23"`
	Wifi     wifiReq           `json:"wifi"`
}

func validate(r applyReq) error {
	for _, f := range features {
		switch r.Features[f.ID] {
		case "install", "uninstall", "skip":
		default:
			return fmt.Errorf("「%s」没有选择", f.Name)
		}
	}
	if r.Features["wifi"] == "install" {
		if err := validateWifi(r.Wifi); err != nil {
			return err
		}
	}
	if r.Features["cluster"] == "install" && r.Features["hud"] != "install" {
		return errors.New("装「仪表地图 + 大字运动信息框」时「导航上 HUD」也要选安装")
	}
	if r.Features["siri"] == "install" && r.Features["hud"] != "install" {
		return errors.New("装「方向盘语音键短按直接唤 Siri」时「导航上 HUD」也要选安装")
	}
	if r.Features["mapswitch"] == "install" && (r.Features["cluster"] != "install" || r.Features["hud"] != "install") {
		return errors.New("装「方向盘 OK 双击切换地图」时，「导航上 HUD」与「仪表地图 + 大字运动信息框」也要选安装")
	}
	if r.Features["hud"] == "uninstall" && (r.Features["cluster"] != "uninstall" || r.Features["mapswitch"] != "uninstall" || r.Features["siri"] != "uninstall") {
		return errors.New("卸载「导航上 HUD」时，「仪表地图 + 大字运动信息框」、「方向盘 OK 双击切换地图」与「语音键短按唤 Siri」也要一起卸载")
	}
	if r.Features["cluster"] == "uninstall" && r.Features["mapswitch"] != "uninstall" {
		return errors.New("卸载「仪表地图 + 大字运动信息框」时，「方向盘 OK 双击切换地图」也要一起卸载")
	}
	if len(r.Dash.Items) > maxRows {
		return fmt.Errorf("信息框最多 %d 项", maxRows)
	}
	if len(r.Dash.RItems) > maxRows {
		return fmt.Errorf("右边信息框最多 %d 项", maxRows)
	}
	if r.Features["cluster"] == "install" {
		if err := validateV23(r.V23); err != nil {
			return err
		}
		if r.Dash.Show && len(r.Dash.Items) == 0 {
			return errors.New("左边信息框至少选一项，或者关掉左边框")
		}
		if r.Dash.RShow && len(r.Dash.RItems) == 0 {
			return errors.New("右边信息框至少选一项，或者关掉右边框")
		}
	}
	for _, list := range [][]string{r.Dash.Items, r.Dash.RItems} {
		seen := map[string]bool{}
		for _, k := range list {
			if _, ok := lookupItem(k); !ok || seen[k] {
				return fmt.Errorf("信息框项目有误：%s", k)
			}
			seen[k] = true
		}
	}
	return nil
}

func (p pkg) apply(r applyReq) ([]string, wifiApplyOut, error) {
	var wout wifiApplyOut
	if err := validate(r); err != nil {
		return nil, wout, err
	}
	var log []string
	// check everything first, change nothing on an error
	for _, f := range features {
		for _, d := range f.Dirs {
			loc, err := p.location(d)
			if err != nil {
				return nil, wout, err
			}
			if loc == "" && r.Features[f.ID] != "skip" {
				return nil, wout, fmt.Errorf("U 盘里找不到「%s」文件夹（%s），请重新复制完整的改装包", f.Name, d)
			}
		}
	}
	for _, f := range features {
		want := r.Features[f.ID]
		for _, d := range f.Dirs {
			loc, _ := p.location(d)
			if loc == "" {
				continue
			}
			if want == "skip" && loc == "mods" {
				if err := os.MkdirAll(p.path(parkDir), 0o755); err != nil {
					return log, wout, err
				}
				if err := os.Rename(p.path("Mods", d), p.path(parkDir, d)); err != nil {
					return log, wout, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				log = append(log, fmt.Sprintf("%s：移到「%s」（这次不刷）", d, parkDir))
			} else if want != "skip" && loc == "park" {
				if err := os.MkdirAll(p.path("Mods"), 0o755); err != nil {
					return log, wout, err
				}
				if err := os.Rename(p.path(parkDir, d), p.path("Mods", d)); err != nil {
					return log, wout, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				log = append(log, fmt.Sprintf("%s：移回 Mods", d))
			}
			u := filepath.Join(p.modPath(d), "uninstall.txt")
			if want == "uninstall" {
				if !isFile(u) {
					if err := os.WriteFile(u, nil, 0o644); err != nil {
						return log, wout, err
					}
				}
				log = append(log, fmt.Sprintf("%s：卸载（已放 uninstall.txt）", d))
			} else {
				if isFile(u) {
					if err := os.Remove(u); err != nil {
						return log, wout, err
					}
					log = append(log, fmt.Sprintf("%s：删掉 uninstall.txt", d))
				}
				if want == "install" {
					log = append(log, fmt.Sprintf("%s：安装", d))
				}
			}
		}
	}
	os.Remove(p.path(parkDir)) // only succeeds when empty
	if r.Features["wifi"] == "install" {
		if loc, _ := p.location("VW_WiFi_Dev"); loc != "" {
			wlog, wo, err := p.writeWifi(r.Wifi)
			if err != nil {
				return log, wout, fmt.Errorf("写入无线配置失败：%v", err)
			}
			log = append(log, wlog...)
			wout = wo
		}
	}
	if r.Features["cluster"] == "install" {
		if loc, _ := p.location("VW_HUD_3_CarPlayCluster"); loc != "" {
			baseDash := r.Dash
			baseDash.OkSwitch = r.Features["mapswitch"] == "install"
			if err := p.writeDash(baseDash); err != nil {
				return log, wout, fmt.Errorf("写 dash.conf 失败：%v", err)
			}
			names := func(keys []string) string {
				out := make([]string, 0, len(keys))
				for _, k := range keys {
					for _, it := range catalogue {
						if it.Key == k {
							out = append(out, strings.ReplaceAll(it.Label, "|", ""))
						}
					}
				}
				return strings.Join(out, "、")
			}
			if r.Dash.Show {
				log = append(log, "左边信息框："+names(r.Dash.Items))
			} else {
				log = append(log, "左边信息框：不显示")
			}
			if r.Dash.RShow {
				log = append(log, "右边信息框："+names(r.Dash.RItems))
			} else {
				log = append(log, "右边信息框：不显示")
			}
			log = append(log, fmt.Sprintf("运动刻度进度条：%s（阻尼平滑动画：%s）",
				map[bool]string{true: "开启", false: "关闭"}[r.Dash.Bars],
				map[bool]string{true: "开启", false: "关闭"}[r.Dash.Smooth]))
			if r.Dash.NavBox > 0 {
				sideTxt := "右框"
				if strings.EqualFold(strings.TrimSpace(r.Dash.NavSide), "left") {
					sideTxt = "左框"
				}
				modeTxt := "仅导航时弹出"
				if r.Dash.NavBox >= 2 {
					modeTxt = "常驻（未导航显示待机框）"
				}
				log = append(log, fmt.Sprintf("大字导航信息卡：放%s · %s（UTC%+d）", sideTxt, modeTxt, clamp(r.Dash.NavTz, -12, 14)))
			} else {
				log = append(log, "大字导航信息卡：关闭")
			}
			v23Logs, err := p.writeV23(r.V23)
			if err != nil {
				return log, wout, fmt.Errorf("写入 v23 皮肤配置失败：%v", err)
			}
			log = append(log, v23Logs...)
		}
	}
	if len(log) == 0 {
		log = append(log, "所有模块均保持「不处理」（已全部存放在「未选模块」，默认不开启任何改装）")
	}
	n := 0
	for _, j := range p.macJunk() {
		if os.Remove(j) == nil {
			n++
		}
	}
	if n > 0 {
		log = append(log, fmt.Sprintf("清理了 %d 个 Mac 隐藏文件（._* / .DS_Store）", n))
	}
	return log, wout, nil
}

// ---- USB mode (v18+ drive log) ----
// "flash": Data/Meta present, inserting the drive in the car starts the ModKit update.
// "record": Data/Meta renamed to _Data/_Meta (ModKit ignores the drive) and a drive_logs folder exists: the drive can
// stay plugged in and the car appends a CSV per trip to drive_logs (needs VW_HUD_2 on the car).

const logDir = "drive_logs"

const aiGuideName = "给AI分析行车记录.txt"
const aiGuide = `【怎么用】
把 drive_logs 里想分析的那个 CSV 文件（一次开车一个文件，文件名里的日期多是 1970，看文件大小和修改时间找）
连同这个说明一起上传到豆包、通义千问、DeepSeek、ChatGPT 等 AI，再把下面"提问模板"整段复制过去发送。
文件太大传不上时，用 Excel / Numbers 打开，只保留想看的那一段时间再另存为 CSV。

【提问模板】（整段复制）
这是我的大众途锐车机记录的行车数据（CSV，约每秒 10 行），列的含义见我上传的说明。请：
1. 概括这趟：时长、里程、平均车速、最高车速、怠速时间占比；
2. 驾驶习惯：急加速、急刹车（纵向加速度超过 ±3 m/s² 算一次）各多少次，大致在第几分钟；
3. 动力：出现过的最大功率、最大扭矩，以及当时的转速、车速、油门；
4. 油耗：按"当前消耗"估算这趟的平均油耗，并说明这个估算的局限；
5. 温度与胎压：机油、变速箱油、冷却液温度多久到正常工作温度，四轮胎压随里程的变化情况；
6. 有没有看起来不正常的数据（数值跳变、长时间为空等），可能是什么原因。
用中文回答，先给结论，再给依据；不确定的地方直接说不确定。

【文件格式】
第 1 行以 # 开头是说明（车机时钟从 1970 年开始，日期不可信），第 2 行是列名（共 34 列，均为车机真实物理传感器数据），之后每行一个时刻。
空格子 = 车机当时没给这个值。

【各列含义】
t_ms         车机开机后的毫秒数（时间轴，相邻两行约 100 毫秒）
spd_kmh      仪表显示车速 km/h（一般比真实车速高几个百分点）
real_spd_kmh 真实车速 km/h（分析速度、里程用这一列）
rpm          发动机转速
power_kw     发动机输出功率 kW（车机计算值）
torque_nm    发动机扭矩 N·m
pedal_pct    油门踏板开度 %
brake_bar    制动压力 bar（踩刹车时 > 0）
steer_deg    方向盘转角 度（正负表示左右）
yaw_dps      横摆角速度 度/秒
ax_ms2       纵向加速度 m/s²（正 = 加速，负 = 减速）
ay_g         横向加速度 g
lever        换挡杆位置：1 = P，2 = R，3 = N，4 = D，5 = S，6 = M
gear         挡位：根据 ZF 8HP65A 实车齿比标定输出 1～8 挡（P/R/N 或刚起步未锁止时可能为 0）
gearbox_c    变速箱油温 °C
oil_c        机油温度 °C（冷车时可能为空）
coolant_c    冷却液温度 °C
cons         当前消耗，单位见 cons_unit；车机报的上限是 40.9 L/100km，大于这个数时也显示 40.9
cons_unit    0 = L/100km，1 = km/L，2 = L/h（停车时车机改用 L/h）
tank_pct     油箱剩余 %
outside_c    车外温度 °C
range_km     续驶里程 km
wheel1_kmh … wheel4_kmh  四个车轮的轮速 km/h
acc          1 = 自适应巡航（ACC）正在控车，0 = 没有（只是待命时也是 0）
pitch_deg    车身俯仰坡度角 度（正 = 抬头上坡，负 = 低头下坡）
roll_deg     车身侧倾坡度角 度
alt_m        GNSS 海拔高度 米
tp_fl, tp_fr, tp_rl, tp_rr  左前、右前、左后、右后轮胎真实胎压 bar

【已知的无效值（分析前请先去掉）】
real_spd_kmh = 655.3 是"无数据"（多出现在停车很久后的第一行），不是真实车速
gearbox_c = -58 是"还没准备好"（刚启动时出现几行）
coolant_c 正常行驶时一直是 90：这是仪表显示用的平滑值，不是传感器原始值，看不出 90 上下的小变化
行与行之间相隔几秒到几十分钟、而前后车速和转速都是 0：是熄火停车，不是丢数据

【不包含】
增压压力、点火角、爆震、空燃比、进气温度等发动机内部数据（要用 OBD 诊断工具读）；原厂 65 号胎压模块固件默认屏蔽了轮内温度上报，因此不含胎温列。
`

func (p pkg) usbMode() string {
	if isDir(p.path("_Data")) && isDir(p.path("_Meta")) && !isDir(p.path("Data")) {
		return "record"
	}
	return "flash"
}

func (p pkg) setUsbMode(mode string) ([]string, error) {
	var log []string
	swap := func(from, to string) error {
		if isDir(p.path(from)) && !isDir(p.path(to)) {
			if err := os.Rename(p.path(from), p.path(to)); err != nil {
				return fmt.Errorf("把 %s 改名为 %s 失败：%v", from, to, err)
			}
			log = append(log, from+" → "+to)
		}
		return nil
	}
	switch mode {
	case "record":
		for _, n := range []string{"Data", "Meta"} {
			if err := swap(n, "_"+n); err != nil {
				return log, err
			}
		}
		if !isDir(p.path(logDir)) {
			if err := os.Mkdir(p.path(logDir), 0o755); err != nil {
				return log, err
			}
			log = append(log, "新建 "+logDir+" 文件夹")
		}
		if err := os.WriteFile(filepath.Join(p.path(logDir), aiGuideName), []byte(aiGuide), 0o644); err == nil {
			log = append(log, "写入 "+logDir+"/"+aiGuideName+"（把行车记录交给 AI 分析时用）")
		}
		log = append(log, "现在是行车记录 U 盘：插在车上不会刷机，行车数据记到 "+logDir)
	case "flash":
		for _, n := range []string{"Data", "Meta"} {
			if err := swap("_"+n, n); err != nil {
				return log, err
			}
		}
		log = append(log, "现在是刷机 U 盘：插到车上会开始刷机（"+logDir+" 里的记录保留）")
	default:
		return nil, errors.New("unknown mode")
	}
	return log, nil
}
