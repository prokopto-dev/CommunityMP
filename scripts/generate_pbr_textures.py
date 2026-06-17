#!/usr/bin/env python3
"""
generate_pbr_textures.py — bulk-generate PBR texture sets (albedo, normal,
roughness, height) for Morrowind diffuse textures using a hosted AI service.

This is an *offline* tool. It:
  1. Walks an input directory of diffuse textures (DDS/PNG/TGA).
  2. For each input, calls a Stable Diffusion-based service (Replicate.com
     by default) to generate a PBR set conditioned on the source image.
  3. Saves the result next to the original with the suffixes Morrowind /
     OpenMW expect (_n.dds for normal+height, _s.dds for specular).

The script is conservative: it skips files that already have a generated
PBR pack, supports --dry-run, and uses incremental progress so a long run
can be interrupted and resumed.

Configuration:

    export REPLICATE_API_TOKEN=r8_xxx           # required
    # OR
    export STABILITY_API_KEY=sk-xxx             # alternative

Usage:

    python3 generate_pbr_textures.py \
        --input  ~/.../Data\\ Files/Textures \
        --output ~/openmw-pbr-out \
        --provider replicate \
        --filter '*.dds' \
        --max 200

Limit the run with --max for an initial sample. Inspect a few results
before unleashing on the whole 5000-texture catalogue.

This script is intentionally a starting point. The model choice, prompts,
and post-processing (normal map decoding, height-map invert, specular
threshold) will need tuning to your art direction.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


# --------------------------------------------------------------------------
# Provider abstraction
# --------------------------------------------------------------------------


@dataclass
class PBRResult:
    albedo: bytes
    normal: bytes        # combined normal+height: RGB = normal, A = height
    specular: Optional[bytes] = None
    roughness: Optional[bytes] = None


class Provider:
    name: str

    def generate(self, source_image_bytes: bytes, prompt_hint: str) -> PBRResult:
        raise NotImplementedError


class ReplicateProvider(Provider):
    """
    Pipeline using tommoore515/pix2pix_tf_albedo2pbrmaps — a TensorFlow
    pix2pix trained on PBR materials. ~$0.001 per sub-model invocation,
    ~2s per call. We chain 3 sub-models (normal, height, smoothness) to
    derive the full PBR pack from the source albedo. Output preserves the
    input texture's structure (vs SDXL which "reimagines" the surface).
    """

    name = "replicate"
    # Pinned version hashes — Replicate refuses naked model paths for some
    # community models, you must include the version.
    MODEL = "tommoore515/pix2pix_tf_albedo2pbrmaps:21bd96b6e69f40e54502d67798f9025ab9e4a9e08f2a1b51dde5131b129a825e"
    UPSCALER = "nightmareai/real-esrgan:b3ef194191d13140337468c916c2c5b96dd0cb06dffc032a022a31807f6a5ea8"

    def __init__(self, token: str, upscale: int = 1):
        try:
            import replicate  # type: ignore
        except ImportError:
            sys.exit("pip install replicate")
        self.client = replicate.Client(api_token=token)
        self.upscale = max(1, upscale)

    def generate(self, source_image_bytes: bytes, prompt_hint: str) -> PBRResult:
        # Optional upscale via real-esrgan before deriving PBR maps. The
        # higher-res albedo gives the pix2pix model finer per-pixel detail
        # to work with, so the resulting normal/height maps are sharper.
        albedo_bytes = source_image_bytes
        source_alpha_bytes = _extract_alpha_if_meaningful(source_image_bytes)

        if self.upscale > 1:
            albedo_bytes = self._run_upscale(albedo_bytes, scale=self.upscale)

        # Match saturation of the upscaled albedo to the original — the
        # upscaler tends to wash colours out.
        albedo_bytes = _match_saturation(albedo_bytes, source_image_bytes)

        # Re-composite source alpha onto the upscaled albedo (resized NN
        # to preserve hard alpha-test edges on foliage / ironwork).
        if source_alpha_bytes is not None:
            albedo_bytes = _composite_alpha(albedo_bytes, source_alpha_bytes)

        data_uri = "data:image/png;base64," + base64.b64encode(albedo_bytes).decode()

        normal     = self._run_pix2pix("albedo2normal",     data_uri)
        height     = self._run_pix2pix("albedo2height",     data_uri)
        smoothness = self._run_pix2pix("albedo2smoothness", data_uri)

        # Sharpen the normal map's tangent-space XY then renormalise Z so
        # the result stays a unit vector — the unsharp_mask alone makes
        # the normal lighting "boil" if Z isn't fixed.
        normal = _sharpen_and_renormalize_normal(normal)

        return PBRResult(
            albedo=albedo_bytes,
            normal=normal,
            specular=height,             # 'specular' slot reused for height map
            roughness=smoothness,
        )

    def _run_upscale(self, image_bytes: bytes, scale: int = 4, max_retries: int = 3) -> bytes:
        import urllib.request, time
        data_uri = "data:image/png;base64," + base64.b64encode(image_bytes).decode()
        last_err = None
        for attempt in range(max_retries):
            try:
                output = self.client.run(
                    self.UPSCALER,
                    input={"image": data_uri, "scale": scale, "face_enhance": False},
                )
                url = str(output) if not isinstance(output, list) else str(output[0])
                with urllib.request.urlopen(url) as resp:
                    return resp.read()
            except Exception as e:
                last_err = e
                msg = str(e)
                if "throttled" in msg.lower() or "rate limit" in msg.lower():
                    time.sleep(11)
                    continue
                if attempt + 1 == max_retries:
                    raise
                time.sleep(2)
        raise RuntimeError(f"upscale failed: {last_err}")

    def _run_pix2pix(self, model_name: str, image_uri: str, max_retries: int = 3) -> bytes:
        import urllib.request, time
        last_err = None
        for attempt in range(max_retries):
            try:
                output = self.client.run(
                    self.MODEL,
                    input={"model": model_name, "imagepath": image_uri},
                )
                url = str(output) if not isinstance(output, list) else str(output[0])
                with urllib.request.urlopen(url) as resp:
                    return resp.read()
            except Exception as e:
                last_err = e
                msg = str(e)
                if "throttled" in msg.lower() or "rate limit" in msg.lower():
                    time.sleep(11)
                    continue
                if attempt + 1 == max_retries:
                    raise
                time.sleep(2)
        raise RuntimeError(f"pix2pix failed: {last_err}")

    # Legacy helper kept for reference; not used by the pix2pix path.
    def _run_pbr(self, inputs: dict, max_retries: int = 3) -> PBRResult:
        # The PBR model returns a list of 7 file URLs (or FileOutput objects):
        # [color, height, normal, roughness, ao, emissive, grid].
        import random, urllib.request, time
        last_err = None
        for attempt in range(max_retries):
            try:
                ins = dict(inputs)
                ins["seed"] = random.randint(1, 2**31 - 1)
                outputs = self.client.run(self.MODEL_VERSION, input=ins)
                if not isinstance(outputs, list) or len(outputs) < 4:
                    raise RuntimeError(f"unexpected output shape: {type(outputs)}")
                color_url    = str(outputs[0])
                height_url   = str(outputs[1])
                normal_url   = str(outputs[2])
                roughness_url = str(outputs[3])
                # ao_url     = str(outputs[4])  # we don't ship AO yet
                # emissive   = str(outputs[5])  # nor emissive
                def fetch(u: str) -> bytes:
                    with urllib.request.urlopen(u) as resp:
                        return resp.read()
                return PBRResult(
                    albedo=fetch(color_url),
                    normal=fetch(normal_url),
                    roughness=fetch(roughness_url),
                    specular=fetch(height_url),  # repurpose 'specular' slot for height (saved as _s for now)
                )
            except Exception as e:
                last_err = e
                msg = str(e)
                if "throttled" in msg.lower() or "rate limit" in msg.lower():
                    time.sleep(11)
                    continue
                if attempt + 1 == max_retries:
                    raise
                time.sleep(2)
        raise RuntimeError(f"PBR generation failed: {last_err}")


class MaterialMakerProvider(Provider):
    """
    midllle/material-maker on Replicate. Returns three real maps
    (Normal, Roughness, Displacement) per call, in contrast to the
    pix2pix path which produced near-flat normals and an alpha=1.0
    "smoothness" that pinned shininess to 255. The model is built for
    tiling textures, so we pass `seamless=True`. Cold start ~80s but
    warm calls are 1-6s; full 65-texture batch ≈ 5 min.
    """

    name = "material-maker"
    MODEL = ("midllle/material-maker:"
             "92fb3df0bf2a5f3bb60af26366677e0a98866ea5b8b5aa4a229f98322118c74e")

    def __init__(self, token: str, tile_size: int = 512, seamless: bool = True):
        try:
            import replicate  # type: ignore
        except ImportError:
            sys.exit("pip install replicate")
        self.client = replicate.Client(api_token=token)
        self.tile_size = tile_size
        self.seamless = seamless

    def generate(self, source_image_bytes: bytes, prompt_hint: str) -> PBRResult:
        import urllib.request
        # Material-maker doesn't reimagine the albedo, so we pass the
        # source image straight through as the diffuse output.
        albedo_bytes = source_image_bytes
        data_uri = "data:image/png;base64," + base64.b64encode(albedo_bytes).decode()

        last_err = None
        for attempt in range(3):
            try:
                outputs = self.client.run(self.MODEL, input={
                    "input_image": data_uri,
                    "tile_size": self.tile_size,
                    "seamless": self.seamless,
                    "mirror": False,
                    "replicate": False,
                    "ishiiruka": False,
                    "ishiiruka_texture_encoder": False,
                })
                break
            except Exception as e:
                last_err = e
                msg = str(e).lower()
                if "throttled" in msg or "rate limit" in msg:
                    time.sleep(11)
                    continue
                if attempt + 1 == 3:
                    raise
                time.sleep(2)
        else:
            raise RuntimeError(f"material-maker failed: {last_err}")

        # Outputs are filename-tagged: ..._Normal.png / ..._Roughness.png / ..._Displacement.png
        def fetch(item) -> bytes:
            url = str(item.url) if hasattr(item, "url") else str(item)
            with urllib.request.urlopen(url) as r:
                return r.read()

        normal_b = roughness_b = displacement_b = None
        for o in outputs:
            url = str(o.url) if hasattr(o, "url") else str(o)
            if "Normal" in url:
                normal_b = fetch(o)
            elif "Rough" in url:
                roughness_b = fetch(o)
            elif "Displacement" in url or "Height" in url:
                displacement_b = fetch(o)

        if normal_b is None:
            raise RuntimeError(f"no Normal in output URLs: {outputs}")

        return PBRResult(
            albedo=albedo_bytes,
            normal=normal_b,
            specular=displacement_b,  # 'specular' slot historically holds height
            roughness=roughness_b,
        )


class StabilityProvider(Provider):
    """Stability AI direct REST API (image-to-image)."""

    name = "stability"
    BASE = "https://api.stability.ai/v2beta/stable-image/generate/sd3"

    def __init__(self, key: str):
        self.key = key

    def generate(self, source_image_bytes: bytes, prompt_hint: str) -> PBRResult:
        import requests  # type: ignore
        out = {}
        for slot, prompt in (
            ("albedo",  f"tileable PBR albedo {prompt_hint}, no shadows, no highlights"),
            ("normal",  f"tileable PBR normal map of {prompt_hint}, OpenGL convention"),
            ("spec",    f"tileable PBR roughness map of {prompt_hint}, grayscale"),
        ):
            resp = requests.post(
                self.BASE,
                headers={"Authorization": f"Bearer {self.key}", "Accept": "image/*"},
                files={"image": ("src.png", source_image_bytes, "image/png")},
                data={"prompt": prompt, "mode": "image-to-image", "strength": 0.6,
                      "model": "sd3-medium", "output_format": "png"},
                timeout=120,
            )
            resp.raise_for_status()
            out[slot] = resp.content
        return PBRResult(albedo=out["albedo"], normal=out["normal"], specular=out["spec"])


# --------------------------------------------------------------------------
# Main pipeline
# --------------------------------------------------------------------------


# --------------------------------------------------------------------------
# Image post-processing helpers (Pillow)
# --------------------------------------------------------------------------


def _extract_alpha_if_meaningful(png_bytes: bytes) -> Optional[bytes]:
    """Return the alpha channel as a single-band PNG if the source has any
    non-trivial transparency, else None."""
    try:
        from PIL import Image
        import io
    except ImportError:
        return None
    try:
        img = Image.open(io.BytesIO(png_bytes))
        if img.mode not in ("RGBA", "LA", "PA"):
            return None
        a = img.convert("RGBA").split()[-1]
        # Quick check: if every pixel is >= 254 there's no real transparency.
        bbox = a.point(lambda p: 255 if p < 254 else 0).getbbox()
        if bbox is None:
            return None
        out = io.BytesIO()
        a.save(out, format="PNG")
        return out.getvalue()
    except Exception:
        return None


def _composite_alpha(albedo_png_bytes: bytes, alpha_png_bytes: bytes) -> bytes:
    """Take the alpha channel from `alpha_png_bytes`, resize to match the
    albedo, paste as alpha. Nearest-neighbour preserves hard edges."""
    try:
        from PIL import Image
        import io
    except ImportError:
        return albedo_png_bytes
    try:
        rgb = Image.open(io.BytesIO(albedo_png_bytes)).convert("RGB")
        a   = Image.open(io.BytesIO(alpha_png_bytes)).convert("L")
        if a.size != rgb.size:
            a = a.resize(rgb.size, Image.Resampling.NEAREST)
        rgba = Image.merge("RGBA", (*rgb.split(), a))
        out = io.BytesIO()
        rgba.save(out, format="PNG")
        return out.getvalue()
    except Exception:
        return albedo_png_bytes


def _match_saturation(target_png_bytes: bytes, reference_png_bytes: bytes) -> bytes:
    """Match mean HSV saturation of target to reference. Real-ESRGAN tends
    to desaturate; this restores Morrowind's painted-art saturation."""
    try:
        from PIL import Image, ImageEnhance
        import io
    except ImportError:
        return target_png_bytes
    try:
        ref = Image.open(io.BytesIO(reference_png_bytes)).convert("HSV")
        tgt = Image.open(io.BytesIO(target_png_bytes)).convert("RGB")

        ref_s = list(ref.split()[1].getdata())
        ref_mean = sum(ref_s) / max(1, len(ref_s)) / 255.0

        tgt_hsv = tgt.convert("HSV")
        tgt_s = list(tgt_hsv.split()[1].getdata())
        tgt_mean = sum(tgt_s) / max(1, len(tgt_s)) / 255.0
        if tgt_mean < 0.01:
            return target_png_bytes

        ratio = max(0.5, min(2.0, ref_mean / tgt_mean))
        enhanced = ImageEnhance.Color(tgt).enhance(ratio)
        out = io.BytesIO()
        enhanced.save(out, format="PNG")
        return out.getvalue()
    except Exception:
        return target_png_bytes


