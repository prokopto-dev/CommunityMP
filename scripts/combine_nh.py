#!/usr/bin/env python3
"""Combine _n.png + _h.png pairs into _nh.png/tga/dds in the pbr-overlay
directory. RGB from the normal, alpha from the heightmap (resized via
LANCZOS to match if needed). OpenMW autoloads *_nh.dds files for the
parallax shader path."""

import sys
from pathlib import Path
from PIL import Image
import subprocess
import shutil

if len(sys.argv) < 2:
    sys.exit("usage: combine_nh.py <pbr-overlay-textures-dir>")

root = Path(sys.argv[1])
magick = shutil.which("magick") or shutil.which("convert")
count_done = count_skipped = count_failed = 0

for normal_png in root.rglob("*_n.png"):
    base = normal_png.with_name(normal_png.name[:-len("_n.png")])
    height_png = base.parent / (base.name + "_h.png")
    if not height_png.exists():
        count_skipped += 1
        continue
    nh_png = base.parent / (base.name + "_nh.png")
    if nh_png.exists() and nh_png.stat().st_mtime >= max(
            normal_png.stat().st_mtime, height_png.stat().st_mtime):
        continue
    try:
        n = Image.open(normal_png).convert("RGB")
        h = Image.open(height_png).convert("L")
        if h.size != n.size:
            h = h.resize(n.size, Image.Resampling.LANCZOS)
        nh = Image.merge("RGBA", (*n.split(), h))
        nh.save(nh_png, format="PNG")
        # mirror to .tga and .dds
        nh_tga = base.parent / (base.name + "_nh.tga")
        nh.save(nh_tga, format="TGA")
        if magick:
            nh_dds = base.parent / (base.name + "_nh.dds")
            subprocess.run(
                [magick, str(nh_png), "-define", "dds:compression=dxt5",
                 "-define", "dds:mipmaps=true", str(nh_dds)],
                check=False, capture_output=True)
        count_done += 1
    except Exception as e:
        print(f"FAILED {normal_png.name}: {e}", file=sys.stderr)
        count_failed += 1

print(f"Combined {count_done}, skipped {count_skipped} (no _h.png), failed {count_failed}")
