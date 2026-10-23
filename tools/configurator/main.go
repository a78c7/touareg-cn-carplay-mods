// 途锐改装设置: a small program shipped at the root of the USB package. Double-click it, choose in the browser
// which mods to install / uninstall / leave out and what the cluster info box shows, then save: it moves the
// mod folders, writes uninstall.txt / dash.conf and removes macOS metadata files. Nothing is sent anywhere;
// the page is served on 127.0.0.1 only and needs a per-run token.
package main

import (
	"crypto/rand"
	"embed"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync/atomic"
	"time"
)

//go:embed web/index.html
var web embed.FS

var version = "dev"

type volInfo struct {
	FS        string `json:"fs"`     // e.g. FAT32, exFAT, NTFS, msdos, apfs
	FAT       bool   `json:"fat"`    // usable by the head unit
	IsRoot    bool   `json:"isRoot"` // the package sits at the root of its drive
	Mount     string `json:"mount"`  // drive / mount point
	TotalGB   int    `json:"totalGB"`
	Removable bool   `json:"removable"` // best effort
}

type stateResp struct {
	Version   string            `json:"version"`
	OS        string            `json:"os"`
	Root      string            `json:"root"`
	Vol       volInfo           `json:"vol"`
	Features  []feature         `json:"features"`
	States    map[string]string `json:"states"`
	Catalogue []item            `json:"catalogue"`
	MaxRows   int               `json:"maxRows"`
	Dash      dashConf          `json:"dash"`
	DashFound bool              `json:"dashFound"`
	MacJunk   int               `json:"macJunk"`
	UsbMode   string            `json:"usbMode"` // "flash" (Data/Meta: inserting it flashes) or "record" (_Data/_Meta + drive_logs)
	Errors    []string          `json:"errors"`
}

func findRoot() (string, error) {
	if len(os.Args) > 1 && isDir(os.Args[1]) { // for testing: explicit package directory
		return filepath.Abs(os.Args[1])
	}
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	if r, err := filepath.EvalSymlinks(exe); err == nil {
		exe = r
	}
	return filepath.Dir(exe), nil
}

func checkRoot(root string) error {
	if isDir(filepath.Join(root, "Data")) && isDir(filepath.Join(root, "Meta")) {
		return nil
	}
	if isDir(filepath.Join(root, "_Data")) && isDir(filepath.Join(root, "_Meta")) { // drive-log mode
		return nil
	}
	if isDir(filepath.Join(root, "USB", "Data")) {
		return fmt.Errorf("这里是下载解压出来的整个文件夹。请把其中「USB」文件夹里面的全部内容复制到 U 盘根目录，再运行 U 盘里的这个程序")
	}
	return fmt.Errorf("这个程序旁边没有 Data、Meta、Mods 文件夹。请把改装包「USB」文件夹里面的全部内容一起复制到 U 盘根目录，再运行 U 盘里的这个程序")
}

func (p pkg) state() stateResp {
	s := stateResp{Version: version, OS: runtime.GOOS, Root: p.Root, Vol: volumeInfo(p.Root), Features: features,
		States: map[string]string{}, Catalogue: catalogue, MaxRows: maxRows}
	for _, f := range features {
		st, err := p.featureState(f)
		if err != nil {
			s.Errors = append(s.Errors, err.Error())
		}
		s.States[f.ID] = st
	}
	s.Dash, s.DashFound = p.readDash()
	s.MacJunk = len(p.macJunk())
	s.UsbMode = p.usbMode()
	return s
}

func openBrowser(url string) error {
	switch runtime.GOOS {
	case "windows":
		return exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	case "darwin":
		return exec.Command("open", url).Start()
	default:
		return exec.Command("xdg-open", url).Start()
	}
}

func waitExit(code int) {
	if runtime.GOOS == "windows" {
		fmt.Println("\n按回车键关闭本窗口。")
		fmt.Scanln()
	}
	os.Exit(code)
}

