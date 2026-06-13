#!/usr/bin/env python3
# Regenerate AboutImageData.h from Source/about.png (run after replacing about.png).
import os
here = os.path.dirname(os.path.abspath(__file__))
png  = os.path.join(here, "about.png")
out  = os.path.join(here, "AboutImageData.h")
data = open(png, "rb").read()
lines = ["#pragma once",
         "// ============================================================================",
         "//  About-page portrait — embedded PNG bytes (auto-generated from Source/about.png).",
         f"//  {len(data)} bytes.",
         "// ============================================================================",
         "namespace AboutImage",
         "{",
         "    static const unsigned char data[] = {"]
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    lines.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
lines += ["    };",
          f"    static const int dataSize = {len(data)};",
          "}",
          ""]
open(out, "w").write("\n".join(lines))
print(f"Wrote {out} ({len(data)} bytes).")
