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
	{"cluster", "仪表地图 + 信息框", "手机导航地图显示在仪表中间，左侧小框显示行车数据；需要和「导航上 HUD」一起装", []string{"VW_HUD_3_CarPlayCluster"}},
}

// info-box catalogue; keys and labels must match g_items[] in src/cl_render/dash.c (build.sh checks the keys)
type item struct {
	Key    string `json:"key"`
	Label  string `json:"label"` // "A|B" = two lines on the cluster (A small on top)
	Unit   string `json:"unit"`
	Sample string `json:"sample"`
	Group  string `json:"group"`
	Note   string `json:"note,omitempty"`
}

var catalogue = []item{
	{"power", "功率", "kW", "187", "动力", "发动机当前输出功率"},
	{"torque", "扭矩", "Nm", "402", "动力", "发动机当前扭矩"},
	{"rpm", "转速", "rpm", "2350", "动力", ""},
	{"pedal", "油门", "%", "42", "动力", "油门踏板开度"},
	{"brake", "制动压力", "bar", "0.0", "动力", ""},
	{"g", "G 值", "g", "+0.22", "加速度", "合成加速度，加速带 +、刹车带 -"},
	{"glong", "纵向 G", "g", "+0.21", "加速度", "+ 加速 / - 刹车"},
	{"glat", "横向 G", "g", "+0.08", "加速度", "转弯"},
	{"speed", "车速", "km/h", "86", "行驶", ""},
	{"steer", "转向角", "°", "-12", "行驶", "方向盘转角"},
	{"gear", "挡位", "", "D6", "行驶", "仪表左下角本来就有"},
	{"oil", "机油温度", "°C", "96", "温度", ""},
	{"gearbox", "变速箱油温", "°C", "78", "温度", ""},
	{"coolant", "冷却液温度", "°C", "90", "温度", ""},
	{"outside", "车外温度", "°C", "21", "温度", ""},
	{"cons", "当前消耗", "L/100km", "11.4", "油耗", "停车时变成 L/h"},
	{"fuel", "燃油存量", "L", "41", "油耗", "油箱剩余升数"},
	{"range", "续驶里程", "km", "412", "油耗", ""},
	{"trip", "自启动起|平均消耗", "L/100km", "9.8", "自启动起", "熄火超过 2 小时后重新开始"},
	{"tripdist", "自启动起|行驶里程", "km", "37", "自启动起", ""},
	{"tripspeed", "自启动起|平均车速", "km/h", "42", "自启动起", ""},
	{"triptime", "自启动起|行驶时间", "h", "0:53", "自启动起", ""},
	{"refuel", "自上次加油起|平均消耗", "L/100km", "10.6", "自上次加油起", "加油后自动清零"},
	{"refueldist", "自上次加油起|行驶里程", "km", "286", "自上次加油起", ""},
	{"refuelspeed", "自上次加油起|平均车速", "km/h", "48", "自上次加油起", ""},
	{"refueltime", "自上次加油起|行驶时间", "h", "5:58", "自上次加油起", ""},
	{"total", "累计|平均消耗", "L/100km", "11.2", "累计", ""},
	{"totaldist", "累计|行驶里程", "km", "1933", "累计", ""},
	{"totalspeed", "累计|平均车速", "km/h", "51", "累计", ""},
	{"totaltime", "累计|行驶时间", "h", "37:51", "累计", ""},
}

const maxRows = 10

type dashConf struct {
	Show    bool     `json:"show"`
	Items   []string `json:"items"`
	X       int      `json:"x"`
	Y       int      `json:"y"`
	Alpha   int      `json:"alpha"`
	TankL   int      `json:"tank_l"`
	LatFlip int      `json:"lat_flip"`
	Unknown []string `json:"unknown,omitempty"` // names in items= that are not in the catalogue
}

func defaultDash() dashConf {
	return dashConf{Show: true, Items: []string{"power", "torque", "g", "rpm", "oil", "gearbox"}, X: 40, Y: 172, Alpha: 200, TankL: 90}
}

type pkg struct{ Root string }

func (p pkg) path(parts ...string) string { return filepath.Join(append([]string{p.Root}, parts...)...) }

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

