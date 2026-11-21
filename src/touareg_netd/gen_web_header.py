#!/usr/bin/env python3
"""Generate embedded C byte array header web_ui_data.h from web_ui.html."""
import pathlib

src = pathlib.Path(__file__).parent / "web_ui.html"
dst = pathlib.Path(__file__).parent / "web_ui_data.h"
data = src.read_bytes()

lines = [
    "/* Generated from web_ui.html by gen_web_header.py; do not edit by hand. */",
    "#ifndef WEB_UI_DATA_H",
    "#define WEB_UI_DATA_H",
    "#include <stddef.h>",
    "static const unsigned char WEB_UI_HTML[] = {",
]
for i in range(0, len(data), 16):
    chunk = ", ".join(f"0x{b:02x}" for b in data[i:i+16])
    lines.append(f"  {chunk},")
lines.append("};")
lines.append(f"static const size_t WEB_UI_HTML_LEN = {len(data)};")
lines.append("#endif")
dst.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Generated {dst} ({len(data)} bytes)")
