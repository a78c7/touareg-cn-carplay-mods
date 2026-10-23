//go:build windows

package main

import (
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