def _sharpen_and_renormalize_normal(normal_png_bytes: bytes) -> bytes:
    """Apply unsharp mask to XY then renormalize Z so the normal stays a
    unit vector. Recovers contrast lost in pix2pix's compression."""
    try:
        from PIL import Image, ImageFilter
        import io, math
    except ImportError:
        return normal_png_bytes
    try:
        n = Image.open(io.BytesIO(normal_png_bytes)).convert("RGB")
        # Sharpen the whole RGB then rebuild Z from sharpened XY so the
        # vector length stays 1.
        sharpened = n.filter(ImageFilter.UnsharpMask(radius=1.5, percent=70, threshold=2))
        r, g, b = sharpened.split()
        r_data = list(r.getdata())
        g_data = list(g.getdata())
        out_b = []
        for i in range(len(r_data)):
            x = (r_data[i] / 255.0) * 2.0 - 1.0
            y = (g_data[i] / 255.0) * 2.0 - 1.0
            zsq = max(0.0, 1.0 - x*x - y*y)
            z = math.sqrt(zsq)
            out_b.append(int((z * 0.5 + 0.5) * 255.0))
        b_new = Image.new("L", b.size)
        b_new.putdata(out_b)
        result = Image.merge("RGB", (r, g, b_new))
        out = io.BytesIO()
        result.save(out, format="PNG")
        return out.getvalue()
    except Exception:
        return normal_png_bytes


