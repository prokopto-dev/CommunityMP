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
import shlex
from pathlib import Path
from typing import Any, Iterable


QUESTDB_SCHEMA = "communitymp.questdb.v1"
SOURCE_SCHEMA = "communitymp.quest.source.v1"
NATIVE_PACKAGE_SCHEMA = "communitymp.quest.package.v1"


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


def empty_tables() -> dict[str, list[dict[str, Any]]]:
    return {
        "packages": [],
        "quest_definitions": [],
        "quest_steps": [],
        "dialogue_topics": [],
        "dialogue_responses": [],
        "conditions": [],
        "quest_effects": [],
        "legacy_effects": [],
    }


def merge_tables(target: dict[str, list[dict[str, Any]]], source: dict[str, list[dict[str, Any]]]) -> None:
    for table_name, rows in source.items():
        target.setdefault(table_name, []).extend(rows)


def normalize_conditions(owner_kind: str, owner_id: str, row: dict[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for order, condition in enumerate(row.get("conditions", [])):
        authority = condition_authority_metadata(condition)
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
                **authority,
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


def condition_authority_metadata(condition: dict[str, Any]) -> dict[str, str]:
    scope = infer_condition_scope(condition)
    if scope == "player":
        requirement = "server-player-state"
        snapshot_policy = "read-player-quest-inventory-snapshot"
    elif scope == "world":
        requirement = "server-world-state"
        snapshot_policy = "read-world-event-snapshot"
    elif scope in {"actor", "actor-filter"}:
        requirement = "cell-simulation-owner"
        snapshot_policy = "read-actor-cell-snapshot"
    else:
        requirement = "server-encounter-context"
        snapshot_policy = "read-dialogue-encounter-snapshot"

    return {
        "evaluationScope": scope,
        "stateScope": scope,
        "authorityRequirement": requirement,
        "snapshotPolicy": snapshot_policy,
    }


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


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    return value


def split_script_lines(script: str) -> Iterable[tuple[int, str]]:
    for line_number, line in enumerate(script.splitlines(), start=1):
        command = line.strip()
        if not command or command.startswith(";"):
            continue
        yield line_number, command


def split_target(command: str) -> tuple[str, str]:
    if "->" not in command:
        return "", command.strip()

    target, remainder = command.split("->", 1)
    return strip_quotes(target), remainder.strip()


def tokenize_command(command: str) -> list[str]:
    lexer = shlex.shlex(command, posix=False)
    lexer.whitespace_split = True
    lexer.commenters = ""
    return [strip_quotes(token) for token in lexer]


def command_policy(effect_kind: str, target: str) -> str:
    if effect_kind.startswith("journal.") or effect_kind.startswith("topic.") or effect_kind.startswith("dialogue."):
        return "server-executable"
    if effect_kind.startswith("inventory."):
        return "inventory-transaction-required"
    if effect_kind.startswith("actor."):
        return "actor-authority-required"
    return "server-review-required"


def command_runtime_metadata(effect_kind: str, target: str) -> dict[str, str]:
    execution_policy = command_policy(effect_kind, target)
    if effect_kind.startswith("journal."):
        return {
            "executionPolicy": execution_policy,
            "stateScope": "player-quest",
            "transactionKind": "quest-state",
            "authorityRequirement": "server-quest-state",
            "conflictPolicy": "monotonic-journal-index",
        }
    if effect_kind.startswith("topic."):
        return {
            "executionPolicy": execution_policy,
            "stateScope": "player-dialogue",
            "transactionKind": "topic-state",
            "authorityRequirement": "server-topic-state",
            "conflictPolicy": "set-union",
        }
    if effect_kind.startswith("dialogue."):
        return {
            "executionPolicy": execution_policy,
            "stateScope": "dialogue-session",
            "transactionKind": "dialogue-session",
            "authorityRequirement": "server-dialogue-session",
            "conflictPolicy": "session-ordered",
        }
    if effect_kind.startswith("inventory."):
        return {
            "executionPolicy": execution_policy,
            "stateScope": "player-or-actor-inventory",
            "transactionKind": "inventory",
            "authorityRequirement": "server-inventory-ledger",
            "conflictPolicy": "transactional-compare-and-swap",
        }
    if effect_kind.startswith("actor."):
        return {
            "executionPolicy": execution_policy,
            "stateScope": "cell-actor",
            "transactionKind": "actor-cell",
            "authorityRequirement": "cell-simulation-owner",
            "conflictPolicy": "cell-authority-sequence",
        }
    return {
        "executionPolicy": execution_policy,
        "stateScope": "unknown",
        "transactionKind": "manual-review",
        "authorityRequirement": "server-review",
        "conflictPolicy": "manual-review",
    }


def target_kind(target: str) -> str:
    if not target:
        return "dialogue-actor"
    if target.lower() == "player":
        return "player"
    return "actor"


def parse_result_command(command: str) -> dict[str, Any]:
    target, body = split_target(command)
    tokens = tokenize_command(body)
    if not tokens:
        return {"effectKind": "unsupported", "rawCommand": command}

    verb = tokens[0].lower()
    args = tokens[1:]
    effect: dict[str, Any] = {
        "rawCommand": command,
        "target": target,
        "targetKind": target_kind(target),
    }

    if verb in {"journal", "setjournalindex"} and len(args) >= 2:
        effect["effectKind"] = "journal.set"
        effect["quest"] = args[0]
        effect["index"] = int(args[1]) if re.fullmatch(r"-?\d+", args[1]) else None
    elif verb == "addtopic" and args:
        effect["effectKind"] = "topic.add"
        effect["topic"] = args[0]
    elif verb in {"additem", "removeitem"} and len(args) >= 2:
        effect["effectKind"] = f"inventory.{verb[:-4]}"
        effect["item"] = args[0]
        effect["count"] = int(args[1]) if re.fullmatch(r"-?\d+", args[1]) else None
    elif verb == "goodbye":
        effect["effectKind"] = "dialogue.goodbye"
    elif verb == "choice":
        effect["effectKind"] = "dialogue.choice"
        effect["choiceCount"] = len(args) // 2
        effect["choices"] = [
            {"text": args[index], "id": args[index + 1]}
            for index in range(0, len(args) - 1, 2)
        ]
    elif verb == "setfight" and args:
        effect["effectKind"] = "actor.setFight"
        effect["value"] = int(args[0]) if re.fullmatch(r"-?\d+", args[0]) else None
    elif verb == "startcombat" and args:
        effect["effectKind"] = "actor.startCombat"
        effect["combatTarget"] = args[0]
    elif verb == "moddisposition" and args:
        effect["effectKind"] = "actor.modDisposition"
        effect["value"] = int(args[0]) if re.fullmatch(r"-?\d+", args[0]) else None
    else:
        effect["effectKind"] = "unsupported"

    effect.update(command_runtime_metadata(effect["effectKind"], target))
    return effect


def make_server_effects(owner_kind: str, owner_id: str, row: dict[str, Any]) -> list[dict[str, Any]]:
    script = row.get("resultScript", "") or ""
    result: list[dict[str, Any]] = []
    for order, (line_number, command) in enumerate(split_script_lines(script)):
        effect = parse_result_command(command)
        result.append(
            {
                "schema": QUESTDB_SCHEMA,
                "effectId": stable_key(owner_id, "effect", order, command),
                "ownerKind": owner_kind,
                "ownerId": owner_id,
                "order": order,
                "sourceLine": line_number,
                **effect,
                "idempotencyKey": stable_key(
                    owner_id,
                    "effect-idempotency",
                    order,
                    effect.get("effectKind", ""),
                    effect.get("quest", ""),
                    effect.get("topic", ""),
                    effect.get("item", ""),
                    command,
                ),
                "source": source_ref(row),
            }
        )
    return result


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


def native_source_ref(package: dict[str, Any], native_id: str = "") -> dict[str, Any]:
    return {
        "packageId": package.get("packageId", ""),
        "nativeId": native_id,
        "sourceFormat": NATIVE_PACKAGE_SCHEMA,
    }


def make_native_conditions(
    owner_kind: str,
    owner_id: str,
    package: dict[str, Any],
    conditions: Iterable[dict[str, Any]],
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for order, condition in enumerate(conditions):
        authority = condition_authority_metadata(condition)
        result.append(
            {
                "schema": QUESTDB_SCHEMA,
                "conditionId": condition.get("conditionId") or stable_key(owner_id, "condition", order),
                "ownerKind": owner_kind,
                "ownerId": owner_id,
                "order": condition.get("order", order),
                "function": condition.get("function", "Invalid"),
                "functionCode": condition.get("functionCode"),
                "comparison": condition.get("comparison", "invalid"),
                "comparisonCode": condition.get("comparisonCode"),
                "variable": condition.get("variable", ""),
                "valueType": condition.get("valueType", "int"),
                "value": condition.get("value"),
                **authority,
                "source": native_source_ref(package, condition.get("conditionId", "")),
            }
        )
    return result


def make_native_effects(
    owner_kind: str,
    owner_id: str,
    package: dict[str, Any],
    effects: Iterable[dict[str, Any]],
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for order, effect in enumerate(effects):
        effect_kind = effect.get("effectKind", "unsupported")
        target = effect.get("target", "")
        runtime = command_runtime_metadata(effect_kind, target)
        for key in ("executionPolicy", "stateScope", "transactionKind", "authorityRequirement", "conflictPolicy"):
            if effect.get(key):
                runtime[key] = effect[key]

        effect_id = effect.get("effectId") or stable_key(owner_id, "effect", order, effect_kind)
        result.append(
            {
                "schema": QUESTDB_SCHEMA,
                "effectId": effect_id,
                "ownerKind": owner_kind,
                "ownerId": owner_id,
                "order": effect.get("order", order),
                "sourceLine": effect.get("sourceLine", 0),
                "effectKind": effect_kind,
                "rawCommand": effect.get("rawCommand", ""),
                "target": target,
                "targetKind": effect.get("targetKind", target_kind(target)),
                "quest": effect.get("quest", ""),
                "topic": effect.get("topic", ""),
                "item": effect.get("item", ""),
                "combatTarget": effect.get("combatTarget", ""),
                "index": effect.get("index"),
                "count": effect.get("count"),
                "value": effect.get("value"),
                "choiceCount": effect.get("choiceCount", 0),
                **runtime,
                "idempotencyKey": effect.get("idempotencyKey")
                or stable_key(
                    owner_id,
                    "effect-idempotency",
                    order,
                    effect_kind,
                    effect.get("quest", ""),
                    effect.get("topic", ""),
                    effect.get("item", ""),
                    effect.get("rawCommand", ""),
                ),
                "source": native_source_ref(package, effect_id),
            }
        )
    return result


def convert_native_package(package: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    if package.get("schema") != NATIVE_PACKAGE_SCHEMA:
        raise ValueError(
            f"expected schema {NATIVE_PACKAGE_SCHEMA!r}, got {package.get('schema')!r}"
        )

    package_id = package.get("packageId") or stable_key(package.get("title", "native-package"))
    package["packageId"] = package_id
    tables = empty_tables()
    tables["packages"].append(
        {
            "schema": QUESTDB_SCHEMA,
            "packageId": package_id,
            "sourceFile": package.get("sourceFile", ""),
            "sourceFileName": package.get("title", package_id),
            "author": package.get("author", ""),
            "description": package.get("description", ""),
            "masters": package.get("dependencies", []),
            "importPolicy": "native-runtime-source",
            "runtimeFormat": QUESTDB_SCHEMA,
        }
    )

    for quest in package.get("quests", []):
        quest_id = quest.get("questId") or stable_key(package_id, "quest", quest.get("title", "quest"))
        source_quest_id = quest.get("sourceQuestId", quest_id)
        tables["quest_definitions"].append(
            {
                "schema": QUESTDB_SCHEMA,
                "questId": quest_id,
                "sourceQuestId": source_quest_id,
                "packageId": package_id,
                "title": quest.get("title", source_quest_id),
                "scopePolicy": quest.get("scopePolicy", "player-default"),
                "sharingPolicy": quest.get("sharingPolicy", "explicit-party-or-world-event"),
                "repeatPolicy": quest.get("repeatPolicy", "native-explicit"),
                "instancingPolicy": quest.get("instancingPolicy", "server-owned-with-player-overrides"),
                "runtimeModel": "server-owned-multiplayer-quest-v1",
                "authorityPolicy": quest.get("authorityPolicy", "server-owned"),
                "stateScope": quest.get("stateScope", "player-default"),
                "transactionPolicy": quest.get("transactionPolicy", "quest-compare-and-swap"),
                "deleted": bool(quest.get("deleted")),
                "source": native_source_ref(package, quest_id),
            }
        )

        for step in quest.get("steps", []):
            step_id = step.get("stepId") or stable_key(
                quest_id, "step", step.get("index"), step.get("text", "")
            )
            tables["quest_steps"].append(
                {
                    "schema": QUESTDB_SCHEMA,
                    "stepId": step_id,
                    "questId": quest_id,
                    "packageId": package_id,
                    "sourceInfoId": step.get("sourceInfoId", step_id),
                    "index": step.get("index"),
                    "status": step.get("status", "None"),
                    "text": step.get("text", ""),
                    "completionPolicy": step.get("completionPolicy", "advance-step"),
                    "deleted": bool(step.get("deleted")),
                    "source": native_source_ref(package, step_id),
                }
            )
            tables["conditions"].extend(
                make_native_conditions("quest_step", step_id, package, step.get("conditions", []))
            )
            tables["quest_effects"].extend(
                make_native_effects("quest_step", step_id, package, step.get("effects", []))
            )

    for topic in package.get("dialogueTopics", []):
        topic_id = topic.get("topicId") or stable_key(
            package_id, "dialogue", topic.get("dialogueType", "Topic"), topic.get("displayName", "topic")
        )
        tables["dialogue_topics"].append(
            {
                "schema": QUESTDB_SCHEMA,
                "topicId": topic_id,
                "sourceTopicId": topic.get("sourceTopicId", topic_id),
                "packageId": package_id,
                "dialogueType": topic.get("dialogueType", "Topic"),
                "displayName": topic.get("displayName", topic.get("sourceTopicId", topic_id)),
                "visibilityPolicy": topic.get("visibilityPolicy", "server-filtered-per-player"),
                "authorityPolicy": topic.get("authorityPolicy", "server-filtered"),
                "deleted": bool(topic.get("deleted")),
                "source": native_source_ref(package, topic_id),
            }
        )

        for response in topic.get("responses", []):
            response_id = response.get("responseId") or stable_key(
                topic_id, "response", response.get("text", "")
            )
            tables["dialogue_responses"].append(
                {
                    "schema": QUESTDB_SCHEMA,
                    "responseId": response_id,
                    "topicId": topic_id,
                    "packageId": package_id,
                    "sourceInfoId": response.get("sourceInfoId", response_id),
                    "order": response.get("order", 0),
                    "actor": response.get("actor", ""),
                    "race": response.get("race", ""),
                    "class": response.get("class", ""),
                    "faction": response.get("faction", ""),
                    "cell": response.get("cell", ""),
                    "rank": response.get("rank"),
                    "gender": response.get("gender"),
                    "pcRank": response.get("pcRank"),
                    "disposition": response.get("disposition"),
                    "text": response.get("text", ""),
                    "resultPolicy": response.get("resultPolicy", "transactional-server-effect"),
                    "authorityPolicy": response.get("authorityPolicy", "server-evaluated"),
                    "transactionPolicy": response.get("transactionPolicy", "dialogue-response-effect-plan"),
                    "deleted": bool(response.get("deleted")),
                    "source": native_source_ref(package, response_id),
                }
            )
            tables["conditions"].extend(
                make_native_conditions("dialogue_response", response_id, package, response.get("conditions", []))
            )
            tables["quest_effects"].extend(
                make_native_effects("dialogue_response", response_id, package, response.get("effects", []))
            )

    return tables


def convert_rows(rows: Iterable[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    tables: dict[str, list[dict[str, Any]]] = empty_tables()
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
                        "runtimeFormat": QUESTDB_SCHEMA,
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
                            "runtimeModel": "server-owned-multiplayer-quest-v1",
                            "authorityPolicy": "server-owned",
                            "stateScope": "player-default",
                            "transactionPolicy": "quest-compare-and-swap",
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
                            "authorityPolicy": "server-filtered",
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
                tables["quest_effects"].extend(make_server_effects("quest_step", step_id, row))
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
                    "authorityPolicy": "server-evaluated",
                    "transactionPolicy": "dialogue-response-effect-plan",
                    "deleted": bool(row.get("deleted")),
                    "source": source_ref(row),
                }
            )
            tables["conditions"].extend(normalize_conditions("dialogue_response", response_id, row))
            if row_has_script(row):
                tables["quest_effects"].extend(make_server_effects("dialogue_response", response_id, row))
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
            "runtimeAuthority": "server-owned records with explicit transaction and authority metadata",
        },
    }
    with (output_dir / "manifest.json").open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    return counts


def read_native_package(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        package = json.load(handle)
    if not isinstance(package, dict):
        raise ValueError(f"{path}: native quest package must be a JSON object")
    return package


def import_sources(sources: Iterable[Path]) -> dict[str, list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    tables = empty_tables()
    for source in sources:
        if source.suffix.lower() == ".json":
            package = read_native_package(source)
            if package.get("schema") == NATIVE_PACKAGE_SCHEMA:
                merge_tables(tables, convert_native_package(package))
                continue

        rows.extend(read_jsonl(source))

    merge_tables(tables, convert_rows(rows))
    return tables


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Import esmtool quest-export JSONL or native CommunityMP quest package JSON into "
            "CommunityMP multiplayer quest tables."
        )
    )
    parser.add_argument(
        "sources",
        nargs="+",
        type=Path,
        help=(
            "JSONL files from `esmtool quest-export` or JSON files with schema "
            f"{NATIVE_PACKAGE_SCHEMA}."
        ),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Directory to write CommunityMP quest database JSONL tables into.",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    tables = import_sources(args.sources)
    counts = write_tables(args.output_dir, tables)
    print(
        "Imported CommunityMP quest database tables: "
        + ", ".join(f"{table}={count}" for table, count in sorted(counts.items()))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
