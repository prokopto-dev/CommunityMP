#!/usr/bin/env python3
"""Export ESM/ESP quest facts and import them into CommunityMP questdb packages."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import communitymp_questdb_import as questdb


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def stable_package_dir_name(content_file: Path) -> str:
    raw = str(content_file.resolve())
    slug = re.sub(r"[^a-z0-9_.-]+", "_", content_file.stem.lower()).strip("_") or "content"
    digest = hashlib.sha1(raw.encode("utf-8")).hexdigest()[:10]
    return f"{slug}_{digest}"


def default_esmtool() -> Path | None:
    root = repo_root()
    candidates = [
        root / "MSVC2022_64_Ninja" / "RelWithDebInfo" / "esmtool.exe",
        root / "MSVC2022_64_Ninja" / "Release" / "esmtool.exe",
        root / "build" / "RelWithDebInfo" / "esmtool.exe",
        root / "build" / "Release" / "esmtool.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    path_candidate = shutil.which("esmtool") or shutil.which("esmtool.exe")
    return Path(path_candidate) if path_candidate else None


def ensure_output_dir(output_dir: Path, force: bool) -> None:
    if not output_dir.exists():
        output_dir.mkdir(parents=True)
        return

    if not force:
        raise FileExistsError(f"{output_dir} already exists; pass --force to replace it")

    resolved_output = output_dir.resolve()
    resolved_parent = output_dir.parent.resolve()
    if resolved_output == resolved_parent or resolved_parent not in resolved_output.parents:
        raise ValueError(f"refusing to replace unsafe output directory {output_dir}")

    shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)


def export_source(esmtool: Path, content_file: Path, source_jsonl: Path) -> None:
    subprocess.run(
        [str(esmtool), "quest-export", str(content_file), str(source_jsonl)],
        check=True,
    )


def import_source(source_jsonl: Path, output_dir: Path) -> dict[str, int]:
    rows = list(questdb.read_jsonl(source_jsonl))
    tables = questdb.convert_rows(rows)
    return questdb.write_tables(output_dir, tables)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Build CommunityMP multiplayer questdb packages directly from ESM/ESP content files. "
            "The content files are extraction sources only; the generated JSONL package is the server runtime format."
        )
    )
    parser.add_argument("content_files", nargs="+", type=Path, help="ESM/ESP files to import.")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path("server") / "data" / "questdb",
        help="Root directory that will receive one questdb package directory per content file.",
    )
    parser.add_argument("--esmtool", type=Path, help="Path to esmtool. Defaults to the local build or PATH.")
    parser.add_argument("--force", action="store_true", help="Replace existing generated package directories.")
    parser.add_argument(
        "--keep-source-jsonl",
        action="store_true",
        help="Keep the intermediate quest-source JSONL beside each generated package.",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    esmtool = args.esmtool or default_esmtool()
    if esmtool is None or not esmtool.is_file():
        raise FileNotFoundError("esmtool was not found; pass --esmtool or build esmtool first")

    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    imported = []
    with tempfile.TemporaryDirectory(prefix="communitymp-questdb-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        for content_file in args.content_files:
            content_file = content_file.resolve()
            if not content_file.is_file():
                raise FileNotFoundError(content_file)

            package_dir = output_root / stable_package_dir_name(content_file)
            ensure_output_dir(package_dir, args.force)

            if args.keep_source_jsonl:
                source_jsonl = package_dir / f"{content_file.stem}.quest-source.jsonl"
            else:
                source_jsonl = temp_dir / f"{stable_package_dir_name(content_file)}.quest-source.jsonl"

            export_source(esmtool, content_file, source_jsonl)
            counts = import_source(source_jsonl, package_dir)
            imported.append((content_file, package_dir, counts))

    for content_file, package_dir, counts in imported:
        summary = ", ".join(f"{table}={count}" for table, count in sorted(counts.items()))
        print(f"Imported {content_file} -> {package_dir}: {summary}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
