#!/usr/bin/env python3
"""Import esmtool quest-export JSONL into CommunityMP quest database tables.

The exporter preserves legacy content facts from ESM/ESP files. This importer
converts those facts into a multiplayer-native table set that the server can own
without treating Bethesda's record format as the runtime quest model.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable


QUESTDB_SCHEMA = "communitymp.questdb.v1"
SOURCE_SCHEMA = "communitymp.quest.source.v1"


def stable_key(*parts: Any) -> str:
    raw = "|".join("" if part is None else str(part) for part in parts)
    slug = re.sub(r"[^a-z0-9_.-]+", "_", raw.lower()).strip("_")
    slug = slug[:96] or "row"
    digest = hashlib.sha1(raw.encode("utf-8")).hexdigest()[:12]
    return f"{slug}_{digest}"


def read_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: invalid JSONL row: {exc}") from exc
            if row.get("schema") != SOURCE_SCHEMA:
                raise ValueError(
                    f"{path}:{line_number}: expected schema {SOURCE_SCHEMA!r}, got {row.get('schema')!r}"
                )
            yield row


def normalize_conditions(owner_kind: str, owner_id: str, row: dict[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for order, condition in enumerate(row.get("conditions", [])):
        result.append(
            {
                "schema": QUESTDB_SCHEMA,
                "conditionId": stable_key(owner_id, "condition", order),
                "ownerKind": owner_kind,
                "ownerId": owner_id,
                "order": order,
                "function": condition.get("function", "Invalid"),
                "functionCode": condition.get("functionCode"),
                "comparison": condition.get("comparison", "invalid"),
                "comparisonCode": condition.get("comparisonCode"),
                "variable": condition.get("variable", ""),
                "valueType": condition.get("valueType", "int"),
                "value": condition.get("value"),
                "evaluationScope": infer_condition_scope(condition),
                "source": source_ref(row),
            }
        )
    return result


def infer_condition_scope(condition: dict[str, Any]) -> str:
    function = condition.get("function", "")
    if function in {"Journal", "Item count"} or function.startswith("Pc"):
        return "player"
    if function in {"Global", "Dead"}:
        return "world"
    if function in {"Local", "Not Local"}:
        return "actor"
    if function in {"Not ID", "Not Faction", "Not Class", "Not Race", "Not Cell"}:
        return "actor-filter"
    return "encounter"


def source_ref(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "packageId": row.get("packageId", ""),
        "dialogueId": row.get("dialogueId", ""),
        "dialogueType": row.get("dialogueType", ""),
        "infoId": row.get("infoId", ""),
        "recordIndex": row.get("recordIndex"),
    }


def row_has_script(row: dict[str, Any]) -> bool:
    return bool((row.get("resultScript") or "").strip())


def make_effect(owner_kind: str, owner_id: str, row: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": QUESTDB_SCHEMA,
        "effectId": stable_key(owner_id, "legacy-script"),
        "ownerKind": owner_kind,
        "ownerId": owner_id,
        "effectKind": "legacy-mwscript",
        "executionPolicy": "server-review-required",
        "script": row.get("resultScript", ""),
        "source": source_ref(row),
    }


def convert_rows(rows: Iterable[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    tables: dict[str, list[dict[str, Any]]] = {
        "packages": [],
        "quest_definitions": [],
        "quest_steps": [],
        "dialogue_topics": [],
        "dialogue_responses": [],
        "conditions": [],
        "legacy_effects": [],
    }
    seen: set[tuple[str, str]] = set()
    dialogues: dict[tuple[str, str], dict[str, Any]] = {}

    for row in rows:
        kind = row.get("kind")
        package_id = row.get("packageId", "")

        if kind == "package":
            table_key = ("packages", package_id)
            if table_key not in seen:
                tables["packages"].append(
                    {
                        "schema": QUESTDB_SCHEMA,
                        "packageId": package_id,
                        "sourceFile": row.get("sourceFile", ""),
                        "sourceFileName": row.get("sourceFileName", ""),
                        "author": row.get("author", ""),
                        "description": row.get("description", ""),
                        "masters": row.get("masters", []),
                        "importPolicy": "content-source-only",
                    }
                )
                seen.add(table_key)
            continue

        if kind == "dialogue":
            dialogue_id = row.get("dialogueId", "")
            dialogue_type = row.get("dialogueType", "Invalid")
            dialogues[(package_id, dialogue_id)] = row

            if dialogue_type == "Journal":
                quest_id = stable_key(package_id, "quest", dialogue_id or row.get("recordIndex"))
                table_key = ("quest_definitions", quest_id)
                if table_key not in seen:
                    tables["quest_definitions"].append(
                        {
                            "schema": QUESTDB_SCHEMA,
                            "questId": quest_id,
                            "sourceQuestId": dialogue_id,
                            "packageId": package_id,
                            "title": row.get("displayName") or dialogue_id,
                            "scopePolicy": "player-default",
                            "sharingPolicy": "explicit-party-or-world-event",
                            "repeatPolicy": "imported-legacy-default",
                            "instancingPolicy": "server-owned-with-player-overrides",
                            "deleted": bool(row.get("deleted")),
                            "source": source_ref(row),
                        }
                    )
                    seen.add(table_key)
            else:
                topic_id = stable_key(package_id, "dialogue", dialogue_type, dialogue_id or row.get("recordIndex"))
                table_key = ("dialogue_topics", topic_id)
                if table_key not in seen:
                    tables["dialogue_topics"].append(
                        {
                            "schema": QUESTDB_SCHEMA,
                            "topicId": topic_id,
                            "sourceTopicId": dialogue_id,
                            "packageId": package_id,
                            "dialogueType": dialogue_type,
                            "displayName": row.get("displayName") or dialogue_id,
                            "visibilityPolicy": "server-filtered-per-player",
                            "deleted": bool(row.get("deleted")),
                            "source": source_ref(row),
                        }
                    )
                    seen.add(table_key)
            continue

        if kind != "info":
            continue

        dialogue_id = row.get("dialogueId", "")
        dialogue_type = row.get("dialogueType", "Invalid")
        parent_dialogue = dialogues.get((package_id, dialogue_id), {})

        if dialogue_type == "Journal":
            quest_id = stable_key(package_id, "quest", dialogue_id or parent_dialogue.get("recordIndex"))
            step_id = stable_key(quest_id, "step", row.get("dataValue"), row.get("infoId") or row.get("recordIndex"))
            tables["quest_steps"].append(
                {
                    "schema": QUESTDB_SCHEMA,
                    "stepId": step_id,
                    "questId": quest_id,
                    "packageId": package_id,
                    "sourceInfoId": row.get("infoId", ""),
                    "index": row.get("dataValue"),
                    "status": row.get("questStatus", "None"),
                    "text": row.get("response", ""),
                    "completionPolicy": infer_completion_policy(row),
                    "deleted": bool(row.get("deleted")),
                    "source": source_ref(row),
                }
            )
            tables["conditions"].extend(normalize_conditions("quest_step", step_id, row))
            if row_has_script(row):
                tables["legacy_effects"].append(make_effect("quest_step", step_id, row))
        else:
            topic_id = stable_key(package_id, "dialogue", dialogue_type, dialogue_id or parent_dialogue.get("recordIndex"))
            response_id = stable_key(topic_id, "response", row.get("infoId") or row.get("recordIndex"))
            tables["dialogue_responses"].append(
                {
                    "schema": QUESTDB_SCHEMA,
                    "responseId": response_id,
                    "topicId": topic_id,
                    "packageId": package_id,
                    "sourceInfoId": row.get("infoId", ""),
                    "order": row.get("infoOrder"),
                    "actor": row.get("actor", ""),
                    "race": row.get("race", ""),
                    "class": row.get("class", ""),
                    "faction": row.get("faction", ""),
                    "cell": row.get("cell", ""),
                    "rank": row.get("rank"),
                    "gender": row.get("gender"),
                    "pcRank": row.get("pcRank"),
                    "disposition": row.get("dataValue") if row.get("dataValueKind") == "disposition" else None,
                    "text": row.get("response", ""),
                    "resultPolicy": "transactional-server-effect",
                    "deleted": bool(row.get("deleted")),
                    "source": source_ref(row),
                }
            )
            tables["conditions"].extend(normalize_conditions("dialogue_response", response_id, row))
            if row_has_script(row):
                tables["legacy_effects"].append(make_effect("dialogue_response", response_id, row))

    return tables


def infer_completion_policy(row: dict[str, Any]) -> str:
    status = row.get("questStatus", "None")
    if status == "Finished":
        return "complete-quest"
    if status == "Restart":
        return "restartable-step"
    if status == "Name":
        return "quest-title"
    return "advance-step"


def write_tables(output_dir: Path, tables: dict[str, list[dict[str, Any]]]) -> dict[str, int]:
    output_dir.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    for table_name, table_rows in tables.items():
        counts[table_name] = len(table_rows)
        path = output_dir / f"{table_name}.jsonl"
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            for row in table_rows:
                handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
                handle.write("\n")

    manifest = {
        "schema": QUESTDB_SCHEMA,
        "tables": counts,
        "stateModel": {
            "playerQuestState": "append-only quest events plus compacted per-player view",
            "worldQuestState": "server-owned shared events keyed by questId and scope",
            "locks": "dialogue/container/quest transactions should acquire server leases before mutation",
        },
    }
    with (output_dir / "manifest.json").open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    return counts


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Import esmtool quest-export JSONL into CommunityMP multiplayer quest tables."
    )
    parser.add_argument("sources", nargs="+", type=Path, help="JSONL files from `esmtool quest-export`.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Directory to write CommunityMP quest database JSONL tables into.",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    all_rows: list[dict[str, Any]] = []
    for source in args.sources:
        all_rows.extend(read_jsonl(source))

    tables = convert_rows(all_rows)
    counts = write_tables(args.output_dir, tables)
    print(
        "Imported CommunityMP quest database tables: "
        + ", ".join(f"{table}={count}" for table, count in sorted(counts.items()))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