def infer_prompt_hint(filename: str) -> str:
    """Derive a short keyword from the texture filename so the AI knows what
    kind of surface to synthesise (rock, dirt, wood, fabric, metal...)."""
    stem = Path(filename).stem.lower()
    # Order matters: longest / most specific keys first so we don't fall
    # through to a generic match.
    table = (
        ("weathered red brick wall", ("brick", "firebrick")),
        ("rough stone wall",         ("imp_wall", "_wall_imp", "stonewall")),
        ("dwemer brushed brass",     ("dwrv", "dwemer", "dwe_")),
        ("daedric obsidian metal",   ("daed", "daedric")),
        ("ebony polished black",     ("ebony",)),
        ("glass-like crystalline",   ("glass",)),
        ("volcanic ash",             ("ash", "ashland")),
        ("lava molten rock glowing", ("lava", "magma")),
        ("snow ice crystal",         ("ice", "snow_ice", "frost")),
        ("silt sand muddy",          ("silt", "mudflats")),
        ("bone weathered",           ("bone", "skull", "skel")),
        ("gold coin metallic",       ("coin", "gold_", "_gold")),
        ("rock", ("rock", "_r_", "_rk_")),
        ("dirt", ("dirt", "earth", "_e_", "_d_")),
        ("grass", ("grass", "moss", "lawn")),
        ("sand", ("sand", "beach")),
        ("wood", ("wood", "plank", "log")),
        ("fabric", ("cloth", "fab", "carpet", "rug", "tapestry")),
        ("metal", ("metal", "iron", "steel", "bronze", "weapon", "armor")),
        ("stone", ("stone", "wall", "marble", "tile")),
    )
    for hint, keys in table:
        if any(k in stem for k in keys):
            return hint
    return "natural surface"


