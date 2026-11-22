//go:build windows

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

var k32 = syscall.NewLazyDLL("kernel32.dll")

// Chinese text in the console window: switch it to UTF-8
func consoleUTF8() {
	k32.NewProc("SetConsoleOutputCP").Call(65001)
	k32.NewProc("SetConsoleCP").Call(65001)
}

func syncFS() {}

// scheduleEject 在当前进程退出后延迟触发 Windows 资源管理器安全弹出（避免自身 exe 占用 U 盘）
func scheduleEject(root string) (string, error) {
	v := volumeInfo(root)
	mnt := strings.TrimSpace(v.Mount)
	if !v.Removable || len(mnt) < 2 || mnt[1] != ':' {
		return "", fmt.Errorf("当前目录不是可移动 U 盘（%s），无法自动弹出", root)
	}
	drive := strings.ToUpper(mnt[:2])
	if drive[0] < 'A' || drive[0] > 'Z' || drive[0] == 'C' {
		return "", fmt.Errorf("拒绝弹出系统盘或异常盘符：%s", drive)
	}
	ps := fmt.Sprintf(
		"Start-Sleep -Milliseconds 1500; $s=New-Object -ComObject Shell.Application; $d=$s.NameSpace(17).ParseName('%s'); if($d){$d.InvokeVerb('Eject')}",
		drive,
	)
	cmd := exec.Command("powershell.exe", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", ps)
	cmd.Dir = os.TempDir()
	// CREATE_NO_WINDOW (0x08000000) | DETACHED_PROCESS (0x00000008)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000 | 0x00000008}
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("启动安全弹出失败：%v", err)
	}
	return drive, nil
}

func volumeInfo(root string) volInfo {
	var v volInfo
	rp, err := syscall.UTF16PtrFromString(root)
	if err != nil {
		return v
	}
	buf := make([]uint16, 512)
	if r, _, _ := k32.NewProc("GetVolumePathNameW").Call(uintptr(unsafe.Pointer(rp)), uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf))); r == 0 {
		return v
	}
	vol := syscall.UTF16ToString(buf) // e.g. E:\
	v.Mount = vol
	v.IsRoot = strings.EqualFold(filepath.Clean(root), filepath.Clean(vol))
	vp, _ := syscall.UTF16PtrFromString(vol)
	name := make([]uint16, 64)
	if r, _, _ := k32.NewProc("GetVolumeInformationW").Call(uintptr(unsafe.Pointer(vp)), 0, 0, 0, 0, 0, uintptr(unsafe.Pointer(&name[0])), uintptr(len(name))); r != 0 {
		v.FS = syscall.UTF16ToString(name)
	}
	v.FAT = strings.EqualFold(v.FS, "FAT32") || strings.EqualFold(v.FS, "FAT")
	dt, _, _ := k32.NewProc("GetDriveTypeW").Call(uintptr(unsafe.Pointer(vp)))
	v.Removable = dt == 2 // DRIVE_REMOVABLE
	var total uint64
	k32.NewProc("GetDiskFreeSpaceExW").Call(uintptr(unsafe.Pointer(vp)), 0, uintptr(unsafe.Pointer(&total)), 0)
	v.TotalGB = int(total >> 30)
	return v
}