func (p pkg) dashPath() string { return filepath.Join(p.modPath("VW_HUD_3_CarPlayCluster"), "Update", "dash.conf") }

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
		case "items":
			d.Items, d.Unknown = nil, nil
			for _, s := range strings.Split(v, ",") {
				if strings.TrimSpace(s) == "" {
					continue
				}
				if key, ok := lookupItem(s); ok {
					if len(d.Items) < maxRows {
						d.Items = append(d.Items, key)
					}
				} else {
					d.Unknown = append(d.Unknown, strings.TrimSpace(s))
				}
			}
		case "show", "info":
			if nerr == nil {
				d.Show = n != 0
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
		}
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
	show := 0
	if d.Show {
		show = 1
	}
	vals := []struct{ k, v string }{
		{"items", strings.Join(d.Items, ",")},
		{"show", strconv.Itoa(show)},
		{"x", strconv.Itoa(d.X)},
		{"y", strconv.Itoa(d.Y)},
		{"alpha", strconv.Itoa(clamp(d.Alpha, 0, 255))},
		{"tank_l", strconv.Itoa(clamp(d.TankL, 0, 200))},
		{"lat_flip", strconv.Itoa(clamp(d.LatFlip, 0, 1))},
	}
	aliases := map[string]string{"info": "show", "info_x": "x", "info_y": "y", "info_alpha": "alpha"}
	done := map[string]bool{}
	var out []string
	text := strings.ReplaceAll(string(b), "\r\n", "\n")
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	for _, line := range lines {
		k, _, ok := confKV(line)
		if ok {
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
	return os.WriteFile(path, []byte(strings.Join(out, "\n")+"\n"), 0o644)
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
}

func validate(r applyReq) error {
	for _, f := range features {
		switch r.Features[f.ID] {
		case "install", "uninstall", "skip":
		default:
			return fmt.Errorf("「%s」没有选择", f.Name)
		}
	}
	if r.Features["cluster"] == "install" && r.Features["hud"] != "install" {
		return errors.New("装「仪表地图 + 信息框」时「导航上 HUD」也要选安装")
	}
	if r.Features["hud"] == "uninstall" && r.Features["cluster"] != "uninstall" {
		return errors.New("卸载「导航上 HUD」时「仪表地图 + 信息框」也要一起卸载")
	}
	if len(r.Dash.Items) > maxRows {
		return fmt.Errorf("信息框最多 %d 项", maxRows)
	}
	if r.Dash.Show && len(r.Dash.Items) == 0 {
		return errors.New("信息框至少选一项，或者关掉信息框")
	}
	seen := map[string]bool{}
	for _, k := range r.Dash.Items {
		if _, ok := lookupItem(k); !ok || seen[k] {
			return fmt.Errorf("信息框项目有误：%s", k)
		}
		seen[k] = true
	}
	return nil
}

func (p pkg) apply(r applyReq) ([]string, error) {
	if err := validate(r); err != nil {
		return nil, err
	}
	var log []string
	// check everything first, change nothing on an error
	for _, f := range features {
		for _, d := range f.Dirs {
			loc, err := p.location(d)
			if err != nil {
				return nil, err
			}
			if loc == "" && r.Features[f.ID] != "skip" {
				return nil, fmt.Errorf("U 盘里找不到「%s」文件夹（%s），请重新复制完整的改装包", f.Name, d)
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
					return log, err
				}
				if err := os.Rename(p.path("Mods", d), p.path(parkDir, d)); err != nil {
					return log, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				log = append(log, fmt.Sprintf("%s：移到「%s」（这次不刷）", d, parkDir))
			} else if want != "skip" && loc == "park" {
				if err := os.Rename(p.path(parkDir, d), p.path("Mods", d)); err != nil {
					return log, fmt.Errorf("移动 %s 失败：%v", d, err)
				}
				log = append(log, fmt.Sprintf("%s：移回 Mods", d))
			}
			u := filepath.Join(p.modPath(d), "uninstall.txt")
			if want == "uninstall" {
				if !isFile(u) {
					if err := os.WriteFile(u, nil, 0o644); err != nil {
						return log, err
					}
				}
				log = append(log, fmt.Sprintf("%s：卸载（已放 uninstall.txt）", d))
			} else {
				if isFile(u) {
					if err := os.Remove(u); err != nil {
						return log, err
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
	if loc, _ := p.location("VW_HUD_3_CarPlayCluster"); loc != "" {
		if err := p.writeDash(r.Dash); err != nil {
			return log, fmt.Errorf("写 dash.conf 失败：%v", err)
		}
		names := make([]string, 0, len(r.Dash.Items))
		for _, k := range r.Dash.Items {
			for _, it := range catalogue {
				if it.Key == k {
					names = append(names, strings.ReplaceAll(it.Label, "|", ""))
				}
			}
		}
		if r.Dash.Show {
			log = append(log, "信息框："+strings.Join(names, "、"))
		} else {
			log = append(log, "信息框：不显示")
		}
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
	return log, nil
}