func main() {
	consoleUTF8()
	fmt.Printf("途锐改装设置 %s\n\n", version)
	root, err := findRoot()
	if err == nil {
		err = checkRoot(root)
	}
	if err != nil {
		fmt.Println("出错：", err)
		waitExit(1)
	}
	p := pkg{Root: root}

	tb := make([]byte, 12)
	rand.Read(tb)
	token := hex.EncodeToString(tb)
	var lastPing atomic.Int64
	lastPing.Store(time.Now().Unix())
	quit := make(chan struct{}, 1)

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		b, _ := web.ReadFile("web/index.html")
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Cache-Control", "no-store")
		w.Write(b)
	})
	api := func(path string, h func(w http.ResponseWriter, r *http.Request)) {
		mux.HandleFunc(path, func(w http.ResponseWriter, r *http.Request) {
			if r.Header.Get("X-Token") != token {
				http.Error(w, "forbidden", http.StatusForbidden)
				return
			}
			lastPing.Store(time.Now().Unix())
			w.Header().Set("Content-Type", "application/json; charset=utf-8")
			w.Header().Set("Cache-Control", "no-store")
			h(w, r)
		})
	}
	api("/api/state", func(w http.ResponseWriter, r *http.Request) { json.NewEncoder(w).Encode(p.state()) })
	api("/api/ping", func(w http.ResponseWriter, r *http.Request) { w.Write([]byte("{}")) })
	api("/api/apply", func(w http.ResponseWriter, r *http.Request) {
		var req applyReq
		if r.Method != http.MethodPost || json.NewDecoder(r.Body).Decode(&req) != nil {
			http.Error(w, "bad request", http.StatusBadRequest)
			return
		}
		log, err := p.apply(req)
		resp := map[string]any{"ok": err == nil, "log": log}
		if err != nil {
			resp["error"] = err.Error()
			fmt.Println("保存失败：", err)
		} else {
			fmt.Println("已保存：")
			for _, l := range log {
				fmt.Println("  " + l)
			}
		}
		json.NewEncoder(w).Encode(resp)
	})
	api("/api/usbmode", func(w http.ResponseWriter, r *http.Request) {
		var req struct {
			Mode string `json:"mode"`
		}
		if r.Method != http.MethodPost || json.NewDecoder(r.Body).Decode(&req) != nil {
			http.Error(w, "bad request", http.StatusBadRequest)
			return
		}
		log, err := p.setUsbMode(req.Mode)
		resp := map[string]any{"ok": err == nil, "log": log, "mode": p.usbMode()}
		if err != nil {
			resp["error"] = err.Error()
			fmt.Println("切换失败：", err)
		} else {
			fmt.Println("U 盘用途：", strings.Join(log, "；"))
		}
		json.NewEncoder(w).Encode(resp)
	})
	api("/api/quit", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("{}"))
		select {
		case quit <- struct{}{}:
		default:
		}
	})

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Println("出错：无法启动本地设置页面：", err)
		waitExit(1)
	}
	url := fmt.Sprintf("http://%s/?t=%s", ln.Addr().String(), token)
	go http.Serve(ln, mux)

	fmt.Println("改装包位置：", root)
	fmt.Println("设置页面已在浏览器里打开。如果没有自动打开，请把下面的地址复制到浏览器：")
	fmt.Println("  " + url)
	fmt.Println("\n设置完成、关掉网页后，本窗口会自动退出（也可以直接关掉）。")
	if err := openBrowser(url); err != nil {
		fmt.Println("（自动打开浏览器失败：", err, "）")
	}

	tick := time.NewTicker(5 * time.Second)
	for {
		select {
		case <-quit:
			time.Sleep(300 * time.Millisecond)
			fmt.Println("\n已退出。")
			os.Exit(0)
		case <-tick.C:
			// the page pings every 10 s; background tabs are throttled, so allow a long gap
			if time.Since(time.Unix(lastPing.Load(), 0)) > 10*time.Minute {
				fmt.Println("\n网页已关闭，退出。")
				os.Exit(0)
			}
		}
	}
}
