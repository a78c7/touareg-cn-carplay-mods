//go:build !windows && !darwin

package main

import (
	"fmt"
	"os"
	"syscall"
)

func consoleUTF8() {}

func syncFS() {
	syscall.Sync()
}

func scheduleEject(root string) (string, error) {
	syncFS()
	return "", fmt.Errorf("当前系统不支持自动弹出 U 盘，请手动安全弹出（%s）", root)
}

// Linux: only used for testing the program (TOUAREG_TEST_ROOT=1 pretends the directory is a drive root)
func volumeInfo(root string) volInfo {
	var st syscall.Statfs_t
	v := volInfo{Mount: root, IsRoot: os.Getenv("TOUAREG_TEST_ROOT") == "1"}
	if syscall.Statfs(root, &st) == nil {
		v.TotalGB = int(st.Blocks * uint64(st.Bsize) >> 30)
		if st.Type == 0x4d44 {
			v.FS, v.FAT = "FAT32", true
		} else {
			v.FS = "其它"
		}
	}
	if os.Getenv("TOUAREG_TEST_FAT") == "1" {
		v.FS, v.FAT = "FAT32", true
	}
	return v
}