def iter_inputs(root: Path, pattern: str) -> Iterable[Path]:
    return (p for p in root.rglob(pattern) if p.is_file())


def already_done(out_dir: Path, src: Path, root: Path) -> bool:
    rel = src.relative_to(root)
    base = (out_dir / rel).with_suffix("")
    return (base.parent / (base.name + "_n.png")).exists()


_FLORA_SUBSTRINGS = (
    "flora", "plant", "mushroom", "fung", "shroom",
    "tree", "bush", "fern", "vine", "weed", "grass",
    "flower", "kelp", "seaweed", "lichen", "moss",
    "leaf", "leaves", "frond", "blossom",
    # Morrowind-specific flora
    "kresh", "kollop", "wickwheat", "corkbulb", "marshmerrow",
    "gold_kanet", "roobrush", "saltrice", "comberry", "chokeweed",
    "hackle-lo", "stoneflower", "trama", "hist", "russula",
    "bittergreen", "willow_flower", "violet_coprinus", "luminous_russula",
    "bungler", "emperor_parasol", "muck",
)


def is_flora(src: Path) -> bool:
    """Heuristic: does this texture depict flora (plants / mushrooms / grass)?
    These typically have alpha-tested masks; they don't roundtrip cleanly
    through the pix2pix PBR model and aren't where parallax / specular shows
    its value anyway."""
    stem = src.stem.lower()
    return any(s in stem for s in _FLORA_SUBSTRINGS)


