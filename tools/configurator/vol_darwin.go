//go:build darwin

package main

import (
	"path/filepath"
	"strings"
	"syscall"
)

func consoleUTF8() {}

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
