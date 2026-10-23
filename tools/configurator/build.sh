#!/bin/sh
# Build 「改装设置」 for Windows (x64) and macOS (universal: Intel + Apple Silicon) into the USB package root.
# Needs Go 1.22+ and llvm-lipo (LLVM 18). usage: sh build.sh v17b
set -e
cd "$(dirname "$0")"
V=${1:?usage: build.sh <version>}
# the info-box catalogue here must match g_items[] in dash.c (keys and labels, same order)
extract_c()  { sed -n '/g_items\[IT_N\] = {/,/^};/p' ../../src/cl_render/dash.c | sed -n 's/^ *{ "\([a-z]*\)", *"\([^"]*\)".*/\1 \2/p'; }
extract_go() { sed -n '/^var catalogue = /,/^}/p' package.go | sed -n 's/^\t{"\([a-z]*\)", "\([^"]*\)".*/\1 \2/p'; }
extract_c | sort > /tmp/cat_c.txt; extract_go | sort > /tmp/cat_go.txt
if ! diff /tmp/cat_c.txt /tmp/cat_go.txt; then echo "catalogue differs from dash.c"; exit 1; fi
echo "catalogue: $(wc -l < /tmp/cat_go.txt) items, same as dash.c"
mkdir -p out
LD="-s -w -buildid= -X main.version=$V"
CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -trimpath -ldflags "$LD" -o out/win_amd64.exe .
CGO_ENABLED=0 GOOS=darwin GOARCH=amd64 go build -trimpath -ldflags "$LD" -o out/mac_amd64 .
CGO_ENABLED=0 GOOS=darwin GOARCH=arm64 go build -trimpath -ldflags "$LD" -o out/mac_arm64 .
llvm-lipo-18 -create -output out/mac_universal out/mac_amd64 out/mac_arm64
cp out/win_amd64.exe "../../USB/改装设置（Windows）.exe"
cp out/mac_universal "../../USB/改装设置（Mac）.command"
chmod 755 "../../USB/改装设置（Mac）.command"
ls -l ../../USB/改装设置*