def has_meaningful_alpha(src: Path) -> bool:
    """Return True if the source texture carries a real transparency mask.
    Uses ImageMagick `identify` so DDS/TGA/PNG all work. A texture is treated
    as transparent if `%[opaque]` reports false AND the alpha channel is not
    flat (Pillow check on a PNG round-trip — guards against DDS files that
    advertise an alpha channel filled with 255)."""
    import shutil, subprocess
    bin_ = shutil.which("magick") or shutil.which("convert")
    if bin_ is None:
        return False  # can't tell — be permissive
    try:
        # First a cheap probe: opaque flag.
        ident = shutil.which("magick")
        if ident is not None:
            probe = subprocess.run([ident, "identify", "-format", "%[opaque]", str(src)],
                                   capture_output=True, text=True, timeout=10)
        else:
            probe = subprocess.run([bin_, "-format", "%[opaque]", "identify", str(src)],
                                   capture_output=True, text=True, timeout=10)
        if probe.returncode == 0 and probe.stdout.strip().lower() == "true":
            return False
        # Confirm by sniffing the alpha histogram on a PNG round-trip — DDS
        # often reports non-opaque even when alpha is uniformly 255.
        png = subprocess.run([bin_, str(src), "png:-"],
                             capture_output=True, timeout=15)
        if png.returncode != 0 or not png.stdout:
            return False
        try:
            from PIL import Image
            import io
            img = Image.open(io.BytesIO(png.stdout))
            if img.mode not in ("RGBA", "LA", "PA"):
                return False
            a = img.convert("RGBA").split()[-1]
            lo, hi = a.getextrema()
            return lo < 250  # any non-trivial transparency
        except Exception:
            return True  # decoded as transparent earlier — believe it
    except Exception:
        return False


