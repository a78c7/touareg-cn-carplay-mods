//go:build !windows && !darwin

package main

import (
	"os"
	"syscall"
)

func consoleUTF8() {}

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
