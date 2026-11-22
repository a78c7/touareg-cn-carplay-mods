//go:build darwin

package main

import (
	"fmt"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
)

func consoleUTF8() {}

func syncFS() {
	syscall.Sync()
}

// scheduleEject 在当前进程退出后延迟触发 macOS 安全弹出（避免自身二进制占用 U 盘导致卸载失败）
func scheduleEject(root string) (string, error) {
	v := volumeInfo(root)
	mnt := filepath.Clean(v.Mount)
	if !v.Removable || !strings.HasPrefix(mnt, "/Volumes/") || mnt == "/Volumes" {
		return "", fmt.Errorf("当前目录不是可移动 U 盘（%s），无法自动弹出", root)
	}
	syncFS()
	cmd := exec.Command(
		"/bin/sh", "-c",
		`sleep 1; /usr/sbin/diskutil eject "$1" >/dev/null 2>&1 || (sleep 2; /usr/sbin/diskutil eject "$1" >/dev/null 2>&1)`,
		"--", mnt,
	)
	cmd.Dir = "/"
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("启动安全弹出失败：%v", err)
	}
	return mnt, nil
}

func cstr(b []int8) string {
	n := 0
	for n < len(b) && b[n] != 0 {
		n++
	}
	out := make([]byte, n)
	for i := 0; i < n; i++ {
		out[i] = byte(b[i])
	}
	return string(out)
}

func volumeInfo(root string) volInfo {
	var st syscall.Statfs_t
	if syscall.Statfs(root, &st) != nil {
		return volInfo{}
	}
	fs, mnt := cstr(st.Fstypename[:]), cstr(st.Mntonname[:])
	v := volInfo{FS: fs, Mount: mnt, IsRoot: filepath.Clean(root) == filepath.Clean(mnt),
		TotalGB: int(st.Blocks * uint64(st.Bsize) >> 30), Removable: strings.HasPrefix(mnt, "/Volumes/")}
	if fs == "msdos" {
		v.FAT, v.FS = true, "FAT32"
	}
	return v
}