def _convert_to_dds(out_dir: Path, src: Path, root: Path):
    """Convert the four PNGs of a pack to DDS (DXT1 for albedo, DXT5 for
    the rest) so OpenMW picks them up via VFS for meshes pointing at the
    original .dds filename."""
    import shutil, subprocess
    rel = src.relative_to(root)
    base = (out_dir / rel).with_suffix("")
    if shutil.which("magick") is None and shutil.which("convert") is None:
        return
    bin_ = shutil.which("magick") or shutil.which("convert")
    pairs = [
        (base.parent / (base.name + ".png"),     base.parent / (base.name + ".dds"),     "dxt1"),
        (base.parent / (base.name + "_n.png"),   base.parent / (base.name + "_n.dds"),   "dxt5"),
        (base.parent / (base.name + "_h.png"),   base.parent / (base.name + "_h.dds"),   "dxt5"),
        (base.parent / (base.name + "_s.png"),   base.parent / (base.name + "_s.dds"),   "dxt5"),
        (base.parent / (base.name + "_spec.png"),base.parent / (base.name + "_spec.dds"),"dxt5"),
    ]
    for png, dds, fmt in pairs:
        if not png.exists():
            continue
        cmd = [bin_, str(png), "-define", f"dds:compression={fmt}",
               "-define", "dds:mipmaps=true", str(dds)]
        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError as e:
            print(f"  DDS conversion failed for {png.name}: {e.stderr.decode()[:120]}",
                  file=sys.stderr)


def save_pack(out_dir: Path, src: Path, root: Path, pack: PBRResult):
    rel = src.relative_to(root)
    base = (out_dir / rel).with_suffix("")
    base.parent.mkdir(parents=True, exist_ok=True)
    (base.parent / (base.name + ".png")).write_bytes(pack.albedo)
    (base.parent / (base.name + "_n.png")).write_bytes(pack.normal)
    # 'specular' slot currently holds the height map for parallax.
    if pack.specular:
        (base.parent / (base.name + "_h.png")).write_bytes(pack.specular)
    # OpenMW's default specular map pattern is `_spec` (cf
    # files/settings-default.cfg : `specular map pattern = _spec`).
    # We also keep `_s` for legacy / alternate-pattern users.
    if pack.roughness:
        (base.parent / (base.name + "_s.png")).write_bytes(pack.roughness)
        (base.parent / (base.name + "_spec.png")).write_bytes(pack.roughness)


