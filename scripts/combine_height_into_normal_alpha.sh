#!/usr/bin/env bash
# Combines each *_h.png/dds height map into the alpha channel of the matching
# *_n.png/dds normal map, so OpenMW activates parallax occlusion mapping
# (`@parallax` define) automatically. The mesh references stay the same;
# only the *_n.dds gets a richer payload.
#
# Run on the directory produced by generate_pbr_textures.py. Outputs:
#   - <base>_n.dds  (RGB normal + alpha height, DXT5 with mipmaps)
#   - <base>_h.png/dds  preserved as-is for explicit consumers
#
# Requires: ImageMagick (magick / convert).

set -euo pipefail

DIR="${1:?usage: $0 <textures-dir>}"
[[ -d "$DIR" ]] || { echo "not a dir: $DIR" >&2; exit 1; }

if command -v magick >/dev/null 2>&1; then
    MAGICK=magick
elif command -v convert >/dev/null 2>&1; then
    MAGICK=convert
else
    echo "Install ImageMagick (brew install imagemagick)" >&2
    exit 1
fi

count=0
fail=0
shopt -s nullglob

process_pair() {
    local n_src="$1"
    local h_src="$2"
    local base="$3"
    local out_dds="${base}_n.dds"
    local tmp_png="${base}_n.tmp.png"

    if "$MAGICK" "$n_src" \( "$h_src" -channel R -separate +channel \) \
        -alpha off -compose CopyOpacity -composite "$tmp_png" 2>/dev/null; then
        if "$MAGICK" "$tmp_png" \
            -define dds:compression=dxt5 -define dds:mipmaps=true \
            "$out_dds" 2>/dev/null; then
            rm -f "$tmp_png"
            count=$((count + 1))
            return 0
        fi
    fi
    rm -f "$tmp_png"
    return 1
}

# Both PNG and DDS workflows: prefer PNG when available (lossless input).
for n_src in "$DIR"/*_n.png "$DIR"/*_n.dds; do
    [[ -f "$n_src" ]] || continue
    base="${n_src%_n.*}"
    # Skip if already processed via PNG branch.
    [[ "$n_src" == *.dds && -f "${base}_n.png" ]] && continue
    h_src=""
    for ext in png dds; do
        [[ -f "${base}_h.$ext" ]] && h_src="${base}_h.$ext" && break
    done
    [[ -n "$h_src" ]] || continue
    if process_pair "$n_src" "$h_src" "$base"; then :; else
        fail=$((fail + 1))
        echo "FAILED on $(basename "$base")" >&2
    fi
done

echo "combined height into normal alpha for $count textures ($fail failed)"
