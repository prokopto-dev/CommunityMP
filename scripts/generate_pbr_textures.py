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
    # Pinned version hash — Replicate refuses naked model paths for some
    # community models, you must include the version.
    MODEL = "tommoore515/pix2pix_tf_albedo2pbrmaps:21bd96b6e69f40e54502d67798f9025ab9e4a9e08f2a1b51dde5131b129a825e"

    def __init__(self, token: str):
        try:
            import replicate  # type: ignore
        except ImportError:
            sys.exit("pip install replicate")
        self.client = replicate.Client(api_token=token)

    def generate(self, source_image_bytes: bytes, prompt_hint: str) -> PBRResult:
        # imagepath accepts a data URI; encode the source PNG inline.
        data_uri = "data:image/png;base64," + base64.b64encode(source_image_bytes).decode()

        normal     = self._run_pix2pix("albedo2normal",     data_uri)
        height     = self._run_pix2pix("albedo2height",     data_uri)
        smoothness = self._run_pix2pix("albedo2smoothness", data_uri)

        return PBRResult(
            albedo=source_image_bytes,   # we keep the original albedo as-is
            normal=normal,
            specular=height,             # 'specular' slot reused for height map
            roughness=smoothness,
        )

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


def infer_prompt_hint(filename: str) -> str:
    """Derive a short keyword from the texture filename so the AI knows what
    kind of surface to synthesise (rock, dirt, wood, fabric, metal...)."""
    stem = Path(filename).stem.lower()
    table = {
        "weathered red brick wall": ("brick", "firebrick"),
        "rough stone wall":         ("imp_wall", "_wall_imp", "stonewall"),
        "rock":   ("rock", "_r_", "_rk_"),
        "dirt":   ("dirt", "earth", "_e_", "_d_"),
        "grass":  ("grass", "moss", "lawn"),
        "sand":   ("sand", "beach"),
        "wood":   ("wood", "plank", "log", "tree"),
        "fabric": ("cloth", "fab", "carpet", "rug"),
        "metal":  ("metal", "iron", "steel", "bronze", "weapon", "armor"),
        "stone":  ("stone", "wall", "marble", "tile"),
    }
    for hint, keys in table.items():
        if any(k in stem for k in keys):
            return hint
    return "natural surface"


def iter_inputs(root: Path, pattern: str) -> Iterable[Path]:
    return (p for p in root.rglob(pattern) if p.is_file())


def already_done(out_dir: Path, src: Path, root: Path) -> bool:
    rel = src.relative_to(root)
    base = (out_dir / rel).with_suffix("")
    return (base.parent / (base.name + "_n.png")).exists()


def save_pack(out_dir: Path, src: Path, root: Path, pack: PBRResult):
    rel = src.relative_to(root)
    base = (out_dir / rel).with_suffix("")
    base.parent.mkdir(parents=True, exist_ok=True)
    (base.parent / (base.name + ".png")).write_bytes(pack.albedo)
    (base.parent / (base.name + "_n.png")).write_bytes(pack.normal)
    # 'specular' slot currently holds the height map for parallax.
    if pack.specular:
        (base.parent / (base.name + "_h.png")).write_bytes(pack.specular)
    if pack.roughness:
        (base.parent / (base.name + "_s.png")).write_bytes(pack.roughness)


def make_provider(name: str) -> Provider:
    if name == "replicate":
        token = os.getenv("REPLICATE_API_TOKEN")
        if not token:
            sys.exit("set REPLICATE_API_TOKEN")
        return ReplicateProvider(token)
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
    p.add_argument("--provider", choices=("replicate", "stability"), default="replicate")
    p.add_argument("--filter",   default="*.dds", help="glob filter, e.g. '*.dds' or '**/*.png'")
    p.add_argument("--max",      type=int, default=0, help="limit count (0 = all)")
    p.add_argument("--dry-run",  action="store_true", help="list inputs, don't call API")
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

    provider = make_provider(args.provider)
    print(f"Provider: {provider.name}, inputs: {len(inputs)}")

    for i, src in enumerate(inputs, 1):
        if already_done(args.output, src, args.input):
            continue
        hint = infer_prompt_hint(src.name)
        try:
            data = src.read_bytes()
            t0 = time.time()
            pack = provider.generate(data, hint)
            save_pack(args.output, src, args.input, pack)
            print(f"[{i}/{len(inputs)}] {src.name}  hint={hint}  {time.time()-t0:.1f}s")
        except Exception as e:
            print(f"[{i}/{len(inputs)}] {src.name}  FAILED: {e}", file=sys.stderr)
            time.sleep(2)


if __name__ == "__main__":
    main()