def make_provider(name: str, upscale: int = 1) -> Provider:
    if name == "replicate":
        token = os.getenv("REPLICATE_API_TOKEN")
        if not token:
            sys.exit("set REPLICATE_API_TOKEN")
        return ReplicateProvider(token, upscale=upscale)
    if name == "material-maker":
        token = os.getenv("REPLICATE_API_TOKEN")
        if not token:
            sys.exit("set REPLICATE_API_TOKEN")
        return MaterialMakerProvider(token)
    if name == "stability":
        key = os.getenv("STABILITY_API_KEY")
        if not key:
            sys.exit("set STABILITY_API_KEY")
        return StabilityProvider(key)
    sys.exit(f"unknown provider: {name}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input",  type=Path, required=True, help="root dir of diffuse textures")
    p.add_argument("--output", type=Path, required=True, help="output dir for PBR packs")
    p.add_argument("--provider", choices=("replicate", "material-maker", "stability"),
                   default="replicate")
    p.add_argument("--filter",   default="*.dds", help="glob filter, e.g. '*.dds' or '**/*.png'")
    p.add_argument("--max",      type=int, default=0, help="limit count (0 = all)")
    p.add_argument("--dry-run",  action="store_true", help="list inputs, don't call API")
    p.add_argument("--upscale",  type=int, default=1, help="upscale factor before PBR (1=off, 4=Real-ESRGAN 4x)")
    p.add_argument("--dds",      action="store_true", help="convert outputs to DXT-compressed DDS via ImageMagick")
    p.add_argument("--skip-transparent", action="store_true",
                   help="skip textures whose alpha channel encodes a mask (foliage, ironwork, glass)")
    p.add_argument("--skip-flora", action="store_true",
                   help="skip plant / mushroom / tree / grass textures by filename")
    args = p.parse_args()

    if not args.input.is_dir():
        sys.exit(f"input not a directory: {args.input}")
    args.output.mkdir(parents=True, exist_ok=True)

    inputs = list(iter_inputs(args.input, args.filter))
    if args.max > 0:
        inputs = inputs[: args.max]

    if args.dry_run:
        for src in inputs:
            print(src.relative_to(args.input), "->", infer_prompt_hint(src.name))
        return

    provider = make_provider(args.provider, upscale=args.upscale)
    print(f"Provider: {provider.name}, upscale: {args.upscale}x, inputs: {len(inputs)}")

    skipped_alpha = 0
    skipped_flora = 0
    for i, src in enumerate(inputs, 1):
        if already_done(args.output, src, args.input):
            continue
        if args.skip_flora and is_flora(src):
            skipped_flora += 1
            continue
        if args.skip_transparent and has_meaningful_alpha(src):
            skipped_alpha += 1
            continue
        hint = infer_prompt_hint(src.name)
        try:
            data = src.read_bytes()
            # Replicate models expect PNG bytes; transcode TGA / DDS /
            # JPEG via Pillow first so the data URI's mime claim
            # matches the actual payload.
            if src.suffix.lower() in (".tga", ".dds", ".jpg", ".jpeg", ".bmp"):
                try:
                    from PIL import Image
                    import io
                    img = Image.open(io.BytesIO(data))
                    if img.mode not in ("RGB", "RGBA"):
                        img = img.convert("RGBA")
                    buf = io.BytesIO()
                    img.save(buf, format="PNG")
                    data = buf.getvalue()
                except Exception as e:
                    raise RuntimeError(f"PNG transcode failed: {e}")
            t0 = time.time()
            pack = provider.generate(data, hint)
            save_pack(args.output, src, args.input, pack)
            if args.dds:
                _convert_to_dds(args.output, src, args.input)
            print(f"[{i}/{len(inputs)}] {src.name}  hint={hint}  {time.time()-t0:.1f}s")
        except Exception as e:
            print(f"[{i}/{len(inputs)}] {src.name}  FAILED: {e}", file=sys.stderr)
            time.sleep(2)

    if args.skip_transparent and skipped_alpha:
        print(f"Skipped {skipped_alpha} texture(s) with mask/transparency")
    if args.skip_flora and skipped_flora:
        print(f"Skipped {skipped_flora} flora texture(s)")


if __name__ == "__main__":
    main()
